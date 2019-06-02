/*
 * namespace_manager.cpp
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#include "namespace_manager.h"
#include "bucket_table.h"
#include "simple_log.h"
#include "base_conn.h"
#include "config_parser.h"
#include "config_server_conn.h"
#include "table_transform.h"
#include "migration_conn.h"

NamespaceManager g_namespace_manager;

bool NamespaceManager::InitTable(const string& ns, uint16_t bucket_cnt, const vector<string>& server_addr_vec)
{
    log_message(kLogLevelInfo, "InitTable, ns=%s, bucket_cnt=%d\n", ns.c_str(), bucket_cnt);
    
    lock_guard<mutex> lock(ns_mutex_);
    for (const string& addr : server_addr_vec) {
        for (auto it = ns_table_map_.begin(); it != ns_table_map_.end(); ++it) {
            if (it->second->HasServerAddr(addr)) {
                log_message(kLogLevelError, "server address already exist: %s\n", addr.c_str());
                return false;
            }
        }
    }

    if (ns_table_map_.find(ns) != ns_table_map_.end()) {
        log_message(kLogLevelError, "namespace already exist: %s\n", ns.c_str());
        return false;
    }

    BucketTable* table = new BucketTable(ns);
    if (!table->InitTable(bucket_cnt, server_addr_vec)) {
        delete table;
        return false;
    }

    ns_table_map_[ns] = table;

    _BroadcastTable(table); // 刚初始化时肯定没有这个业务名的proxy，可以用这个函数同步对照表到备cs
    return true;
}

bool NamespaceManager::InitTable(const string& ns, uint32_t version, const map<uint16_t, string>& bucket_map,
               uint16_t migrating_bucket_id, const string& migrating_server_addr, bool save_file)
{
    log_message(kLogLevelInfo, "InitTable, ns=%s, version=%d, migrating_bucket_id=%d, migrating_server_addr=%s, save=%d\n",
                ns.c_str(), version, migrating_bucket_id, migrating_server_addr.c_str(), (int)save_file);
    
    lock_guard<mutex> lock(ns_mutex_);
    auto it = ns_table_map_.find(ns);
    if (it != ns_table_map_.end()) {
        log_message(kLogLevelInfo, "update table: ns=%s\n", ns.c_str());
        return it->second->InitTable(version, bucket_map, migrating_bucket_id, migrating_server_addr, save_file);
    } else {
        log_message(kLogLevelInfo, "init table: ns=%s\n", ns.c_str());
        
        BucketTable* table = new BucketTable(ns);
        if (table->InitTable(version, bucket_map, migrating_bucket_id, migrating_server_addr, save_file)) {
            ns_table_map_[ns] = table;
            return true;
        } else {
            delete table;
            return false;
        }
    }
}

bool NamespaceManager::DelTable(const string& ns)
{
    log_message(kLogLevelInfo, "DelTable, ns=%s\n", ns.c_str());
    lock_guard<mutex> lock(ns_mutex_);
    
    auto it_table = ns_table_map_.find(ns);
    if (it_table == ns_table_map_.end()) {
        return false;
    }
    
    delete it_table->second;
    ns_table_map_.erase(it_table);
    
    ns_server_down_map_.erase(ns);
    ns_migration_map_.erase(ns);
    
    if (!g_config.is_master)
        return true;
    
    auto it_ns = ns_proxy_map_.find(ns);
    if (it_ns != ns_proxy_map_.end()) {
        for (auto it = it_ns->second.begin(); it != it_ns->second.end(); ++it) {
            PktDelNamespace* pkt = new PktDelNamespace(ns);
            BaseConn::SendPkt(it->first, pkt);
            log_message(kLogLevelInfo, "send DelNsPkt to proxy: %s\n", it->second.info.c_str());
        }
        
        ns_proxy_map_.erase(it_ns);
    }
    
    PktDelNamespace pkt(ns);
    broadcast_pkt_to_slaves(&pkt);
    
    return true;
}

bool NamespaceManager::ScaleUpTable(const string& ns, const string& addr)
{
    log_message(kLogLevelInfo, "ScaleUpTable, ns=%s, addr=%s\n", ns.c_str(), addr.c_str());
    
    lock_guard<mutex> lock(ns_mutex_);

    for (auto it = ns_table_map_.begin(); it != ns_table_map_.end(); ++it) {
        if (it->second->HasServerAddr(addr)) {
            log_message(kLogLevelError, "server address already exist: %s\n", addr.c_str());
            return false;
        }
    }
    
    auto it = ns_table_map_.find(ns);
    if (it == ns_table_map_.end()) {
        log_message(kLogLevelError, "namespace not exist: %s\n", ns.c_str());
        return false;
    }
    
    BucketTable* table = it->second;
    if (!table->ScaleUpTable(addr)) {
        return false;
    }
    
    _BroadcastTable(table);
    return true;
}

bool NamespaceManager::ScaleDownTable(const string& ns, const string& addr)
{
    log_message(kLogLevelInfo, "ScaleDownTable, ns=%s, addr=%s\n", ns.c_str(), addr.c_str());
    
    lock_guard<mutex> lock(ns_mutex_);
    
    auto it = ns_table_map_.find(ns);
    if (it == ns_table_map_.end()) {
        log_message(kLogLevelError, "namespace not exist: %s\n", ns.c_str());
        return false;
    }
    
    BucketTable* table = it->second;
    if (!table->ScaleDownTable(addr)) {
        return false;
    }
    
    _BroadcastTable(table);
    return true;
}

void NamespaceManager::_DoHA(const string &ns, const string &addr, const string &slave_addr)
{
    if (g_config.cluster_type == "data_store") {
        if (!slave_addr.empty()) {
            ReplaceServerAddress(ns, addr, slave_addr);
        }
    } else {
        ScaleDownTable(ns, addr);
    }
}

void NamespaceManager::ReportServerDown(const string& ns, const string& addr, const string& slave_addr, net_handle_t handle)
{
    if (!g_config.support_ha) {
        return;
    }
    
    unique_lock<mutex> lock(ns_mutex_);
    auto it_proxy = ns_proxy_map_.find(ns);
    if (it_proxy == ns_proxy_map_.end()) {
        log_message(kLogLevelError, "no proxy in namespace: %s\n", ns.c_str());
        return;
    }
    
    // 如果namespace只有一个proxy时，直接调用ScaleDownTable方法
    // 如果有多个proxy时，先保存状态，然后检查所有proxy是否都已上报server down,
    // 只有都上报的情况下才调用ScaleDownTable方法
    int proxy_cnt = (int)it_proxy->second.size();
    if (proxy_cnt == 1) {
        // Caution: 由于_DoHA里面会加锁，所有需要先解锁，否则mutex在已经加锁的情况下再加锁一次会造成线程死锁
        lock.unlock();
        _DoHA(ns, addr, slave_addr);
        return;
    }
    
    auto it_down = ns_server_down_map_.find(ns);
    if (it_down == ns_server_down_map_.end()) {
        // 该namespace下从来没有上报过，保存状态后不用检查上报proxy的个数，因为才1个proxy上报
        set<net_handle_t> handle_set = {handle};
        ServerDownCount down_count_map;
        down_count_map[addr] = handle_set;
        ns_server_down_map_[ns] = down_count_map;
    } else {
        auto it_addr = it_down->second.find(addr);
        if (it_addr == it_down->second.end()) {
            // 该namespace下有其他addr上报，但该addr没有上报过，保存状态后可以直接退出，因为才1个proxy上报
            set<net_handle_t> handle_set = {handle};
            it_down->second[addr] = handle_set;
        } else {
            // 该namespace下有该addr上报过，保存状态后，需要检查是否所有proxy都已经上报过
            it_addr->second.insert(handle);
            if ((int)it_addr->second.size() == proxy_cnt) {
                it_down->second.erase(it_addr);
                
                lock.unlock();
                _DoHA(ns, addr, slave_addr);
            }
        }
    }
}

void NamespaceManager::ReportServerUp(const string& ns, const string& addr, net_handle_t handle)
{
    if (!g_config.support_ha) {
        return;
    }
    
    lock_guard<mutex> lock(ns_mutex_);
    auto it_down = ns_server_down_map_.find(ns);
    if (it_down != ns_server_down_map_.end()) {
        auto it_server = it_down->second.find(addr);
        if (it_server != it_down->second.end()) {
            it_server->second.erase(handle);
        }
    }
}

vector<string> NamespaceManager::GetNamespaceList()
{
    lock_guard<mutex> lock(ns_mutex_);
    vector<string> ns_vec;
    for (auto it = ns_table_map_.begin(); it != ns_table_map_.end(); ++it) {
        ns_vec.push_back(it->first);
    }
    
    return ns_vec;
}

bool NamespaceManager::GetBucketTable(const string& ns, uint32_t& version, map<uint16_t, string>& table,
                                      uint16_t& migrating_bucket_id, string& migrating_server_addr)
{
    lock_guard<mutex> lock(ns_mutex_);
    auto it = ns_table_map_.find(ns);
    if (it != ns_table_map_.end()) {
        version = it->second->GetVersion();
        table = it->second->GetBucketMap();
        migrating_bucket_id = it->second->GetMigratingBucetId();
        migrating_server_addr = it->second->GetMigratingServerAddr();
        return true;
    } else {
        return false;
    }
}

void NamespaceManager::AddProxy(const string& ns, net_handle_t handle, int state, const string& info)
{
    ProxyStateInfo_t state_info = {state, info};
    
    lock_guard<mutex> lock(ns_mutex_);
    
    auto it = ns_proxy_map_.find(ns);
    if (it != ns_proxy_map_.end()) {
        it->second[handle] = state_info;
    } else {
        ProxyInfoMap state_info_map = {{handle, state_info}};
        ns_proxy_map_[ns] = state_info_map;
    }
}

void NamespaceManager::DelProxy(const string& ns, net_handle_t handle)
{
    ns_mutex_.lock();
    auto it = ns_proxy_map_.find(ns);
    if (it != ns_proxy_map_.end()) {
        it->second.erase(handle);
        
        if (it->second.empty()) {
            ns_proxy_map_.erase(it);
        }
    }
    
    auto it_down = ns_server_down_map_.find(ns);
    if (it_down != ns_server_down_map_.end()) {
        for (auto it_server = it_down->second.begin(); it_server != it_down->second.end(); it_server++) {
            it_server->second.erase(handle);
        }
    }
    
    ns_mutex_.unlock();
    
    // 这个是为了处理一种特殊情况，其中一个proxy挂了，或者断开连接，
    // 其他proxy都处于FREEZE_ACK状态，或者这是最后一个proxy了，
    // 这时需要发送bucket对照表到其他proxy，如果在数据迁移，需要发回ack给迁移客户端
    HandleFreezeProxyAck(ns, NETLIB_INVALID_HANDLE);
}

bool NamespaceManager::GetProxyInfo(const string& ns, ProxyInfoMap& proxy_info_map)
{
    lock_guard<mutex> lock(ns_mutex_);
    
    if (ns_table_map_.find(ns) == ns_table_map_.end()) {
        return false;
    }
    
    auto it_ns = ns_proxy_map_.find(ns);
    if (it_ns != ns_proxy_map_.end()) {
        proxy_info_map = it_ns->second;
    }
    
    return true;
}

uint32_t NamespaceManager::StartMigration(const string& ns, uint16_t bucket_id, const string& new_server_addr,
                                          uint8_t scale_up, MigrationConn* conn)
{
    lock_guard<mutex> lock(ns_mutex_);
    
    auto it_table = ns_table_map_.find(ns);
    if (it_table == ns_table_map_.end()) {
        return MIGRATION_RESULT_NO_NAMESPACE;
    }
    
    BucketTable* table = it_table->second;
    uint16_t migrating_bucket_id = table->GetMigratingBucetId();
    if (migrating_bucket_id != kInvalidBucketId) {
        if (migrating_bucket_id != bucket_id) {
            log_message(kLogLevelError, "ns=%s, migrate_id=%d, req_id=%d\n", ns.c_str(), migrating_bucket_id, bucket_id);
            return MIGRATION_RESULT_IN_MIGRAING;
        } else {
            // bucket_id已经在迁移， 表示migration_tool从上次未完成的迁移处开始迁移，直接返回ack
            PktStartMigrationAck ack_pkt(ns, bucket_id, 0);
            conn->SendPkt(&ack_pkt);
            return MIGRATION_RESULT_SUCCESS;
        }
    }
    
    if (scale_up) {
        for (auto it = ns_table_map_.begin(); it != ns_table_map_.end(); ++it) {
            if ((it->second->GetNamespace() != ns) && it->second->HasServerAddr(new_server_addr)) {
                log_message(kLogLevelError, "scale up server already exist: %s\n", new_server_addr.c_str());
                return MIGRATION_RESULT_ADDRESS_EXIST;
            }
        }
    } else {
        if (!table->HasServerAddr(new_server_addr)) {
            log_message(kLogLevelError, "scale down server not exist: %s", new_server_addr.c_str());
            return MIGRATION_RESULT_ADDRESS_NOT_EXIST;
        }
    }
    
    uint32_t result = table->UpdateTable(bucket_id, new_server_addr, true);
    if (result) {
        return result;
    }
    
    _BroadcastTable(table);
    
    auto it_ns = ns_proxy_map_.find(ns);
    if (it_ns == ns_proxy_map_.end()) {
        // no proxy in this namespace, can start migration immediately
        PktStartMigrationAck pkt(ns, bucket_id, 0);
        conn->SendPkt(&pkt);
    } else {
        ns_migration_map_[ns] = {conn->GetHandle(), bucket_id, PKT_ID_START_MIGRATION_ACK};
    }
    
    return MIGRATION_RESULT_SUCCESS;
}

uint32_t NamespaceManager::CompleteMigration(const string& ns, uint16_t bucket_id, const string& new_server_addr,
                                             uint8_t scale_up, MigrationConn* conn)
{
    lock_guard<mutex> lock(ns_mutex_);
    
    auto it_table = ns_table_map_.find(ns);
    if (it_table == ns_table_map_.end()) {
        return MIGRATION_RESULT_NO_NAMESPACE;
    }
    
    BucketTable* table = it_table->second;
    uint32_t result = table->UpdateTable(bucket_id, new_server_addr, false);
    if (result) {
        return result;
    }
    
    _BroadcastTable(table);
    
    auto it_ns = ns_proxy_map_.find(ns);
    if (it_ns == ns_proxy_map_.end()) {
        // no proxy in this namespace, can send back ack immediately
        PktCompleteMigrationAck pkt(ns, bucket_id, 0);
        conn->SendPkt(&pkt);
    } else {
        ns_migration_map_[ns] = {conn->GetHandle(), bucket_id, PKT_ID_COMPLETE_MIGRATION_ACK};
    }
    
    return MIGRATION_RESULT_SUCCESS;
}

uint32_t NamespaceManager::ReplaceServerAddress(const string& ns, const string& old_addr, const string& new_addr)
{
    lock_guard<mutex> lock(ns_mutex_);
    
    auto it_table = ns_table_map_.find(ns);
    if (it_table == ns_table_map_.end()) {
        log_message(kLogLevelError, "no namespace=%s for ReplaceServerAddress\n", ns.c_str());
        return MIGRATION_RESULT_NO_NAMESPACE;
    }
    
    BucketTable* table = it_table->second;
    uint32_t result = table->ReplaceServerAddress(old_addr, new_addr);
    if (result) {
        return result;
    }
    
    _BroadcastTable(table);
    
    return MIGRATION_RESULT_SUCCESS;
}

void NamespaceManager::HandleFreezeProxyAck(const string& ns, net_handle_t proxy_handle)
{
    lock_guard<mutex> lock(ns_mutex_);
    
    auto it_ns = ns_proxy_map_.find(ns);
    if (it_ns != ns_proxy_map_.end()) {
        auto it_info = it_ns->second.find(proxy_handle);
        if (it_info != it_ns->second.end()) {
            it_info->second.state = PROXY_STATE_FREEZE_ACK;
        }
        
        bool all_acked = true;
        for (auto it = it_ns->second.begin(); it != it_ns->second.end(); ++it) {
            if (it->second.state != PROXY_STATE_FREEZE_ACK) {
                all_acked = false;
                break;
            }
        }
        
        if (all_acked) {
            auto it_table = ns_table_map_.find(ns);
            if (it_table == ns_table_map_.end()) {
                log_message(kLogLevelError, "no namespace=%s for HandleFreezeProxyAck\n", ns.c_str());
                return;
            }
            
            BucketTable* table = it_table->second;
            
            uint32_t version = table->GetVersion();
            map<uint16_t, string>& bucket_map = table->GetBucketMap();
            uint32_t migrating_bucket_id = table->GetMigratingBucetId();
            string migrating_server_addr = table->GetMigratingServerAddr();
            map<string, vector<uint16_t>> server_buckets_map = transform_table(bucket_map);
            
            int proxy_state = _SendBackAck(ns);
            
            for (auto it = it_ns->second.begin(); it != it_ns->second.end(); ++it) {
                it->second.state = proxy_state;
                net_handle_t handle = it->first;
                log_message(kLogLevelInfo, "send bucket table to proxy: %s\n", it->second.info.c_str());
                    
                PktBucketTableResp* pkt = new PktBucketTableResp(ns, version, server_buckets_map,
                                                                 migrating_bucket_id, migrating_server_addr);
                BaseConn::SendPkt(handle, pkt);
            }
        }
    } else {
        _SendBackAck(ns);
    }
}

int NamespaceManager::_SendBackAck(const string& ns)
{
    int proxy_state = PROXY_STATE_ONLINE;
    if (g_config.cluster_type == "data_store") {
        auto it = ns_migration_map_.find(ns);
        if (it != ns_migration_map_.end()) {
            if (it->second.pkt_type == PKT_ID_START_MIGRATION_ACK) {
                log_message(kLogLevelInfo, "send start migration ack ns=%s\n", ns.c_str());
                PktStartMigrationAck* pkt = new PktStartMigrationAck(ns, it->second.bucket_id, 0);
                BaseConn::SendPkt(it->second.handle, pkt);
                proxy_state = PROXY_STATE_MIGRATING;
            } else if (it->second.pkt_type == PKT_ID_COMPLETE_MIGRATION_ACK) {
                log_message(kLogLevelInfo, "send complete migration ack ns=%s\n", ns.c_str());
                PktCompleteMigrationAck* pkt = new PktCompleteMigrationAck(ns, it->second.bucket_id, 0);
                BaseConn::SendPkt(it->second.handle, pkt);
            }
            
            ns_migration_map_.erase(it);
        }
    }
    
    return proxy_state;
}

void NamespaceManager::_BroadcastTable(BucketTable* table)
{
    string ns = table->GetNamespace();
    uint32_t version = table->GetVersion();
    map<uint16_t, string>& bucket_map = table->GetBucketMap();
    uint32_t migrating_bucket_id = table->GetMigratingBucetId();
    string migrating_server_addr = table->GetMigratingServerAddr();
    map<string, vector<uint16_t>> server_buckets_map = transform_table(bucket_map);
    
    PktBucketTableResp pkt(ns, version, server_buckets_map, migrating_bucket_id, migrating_server_addr);
    broadcast_pkt_to_slaves(&pkt);
    
    auto it_ns = ns_proxy_map_.find(ns);
    if (it_ns != ns_proxy_map_.end()) {
        for (auto it = it_ns->second.begin(); it != it_ns->second.end(); ++it) {
            net_handle_t handle = it->first;
            
            it->second.state = PROXY_STATE_FREEZE;
            
            log_message(kLogLevelInfo, "send freeze req to proxy: %s\n", it->second.info.c_str());
            PktBase* freeze_pkt = new PktFreezeProxy(ns);
            BaseConn::SendPkt(handle, freeze_pkt);
        }
    }
}
