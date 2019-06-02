/*
 *  common.cpp
 *
 *  Created on: 2016-7-20
 *      Author: ziteng
 */

#include "common.h"
#include "simple_log.h"
#include "table_transform.h"
#include "namespace_manager.h"

void handle_bucket_table_req(BaseConn* conn, PktBucketTableReq* pkt, uint32_t expect_from_type)
{
    string& ns = pkt->GetNamespace();
    uint32_t from_type = pkt->GetFromType();
    string& info = pkt->GetInfo();
    char* peer_ip = conn->GetPeerIP();
    uint16_t peer_port = conn->GetPeerPort();
    log_message(kLogLevelInfo, "handle_bucket_table_req, ns=%s, from_type=%d, info=%s, peer_addr=%s:%d\n",
                ns.c_str(), from_type, info.c_str(), peer_ip, peer_port);
    if (from_type != expect_from_type) {
        PktBucketTableResp resp_pkt(ns, TABLE_RESULT_WRONG_REQUESTOR);
        conn->SendPkt(&resp_pkt);
        return;
    }
    
    uint32_t version = 0;
    bucket_server_map_t bucket_map;
    uint16_t migrating_bucket_id;
    string migrating_server_addr;
    bool success = g_namespace_manager.GetBucketTable(ns, version, bucket_map, migrating_bucket_id, migrating_server_addr);
    if (!success) {
        PktBucketTableResp resp_pkt(ns, TABLE_RESULT_NO_NAMESPACE);
        conn->SendPkt(&resp_pkt);
        return;
    }
    
    server_buckets_map_t server_map = transform_table(bucket_map);
    PktBucketTableResp resp_pkt(ns, version, server_map, migrating_bucket_id, migrating_server_addr);
    conn->SendPkt(&resp_pkt);
    
    if (from_type == TABLE_REQ_FROM_PROXY) {
        int proxy_state = migrating_server_addr.empty() ? PROXY_STATE_ONLINE : PROXY_STATE_MIGRATING;
        g_namespace_manager.AddProxy(ns, conn->GetHandle(), proxy_state, info);
    }
}

void jsonize_bucket_table(Json::Value& root, uint32_t version, const map<uint16_t, string>& bucket_map,
                          uint16_t migrating_bucket_id, const string& migrating_server_addr)
{
    server_buckets_map_t server_map = transform_table(bucket_map);
    Json::Value json_table;
    for (auto it_server = server_map.begin(); it_server != server_map.end(); ++it_server) {
        string server_addr = it_server->first;
        Json::Value json_buckets;
        
        for (uint16_t bucket_id : it_server->second) {
            json_buckets.append(bucket_id);
        }
        
        json_table[server_addr] = json_buckets;
    }
    
    root["table"] = json_table;
    root["version"] = version;
    root["migrating_bucket_id"] = migrating_bucket_id;
    root["migrating_server_addr"] = migrating_server_addr;
}
