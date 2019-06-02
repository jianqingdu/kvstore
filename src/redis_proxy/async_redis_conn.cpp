//
//  async_redis_conn.cpp
//
//  Created by ziteng on 19-5-28.
//

#include "async_redis_conn.h"
#include "simple_log.h"
#include "client_conn.h"
#include "config_parser.h"

const int kRedisConnTimeout = 2000;

IoThreadResource<AsyncRedisConn> g_redis_conns;

AsyncRedisConn::AsyncRedisConn()
{
    close_in_timer_ = false;
    conn_state_ = REDIS_CONN_STATE_IDLE;
}

void AsyncRedisConn::Init(int thread_index)
{
    thread_index_ = thread_index;
    Connect(g_config.redis_ip, g_config.redis_port, thread_index_);
}

void AsyncRedisConn::DoRawCmd(ClientSeq_t client_seq, const string& cmd)
{
    request_list_.push_back(client_seq);
    
    if (conn_state_ == REDIS_CONN_STATE_OK) {
        Send((void*)cmd.data(), (int)cmd.length());
    } else {
        if (m_handle == NETLIB_INVALID_HANDLE) {
            Connect(g_config.redis_ip, g_config.redis_port, thread_index_);
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
        _SendAuthCmd();
    } else if (g_config.redis_dbnum != 0) {
        _SendSelectCmd();
    } else {
        _SendPendingRequest();
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
            if (conn_state_ == REDIS_CONN_STATE_SEND_PASSWORD) {
                if (reply.GetType() == REDIS_TYPE_STATUS) {
                    log_message(kLogLevelInfo, "auth OK\n");
                    
                    if (g_config.redis_dbnum != 0) {
                        _SendSelectCmd();
                    } else {
                        _SendPendingRequest();
                    }
                } else if (reply.GetType() == REDIS_TYPE_ERROR) {
                    log_message(kLogLevelError, "auth failed: %s\n", reply.GetStrValue().c_str());
                    OnClose();
                    return;
                }
            } else if (conn_state_ == REDIS_CONN_STATE_SELECT_DB) {
                if (reply.GetType() == REDIS_TYPE_STATUS) {
                    _SendPendingRequest();
                } else if (reply.GetType() == REDIS_TYPE_ERROR) {
                    log_message(kLogLevelError, "select db failed: %s\n", reply.GetStrValue().c_str());
                    OnClose();
                    return;
                }
            } else {
                if ((reply.GetType() == REDIS_TYPE_ERROR) && !strncmp(reply.GetStrValue().c_str(), "NOAUTH", 6)) {
                    // handle situation for settting requirepass while Redis is running
                    log_message(kLogLevelError, "redis require auth, close the connection\n");
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
    
    conn_state_ = REDIS_CONN_STATE_IDLE;
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
        client_conn->HandleResponse(req_seq.seq_no, reply, buf, len);
    }
    
    req_seq.request_cnt--;
    if (req_seq.request_cnt == 0) {
        request_list_.pop_front();
    }
}

void AsyncRedisConn::_SendAuthCmd()
{
    log_message(kLogLevelInfo, "send auth command\n");
    
    conn_state_ = REDIS_CONN_STATE_SEND_PASSWORD;
    vector<string> auth_cmd_vec = {"auth", g_config.redis_password};
    string auth_cmd;
    build_request(auth_cmd_vec, auth_cmd);
    Send((void*)auth_cmd.data(), (int)auth_cmd.length());
    
}

void AsyncRedisConn::_SendSelectCmd()
{
    log_message(kLogLevelInfo, "send select command\n");
    
    conn_state_ = REDIS_CONN_STATE_SELECT_DB;
    vector<string> select_cmd_vec = {"select", std::to_string(g_config.redis_dbnum)};
    string select_cmd;
    build_request(select_cmd_vec, select_cmd);
    Send((void*)select_cmd.data(), (int)select_cmd.length());
}

void AsyncRedisConn::_SendPendingRequest()
{
    log_message(kLogLevelInfo, "prepare finished, send pending commands\n");
    conn_state_ = REDIS_CONN_STATE_OK;
    
    if (pending_request_.GetReadableLen() > 0) {
        Send(pending_request_.GetReadBuffer(), pending_request_.GetReadableLen());
        log_message(kLogLevelInfo, "send pending requests\n");
    }
}
