//
//  async_redis_conn.h
//
//  Created by ziteng on 19-5-28.
//

#ifndef __PROXY_ASYNC_REDIS_CONN_H__
#define __PROXY_ASYNC_REDIS_CONN_H__

#include "base_conn.h"
#include "redis_parser.h"
#include "io_thread_resource.h"

enum {
    REDIS_CONN_STATE_IDLE           = 1,
    REDIS_CONN_STATE_SEND_PASSWORD  = 2,
    REDIS_CONN_STATE_SELECT_DB      = 3,
    REDIS_CONN_STATE_OK             = 4,
};

typedef struct {
    net_handle_t    conn_handle;
    uint32_t        seq_no;
    uint32_t        request_cnt;
} ClientSeq_t;

class AsyncRedisConn : public BaseConn {
public:
    AsyncRedisConn();
    virtual ~AsyncRedisConn() {}

    void Init(int thread_index);
    void SetCloseInTimer() { close_in_timer_ = true; }
    void DoRawCmd(ClientSeq_t client_seq, const string& cmd);
    
    virtual void OnConfirm();
    virtual void OnRead();
    virtual void OnClose();
    virtual void OnTimer(uint64_t curr_tick);

    int GetPendingRequestCnt() { return (int)request_list_.size(); }
private:
    void _HandleRedisReply(const RedisReply& reply, char* buf, int len);
    
    void _SendAuthCmd();
    void _SendSelectCmd();
    void _SendPendingRequest();
private:
    int             thread_index_;
    SimpleBuffer    pending_request_;
    list<ClientSeq_t>   request_list_;
    bool            close_in_timer_;
    int             conn_state_;
};

extern IoThreadResource<AsyncRedisConn> g_redis_conns;

#endif
