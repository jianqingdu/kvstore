//
//  redis_conn_table.h
//  kv-store
//
//  Created by ziteng on 16-5-19.
//

#ifndef __PROXY_REDIS_CONN_TABLE_H__
#define __PROXY_REDIS_CONN_TABLE_H__

#include "async_redis_conn.h"
#include "io_thread_resource.h"

// 每个线程都有自己的RedisConnTable，所以没必要用线程锁,
// 每个线程用自己的ClientConn的m_handle来获取自己的RedisConnTable, 比如:
// RedisConnTable* conn_table = g_redis_conn_tables.GetIOResource(m_handle)
// conn_table->Update();

class RedisConnTable {
public:
    RedisConnTable() : bucket_table_version_(0) {}
    virtual ~RedisConnTable();
    
    void Init(int thread_index) { thread_index_ = thread_index; }
    void Update();
    AsyncRedisConn* GetConn(const string& addr);
    
    int GetPendingRequestCnt();
private:
    void _AddConn(const string& addr);
private:
    // 每个线程都有自己的bucket对照表版本号，如果该版本号和g_bucket_table里面的版本不一致，
    // 则需要更新本线程的addr_conn_map_
    int                             thread_index_;
    uint32_t                        bucket_table_version_;
    map<string, AsyncRedisConn*>    addr_conn_map_;
};

extern IoThreadResource<RedisConnTable> g_redis_conn_tables;


#endif
