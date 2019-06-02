/*
 *  bucket_table.h
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#ifndef __CS_BUCKET_TABLE_H__
#define __CS_BUCKET_TABLE_H__

#include "util.h"

const uint16_t kMinBucketCnt = 256;
const uint16_t kMaxBucketCnt = 2048;
const uint16_t kInvalidBucketId = 0xFFFF;

class BucketTable {
public:
    BucketTable(const string& ns) : namespace_(ns) {}
    virtual ~BucketTable();
    
    string& GetNamespace() { return namespace_; }
    uint32_t GetVersion() { return version_; }
    map<uint16_t, string>& GetBucketMap() { return bucket_map_; }
    uint16_t GetMigratingBucetId() { return migrating_bucket_id_; }
    string& GetMigratingServerAddr() { return migrating_server_addr_; }
    
    // 按照平均分配bucket到每个redis实例的原则初始化bucket对照表, 来自HTTP后台
    bool InitTable(uint16_t bucket_cnt, const vector<string>& server_addr_vec);
    
    // 根据从disk读取或从master同步过来的对照表内容，初始化或更新对照表
    bool InitTable(uint32_t version, const map<uint16_t, string>& bucket_map,
                   uint16_t migrating_bucket_id, const string& migrating_server_addr, bool save_table);
    
    // 只更新一个bucket的redis地址，设置或清除正在迁移的bucket_id
    uint32_t UpdateTable(uint16_t bucket_id, const string& new_server_addr, bool is_migrating);
    
    // 用于cache集群
    bool ScaleUpTable(const string& addr);
    
    bool ScaleDownTable(const string& addr);
    
    // 主redis的机器挂了，通过HTTP后台手动切换备redis为主
    uint32_t ReplaceServerAddress(const string& old_addr, const string& new_addr);
    
    bool HasServerAddr(const string& addr);
private:
    void _UpdateServerAddrs();
    
    string                  namespace_; // 保存bucket对照表时的文件名
    uint32_t                version_;
    map<uint16_t, string>   bucket_map_;
    uint16_t                migrating_bucket_id_;
    string                  migrating_server_addr_;
    
    set<string>             server_addr_set_;
};

#endif
