/*
 *  bucket_table.cpp
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#include "bucket_table.h"
#include "mur_mur_hash2.h"
#include "string_match.h"
#include "sha1.h"

BucketTable g_bucket_table;

// performance improvement，int2string is much more faster than std::to_string, and profiler tool shows that
// to_string in ProcessKey is a hot spot
static string int2string(int val)
{
    if (val == 0) {
        return "0";
    }
    
    char sign = '+';
    if (val < 0) {
        val = -val;
        sign = '-';
    }
    
    char buf[16];
    buf[15] = 0;
    int pos = 14;
    while (val > 0) {
        buf[pos] = val % 10 + '0';
        --pos;
        val /= 10;
    }
    
    if (sign == '-') {
        buf[pos] = sign;
        --pos;
    }
    return buf + pos + 1;
}

void BucketTable::Init(uint32_t version, const map<uint16_t, string>& bucket_map,
                       uint16_t migrating_bucket_id, const string& migrating_server_addr)
{
    lock_guard<mutex> lock(table_mutex_);
    if (version_ != version) {
        version_ = version;
        bucket_map_ = bucket_map;
        migrating_bucket_id_ = migrating_bucket_id;
        migrating_server_addr_ = migrating_server_addr;
        bucket_cnt_ = (uint16_t)bucket_map.size();
    }
}

void BucketTable::GetBucketTalbe(uint32_t& version, map<uint16_t, string>& bucket_map,
                                 uint16_t& migrating_bucket_id, string& migrating_server_addr)
{
    lock_guard<mutex> lock(table_mutex_);
    version = version_;
    bucket_map = bucket_map_;
    migrating_bucket_id = migrating_bucket_id_;
    migrating_server_addr = migrating_server_addr_;
}

bool BucketTable::IsTableUpdated(uint32_t version)
{
    lock_guard<mutex> lock(table_mutex_);
    
    if (version == version_) {
        return false;
    } else {
        return true;
    }
}

void BucketTable::GetServerAddrs(uint32_t& version, set<string>& server_addr_set)
{
    lock_guard<mutex> lock(table_mutex_);
    
    version = version_;
    for (auto it = bucket_map_.begin(); it != bucket_map_.end(); ++it) {
        server_addr_set.insert(it->second);
    }
    
    if (!migrating_server_addr_.empty()) {
        server_addr_set.insert(migrating_server_addr_);
    }
}

bool BucketTable::IsKeyInMigrating(const string& key)
{
    uint32_t hash = mur_mur_hash2(key.c_str(), (int)key.size(), HASH_SEED);
    
    lock_guard<mutex> lock(table_mutex_);
    uint16_t bucket_id = hash % bucket_cnt_;
    
    if (bucket_id == migrating_bucket_id_) {
        return true;
    } else {
        return false;
    }
}

void BucketTable::ProcessKey(string& key, string& addr, string& migrate_addr)
{
    _ConvertKeyIfMatch(key);
    uint32_t hash = mur_mur_hash2(key.c_str(), (int)key.size(), HASH_SEED);
    
    lock_guard<mutex> lock(table_mutex_);
    uint16_t bucket_id = hash % bucket_cnt_;
    addr = bucket_map_[bucket_id];
    if (bucket_id == migrating_bucket_id_) {
        migrate_addr = migrating_server_addr_;
    }
    
    string bucket_prefix = int2string(bucket_id) + "_";
    key.insert(0, bucket_prefix);
}

void BucketTable::ProcessKeysCmd(vector<string>& cmd_vec, map<string, vector<string>>& addr_cmd_vec_map,
                                 string& old_addr, string& migrate_addr, vector<string>& migrate_cmd_vec)
{
    lock_guard<mutex> lock(table_mutex_);
    int cmd_cnt = (int)cmd_vec.size();
    for (int i = 1; i < cmd_cnt; ++i) {
        string& key = cmd_vec[i];
        _ConvertKeyIfMatch(key);
        
        uint32_t hash = mur_mur_hash2(key.c_str(), (int)key.size(), HASH_SEED);
        uint16_t bucket_id = hash % bucket_cnt_;
        string addr = bucket_map_[bucket_id];
        
        string bucket_prefix = int2string(bucket_id) + "_";
        key.insert(0, bucket_prefix);
        
        if (bucket_id == migrating_bucket_id_) {
            old_addr = addr;
            migrate_addr = migrating_server_addr_;
            
            if (migrate_cmd_vec.empty()) {
                migrate_cmd_vec.push_back(cmd_vec[0]);
            }
            migrate_cmd_vec.push_back(key);
            
            addr = migrating_server_addr_;
        }
        
        vector<string>& group_cmd_vec = addr_cmd_vec_map[addr];
        if (group_cmd_vec.empty()) {
            group_cmd_vec.push_back(cmd_vec[0]);
        }
        
        group_cmd_vec.push_back(key);
    }
}

void BucketTable::ProcessKeyValuesCmd(vector<string>& cmd_vec, map<string, vector<string>>& addr_cmd_vec_map,
                                      string& old_addr, string& migrate_addr, vector<string>& migrate_cmd_vec)
{
    lock_guard<mutex> lock(table_mutex_);
    int cmd_cnt = (int)cmd_vec.size();
    for (int i = 1; i < cmd_cnt; i += 2) {
        string& key = cmd_vec[i];
        string& value = cmd_vec[i + 1];
        _ConvertKeyIfMatch(key);
        
        uint32_t hash = mur_mur_hash2(key.c_str(), (int)key.size(), HASH_SEED);
        uint16_t bucket_id = hash % bucket_cnt_;
        string addr = bucket_map_[bucket_id];
        
        string bucket_prefix = int2string(bucket_id) + "_";
        key.insert(0, bucket_prefix);
        
        if (bucket_id == migrating_bucket_id_) {
            old_addr = addr;
            migrate_addr = migrating_server_addr_;
            
            if (migrate_cmd_vec.empty()) {
                migrate_cmd_vec.push_back(cmd_vec[0]);
            }
            migrate_cmd_vec.push_back(key);
            migrate_cmd_vec.push_back(value);
            
            addr = migrating_server_addr_;
        }
        
        vector<string>& group_cmd_vec = addr_cmd_vec_map[addr];
        if (group_cmd_vec.empty()) {
            group_cmd_vec.push_back(cmd_vec[0]);
        }
        
        group_cmd_vec.push_back(key);
        group_cmd_vec.push_back(value);
    }
}

void BucketTable::_ConvertKeyIfMatch(string &key)
{
    if (key_pattern_set_.empty()) {
        return;
    }
    
    for (const string& pattern : key_pattern_set_) {
        if (stringmatch(pattern,  key, 0)) {
            /* use 'key rewrite' to shield the original key
             it was used when we want to delete some key patterns like ABC*, 
             Redis's keys command will block the server, so we use this method to bypass that,
             Caution: it should only be used in the scenario of cache keys with a short TTL
             */
            key = SHA1(key);
            break;
        }
    }
}
