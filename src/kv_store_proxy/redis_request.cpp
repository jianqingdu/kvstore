//
//  redis_request.cpp
//  kv-store
//
//  Created by ziteng on 16-6-16.
//

#include "redis_request.h"
#include "client_conn.h"
#include "bucket_table.h"
#include "redis_conn_table.h"
#include "simple_log.h"
#include "redis_monitor_thread.h"

bool SingleKeyRedisRequest::HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec)
{
    seq_no_ = client_conn->IncrSeqNo();
    
    string& key = cmd_vec[1];
    string addr;
    g_bucket_table.ProcessKey(key, addr, migrating_addr_);
    RedisConnTable* conn_table = g_redis_conn_tables.GetIOResource(client_conn->GetHandle());
    conn_table->Update();
    
    AsyncRedisConn* redis_conn = conn_table->GetConn(addr);
    if (!redis_conn) {
        log_message(kLogLevelError, "no redis conn for: %s\n", addr.c_str());
        SetFailureResponse(KRedisErrorNoRedisConn);
        return false;
    }
    
    build_request(cmd_vec, request_);
    ClientSeq_t req_seq = {client_conn->GetHandle(), seq_no_, 1};
    
    if (!migrating_addr_.empty()) {
        // if key is in migrating，first migrate key to destination，then run redis command in the destination server
        string host;
        int port;
        get_ip_port(migrating_addr_, host, port);
        
        vector<string> migrate_cmd_vec = {"migrate", host, std::to_string(port), key,
            std::to_string(0), std::to_string(1000)};
        
        string migrate_request;
        build_request(migrate_cmd_vec, migrate_request);
        redis_conn->DoRawCmd(req_seq, migrate_request);
        
        state_ = REQUEST_STATE_MIGRATING;
        return true;
    }

    redis_conn->DoRawCmd(req_seq, request_);
    state_ = REQUEST_STATE_SENDING_REQUEST;
    return true;
}

void SingleKeyRedisRequest::HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len,
                                           const string& server_addr)
{
    if (state_ == REQUEST_STATE_MIGRATING) {
        RedisConnTable* conn_table = g_redis_conn_tables.GetIOResource(client_conn->GetHandle());
        conn_table->Update();
        AsyncRedisConn* redis_conn = conn_table->GetConn(migrating_addr_);
        if (!redis_conn) {
            log_message(kLogLevelError, "no redis conn for: %s\n", migrating_addr_.c_str());
            SetFailureResponse(KRedisErrorNoRedisConn);
            return;
        }
        
        ClientSeq_t req_seq = {client_conn->GetHandle(), seq_no_, 1};
        redis_conn->DoRawCmd(req_seq, request_);
        state_ = REQUEST_STATE_SENDING_REQUEST;
    } else if (state_ == REQUEST_STATE_SENDING_REQUEST) {
        response_.append(buf, len);
        state_ = REQUEST_STATE_COMPLETE;
    } else {
        log_message(kLogLevelError, "receive duplicated response\n");
    }
}

void migrate_keys(AsyncRedisConn* redis_conn, const string& migrate_addr, const vector<string>& migrate_cmd_vec,
                  ClientSeq_t req_seq, int& migration_cnt)
{
    string host;
    int port;
    get_ip_port(migrate_addr, host, port);
    
    int incr_step = (migrate_cmd_vec[0] == "MSET") ? 2 : 1;
    
    int migrate_cmd_cnt = (int)migrate_cmd_vec.size();
    for (int i = 1; i < migrate_cmd_cnt; i += incr_step) {
        const string& key = migrate_cmd_vec[i];
        
        vector<string> migrate_cmd_vec = {"migrate", host, std::to_string(port), key,
            std::to_string(0), std::to_string(1000)};
        
        string request;
        build_request(migrate_cmd_vec, request);
        redis_conn->DoRawCmd(req_seq, request);
    
        migration_cnt++;
    }
}

bool process_multiple_keys_request(RedisConnTable* conn_table, const string& addr, const vector<string>& group_cmd_vec,
                                   ClientSeq_t req_seq)
{
    AsyncRedisConn* redis_conn = conn_table->GetConn(addr);
    if (!redis_conn) {
        log_message(kLogLevelError, "no conn to addr: %s\n", addr.c_str());
        return false;
    }
    
    string request;
    build_request(group_cmd_vec, request);
    redis_conn->DoRawCmd(req_seq, request);
    return true;
}

void send_addr_cmd_vec_map_request(RedisConnTable* conn_table, map<string, vector<string>> addr_cmd_vec_map,
                                   ClientSeq_t req_seq, int& request_cnt)
{
    for (auto it = addr_cmd_vec_map.begin(); it != addr_cmd_vec_map.end(); ++it) {
        const string& addr = it->first;
        const vector<string>& group_cmd_vec = it->second;
        
        if (process_multiple_keys_request(conn_table, addr, group_cmd_vec, req_seq)) {
            ++request_cnt;
        }
    }
}

// MGET, DEL, PFCOUNT
bool MultipleKeysRedisRequest::HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec)
{
    cmd_vec_ = cmd_vec;
    
    vector<string> migrate_cmd_vec;
    string old_addr, migrate_addr;
    g_bucket_table.ProcessKeysCmd(cmd_vec_, addr_cmd_vec_map_, old_addr, migrate_addr, migrate_cmd_vec);
    
    RedisConnTable* conn_table = g_redis_conn_tables.GetIOResource(client_conn->GetHandle());
    conn_table->Update();
    
    seq_no_ = client_conn->IncrSeqNo();
    ClientSeq_t req_seq = {client_conn->GetHandle(), seq_no_, 1};
    
    if (!migrate_cmd_vec.empty()) {
        AsyncRedisConn* redis_conn = conn_table->GetConn(old_addr);
        if (!redis_conn) {
            log_message(kLogLevelError, "no redis conn to old_addr: %s\n", old_addr.c_str());
            SetFailureResponse(KRedisErrorNoRedisConn);
            return false;
        }
        
        migrate_keys(redis_conn, migrate_addr, migrate_cmd_vec, req_seq, migration_count_);
        state_ = REQUEST_STATE_MIGRATING;
        return true;
    }
    
    send_addr_cmd_vec_map_request(conn_table, addr_cmd_vec_map_, req_seq, request_count_);
    state_ = REQUEST_STATE_SENDING_REQUEST;
    return true;
}

void MultipleKeysRedisRequest::HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len,
                                              const string& server_addr)
{
    if (state_ == REQUEST_STATE_MIGRATING) {
        --migration_count_;
        if (migration_count_ > 0) {
            return;
        }
        
        RedisConnTable* conn_table = g_redis_conn_tables.GetIOResource(client_conn->GetHandle());
        conn_table->Update();
        
        ClientSeq_t req_seq = {client_conn->GetHandle(), seq_no_, 1};
        send_addr_cmd_vec_map_request(conn_table, addr_cmd_vec_map_, req_seq, request_count_);
        
        state_ = REQUEST_STATE_SENDING_REQUEST;
    } else if (state_ == REQUEST_STATE_SENDING_REQUEST) {
        if (cmd_vec_[0] == "MGET") {
            if (reply.GetType() == REDIS_TYPE_ARRAY) {
                vector<RedisReply> reply_array = reply.GetElements();
                vector<string> group_cmd_vec = addr_cmd_vec_map_[server_addr];
                
                int reply_cnt = (int)reply_array.size();
                if ((reply_cnt + 1) != (int)group_cmd_vec.size()) {
                    log_message(kLogLevelError, "MGET return number not match request number, (%d, %d)\n",
                                reply_cnt + 1, (int)group_cmd_vec.size());
                } else {
                    for (int i = 0; i < reply_cnt; i++) {
                        RedisReply& element = reply_array[i];
                        if (element.GetType() == REDIS_TYPE_STRING) {
                            const string& key = group_cmd_vec[i + 1];
                            kv_map_[key] = element.GetStrValue();
                        }
                    }
                }
            }
        } else {
            if (reply.GetType() == REDIS_TYPE_INTEGER) {
                total_cnt_ += reply.GetIntValue();
            }
        }
        
        --request_count_;
        if (request_count_ == 0) {
            state_ = REQUEST_STATE_COMPLETE;
            
            if (cmd_vec_[0] == "MGET") {
                vector<string> value_vec;
                int cmd_cnt = (int)cmd_vec_.size();
                for (int i = 1; i < cmd_cnt; ++i) {
                    // 没有key时kv_map[key]会返回空字符串，在build_response()里面, 空字符串返回nil值
                    value_vec.push_back(kv_map_[cmd_vec_[i]]);
                }
                
                build_response(value_vec, response_);
            } else {
                response_ = ":" + std::to_string(total_cnt_) + "\r\n";
            }
        }
    } else {
        log_message(kLogLevelError, "receive duplicated response\n");
    }
}

// MSET
bool MultipleKeyValuesRedisRequest::HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec)
{
    vector<string> migrate_cmd_vec;
    string old_addr, migrate_addr;
    g_bucket_table.ProcessKeyValuesCmd(cmd_vec, addr_cmd_vec_map_, old_addr, migrate_addr, migrate_cmd_vec);
    
    RedisConnTable* conn_table = g_redis_conn_tables.GetIOResource(client_conn->GetHandle());
    conn_table->Update();
    
    seq_no_ = client_conn->IncrSeqNo();
    ClientSeq_t req_seq = {client_conn->GetHandle(), seq_no_, 1};
    
    if (!migrate_cmd_vec.empty()) {
        AsyncRedisConn* redis_conn = conn_table->GetConn(old_addr);
        if (!redis_conn) {
            log_message(kLogLevelError, "no redis conn to old_addr: %s\n", old_addr.c_str());
            SetFailureResponse(KRedisErrorNoRedisConn);
            return false;
        }
        
        migrate_keys(redis_conn, migrate_addr, migrate_cmd_vec, req_seq, migration_count_);
        state_ = REQUEST_STATE_MIGRATING;
        return true;
    }
    
    send_addr_cmd_vec_map_request(conn_table, addr_cmd_vec_map_, req_seq, request_count_);
    state_ = REQUEST_STATE_SENDING_REQUEST;
    return true;
}

void MultipleKeyValuesRedisRequest::HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len,
                                                   const string& server_addr)
{
    if (state_ == REQUEST_STATE_MIGRATING) {
        --migration_count_;
        if (migration_count_ > 0) {
            return;
        }
        
        RedisConnTable* conn_table = g_redis_conn_tables.GetIOResource(client_conn->GetHandle());
        conn_table->Update();
        
        ClientSeq_t req_seq = {client_conn->GetHandle(), seq_no_, 1};
        send_addr_cmd_vec_map_request(conn_table, addr_cmd_vec_map_, req_seq, request_count_);
        
        state_ = REQUEST_STATE_SENDING_REQUEST;
    } else if (state_ == REQUEST_STATE_SENDING_REQUEST) {
        --request_count_;
        if (request_count_ == 0) {
            state_ = REQUEST_STATE_COMPLETE;
            response_ = "+OK\r\n";
        }
    } else {
        log_message(kLogLevelError, "receive duplicated response\n");
    }
}

bool PipelineRedisRequest::HandlePipelineCmd(ClientConn* client_conn, vector<vector<string>>& request_vec)
{
    total_cmd_cnt_ = (int)request_vec.size();
    response_cmd_cnt_ = 0;
    request_addr_vec_ = new string[total_cmd_cnt_];
    response_vec_ = new string[total_cmd_cnt_];
    map<string, vector<vector<string>>> addr_request_vec_map;
    
    seq_no_ = client_conn->IncrSeqNo();
    net_handle_t conn_handle = client_conn->GetHandle();
    RedisConnTable* conn_table = g_redis_conn_tables.GetIOResource(conn_handle);
    conn_table->Update();
    
    for (int i = 0; i < total_cmd_cnt_; i++) {
        vector<string>& cmd_vec = request_vec[i];
        string& key = cmd_vec[1];
        string addr;
        string migrating_addr;
        g_bucket_table.ProcessKey(key, addr, migrating_addr);
        if (!migrating_addr.empty()) {
            // key in migration will not enter pipeline request
            log_message(kLogLevelError, "key %s in migrating for pipeline\n", key.c_str());
        }
        
        if (addr_request_vec_map[addr].empty()) {
            vector<vector<string>> group_request_vec;
            group_request_vec.push_back(cmd_vec);
            addr_request_vec_map[addr] = group_request_vec;
        } else {
            addr_request_vec_map[addr].push_back(cmd_vec);
        }
        
        request_addr_vec_[i] = addr;
    }
    
    state_ = REQUEST_STATE_COMPLETE;
    bool send_request = false;
    
    for (auto it_group = addr_request_vec_map.begin(); it_group != addr_request_vec_map.end(); ++it_group) {
        string request_pipeline;
        for (auto it = it_group->second.begin(); it != it_group->second.end(); ++it) {
            string request;
            build_request(*it, request);
            request_pipeline.append(request);
        }
        
        uint32_t request_cnt = (uint32_t)it_group->second.size();
        string addr = it_group->first;
        AsyncRedisConn* redis_conn = conn_table->GetConn(addr);
        if (!redis_conn) {
            log_message(kLogLevelError, "get async redis conn failed for: %s\n", addr.c_str());
            RedisReply reply;
            char* resp_buf = (char*)KRedisErrorNoRedisConn.data();
            int resp_len = (int)KRedisErrorNoRedisConn.size();
            for (uint32_t i = 0; i < request_cnt; i++) {
                HandleResponse(client_conn, reply, resp_buf, resp_len, addr);
            }
        } else {
            ClientSeq_t req_seq = {conn_handle, seq_no_, request_cnt};
            redis_conn->DoRawCmd(req_seq, request_pipeline);
            
            state_ = REQUEST_STATE_SENDING_REQUEST;
            send_request = true; //只要有一个发送成功，就需要返回true，等待回复
        }
    }
    
    return send_request;
}

void PipelineRedisRequest::HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len,
                                          const string& server_addr)
{
    for (int i = 0; i < total_cmd_cnt_; i++) {
        if ((request_addr_vec_[i] == server_addr) && (response_vec_[i].empty())) {
            response_vec_[i].append(buf, len);
            response_cmd_cnt_++;
            break;
        }
    }
    
    if (response_cmd_cnt_ == total_cmd_cnt_) {
        state_ = REQUEST_STATE_COMPLETE;
        
        for (int i = 0; i < total_cmd_cnt_; i++) {
            response_.append(response_vec_[i]);
        }
    }
}

bool FlushDBRedisRequest::HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec)
{
    seq_no_ = client_conn->IncrSeqNo();
    
    RedisConnTable* conn_table = g_redis_conn_tables.GetIOResource(client_conn->GetHandle());
    conn_table->Update();
    
    uint32_t version;
    set<string> server_addr_set;
    g_bucket_table.GetServerAddrs(version, server_addr_set);
    
    g_monitor_thread.SetPingSwitch(false);
    build_request(cmd_vec, request_);
    ClientSeq_t req_seq = {client_conn->GetHandle(), seq_no_, 1};
    
    total_server_cnt_ = 0;
    for (const string& addr: server_addr_set) {
        AsyncRedisConn* redis_conn = conn_table->GetConn(addr);
        if (redis_conn) {
            redis_conn->DoRawCmd(req_seq, request_);
            total_server_cnt_++;
            state_ = REQUEST_STATE_SENDING_REQUEST;
        }
    }
    
    if (total_server_cnt_) {
        return true;
    } else {
        SetFailureResponse(KRedisErrorNoRedisConn);
        return false;
    }
}

void FlushDBRedisRequest::HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len, const string& server_addr)
{
    --total_server_cnt_;
    if (total_server_cnt_ == 0) {
        g_monitor_thread.SetPingSwitch(true);
        response_ = "+OK\r\n";
        state_ = REQUEST_STATE_COMPLETE;
    }
}

// send randomkey command to all redis, and return a random key if have any key returned
bool RandomKeyRedisRequest::HandleCmd(ClientConn* client_conn, vector<string>& cmd_vec)
{
    seq_no_ = client_conn->IncrSeqNo();
    
    RedisConnTable* conn_table = g_redis_conn_tables.GetIOResource(client_conn->GetHandle());
    conn_table->Update();
    
    uint32_t version;
    set<string> server_addr_set;
    g_bucket_table.GetServerAddrs(version, server_addr_set);
    
    build_request(cmd_vec, request_);
    ClientSeq_t req_seq = {client_conn->GetHandle(), seq_no_, 1};
    
    total_server_cnt_ = 0;
    for (const string& addr: server_addr_set) {
        AsyncRedisConn* redis_conn = conn_table->GetConn(addr);
        if (redis_conn) {
            redis_conn->DoRawCmd(req_seq, request_);
            total_server_cnt_++;
            state_ = REQUEST_STATE_SENDING_REQUEST;
        }
    }
    
    if (total_server_cnt_) {
        return true;
    } else {
        SetFailureResponse(KRedisErrorNoRedisConn);
        return false;
    }
}

void RandomKeyRedisRequest::HandleResponse(ClientConn* client_conn, const RedisReply& reply, char* buf, int len,
                                           const string& server_addr)
{
    --total_server_cnt_;
    if (reply.GetType() == REDIS_TYPE_STRING) {
        string key = reply.GetStrValue();
        size_t pos = key.find('_');
        if (pos != string::npos) {
            string raw_key = key.substr(pos + 1); // remove xxx_ prefix in key
            key_vec_.push_back(raw_key);
        }
    }
    
    if (total_server_cnt_ == 0) {
        if (key_vec_.empty()) {
            response_ = "$-1\r\n";
        } else {
            int rand_index = rand() % key_vec_.size();
            string key = key_vec_[rand_index];
            string len_str = to_string(key.size());
            response_.append("$");
            response_.append(len_str);
            response_.append("\r\n");
            response_.append(key);
            response_.append("\r\n");
        }
        
        state_ = REQUEST_STATE_COMPLETE;
    }
}
