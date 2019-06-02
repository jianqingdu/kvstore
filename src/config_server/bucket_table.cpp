/*
 *  bucket_table.cpp
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#include "bucket_table.h"
#include "simple_log.h"
#include "file_operation.h"
#include "pkt_definition.h"
#include "config_parser.h"
#include "table_transform.h"

BucketTable::~BucketTable()
{
    del_bucket_table(namespace_);
}

bool BucketTable::InitTable(uint16_t bucket_cnt, const vector<string>& server_addr_vec)
{
    if ((bucket_cnt < kMinBucketCnt) || (bucket_cnt > kMaxBucketCnt)) {
        log_message(kLogLevelError, "bucket_cnt=%d not in range\n", bucket_cnt);
        return false;
    }
    
    if (server_addr_vec.empty()) {
        log_message(kLogLevelError, "empty server address\n");
        return false;
    }
    
    // check duplicate redis server address
    for (const string& addr : server_addr_vec) {
        server_addr_set_.insert(addr);
    }
    if (server_addr_set_.size() != server_addr_vec.size()) {
        log_message(kLogLevelError, "duplicated server address\n");
        return false;
    }
    
    version_ = 1;
    migrating_bucket_id_ = kInvalidBucketId;
    
    uint16_t server_cnt = (uint16_t)server_addr_vec.size();
    for (uint16_t bucket_id = 0; bucket_id < bucket_cnt; ++bucket_id) {
        bucket_map_[bucket_id] = server_addr_vec[bucket_id % server_cnt];
    }
    
    return save_bucket_table(namespace_, version_, bucket_map_, migrating_bucket_id_, migrating_server_addr_);
}

bool BucketTable::InitTable(uint32_t version, const map<uint16_t, string>& bucket_map,
               uint16_t migrating_bucket_id, const string& migrating_server_addr, bool save_table)
{
    if (bucket_map.empty()) {
        log_message(kLogLevelError, "empty table\n");
        return false;
    }
    
    if ((migrating_bucket_id != kInvalidBucketId) && (migrating_bucket_id > (uint16_t)bucket_map.size())) {
        log_message(kLogLevelError, "migrating_bucket_id=%d larger than table size\n", migrating_bucket_id);
        return false;
    }
    
    version_ = version;
    bucket_map_ = bucket_map;
    migrating_bucket_id_ = migrating_bucket_id;
    migrating_server_addr_ = migrating_server_addr;
    
    _UpdateServerAddrs();
    
    if (save_table) {
        return save_bucket_table(namespace_, version_, bucket_map_, migrating_bucket_id_, migrating_server_addr_);
    } else {
        return true;
    }
}

uint32_t BucketTable::UpdateTable(uint16_t bucket_id, const string& new_server_addr, bool is_migrating)
{
    if (bucket_map_.find(bucket_id) == bucket_map_.end()) {
        log_message(kLogLevelError, "UpdateTable, no bucket_id=%d\n", bucket_id);
        return MIGRATION_RESULT_NO_BUCKET_ID;
    }
    
    version_++;
    if (is_migrating) {
        migrating_bucket_id_ = bucket_id;
        migrating_server_addr_ = new_server_addr;
    } else {
        migrating_bucket_id_ = kInvalidBucketId;
        migrating_server_addr_.clear();
        bucket_map_[bucket_id] = new_server_addr;
    }
    
    _UpdateServerAddrs();
    
    if (save_bucket_table(namespace_, version_, bucket_map_, migrating_bucket_id_, migrating_server_addr_)) {
        return 0;
    } else {
        return MIGRATION_RESULT_SAVE_FAILURE;
    }
}

bool BucketTable::ScaleUpTable(const string& addr)
{
    server_buckets_map_t old_map = transform_table(bucket_map_);
    server_buckets_map_t new_map;
    scale_up_table(addr, bucket_map_.size(), old_map, new_map);
    
    bucket_map_ = transform_table(new_map);
    version_++;
    
    _UpdateServerAddrs();
    
    return save_bucket_table(namespace_, version_, bucket_map_, migrating_bucket_id_, migrating_server_addr_);
}

bool BucketTable::ScaleDownTable(const string& addr)
{
    if (server_addr_set_.size() <= 1) {
        log_message(kLogLevelError, "server size reach 1, can not scale down table\n");
        return false;
    }
    
    if (server_addr_set_.find(addr) == server_addr_set_.end()) {
        log_message(kLogLevelError, "address not exist: %s\n", addr.c_str());
        return false;
    }
    
    server_buckets_map_t old_map = transform_table(bucket_map_);
    server_buckets_map_t new_map;
    scale_down_table(addr, bucket_map_.size(), old_map, new_map);
    
    if (new_map.empty()) {
        log_message(kLogLevelError, "scale_down_table failed: ns=%s\n", namespace_.c_str());
        return false;
    }
    
    bucket_map_ = transform_table(new_map);
    version_++;
    
    _UpdateServerAddrs();
    
    return save_bucket_table(namespace_, version_, bucket_map_, migrating_bucket_id_, migrating_server_addr_);
}

uint32_t BucketTable::ReplaceServerAddress(const string& old_addr, const string& new_addr)
{
    if (!HasServerAddr(old_addr)) {
        log_message(kLogLevelError, "no old_addr in this table\n");
        return MIGRATION_RESULT_ADDRESS_NOT_EXIST;
    }
    
    if (HasServerAddr(new_addr)) {
        log_message(kLogLevelError, "new_addr alreay in this table\n");
        return MIGRATION_RESULT_ADDRESS_EXIST;
    }
    
    version_++;
    server_addr_set_.erase(old_addr);
    server_addr_set_.insert(new_addr);
    
    for (auto it = bucket_map_.begin(); it != bucket_map_.end(); ++it) {
        if (it->second == old_addr) {
            it->second = new_addr;
        }
    }
    
    if (migrating_server_addr_ == old_addr) {
        migrating_server_addr_ = new_addr;
    }
    
    if (save_bucket_table(namespace_, version_, bucket_map_, migrating_bucket_id_, migrating_server_addr_)) {
        return 0;
    } else {
        return MIGRATION_RESULT_SAVE_FAILURE;
    }
}

bool BucketTable::HasServerAddr(const string& addr)
{
    if (server_addr_set_.find(addr) != server_addr_set_.end()) {
        return true;
    } else {
        return false;
    }
}

void BucketTable::_UpdateServerAddrs()
{
    server_addr_set_.clear();
    for (auto it = bucket_map_.begin(); it != bucket_map_.end(); ++it) {
        server_addr_set_.insert(it->second);
    }
    
    if (!migrating_server_addr_.empty()) {
        server_addr_set_.insert(migrating_server_addr_);
    }
}
