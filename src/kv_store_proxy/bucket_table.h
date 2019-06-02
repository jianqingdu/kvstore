/*
 *  bucket_table.h
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#ifndef __PROXY_BUCKET_TABLE_H__
#define __PROXY_BUCKET_TABLE_H__

#include "util.h"

class BucketTable {
public:
    BucketTable() : version_(0) {}
    virtual ~BucketTable() {}
    
    void Init(uint32_t version, const map<uint16_t, string>& bucket_map,
              uint16_t migrating_bucket_id, const string& migrating_server_addr);
    
    void GetBucketTalbe(uint32_t& version, map<uint16_t, string>& bucket_map,
                        uint16_t& migrating_bucket_id, string& migrating_server_addr);
    
    bool IsTableUpdated(uint32_t version);
    
    void GetServerAddrs(uint32_t& version, set<string>& server_addr_set);
    
    bool IsKeyInMigrating(const string& key);
    
    // use key to calculate bucket_id, change key，insert bucket_id_ before key
    // get redis server address by bucket_id，if bucket_id is migrating，also return migrating destination address
    void ProcessKey(string& key, string& addr, string& migrate_addr);
    
    void ProcessKeysCmd(vector<string>& cmd_vec, map<string, vector<string>>& addr_cmd_vec_map,
                        string& old_addr, string& migrate_addr, vector<string>& migrate_cmd_vec);
    
    void ProcessKeyValuesCmd(vector<string>& cmd_vec, map<string, vector<string>>& addr_cmd_vec_map,
                             string& old_addr, string& migrate_addr, vector<string>& migrate_cmd_vec);
    
    void AddPattern(const string& pattern) { key_pattern_set_.insert(pattern); }
    void DelPattern(const string& pattern) { key_pattern_set_.erase(pattern); }

private:
    void _ConvertKeyIfMatch(string& key);
    
private:
    mutex                   table_mutex_;
    uint32_t                version_;
    map<uint16_t, string>   bucket_map_;
    uint16_t                migrating_bucket_id_;
    string                  migrating_server_addr_;
    uint16_t                bucket_cnt_;
    
    set<string>             key_pattern_set_; // the key of these patten to sha1(key)
};

extern BucketTable g_bucket_table;

#endif
