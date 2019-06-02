/*
 * namespace_manager.h
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#ifndef __CS_NAMESPACE_MANAGER_H__
#define __CS_NAMESPACE_MANAGER_H__

#include "bucket_table.h"
#include "pkt_definition.h"

enum {
    PROXY_STATE_OFFLINE     = 0,
    PROXY_STATE_ONLINE      = 1,
    PROXY_STATE_FREEZE      = 2,
    PROXY_STATE_FREEZE_ACK  = 3,
    PROXY_STATE_MIGRATING   = 4,
};

const string kProxyStateName[] = {"OFFLINE", "ONLINE", "FREEZE", "FREEZE_ACK", "MIGRATING"};

struct ProxyStateInfo_t {
    int     state;
    string  info;
};

struct MigrationAck_t {
    int         handle;
    uint16_t    bucket_id;
    uint32_t    pkt_type;
};

typedef map<net_handle_t, ProxyStateInfo_t> ProxyInfoMap;
typedef map<string, set<net_handle_t>> ServerDownCount; // redis address -> all proxy that report this server is down

class MigrationConn;

// ScaleUpTable, ScaleDownTable, StartMigration, CompleteMigration, ReplaceServerAddress这5个方法里面
// 如果bucket对照表的更新涉及到多个proxy，则需要用Two Phase Commit(2PC) Protocol来更新, 这样就不会出现多个proxy
// 在更新瞬间的状态不一致问题， 2PC协议:
// 1. 1st phase: 通知所有proxy进入freeze状态，在该状态不处理请求
// 2. 2st phase: cs收到**所有proxy**进入freeze状态的ack后，广播通知所有proxy最新的bucket对照表，
//      proxy可以继续处理请求，包括freeze状态时缓存的请求

class NamespaceManager {
public:
    NamespaceManager() {}
    virtual ~NamespaceManager() {}
    
    // generate the table by HTTP request
    bool InitTable(const string& ns, uint16_t bucket_cnt, const vector<string>& server_addr_vec);
    
    // generate the table by load from file or from master cs
    // can be init or update
    bool InitTable(const string& ns, uint32_t version, const map<uint16_t, string>& bucket_map,
                   uint16_t migrating_bucket_id, const string& migrating_server_addr, bool save_file);
    
    bool DelTable(const string& ns);
    
    bool ScaleUpTable(const string& ns, const string& addr);
    
    bool ScaleDownTable(const string& ns, const string& addr);
    
    void ReportServerDown(const string& ns, const string& addr, const string& slave_addr, net_handle_t handle);
    void ReportServerUp(const string& ns, const string& addr, net_handle_t handle);
    
    vector<string> GetNamespaceList();
    bool GetBucketTable(const string& ns, uint32_t& version, map<uint16_t, string>& table,
                        uint16_t& migrating_bucket_id, string& migrating_server_addr);
    
    void AddProxy(const string& ns, net_handle_t handle, int state, const string& info);
    void DelProxy(const string& ns, net_handle_t handle);
    bool GetProxyInfo(const string& ns, ProxyInfoMap& proxy_info_map);
    
    uint32_t StartMigration(const string& ns, uint16_t bucket_id, const string& new_server_addr,
                            uint8_t scale_up, MigrationConn* conn);
    uint32_t CompleteMigration(const string& ns, uint16_t bucket_id, const string& new_server_addr,
                               uint8_t scale_up, MigrationConn* conn);
    
    uint32_t ReplaceServerAddress(const string& ns, const string& old_addr, const string& new_addr);
    
    void HandleFreezeProxyAck(const string& ns, net_handle_t proxy_handle);
private:
    void _DoHA(const string& ns, const string& addr, const string& new_addr);
    int _SendBackAck(const string& ns);
    void _BroadcastTable(BucketTable* table);
    
private:
    mutex                       ns_mutex_;
    map<string, BucketTable*>   ns_table_map_;
    map<string, ProxyInfoMap>   ns_proxy_map_;
    map<string, ServerDownCount> ns_server_down_map_;
    map<string, MigrationAck_t> ns_migration_map_;
};

extern NamespaceManager g_namespace_manager;

#endif
