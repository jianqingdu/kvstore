//
//  redis_monitor_thread.cpp
//  kv-store
//
//  Created by ziteng on 16/7/21.
//

#include "redis_monitor_thread.h"
#include "bucket_table.h"
#include "simple_log.h"
#include "block_socket.h"
#include "redis_parser.h"
#include "pkt_definition.h"
#include "config_parser.h"
#include "config_server_conn.h"

RedisMonitorThread g_monitor_thread;

const int kRedisTimeout = 1000; // 1 second, connect/read/write timeout
const uint32_t kSleepTime = 3;   // 3 seconds
const int kMaxStopPingCount = 60;

static bool recv_resp(net_handle_t handle, RedisReply& reply)
{
    SimpleBuffer buf;
    while (true) {
        uint32_t free_buf_len = buf.GetWritableLen();
        if (free_buf_len < 1025) {
            buf.Extend(1025); // reserve 1 byte for text protocol to add '\0'
        }
        
        int len = block_recv(handle, buf.GetWriteBuffer(), 1024);
        if (len <= 0) {
            log_message(kLogLevelError, "receive redis response failed\n");
            block_close(handle);
            return false;
        }
        
        buf.IncWriteOffset(len);
        
        char* redis_resp = (char*)buf.GetBuffer();
        int redis_len = buf.GetWriteOffset();
        redis_resp[redis_len] = '\0';
        int ret = parse_redis_response(redis_resp, redis_len, reply);
        if (ret < 0) {
            log_message(kLogLevelError, "parse redis failed: %s\n", redis_resp);
            block_close(handle);
            return false;
        } else if (ret > 0) {
            break;
        }
    }
    
    return true;
}

static bool do_redis_cmd(const string& addr, const string& cmd, RedisReply& reply)
{
    string server_ip;
    int port;
    if (!get_ip_port(addr, server_ip, port)) {
        log_message(kLogLevelError, "parse redis addr %s failed\n", addr.c_str());
        return false;
    }
    
    net_handle_t handle = connect_with_timeout(server_ip.c_str(), port, kRedisTimeout);
    if (handle == NETLIB_INVALID_HANDLE) {
        log_message(kLogLevelError, "connect redis %s failed\n", addr.c_str());
        return false;
    }
    
    block_set_timeout(handle, kRedisTimeout);
    
    if (!g_config.redis_password.empty()) {
        vector<string> auth_cmd_vec = {"auth", g_config.redis_password};
        string auth_cmd;
        build_request(auth_cmd_vec, auth_cmd);
        
        int auth_cmd_len = (int)auth_cmd.size();
        if (block_send_all(handle, (void*)auth_cmd.data(), auth_cmd_len) != auth_cmd_len) {
            log_message(kLogLevelError, "send redis auth cmd to %s failed\n", addr.c_str());
            block_close(handle);
            return false;
        }
        
        if (!recv_resp(handle, reply)) {
            log_message(kLogLevelError, "recv_resp from %s failed\n", addr.c_str());
            return false;
        }
    }
    
    int cmd_len = (int)cmd.size();
    if (block_send_all(handle, (void*)cmd.data(), cmd_len) != cmd_len) {
        log_message(kLogLevelError, "send redis cmd to %s failed\n", addr.c_str());
        block_close(handle);
        return false;
    }
    
    if (!recv_resp(handle, reply)) {
        log_message(kLogLevelError, "recv_resp from %s failed\n", addr.c_str());
        return false;
    }
    
    block_close(handle);
    return true;
}

static bool ping(const string &addr)
{
    RedisReply reply;
    string ping_cmd;
    vector<string> cmd_vec = {"PING"};
    build_request(cmd_vec, ping_cmd);
    
    bool ret = do_redis_cmd(addr, ping_cmd, reply);
    // while Redis is loading RDB or AOF file, it will return ERROR: "LOADING Redis is loading the dataset in memory",
    // so do not need to compare return result if Redis is still alive
    if (ret) {
        return true;
    } else {
        return false;
    }
}

static bool get_slave_addr(const string& addr, string& slave_addr)
{
    RedisReply reply;
    string info_replication_cmd;
    vector<string> cmd_vec = {"INFO", "REPLICATION"};
    build_request(cmd_vec, info_replication_cmd);
    
    bool ret = do_redis_cmd(addr, info_replication_cmd, reply);
    if (ret && (reply.GetType() == REDIS_TYPE_STRING)) {
        string info = reply.GetStrValue();
        parse_slave_addr(info, slave_addr);
        if (!slave_addr.empty()) {
            log_message(kLogLevelInfo, "slave of redis %s is %s\n", addr.c_str(), slave_addr.c_str());
        }
    }
    
    return ret;
}

static bool slaveof_no_one(const string& addr)
{
    RedisReply reply;
    string slaveof_no_one_cmd;
    vector<string> cmd_vec = {"SLAVEOF", "NO", "ONE"};
    build_request(cmd_vec, slaveof_no_one_cmd);
    
    bool ret = do_redis_cmd(addr, slaveof_no_one_cmd, reply);
    if (ret && (reply.GetType() == REDIS_TYPE_STATUS) && (reply.GetStrValue() == "OK")) {
        vector<string> rewrite_cmd_vec = {"CONFIG", "REWRITE"};
        string config_rewrite_cmd;
        build_request(rewrite_cmd_vec, config_rewrite_cmd);
        do_redis_cmd(addr, config_rewrite_cmd, reply);
        return true;
    } else {
        return false;
    }
}

static void update_slave_addr_map(const set<string>& serv_addr_set, map<string, string>& slave_addr_map)
{
    for (const string& addr: serv_addr_set) {
        string slave_addr;
        bool ret = get_slave_addr(addr, slave_addr);
        if (ret) {
            slave_addr_map[addr] = slave_addr;
        }
    }
}

void RedisMonitorThread::OnThreadRun()
{
    uint32_t table_version = 0;
    set<string> server_addr_set;
    map<string, uint32_t> addr_down_map;
    map<string, string> slave_addr_map;
    uint32_t slave_addr_update_cnt = 0;

    is_ping_ = true;
    while (true) {
        sleep(kSleepTime);
        
        if (g_bucket_table.IsTableUpdated(table_version)) {
            server_addr_set.clear();
            g_bucket_table.GetServerAddrs(table_version, server_addr_set);
            
            // iterate all slave address, if the slave address is in the new bucket table,
            // it must be a slave promoted to a master, and proxy need send command "slaveof no one" to it
            for (auto it = slave_addr_map.begin(); it != slave_addr_map.end(); ++it) {
                string slave_addr = it->second;
                if (!slave_addr.empty() && (server_addr_set.find(slave_addr) != server_addr_set.end())) {
                    slaveof_no_one(slave_addr);
                }
            }
            
            slave_addr_update_cnt = 0;
            slave_addr_map.clear();
            update_slave_addr_map(server_addr_set, slave_addr_map);
        }
        
        if (!is_ping_) {
            log_message(kLogLevelInfo, "cluster in FlushDB command, stop ping Redis\n");
            stop_ping_cnt_++;
            if (stop_ping_cnt_ > kMaxStopPingCount) {
                stop_ping_cnt_ = 0;
                is_ping_ = true;
            }
            continue;
        }
        
        for (const string& addr : server_addr_set) {
            if (!ping(addr)) {
                addr_down_map[addr]++;
                
                if (addr_down_map[addr] >= (g_config.redis_down_timeout / kSleepTime)) {
                    log_message(kLogLevelError, "redis server %s is down\n", addr.c_str());
                    
                    PktStorageServerDown* pkt = new PktStorageServerDown(g_config.biz_namespace, addr, slave_addr_map[addr]);
                    send_to_config_server(pkt);
                }
            } else {
                if (addr_down_map[addr] >= (g_config.redis_down_timeout / kSleepTime)) {
                    log_message(kLogLevelInfo, "redis server %s is up again\n", addr.c_str());
                    PktStorageServerUp* pkt = new PktStorageServerUp(g_config.biz_namespace, addr);
                    send_to_config_server(pkt);
                }
                addr_down_map[addr] = 0;
            }
        }
        
        ++slave_addr_update_cnt;
        if (slave_addr_update_cnt >= (g_config.update_slave_interval / kSleepTime)) {
            // update slave address map every 30 minutes cause slave redis may be updated
            slave_addr_update_cnt = 0;
            update_slave_addr_map(server_addr_set, slave_addr_map);
        }
    }
}
