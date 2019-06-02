//
//  redis_request.cpp
//
//  Created by ziteng on 19-5-28.
//

#include "redis_request.h"
#include "client_conn.h"
#include "simple_log.h"
#include "async_redis_conn.h"

bool SingleRedisRequest::HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec)
{
    seq_no_ = client_conn->IncrSeqNo();
    build_request(cmd_vec, request_);
    ClientSeq_t req_seq = {client_conn->GetHandle(), seq_no_, 1};
    
    AsyncRedisConn* redis_conn = g_redis_conns.GetIOResource(client_conn->GetHandle());
    redis_conn->DoRawCmd(req_seq, request_);
    state_ = REQUEST_STATE_SENDING_REQUEST;
    return true;
}

void SingleRedisRequest::HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len)
{
    if (state_ == REQUEST_STATE_SENDING_REQUEST) {
        response_.append(buf, len);
        state_ = REQUEST_STATE_COMPLETE;
    } else {
        log_message(kLogLevelError, "receive duplicated response\n");
    }
}

bool PipelineRedisRequest::HandlePipelineCmd(ClientConn* client_conn, vector<vector<string>>& request_vec)
{
    total_cmd_cnt_ = (int)request_vec.size();
    response_cmd_cnt_ = 0;
    
    seq_no_ = client_conn->IncrSeqNo();
    net_handle_t conn_handle = client_conn->GetHandle();
    AsyncRedisConn* redis_conn = g_redis_conns.GetIOResource(conn_handle);
    
    string request_pipeline;
    for (int i = 0; i < total_cmd_cnt_; i++) {
        vector<string>& cmd_vec = request_vec[i];
        string request;
        build_request(cmd_vec, request);
        request_pipeline.append(request);
    }
    
    ClientSeq_t req_seq = {conn_handle, seq_no_, (uint32_t)total_cmd_cnt_};
    redis_conn->DoRawCmd(req_seq, request_pipeline);
    
    state_ = REQUEST_STATE_SENDING_REQUEST;
    return true;
}

void PipelineRedisRequest::HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len)
{
    response_.append(buf, len);
    response_cmd_cnt_++;
    
    if (response_cmd_cnt_ == total_cmd_cnt_) {
        state_ = REQUEST_STATE_COMPLETE;
    }
}
