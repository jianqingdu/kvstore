//
//  src_redis_conn.cpp
//  kv-store
//
//  Created by ziteng on 16-7-1.
//

#include "src_redis_conn.h"
#include "redis_parser.h"
#include "cmd_line_parser.h"
#include "sync_task.h"
#include <ctype.h>
#include <algorithm>
using namespace std;

static bool g_is_state_file_exist = false;

void SrcRedisConn::OnConfirm()
{
    fprintf(g_config.logfp, "connect to redis %s:%d success\n", m_peer_ip.c_str(), m_peer_port);
    BaseConn::OnConfirm();
    
    if (!g_config.src_redis_password.empty()) {
        vector<string> cmd_vec = {"AUTH", g_config.src_redis_password};
        string request;
        build_request(cmd_vec, request);
        Send((void*)request.data(), (int)request.size());
        
        conn_state_ = CONN_STATE_AUTH;
    } else {
        vector<string> cmd_vec = {"SYNC"};
        string request;
        build_request(cmd_vec, request);
        Send((void*)request.data(), (int)request.size());
        
        conn_state_ = CONN_STATE_READ_RDB_LEN;
    }
}

void SrcRedisConn::OnRead()
{
    int write_offset = m_in_buf.GetWriteOffset();
    _RecvData();
    int recv_bytes  = m_in_buf.GetWriteOffset() - write_offset;
    
    // 处理redis协议需要按C语言以'\0'结尾的字符串处理，接收buf预留了至少1个字节来放'\0'
    uchar_t* write_pos = m_in_buf.GetWriteBuffer();
    write_pos[0] = 0;
    
    if (conn_state_ == CONN_STATE_AUTH) {
        _HandleAuth();
    }
    
    if (conn_state_ == CONN_STATE_READ_RDB_LEN) {
        _ReadRdbLength();
    }
    
    if (conn_state_ == CONN_STATE_READ_RDB_DATA) {
        _ReadRdbData();
        
        uint64_t current_tick = get_tick_count();
        if (current_tick >= start_tick_ + 1000) {
            start_tick_ = current_tick;
            recv_bytes_ = recv_bytes;
        } else {
            recv_bytes_ += recv_bytes;
    
            if (recv_bytes_ > g_config.network_limit) {
                uint32_t sleep_time = (uint32_t)(current_tick - start_tick_) * 1000;
                fprintf(g_config.logfp, "receive too fast, sleep %d microsecond\n", sleep_time);
                usleep(sleep_time);
            }
        }
    }
    
    if (conn_state_ == CONN_STATE_SYNC_CMD) {
        _SyncWriteCommand();
    }
    
    m_in_buf.ResetOffset();
}

void SrcRedisConn::OnClose()
{
    if (m_open) {
        fprintf(g_config.logfp, "connection to redis %s:%d broken\n", m_peer_ip.c_str(), m_peer_port);
    } else {
        fprintf(g_config.logfp, "connect to redis %s:%d failed\n", m_peer_ip.c_str(), m_peer_port);
    }
    
    BaseConn::Close();
}

void SrcRedisConn::OnTimer(uint64_t curr_tick)
{
    // 如果RDB同步完成后，没有数据更新请求，AOF任务就需要在定时器里面启动
    if (g_sync_rdb_finished) {
        _CommitAofTask();
        
        if (g_sync_rdb_finished && !g_is_state_file_exist && g_thread_pool.GetTotalTaskCnt() < 10) {
            string file_name = "state." + to_string(getpid());
            FILE* fp = fopen(file_name.c_str(), "w");
            if (fp) {
                fprintf(fp, "SYNCHRONIZED\n");
                fclose(fp);
                g_is_state_file_exist = true;
            }
        }
    }
}

void SrcRedisConn::_HandleAuth()
{
    RedisReply reply;
    int ret = parse_redis_response((char*)m_in_buf.GetReadBuffer(), m_in_buf.GetReadableLen(), reply);
    if (ret > 0) {
        if ((reply.GetType() == REDIS_TYPE_STATUS) && (reply.GetStrValue() == "OK")) {
            fprintf(g_config.logfp, "Auth OK, continue\n");
            m_in_buf.Read(NULL, ret);
            
            vector<string> cmd_vec = {"SYNC"};
            string request;
            build_request(cmd_vec, request);
            Send((void*)request.data(), (int)request.size());
            
            conn_state_ = CONN_STATE_READ_RDB_LEN;
        } else {
            fprintf(g_config.logfp, "Auth failed, exit\n");
            exit(1);
        }
    } else if (ret < 0) {
        fprintf(g_config.logfp, "redis parse failed, exit\n");
        exit(1);
    }
}

void SrcRedisConn::_ReadRdbLength()
{
    // SYNC command return format:  $len\r\n rdb_data(len bytes)
    char* redis_cmd = (char*)m_in_buf.GetReadBuffer();
    int redis_len = m_in_buf.GetReadableLen();
    if (redis_len < 3) {
        return;
    }
    
    char* new_line = strstr(redis_cmd, "\r\n");
    if (!new_line) {
        return;
    }
    
    // 有时Redis-Server对于SYNC命令会先返回多个\n, 然后再返回$len\r\n
    int dollar_pos = 0;
    while ((redis_cmd[dollar_pos] != '$') && (redis_cmd + dollar_pos != new_line)) {
        dollar_pos++;
    }
    
    if (redis_cmd[dollar_pos] != '$') {
        fprintf(g_config.logfp, "SYNC response without a $ exit: %s\n", redis_cmd);
        exit(1);
    }
    
    long rdb_total_len  = 0;
    int ok = string2long(redis_cmd + dollar_pos + 1, new_line - (redis_cmd + dollar_pos + 1), rdb_total_len);
    if (!ok) {
        fprintf(g_config.logfp, "no length for rdb file: %s\n", redis_cmd);
        exit(1);
    }
    
    int rdb_start_pos = (int)(new_line - redis_cmd) + 2;
    m_in_buf.Read(NULL, rdb_start_pos);
    
    fprintf(g_config.logfp, "rdb_len=%ld\n", rdb_total_len);
    rdb_remain_len_ = rdb_total_len;
    conn_state_ = CONN_STATE_READ_RDB_DATA;
    recv_bytes_ = 0;
    start_tick_ = get_tick_count();
    
    // write to file cause rdb file may be too large that can not reside in memory
    rdb_file_ = fopen(g_config.rdb_file.c_str(), "wb");
    if (!rdb_file_) {
        fprintf(g_config.logfp, "open file %s for write failed, exit\n", g_config.rdb_file.c_str());
        exit(1);
    }
}

void SrcRedisConn::_ReadRdbData()
{
    char* rdb_data = (char*)m_in_buf.GetReadBuffer();
    long rdb_len = m_in_buf.GetReadableLen();
    if (rdb_len > rdb_remain_len_) {
        rdb_len = rdb_remain_len_;
    }
    
    if (fwrite(rdb_data, 1, rdb_len, rdb_file_) != (size_t)rdb_len) {
        fprintf(g_config.logfp, "fwrite failed, exit\n");
        exit(1);
    }
    
    rdb_remain_len_ -= rdb_len;
    m_in_buf.Read(NULL, (uint32_t)rdb_len);
    if (rdb_remain_len_ == 0) {
        conn_state_ = CONN_STATE_SYNC_CMD;
        fclose(rdb_file_);
        rdb_file_ = NULL;
        
        fprintf(g_config.logfp, "read all rdb data\n");
        
        SyncRdbTask* task = new SyncRdbTask;
        g_thread_pool.AddTask(task);
    }
}

void SrcRedisConn::_SyncWriteCommand()
{
    while (true) {
        vector<string> cmd_vec;
        string error_msg;
        int ret = parse_redis_request((char*)m_in_buf.GetReadBuffer(), m_in_buf.GetReadableLen(), cmd_vec, error_msg);
        if (ret > 0) {
            string& cmd = cmd_vec[0];
            transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
            
            if (cmd == "PING") {
                ;
            } else if (cmd == "SELECT") {
                cur_db_num_ = atoi(cmd_vec[1].c_str());
            } else {
                if ((g_config.src_redis_db == -1) || (g_config.src_redis_db == cur_db_num_)) {
                    string raw_cmd;
                    if (g_config.prefix.empty()) {
                        // do not need delete key prefix
                        raw_cmd.append((char*)m_in_buf.GetReadBuffer(), ret);
                    } else {
                        // delete key prefix
                        if ((cmd == "DEL") || (cmd == "MGET") || (cmd == "PFCOUNT")) {
                            int cmd_cnt = (int)cmd_vec.size();
                            for (int i = 1; i < cmd_cnt; ++i) {
                                _RemoveKeyPrefix(cmd_vec[i]);
                            }
                        } else if (cmd == "MSET") {
                            int cmd_cnt = (int)cmd_vec.size();
                            for (int i = 1; i < cmd_cnt; i += 2) {
                                _RemoveKeyPrefix(cmd_vec[i]);
                            }
                        } else {
                            _RemoveKeyPrefix(cmd_vec[1]);
                        }
                        
                        build_request(cmd_vec, raw_cmd);
                    }
                    
                    if (!g_sync_rdb_finished) {
                        if (!aof_file_) {
                            aof_file_ = fopen(g_config.aof_file.c_str(), "wb");
                            if (!aof_file_) {
                                fprintf(g_config.logfp, "fopen aof file %s for write failed\n", g_config.aof_file.c_str());
                                exit(1);
                            }
                        }
                        
                        int cmd_len = (int)raw_cmd.size();
                        fwrite(&cmd_len, 4, 1, aof_file_);
                        fwrite(raw_cmd.data(), 1, raw_cmd.size(), aof_file_);
                    } else {
                        _CommitAofTask();
                            
                        SyncCmdTask* task = new SyncCmdTask(raw_cmd);
                        g_thread_pool.AddTask(task);
                    }
                }
            }
            
            m_in_buf.Read(NULL, ret);
        } else if (ret < 0) {
            OnClose();
            break;
        } else {
            // not yet receive a whole command
            break;
        }
    }
}

void SrcRedisConn::_RemoveKeyPrefix(string& key)
{
    size_t pos = key.find(g_config.prefix);
    if (pos != string::npos) {
        key = key.substr(pos + g_config.prefix.size());
    }
}

void SrcRedisConn::_CommitAofTask()
{
    if (aof_file_) {
        g_sync_aof_finished = false;
        int cmd_len = 0;
        fwrite(&cmd_len, 4, 1, aof_file_);
        fclose(aof_file_);
        aof_file_ = NULL;
        
        SyncAofTask* task = new SyncAofTask;
        g_thread_pool.AddTask(task);
    }
}
