/*
 *  client_conn.cpp
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#include "client_conn.h"
#include "config_parser.h"
#include "simple_log.h"
#include "util.h"
#include "bucket_table.h"
#include "redis_conn_table.h"
#include "stats_info.h"
#include "qps_limiter.h"
#include "config_server_conn.h"
#include <algorithm>
#include <ctype.h>
using namespace std;

StatsInfo g_stats_info;
QpsLimiter g_qps_limiter;

set<string> g_single_key_cmds = {
    // keys
    "DUMP", "EXISTS", "EXPIRE", "EXPIREAT", "PERSIST", "PEXPIRE", "PEXPIREAT", "PTTL", "RESTORE", "TTL", "TYPE",
    
    // strings
    "APPEND", "DECR", "DECRBY", "GET", "GETRANGE", "GETSET", "INCR", "INCRBY", "INCRBYFLOAT",
    "PSETEX", "SET", "SETEX", "SETNX", "SETRANGE", "STRLEN", "SETBIT", "GETBIT", "BITCOUNT", "BITPOS",
    
    // hashs
    "HDEL", "HEXISTS", "HGET", "HGETALL", "HINCRBY", "HINCRBYFLOAT", "HKEYS", "HLEN", "HMGET",
    "HMSET", "HSET", "HSETNX", "HVALS", "HSCAN",
    
    // sets
    "SADD", "SCARD", "SISMEMBER", "SMEMBERS", "SPOP", "SRANDMEMBER", "SREM", "SSCAN",
    
    // sorted sets
    "ZADD", "ZCARD", "ZCOUNT", "ZINCRBY", "ZLEXCOUNT", "ZRANGE", "ZRANGEBYLEX",
    "ZRANGEBYSCORE", "ZRANK", "ZREM", "ZREMRANGEBYLEX", "ZREMRANGEBYRANK", "ZREMRANGEBYSCORE",
    "ZREVRANGE", "ZREVRANGEBYLEX", "ZREVRANGEBYSCORE", "ZREVRANK", "ZSCORE", "ZSCAN",
    
    // lists
    "LINDEX", "LINSERT", "LLEN", "LPUSH", "LPUSHX", "LPOP", "LRANGE", "LREM", "LSET", "LTRIM", "RPOP",
    "RPUSH", "RPUSHX",
    
    // hyperloglog
    "PFADD",
};

set<string> g_multiple_key_cmds = {"DEL", "MGET", "PFCOUNT"};

set<string> g_multiple_key_value_cmds = {"MSET"};


ClientConn::ClientConn()
{
    g_stats_info.IncrClientCount();
    
    m_conn_timeout = g_config.client_timeout * 1000;
    seq_no_ = 0;
}

ClientConn::~ClientConn()
{
    g_stats_info.DecrClientCount();
    
    for (auto it = request_list_.begin(); it != request_list_.end(); ++it) {
        RedisRequest* req = *it;
        delete req;
    }
    request_list_.clear();
}

void ClientConn::OnConnect(BaseSocket* base_socket)
{
    BaseConn::OnConnect(base_socket);
    log_message(kLogLevelInfo, "connect from client %s:%d\n", m_peer_ip.c_str(), m_peer_port);
    if (g_stats_info.GetClientCount() > g_config.max_client_num) {
        log_message(kLogLevelError, "reach max client number\n");
        SendResponse(kRedisErrorMaxClientLimit);
        Close();
    }
}

void ClientConn::OnRead()
{
    _RecvData();
    
    // when parse redis protocol, this makes sure searching string pattern will not pass the boundary
    uchar_t* write_pos = m_in_buf.GetWriteBuffer();
    write_pos[0] = 0;
    
    vector<vector<string>> request_vec;
    
    while (true) {
        vector<string> cmd_vec;
        string error_msg;
        int ret = parse_redis_request((const char*)m_in_buf.GetReadBuffer(), m_in_buf.GetReadableLen(), cmd_vec, error_msg);
        if (ret > 0) {
            if (!cmd_vec.empty()) {
                string& cmd = cmd_vec[0];
                transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
            
                request_vec.push_back(cmd_vec);
            }
            m_in_buf.Read(NULL, ret);
        } else if (ret < 0) {
            string error_resp = "-ERR ";
            error_resp += error_msg;
            error_resp += "\r\n";
            SendResponse(error_resp);
            Close();
            return;
        } else {
            // not yet receive a whole command
            m_in_buf.ResetOffset();
            break;
        }
    }
    
    if (g_is_freeze) {
        freeze_request_list_.push_back(request_vec);
        return;
    }
    
    ++g_in_request_thread_cnt;
    
    _ProcessFreezeRequestList();
    
    int request_cnt = (int)request_vec.size();
    if (request_cnt > 1) {
        _HandlePipelineCommands(request_vec);
    } else if (request_cnt == 1) {
        _HandleRedisCommand(request_vec[0]);
    }
    --g_in_request_thread_cnt;
}

void ClientConn::OnTimer(uint64_t curr_tick)
{
    BaseConn::OnTimer(curr_tick);
    
    // checkout request timeout
    while (!request_list_.empty()) {
        RedisRequest* req = request_list_.front();
        uint64_t past_time = curr_tick - req->GetStartTime();
        if (past_time >= g_config.request_timeout) {
            uint32_t cmd_cnt =  (req->GetType() == REQUEST_TYPE_PIPELINE) ? ((PipelineRedisRequest*)req)->GetTotalCmdCnt() : 1;
            for (uint32_t i = 0; i < cmd_cnt; i++) {
                Send((void*)kRedisErrorTimeout.data(), (int)kRedisErrorTimeout.length());
            }
            g_stats_info.IncrSlowCmdCount();
            g_stats_info.IncrRtCount((uint32_t)past_time);
            request_list_.pop_front();
            delete req;
        } else {
            break;
        }
    }
    
    if (!g_is_freeze) {
        ++g_in_request_thread_cnt;
        _ProcessFreezeRequestList();
        --g_in_request_thread_cnt;
    }
}

void ClientConn::HandleResponse(uint32_t seq_no, const RedisReply& reply, char* buf, int len, const string& server_addr)
{
    for (auto it = request_list_.begin(); it != request_list_.end(); ++it) {
        RedisRequest* req = *it;
        if (req->GetSeqNo() == seq_no) {
            req->HandleResponse(this, reply, buf, len, server_addr);
            break;
        }
    }
    
    _SendCompleteRequest();
}

void ClientConn::SendResponse(const string& resp_str)
{
    if (request_list_.empty()) {
        Send((void*)resp_str.data(), (int)resp_str.size());
    } else {
        RedisRequest* request = new RedisRequest(IncrSeqNo(), resp_str);
        request_list_.push_back(request);
    }
}

void ClientConn::_HandlePipelineCommands(vector<vector<string>>& request_vec)
{
    // pipeline is counted as one query
    g_stats_info.IncrTotalCmdCount();
    
    int total_cmd_cnt = (int)request_vec.size();
    if (g_qps_limiter.IncrAndCheckLimit()) {
        // reach the QPS limit，sendback error message to every request
        for (int i = 0; i < total_cmd_cnt; i++) {
            SendResponse(kRedisErrorReachQpsLimit);
        }
        return;
    }
    
    for (int i = 0; i < total_cmd_cnt; i++) {
        vector<string>& cmd_vec = request_vec[i];
        string& cmd = cmd_vec[0];
        
        g_stats_info.IncrCmdCount(cmd);
        
        if ((g_single_key_cmds.find(cmd) == g_single_key_cmds.end()) || ((int)cmd_vec.size() < 2)
            || (g_bucket_table.IsKeyInMigrating(cmd_vec[1]))) {
            // do not process pipeline request if it's a multiple key reqeust or the key is in migration
            for (int i = 0; i < total_cmd_cnt; i++) {
                _HandleRedisCommand(request_vec[i], true);
            }
            
            return;
        }
    }
    
    PipelineRedisRequest* request = new PipelineRedisRequest;
    request_list_.push_back(request);
    if (!request->HandlePipelineCmd(this, request_vec)) {
        _SendCompleteRequest();
    }
}

void ClientConn::_HandleRedisCommand(vector<string>& cmd_vec, bool is_in_pipeline)
{
    string& cmd = cmd_vec[0];
    
    if (!is_in_pipeline) {
        g_stats_info.IncrTotalCmdCount();
        g_stats_info.IncrCmdCount(cmd);
        if (g_qps_limiter.IncrAndCheckLimit()) {
            SendResponse(kRedisErrorReachQpsLimit);
            return;
        }
    }
    
    if (cmd == "PING") {
        SendResponse("+PONG\r\n");
        return;
    }
    
    if (cmd == "QUIT") {
        SendResponse("+OK\r\n");
        Close();
        return;
    }
    
    if (cmd == "SELECT") {
        SendResponse("+OK\r\n");
        return;
    }
    
    if ((cmd == "FLUSHDB") || (cmd == "FLUSHALL")) {
        _HandleFlushDBCommand(cmd_vec);
        return;
    }
    
    if (cmd == "RANDOMKEY") {
        _HandleRandomKeyCommand(cmd_vec);
        return;
    }
    
    if ((int)cmd_vec.size() < 2) {
        log_message(kLogLevelError, "no argument for command: %s\n", cmd.c_str());
        SendResponse(kRedisErrorNumOfArgument);
        return;
    }
    
    if (g_single_key_cmds.find(cmd) != g_single_key_cmds.end()) {
        _HandleSingleKeyCommand(cmd_vec);
    } else if (g_multiple_key_cmds.find(cmd) != g_multiple_key_cmds.end()) {
        _HandleMultipleKeyCommand(cmd_vec);
    } else if (g_multiple_key_value_cmds.find(cmd) != g_multiple_key_value_cmds.end()) {
        _HandleMultipleKeyValueCommand(cmd_vec);
    } else {
        SendResponse(kRedisErrorCommandNotSupport);
        log_message(kLogLevelError, "not support redis command: %s\n", cmd.c_str());
    }
}

void ClientConn::_HandleSingleKeyCommand(vector<string>& cmd_vec)
{
    SingleKeyRedisRequest* request = new SingleKeyRedisRequest();
    request_list_.push_back(request);
    if (!request->HandleCmd(this, cmd_vec)) {
        _SendCompleteRequest();
    }
}

// MGET, DEL, PFCOUNT
void ClientConn::_HandleMultipleKeyCommand(vector<string>& cmd_vec)
{
    MultipleKeysRedisRequest* request = new MultipleKeysRedisRequest();
    request_list_.push_back(request);
    if (!request->HandleCmd(this, cmd_vec)) {
        _SendCompleteRequest();
    }
}

// MSET
void ClientConn::_HandleMultipleKeyValueCommand(vector<string>& cmd_vec)
{
    if (cmd_vec.size() % 2 != 1) {
        SendResponse(kRedisErrorNumOfArgument);
        return;
    }

    MultipleKeyValuesRedisRequest* request = new MultipleKeyValuesRedisRequest();
    request_list_.push_back(request);
    if (!request->HandleCmd(this, cmd_vec)) {
        _SendCompleteRequest();
    }
}

// FLASHDB, FLASHALL
void ClientConn::_HandleFlushDBCommand(vector<string>& cmd_vec)
{
    if (cmd_vec.size() != 1) {
        SendResponse(kRedisErrorNumOfArgument);
        return;
    }
    
    log_message(kLogLevelInfo, "FLUSHDB from client: %s\n", GetPeerIP());
    FlushDBRedisRequest* request = new FlushDBRedisRequest();
    request_list_.push_back(request);
    if (!request->HandleCmd(this, cmd_vec)) {
        _SendCompleteRequest();
    }
}

void ClientConn::_HandleRandomKeyCommand(vector<string>& cmd_vec)
{
    if (cmd_vec.size() != 1) {
        SendResponse(kRedisErrorNumOfArgument);
        return;
    }
    
    RandomKeyRedisRequest* request = new RandomKeyRedisRequest();
    request_list_.push_back(request);
    if (!request->HandleCmd(this, cmd_vec)) {
        _SendCompleteRequest();
    }
}

void ClientConn::_SendCompleteRequest()
{
    while (!request_list_.empty()) {
        RedisRequest* req = request_list_.front();
        if (req->IsComplete()) {
            uint32_t rt = (uint32_t)(get_tick_count() - req->GetStartTime());
            if (rt > g_config.slow_cmd_time) {
                g_stats_info.IncrSlowCmdCount();
            }
            
            g_stats_info.IncrRtCount(rt);
            
            string& resp = req->GetResponse();
            Send((void*)resp.data(), (int)resp.length());
            request_list_.pop_front();
            delete req;
        } else {
            break;
        }
    }
}

void ClientConn::_ProcessFreezeRequestList()
{
    if (!freeze_request_list_.empty()) {
        for (auto it = freeze_request_list_.begin(); it != freeze_request_list_.end(); ++it) {
            int request_cnt = (int)it->size();
            vector<vector<string>>& request_vec = *it;
            if (request_cnt > 1) {
                _HandlePipelineCommands(request_vec);
            } else if (request_cnt == 1) {
                _HandleRedisCommand(request_vec[0]);
            }
        }
        
        freeze_request_list_.clear();
    }
}
