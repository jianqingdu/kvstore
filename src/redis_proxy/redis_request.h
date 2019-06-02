//
//  redis_request.h
//
//  Created by ziteng on 19-5-28.
//

#ifndef __PROXY_REDIS_REQUEST_H__
#define __PROXY_REDIS_REQUEST_H__

#include "util.h"
#include "redis_parser.h"

enum {
    REQUEST_STATE_IDLE              = 0,
    REQUEST_STATE_SENDING_REQUEST   = 1,
    REQUEST_STATE_COMPLETE          = 2,
};

enum {
    REQUEST_TYPE_BASE               = 0,
    REQUEST_TYPE_SINGLE             = 1,
    REQUEST_TYPE_PIPELINE           = 2,
};

const string kRedisErrorParseFailure = "-ERR parse redis protocol failed\r\n";
const string kRedisErrorReachQpsLimit = "-ERR reach qps limit\r\n";
const string kRedisErrorCommandNotSupport = "-ERR command not support\r\n";
const string KRedisErrorNoRedisConn = "-ERR no redis connection\r\n";
const string kRedisErrorNumOfArgument = "-ERR wrong number of arguments\r\n";
const string kRedisErrorTimeout = "-ERR request timeout\r\n";
const string kRedisErrorMaxClientLimit = "-ERR reach max number of clients\r\n";
const string kRedisErrorReadonly = "-ERR readonly mode\r\n";

class ClientConn;

// asynchronous redis request context
class RedisRequest
{
public:
    RedisRequest() : state_(REQUEST_STATE_IDLE) { start_time_ = get_tick_count(); }
    RedisRequest(uint32_t seq_no, const string& resp) :
        state_(REQUEST_STATE_COMPLETE), seq_no_(seq_no), response_(resp) {
            start_time_ = get_tick_count();
    }
    virtual ~RedisRequest() {}
    
    uint32_t GetSeqNo() { return seq_no_; }
    uint64_t GetStartTime() { return start_time_; }
    bool IsComplete() { return state_ == REQUEST_STATE_COMPLETE; }
    string& GetResponse() { return response_; }
    void SetFailureResponse(const string& resp) { response_ = resp; state_ = REQUEST_STATE_COMPLETE; }
    
    virtual int GetType() { return REQUEST_TYPE_BASE; }
    virtual bool HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec) { return true; }
    virtual void HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len) {}
protected:
    int         state_;
    uint32_t    seq_no_;
    uint64_t    start_time_;    // milliseconds
    
    string      request_;
    string      response_;
};

class SingleRedisRequest : public RedisRequest
{
public:
    SingleRedisRequest() {}
    virtual ~SingleRedisRequest() {}
    
    virtual int GetType() { return REQUEST_TYPE_SINGLE; }
    virtual bool HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec);
    virtual void HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len);
};

class PipelineRedisRequest : public RedisRequest {
public:
    PipelineRedisRequest() {}
    virtual ~PipelineRedisRequest() {}
    
    virtual int GetType() { return REQUEST_TYPE_PIPELINE; }
    int GetTotalCmdCnt() { return total_cmd_cnt_; }
    bool HandlePipelineCmd(ClientConn* client_conn, vector<vector<string>>& request_vec);
    virtual void HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len);
private:
    int     total_cmd_cnt_;
    int     response_cmd_cnt_;
};

#endif
