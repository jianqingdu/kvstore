/*
 *  async_redis_conn.h
 *
 *  Created on: 2016-6-14
 *      Author: ziteng
 */

#ifndef __PROXY_ASYNC_REDIS_CONN_H__
#define __PROXY_ASYNC_REDIS_CONN_H__

#include "base_conn.h"
#include "redis_parser.h"

typedef struct {
    net_handle_t    conn_handle;
    uint32_t        seq_no;
    uint32_t        request_cnt;
} ClientSeq_t;

class AsyncRedisConn : public BaseConn {
public:
    AsyncRedisConn(string server_ip, uint16_t server_port, int thread_index);
    virtual ~AsyncRedisConn() {}

    void SetCloseInTimer() { close_in_timer_ = true; }
    void DoRawCmd(ClientSeq_t client_seq, const string& cmd);
    
    virtual void OnConfirm();
    virtual void OnRead();
    virtual void OnClose();
    virtual void OnTimer(uint64_t curr_tick);

    int GetPendingRequestCnt() { return (int)request_list_.size(); }
private:
    void _HandleRedisReply(const RedisReply& reply, char* buf, int len);
    
private:
    string          server_ip_;
    uint16_t        server_port_;
    string          server_addr_;
    int             thread_index_;
    SimpleBuffer    pending_request_;
    list<ClientSeq_t>   request_list_;
    bool            close_in_timer_;
    bool            is_auth_;
};

#endif
