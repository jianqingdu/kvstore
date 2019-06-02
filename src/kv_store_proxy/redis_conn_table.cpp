//
//  redis_conn_table.cpp
//  kv-store
//
//  Created by ziteng on 16-5-19.
//

#include "redis_conn_table.h"
#include "bucket_table.h"

IoThreadResource<RedisConnTable> g_redis_conn_tables;

RedisConnTable::~RedisConnTable()
{
    for (auto it = addr_conn_map_.begin(); it != addr_conn_map_.end(); ++it) {
        it->second->Close();
    }
}

void RedisConnTable::Update()
{
    if (!g_bucket_table.IsTableUpdated(bucket_table_version_)) {
        return;
    }
    
    set<string> addr_set;
    g_bucket_table.GetServerAddrs(bucket_table_version_, addr_set);
    
    if (addr_conn_map_.empty()) {
        // init
        for (const string& addr : addr_set) {
            _AddConn(addr);
        }
    } else {
        // update
        // 1. remove old redis conn if complete scale down
        for (auto it = addr_conn_map_.begin(); it != addr_conn_map_.end(); ) {
            auto it_old = it;
            ++it;
            if (addr_set.find(it_old->first) == addr_set.end()) {
                // 为AsyncRedisConn设置close标志，在自己OnTimer的时候Close，
                // 原因是当Bucket对照表变化时，ClientConn的OnTimer也可能调用Update，如果AsyncRedisConn和ClientConn
                // 在EventLoop里面的socket_map相邻，直接删除AsyncRedisConn对象，会导致EventLoop里面OnTimer的迭代器出错
                it_old->second->SetCloseInTimer();
                addr_conn_map_.erase(it_old);
            }
        }
        
        // 2. add new redis conn if just start scale up
        for (auto it = addr_set.begin(); it != addr_set.end(); ++it) {
            const string& addr = *it;
            if (addr_conn_map_.find(addr) == addr_conn_map_.end()) {
                _AddConn(addr);
            }
        }
    }
}

void RedisConnTable::_AddConn(const string& addr)
{
    string ip;
    int port;
    if (get_ip_port(addr, ip, port)) {
        AsyncRedisConn* redis_conn = new AsyncRedisConn(ip, port, thread_index_);
        addr_conn_map_[addr] = redis_conn;
    }
}

AsyncRedisConn* RedisConnTable::GetConn(const string& addr)
{
    auto it = addr_conn_map_.find(addr);
    if (it != addr_conn_map_.end()) {
        return it->second;
    } else {
        return NULL;
    }
}

int RedisConnTable::GetPendingRequestCnt()
{
    int cnt = 0;
    for (auto it = addr_conn_map_.begin(); it != addr_conn_map_.end(); ++it) {
        cnt += it->second->GetPendingRequestCnt();
    }
    
    return cnt;
}
