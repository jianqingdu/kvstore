//
//  client_conn.cpp
//
//  Created by ziteng on 19-5-28.
//

#include "client_conn.h"
#include "config_parser.h"
#include "simple_log.h"
#include "util.h"
#include "stats_info.h"
#include "qps_limiter.h"
#include <algorithm>
#include <ctype.h>
using namespace std;

StatsInfo g_stats_info;
QpsLimiter g_qps_limiter;

set<string> g_support_cmds = {
    // keys
    "DUMP", "EXISTS", "EXPIRE", "EXPIREAT", "PERSIST", "PEXPIRE", "PEXPIREAT", "PTTL", "RESTORE", "TTL", "TYPE",
    "DEL", "RANDOMKEY",
    
    // strings
    "APPEND", "DECR", "DECRBY", "GET", "GETRANGE", "GETSET", "INCR", "INCRBY", "INCRBYFLOAT",
    "PSETEX", "SET", "SETEX", "SETNX", "SETRANGE", "STRLEN", "SETBIT", "GETBIT", "BITCOUNT", "BITPOS",
    "MGET", "MSET",
    
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
    "PFADD", "PFCOUNT", "PFMERGE"
};


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
    
    int request_cnt = (int)request_vec.size();
    if (request_cnt > 1) {
        _HandlePipelineCommands(request_vec);
    } else if (request_cnt == 1) {
        _HandleRedisCommand(request_vec[0]);
    }
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
}

void ClientConn::HandleResponse(uint32_t seq_no, const RedisReply& reply, char* buf, int len)
{
    for (auto it = request_list_.begin(); it != request_list_.end(); ++it) {
        RedisRequest* req = *it;
        if (req->GetSeqNo() == seq_no) {
            req->HandleResponse(this, reply, buf, len);
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
        if (g_support_cmds.find(cmd) == g_support_cmds.end()) {
            // do not process pipeline request if it's a multiple key reqeust or the key is in migration or in readonly mode
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
    
    if (cmd == "AUTH") {
        SendResponse("+OK\r\n");
        return;
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
    
    if (g_support_cmds.find(cmd) != g_support_cmds.end()) {
        _HandleSingleCommand(cmd_vec);
    } else {
        SendResponse(kRedisErrorCommandNotSupport);
        log_message(kLogLevelError, "not support redis command: %s\n", cmd.c_str());
    }
}

void ClientConn::_HandleSingleCommand(vector<string>& cmd_vec)
{
    SingleRedisRequest* request = new SingleRedisRequest();
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
