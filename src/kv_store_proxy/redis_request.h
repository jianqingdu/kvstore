//
//  redis_request.h
//  kv-store
//
//  Created by ziteng on 16-6-16.
//

#ifndef __PROXY_REDIS_REQUEST_H__
#define __PROXY_REDIS_REQUEST_H__

#include "util.h"
#include "redis_parser.h"

enum {
    REQUEST_STATE_IDLE              = 0,
    REQUEST_STATE_MIGRATING         = 1,
    REQUEST_STATE_SENDING_REQUEST   = 2,
    REQUEST_STATE_COMPLETE          = 3,
};

enum {
    REQUEST_TYPE_BASE               = 0,
    REQUEST_TYPE_SINGLE_KEY         = 1,
    REQUEST_TYPE_MULTIPLE_KEY       = 2,
    REQUEST_TYPE_MULTIPLE_KEY_VALUE = 3,
    REQUEST_TYPE_PIPELINE           = 4,
    REQUEST_TYPE_FLUSHDB            = 5,
    REQUEST_TYPE_RANDOMKEY          = 6,
};

const string kRedisErrorParseFailure = "-ERR parse redis protocol failed\r\n";
const string kRedisErrorReachQpsLimit = "-ERR reach qps limit\r\n";
const string kRedisErrorCommandNotSupport = "-ERR command not support\r\n";
const string KRedisErrorNoRedisConn = "-ERR no redis connection\r\n";
const string kRedisErrorNumOfArgument = "-ERR wrong number of arguments\r\n";
const string kRedisErrorTimeout = "-ERR request timeout\r\n";
const string kRedisErrorMaxClientLimit = "-ERR reach max number of clients\r\n";

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
    virtual void HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len, const string& server_addr) {}
protected:
    int         state_;
    uint32_t    seq_no_;
    uint64_t    start_time_;    // milliseconds
    
    string      request_;
    string      response_;
};

class SingleKeyRedisRequest : public RedisRequest
{
public:
    SingleKeyRedisRequest() {}
    virtual ~SingleKeyRedisRequest() {}
    
    virtual int GetType() { return REQUEST_TYPE_SINGLE_KEY; }
    virtual bool HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec);
    virtual void HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len, const string& server_addr);
private:
    string  migrating_addr_;
};

class MultipleKeysRedisRequest : public RedisRequest
{
public:
    MultipleKeysRedisRequest() : total_cnt_(0), migration_count_(0), request_count_(0) {}
    virtual ~MultipleKeysRedisRequest() {}
    
    virtual int GetType() { return REQUEST_TYPE_MULTIPLE_KEY; }
    virtual bool HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec);
    virtual void HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len, const string& server_addr);
public:
    vector<string> cmd_vec_;
    map<string, vector<string>> addr_cmd_vec_map_;
    
    // MGET need a kv map, so when all response received, we can reorder the response with the sequence of request keys
    map<string, string> kv_map_;
    long    total_cnt_;
    int     migration_count_;
    int     request_count_;
};

class MultipleKeyValuesRedisRequest : public RedisRequest
{
public:
    MultipleKeyValuesRedisRequest() :migration_count_(0), request_count_(0) {}
    virtual ~MultipleKeyValuesRedisRequest() {}
    
    virtual int GetType() { return REQUEST_TYPE_MULTIPLE_KEY_VALUE; }
    virtual bool HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec);
    virtual void HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len, const string& server_addr);
public:
    map<string, vector<string>> addr_cmd_vec_map_;
    int migration_count_;
    int request_count_;
};

class PipelineRedisRequest : public RedisRequest {
public:
    PipelineRedisRequest() : request_addr_vec_(NULL), response_vec_(NULL) {}
    virtual ~PipelineRedisRequest() {
        if (request_addr_vec_) {
            delete [] request_addr_vec_;
        }
        
        if (response_vec_) {
            delete [] response_vec_;
        }
    }
    
    virtual int GetType() { return REQUEST_TYPE_PIPELINE; }
    int GetTotalCmdCnt() { return total_cmd_cnt_; }
    bool HandlePipelineCmd(ClientConn* client_conn, vector<vector<string>>& request_vec);
    virtual void HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len, const string& server_addr);
private:
    int     total_cmd_cnt_;
    int     response_cmd_cnt_;
    string* request_addr_vec_;
    string* response_vec_;
};

class FlushDBRedisRequest : public RedisRequest {
public:
    FlushDBRedisRequest() {}
    virtual ~FlushDBRedisRequest() {}
    
    virtual int GetType() { return REQUEST_TYPE_FLUSHDB; }
    virtual bool HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec);
    virtual void HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len, const string& server_addr);
private:
    int total_server_cnt_; 
};

class RandomKeyRedisRequest : public RedisRequest {
public:
    RandomKeyRedisRequest() {}
    virtual ~RandomKeyRedisRequest() {}
    
    virtual int GetType() { return REQUEST_TYPE_RANDOMKEY; }
    virtual bool HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec);
    virtual void HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len, const string& server_addr);
private:
    int total_server_cnt_;
    vector<string> key_vec_;
};

#endif
