/*
 * pkt_definition.cpp
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#include "pkt_definition.h"

PktHeartBeat::PktHeartBeat()
{
    SETUP_PKT_HEADER(PKT_ID_HEARTBEAT)
    WriteHeader();
}

PktHeartBeat::PktHeartBeat(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    PARSE_PACKET_ASSERT
}


PktNamespaceListReq::PktNamespaceListReq()
{
    SETUP_PKT_HEADER(PKT_ID_NAMESPACE_LIST_REQ)
    WriteHeader();
}

PktNamespaceListReq::PktNamespaceListReq(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    PARSE_PACKET_ASSERT
}

PktNamespaceListResp::PktNamespaceListResp(const vector<string>& namespace_list)
{
    SETUP_PKT_HEADER(PKT_ID_NAMESPACE_LIST_RESP)
    
    os << (uint32_t)namespace_list.size();
    for (const string& ns : namespace_list) {
        os << ns;
    }
    WriteHeader();
}

PktNamespaceListResp::PktNamespaceListResp(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    
    uint32_t ns_cnt;
    is >> ns_cnt;
    for (uint32_t i = 0; i < ns_cnt; ++i) {
        string ns;
        is >> ns;
        namespace_list_.push_back(ns);
    }
    PARSE_PACKET_ASSERT
}

PktBucketTableReq::PktBucketTableReq(const string& ns, uint32_t from, const string& info)
{
    SETUP_PKT_HEADER(PKT_ID_BUCKET_TABLE_REQ)
    
    os << ns;
    os << from;
    os << info;
    WriteHeader();
}

PktBucketTableReq::PktBucketTableReq(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    
    is >> namespace_;
    is >> from_;
    is >> info_;
    PARSE_PACKET_ASSERT
}

PktBucketTableResp::PktBucketTableResp(const string& ns, uint32_t result)
{
    SETUP_PKT_HEADER(PKT_ID_BUCKET_TABLE_RESP)
    
    os << ns;
    os << result;
    WriteHeader();
}

PktBucketTableResp::PktBucketTableResp(const string& ns, uint32_t version,
                                       const map<string, vector<uint16_t>>& server_buckets_map,
                                       uint16_t migrating_bucket_id, const string& migrating_server_addr)
{
    SETUP_PKT_HEADER(PKT_ID_BUCKET_TABLE_RESP)
    
    os << ns;
    os << (uint32_t)0;
    os << version;
    os << (uint32_t)server_buckets_map.size();
    for (auto it_server = server_buckets_map.begin(); it_server != server_buckets_map.end(); ++it_server) {
        string server_addr = it_server->first;
        
        os << server_addr;
        os << (uint32_t)it_server->second.size();
        for (uint16_t bucket_id : it_server->second) {
            os << bucket_id;
        }
    }
    
    os << migrating_bucket_id;
    os << migrating_server_addr;
    
    WriteHeader();
}

PktBucketTableResp::PktBucketTableResp(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    
    uint32_t server_cnt;
    is >> namespace_;
    is >> result_;
    
    if (result_) {
        version_ = 0;
        migrating_bucket_id_ = 0xFFFF;
        return;
    }
    
    is >> version_;
    is >> server_cnt;
    for (uint32_t i = 0; i < server_cnt; ++i) {
        string server_addr;
        uint32_t bucket_cnt;
        vector<uint16_t> bucket_vec;
        
        is >> server_addr;
        is >> bucket_cnt;
        for (uint32_t j = 0; j < bucket_cnt; ++j) {
            uint16_t bucket_id;
            is >> bucket_id;
            bucket_vec.push_back(bucket_id);
        }
        
        server_buckets_map_[server_addr] = bucket_vec;
    }
    
    is >> migrating_bucket_id_;
    is >> migrating_server_addr_;
    
    PARSE_PACKET_ASSERT
}

//////
PktStartMigration::PktStartMigration(const string& ns, uint16_t bucket_id,
                                     const string& new_server_addr, uint8_t scale_up)
{
    SETUP_PKT_HEADER(PKT_ID_START_MIGRATION)
    
    os << ns;
    os << bucket_id;
    os << new_server_addr;
    os << scale_up;
    WriteHeader();
}

PktStartMigration::PktStartMigration(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    
    is >> namespace_;
    is >> bucket_id_;
    is >> new_server_addr_;
    is >> scale_up_;
    PARSE_PACKET_ASSERT
}

PktStartMigrationAck::PktStartMigrationAck(const string& ns, uint16_t bucket_id, uint32_t result)
{
    SETUP_PKT_HEADER(PKT_ID_START_MIGRATION_ACK)
    
    os << ns;
    os << bucket_id;
    os << result;
    WriteHeader();
}

PktStartMigrationAck::PktStartMigrationAck(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    
    is >> namespace_;
    is >> bucket_id_;
    is >> result_;
    PARSE_PACKET_ASSERT
}

PktCompleteMigration::PktCompleteMigration(const string& ns, uint16_t bucket_id,
                                           const string& new_server_addr, uint8_t scale_up)
{
    SETUP_PKT_HEADER(PKT_ID_COMPLETE_MIGRATION)
    
    os << ns;
    os << bucket_id;
    os << new_server_addr;
    os << scale_up;
    WriteHeader();
}

PktCompleteMigration::PktCompleteMigration(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    
    is >> namespace_;
    is >> bucket_id_;
    is >> new_server_addr_;
    is >> scale_up_;
    PARSE_PACKET_ASSERT
}

PktCompleteMigrationAck::PktCompleteMigrationAck(const string& ns, uint16_t bucket_id, uint32_t result)
{
    SETUP_PKT_HEADER(PKT_ID_COMPLETE_MIGRATION_ACK)
    
    os << ns;
    os << bucket_id;
    os << result;
    WriteHeader();
}

PktCompleteMigrationAck::PktCompleteMigrationAck(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    
    is >> namespace_;
    is >> bucket_id_;
    is >> result_;
    PARSE_PACKET_ASSERT
}

PktDelNamespace::PktDelNamespace(const string& ns)
{
    SETUP_PKT_HEADER(PKT_ID_DEL_NAMESPACE)
    
    os << ns;
    WriteHeader();
}

PktDelNamespace::PktDelNamespace(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    
    is >> namespace_;
    PARSE_PACKET_ASSERT
}

PktFreezeProxy::PktFreezeProxy(const string& ns)
{
    SETUP_PKT_HEADER(PKT_ID_FREEZE_PROXY)
    
    os << ns;
    WriteHeader();
}

PktFreezeProxy::PktFreezeProxy(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    
    is >> namespace_;
    PARSE_PACKET_ASSERT
}

PktStorageServerDown::PktStorageServerDown(const string& ns, const string& addr, const string& slave_addr)
{
    SETUP_PKT_HEADER(PKT_ID_STORAGE_SERVER_DOWN)
    
    os << ns;
    os << addr;
    os << slave_addr;
    WriteHeader();
}

PktStorageServerDown::PktStorageServerDown(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    
    is >> namespace_;
    is >> server_addr_;
    is >> slave_addr_;
    PARSE_PACKET_ASSERT
}

PktStorageServerUp::PktStorageServerUp(const string& ns, const string& addr)
{
    SETUP_PKT_HEADER(PKT_ID_STORAGE_SERVER_UP)
    
    os << ns;
    os << addr;
    WriteHeader();
}

PktStorageServerUp::PktStorageServerUp(uchar_t* buf, uint32_t len)
{
    READ_PKT_HEADER
    
    is >> namespace_;
    is >> server_addr_;
    PARSE_PACKET_ASSERT
}
