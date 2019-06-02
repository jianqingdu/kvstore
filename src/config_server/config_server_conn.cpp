/*
 * config_server_conn.cpp
 *
 *  Created on: 2016-3-17
 *      Author: ziteng
 */

#include "config_server_conn.h"
#include "simple_log.h"
#include "event_loop.h"
#include "config_parser.h"
#include "namespace_manager.h"
#include "table_transform.h"
#include "common.h"

static mutex        g_sync_mutex;
static net_handle_t g_master_handle = NETLIB_INVALID_HANDLE;
static set<net_handle_t> g_slave_handle_set;

static void slave_config_server_timer_callback(void* callback_data, uint8_t msg, uint32_t handle, void* pParam)
{
    // reconnecting to master config server if disconnected, only for slave config server
    if (g_master_handle == NETLIB_INVALID_HANDLE) {
        ConfigServerConn* master_conn = new ConfigServerConn();
        net_handle_t master_handle = master_conn->Connect(g_config.master_cs_ip.c_str(), g_config.master_cs_port);
        if (master_handle != NETLIB_INVALID_HANDLE) {
            lock_guard<mutex> lock(g_sync_mutex);
            g_master_handle = master_handle;
        }
    }
}

void init_connection_to_master()
{
    if (!g_config.is_master) {
        ConfigServerConn* master_conn = new ConfigServerConn();
        g_master_handle = master_conn->Connect(g_config.master_cs_ip.c_str(), g_config.master_cs_port);
        if (g_master_handle == NETLIB_INVALID_HANDLE) {
            log_message(kLogLevelError, "!!!connect failed immediately\n");
            exit(1);
        }
        
        get_main_event_loop()->AddTimer(slave_config_server_timer_callback, NULL, 5000);
    }
}

void broadcast_pkt_to_slaves(PktBase* pkt)
{
    lock_guard<mutex> lock(g_sync_mutex);
    for (net_handle_t conn_handle : g_slave_handle_set) {
        PktBase* dup_pkt = PktBase::DuplicatePacket(pkt);
        BaseConn::SendPkt(conn_handle, dup_pkt);
    }
}

///////
void ConfigServerConn::Close()
{
    if (!m_open) {
        // connect failed
        log_message(kLogLevelInfo, "Connect to ConfigServer %s:%d failed\n", m_peer_ip.c_str(), m_peer_port);
    } else {
        // connection broken
        log_message(kLogLevelInfo, "Connection to ConfigServer %s:%d broken\n", m_peer_ip.c_str(), m_peer_port);
    }

    g_sync_mutex.lock();
    if (g_master_handle == m_handle) {
        g_master_handle = NETLIB_INVALID_HANDLE;
    } else {
        g_slave_handle_set.erase(m_handle);
    }
    g_sync_mutex.unlock();
    
    BaseConn::Close();
}

void ConfigServerConn::OnConnect(BaseSocket* base_socket)
{
    BaseConn::OnConnect(base_socket);
    log_message(kLogLevelInfo, "connect from ConfigServer %s:%d\n", m_peer_ip.c_str(), m_peer_port);
    
    lock_guard<mutex> lock(g_sync_mutex);
    g_slave_handle_set.insert(m_handle);
}

void ConfigServerConn::OnConfirm()
{
    BaseConn::OnConfirm();
    log_message(kLogLevelInfo, "Connect to ConfigServer %s:%d success\n", m_peer_ip.c_str(), m_peer_port);
    
    PktNamespaceListReq pkt;
    SendPkt(&pkt);
}

void ConfigServerConn::OnTimer(uint64_t curr_tick)
{
    if (m_open && (curr_tick > m_last_send_tick + m_heartbeat_interval)) {
        PktHeartBeat pkt;
        SendPkt(&pkt);
    }
    
    if (curr_tick > m_last_recv_tick + m_conn_timeout) {
        log_message(kLogLevelWarning, "ConfigServerConn timeout, addr=%s:%d\n", m_peer_ip.c_str(), m_peer_port);
        
        Close();
    }
}

void ConfigServerConn::HandlePkt(PktBase* pkt)
{
    switch (pkt->GetPktId()) {
        case PKT_ID_HEARTBEAT:
            break;
        case PKT_ID_NAMESPACE_LIST_REQ:
            _HandleNamespaceListReq((PktNamespaceListReq *)pkt);
            break;
        case PKT_ID_NAMESPACE_LIST_RESP:
            _HandleNamespaceListResp((PktNamespaceListResp *)pkt);
            break;
        case PKT_ID_BUCKET_TABLE_REQ:
            _HandleBucketTableReq((PktBucketTableReq *)pkt);
            break;
        case PKT_ID_BUCKET_TABLE_RESP:
            _HandleBucketTableResp((PktBucketTableResp *)pkt);
            break;
        case PKT_ID_DEL_NAMESPACE:
            _HandleDelNamespace((PktDelNamespace *)pkt);
            break;
        default:
            log_message(kLogLevelError, "unknown pkt_id=%u for config server\n", pkt->GetPktId());
            break;
    }
}

void ConfigServerConn::_HandleNamespaceListReq(PktNamespaceListReq* pkt)
{
    log_message(kLogLevelInfo, "NamespaceListReq\n");
    
    vector<string> ns_list = g_namespace_manager.GetNamespaceList();
    PktNamespaceListResp resp_pkt(ns_list);
    SendPkt(&resp_pkt);
}

void ConfigServerConn::_HandleNamespaceListResp(PktNamespaceListResp* pkt)
{
    vector<string>& ns_list = pkt->GetNamespaceList();
    string addr_info = g_config.cs_listen_ip + ":" + std::to_string(g_config.cs_listen_port);
    
    for (const string& ns : ns_list) {
        PktBucketTableReq req_pkt(ns, TABLE_REQ_FROM_CONFIG_SERVER, addr_info);
        SendPkt(&req_pkt);
        log_message(kLogLevelInfo, "NamespaceListResp: %s\n", ns.c_str());
    }
}

void ConfigServerConn::_HandleBucketTableReq(PktBucketTableReq* pkt)
{
    handle_bucket_table_req(this, pkt, TABLE_REQ_FROM_CONFIG_SERVER);
}

void ConfigServerConn::_HandleBucketTableResp(PktBucketTableResp* pkt)
{
    string& ns = pkt->GetNamespace();
    uint32_t result = pkt->GetResult();
    if (result) {
        log_message(kLogLevelError, "BucketTableResp failed, ns=%s, result=%d\n", ns.c_str(), result);
        return;
    }
    
    uint32_t version = pkt->GetVersion();
    uint16_t migrating_bucket_id = pkt->GetMigratingBucketId();
    string& migrating_server_addr = pkt->GetMigratingServerAddr();
    map<string, vector<uint16_t>> server_bucket_map = pkt->GetServerBucketsMap();
    map<uint16_t, string> bucket_server_map = transform_table(server_bucket_map);
    
    log_message(kLogLevelInfo, "BucketTableResp, ns=%s, version=%d, bucket_id=%d, server_addr=%s\n",
                ns.c_str(), version, migrating_bucket_id, migrating_server_addr.c_str());
    if (!g_namespace_manager.InitTable(ns, version, bucket_server_map, migrating_bucket_id, migrating_server_addr, true)) {
        log_message(kLogLevelError, "InitTable failed, ns=%s\n", ns.c_str());
    }
}

void ConfigServerConn::_HandleDelNamespace(PktDelNamespace* pkt)
{
    string& ns = pkt->GetNamespace();
    log_message(kLogLevelInfo, "DelNamespace ns=%s\n", ns.c_str());
    
    g_namespace_manager.DelTable(ns);
}
