/*
 *  async_redis_conn.cpp
 *
 *  Created on: 2016-6-14
 *      Author: ziteng
 */

#include "async_redis_conn.h"
#include "simple_log.h"
#include "client_conn.h"
#include "config_parser.h"

const int kRedisConnTimeout = 2000;

AsyncRedisConn::AsyncRedisConn(string server_ip, uint16_t server_port, int thread_index)
: server_ip_(server_ip), server_port_(server_port), thread_index_(thread_index), close_in_timer_(false), is_auth_(false)
{
    server_addr_ = server_ip + ":" + std::to_string(server_port);
    
    Connect(server_ip_, server_port_, thread_index_);
}

void AsyncRedisConn::DoRawCmd(ClientSeq_t client_seq, const string& cmd)
{
    request_list_.push_back(client_seq);
    
    if (is_auth_) {
        Send((void*)cmd.data(), (int)cmd.length());
    } else {
        if (m_handle == NETLIB_INVALID_HANDLE) {
            Connect(server_ip_, server_port_, thread_index_);
            m_last_send_tick = get_tick_count();
        }
        
        pending_request_.Write((void*)cmd.data(), (int)cmd.length());
        log_message(kLogLevelInfo, "add command to pending requests, total_len=%d\n", pending_request_.GetReadableLen());
    }
}

void AsyncRedisConn::OnConfirm()
{
    log_message(kLogLevelInfo, "connect to redis %s:%d success\n", m_peer_ip.c_str(), m_peer_port);
    BaseConn::OnConfirm();
    
    if (!g_config.redis_password.empty()) {
        vector<string> auth_cmd_vec = {"auth", g_config.redis_password};
        string auth_cmd;
        build_request(auth_cmd_vec, auth_cmd);
        Send((void*)auth_cmd.data(), (int)auth_cmd.length());
        is_auth_ = false;
    } else {
        is_auth_ = true;
        
        if (pending_request_.GetReadableLen() > 0) {
            Send(pending_request_.GetReadBuffer(), pending_request_.GetReadableLen());
            log_message(kLogLevelInfo, "send pending requests\n");
        }
    }
}

void AsyncRedisConn::OnRead()
{
    _RecvData();
    
    // when parse redis protocol, this makes sure searching string pattern will not pass the boundary
    uchar_t* write_pos = m_in_buf.GetWriteBuffer();
    write_pos[0] = 0;
    
    while (true) {
        RedisReply reply;
        int ret = parse_redis_response((char*)m_in_buf.GetReadBuffer(), m_in_buf.GetReadableLen(), reply);
        if (ret > 0) {
            if (!is_auth_) {
                // does not handle situation if password is not correct
                is_auth_ = true;
                if (pending_request_.GetReadableLen() > 0) {
                    Send(pending_request_.GetReadBuffer(), pending_request_.GetReadableLen());
                    log_message(kLogLevelInfo, "send pending requests\n");
                }
                
                if (reply.GetType() == REDIS_TYPE_STATUS) {
                    log_message(kLogLevelInfo, "auth OK\n");
                } else if (reply.GetType() == REDIS_TYPE_ERROR) {
                    log_message(kLogLevelInfo, "auth failed: %s\n", reply.GetStrValue().c_str());
                }
            } else {
                if ((reply.GetType() == REDIS_TYPE_ERROR) && !strncmp(reply.GetStrValue().c_str(), "NOAUTH", 6)) {
                    // handle situation for settting requirepass while Redis is running
                    log_message(kLogLevelError, "redis require auth, close the connection\n");
                    is_auth_ = false;
                    OnClose();
                    return;
                }
                
                _HandleRedisReply(reply, (char*)m_in_buf.GetReadBuffer(), ret);
            }
            
            m_in_buf.Read(NULL, ret);
        } else if (ret < 0) {
            OnClose();
            break;
        } else {
            // not yet receive a whole command
            m_in_buf.ResetOffset();
            m_base_socket->SetFastAck(); // fix some large RT cause by delayed ACK
            break;
        }
    }
}

void AsyncRedisConn::OnClose()
{
    if (m_open) {
        log_message(kLogLevelInfo, "connection to redis %s:%d broken\n", m_peer_ip.c_str(), m_peer_port);
    } else {
        log_message(kLogLevelInfo, "connect to redis %s:%d failed\n", m_peer_ip.c_str(), m_peer_port);
    }
    
    is_auth_ = false;
    pending_request_.Clear();
    request_list_.clear();
    AddRef();   // reuse connection object when reconnecting
    BaseConn::Close();
}

void AsyncRedisConn::OnTimer(uint64_t curr_tick)
{
    // process connect timeout
    if (!m_open && (curr_tick > m_last_send_tick + kRedisConnTimeout)) {
        OnClose();
        return;
    }
    
    if (close_in_timer_) {
        Close();
    }
}

void AsyncRedisConn::_HandleRedisReply(const RedisReply& reply, char* buf, int len)
{
    ClientSeq_t& req_seq = request_list_.front();
    ClientConn* client_conn = (ClientConn*)get_base_conn(req_seq.conn_handle);
    if (client_conn) {
        client_conn->HandleResponse(req_seq.seq_no, reply, buf, len, server_addr_);
    }
    
    req_seq.request_cnt--;
    if (req_seq.request_cnt == 0) {
        request_list_.pop_front();
    }
}
