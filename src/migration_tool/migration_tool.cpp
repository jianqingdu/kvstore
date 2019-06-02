//
//  migration_tool.cpp
//  kv-store
//
//  Created by ziteng on 16-5-24.
//

#include "util.h"
#include "block_socket.h"
#include "pkt_definition.h"
#include "table_transform.h"
#include "hiredis.h"
#include "redis_conn.h"
#include "cmd_line_parser.h"
#include "redis_parser.h"

void print_bucket_table(const map<string, vector<uint16_t>>& server_buckets_map)
{
    for (auto it = server_buckets_map.begin(); it != server_buckets_map.end(); ++it) {
        fprintf(g_config.logfp, "%s: \n", it->first.c_str());
        int line_cnt = 0;
        for (const uint16_t bucket_id : it->second) {
            fprintf(g_config.logfp, "%4d ", bucket_id);
            line_cnt++;
            if (line_cnt % 20 == 0) {
                fprintf(g_config.logfp, "\n");
            }
        }
        fprintf(g_config.logfp, "\n");
    }
}

bool get_bucket_table(map<string, vector<uint16_t>>& server_buckets_map, uint16_t& migrating_bucket_id,
                      string& migrating_server_addr)
{
    PktBucketTableReq req_pkt(g_config.biz_namespace, TABLE_REQ_FROM_MIGRATION_TOOL, "");
    PktBase* resp_pkt = block_request(g_config.cs_host.c_str(), g_config.cs_port, 2000, &req_pkt);
    if (!resp_pkt || (resp_pkt->GetPktId() != PKT_ID_BUCKET_TABLE_RESP)) {
        fprintf(g_config.logfp, "no bucket table response from cs\n");
        
        if (resp_pkt)
            delete resp_pkt;
        return false;
    }
    
    PktBucketTableResp* table_pkt = (PktBucketTableResp*)resp_pkt;
    uint32_t result = table_pkt->GetResult();
    if (result != 0) {
        fprintf(g_config.logfp, "get bucket table result=%d\n", result);
        delete resp_pkt;
        return false;
    }
    
    server_buckets_map = table_pkt->GetServerBucketsMap();
    migrating_bucket_id = table_pkt->GetMigratingBucketId();
    migrating_server_addr = table_pkt->GetMigratingServerAddr();
    
    delete resp_pkt;
    return true;
}

bool is_valid_migrating_addr(uint16_t migrating_bucket_id, const string& migrating_server_addr,
        map<uint16_t, string>& old_bucket_server_map, map<uint16_t, string>& new_bucket_server_map)
{
    if (migrating_server_addr.empty())
        return true;
    
    if (g_config.command == "add" ) {
        if (migrating_server_addr != g_config.redis_addr) {
            fprintf(g_config.logfp, "can not add a new redis server that is not the same with the current migrating server\n");
            return false;
        }
        
        if (new_bucket_server_map[migrating_bucket_id] != g_config.redis_addr) {
            fprintf(g_config.logfp, "migrating address is not the same with the new bucket table");
            return false;
        }
    } else {
        if (old_bucket_server_map[migrating_bucket_id] != g_config.redis_addr) {
            fprintf(g_config.logfp, "migrating address is not the same with the old bucket table");
            return false;
        }
    }
    
    return true;
}

bool get_master_slave_redis_map(const set<string>& master_servers, map<string, string>& addr_map)
{
    for (const string master_addr: master_servers) {
        string master_ip;
        int master_port;
        if (!get_ip_port(master_addr, master_ip, master_port)) {
            return false;
        }
        
        RedisConn redis_conn(master_ip, master_port);
        redis_conn.SetPassword(g_config.redis_password);
        if (redis_conn.Init()) {
            fprintf(g_config.logfp, "redis_connect failed %s\n", master_addr.c_str());
            return false;
        }
        
        redisReply* reply = redis_conn.DoCmd("info replication");
        if (reply && (reply->type == REDIS_REPLY_STRING)) {
            string replication_info(reply->str, reply->len);
            
            string slave_addr;
            parse_slave_addr(replication_info, slave_addr);
            if (slave_addr.empty()) {
                fprintf(g_config.logfp, "no slave addr\n");
                return false;
            }
            
            addr_map[master_addr] = slave_addr;
            fprintf(g_config.logfp, "%s->%s\n", master_addr.c_str(), slave_addr.c_str());
        } else {
            fprintf(g_config.logfp, "no reply from %s\n", master_addr.c_str());
            return false;
        }
    }
    
    return true;
}

bool redis_migration(uint16_t bucket_id, RedisConn& master_conn, RedisConn& slave_conn,
                     const string& dst_ip, int dst_port)
{
    uint64_t start_tick = get_tick_count();
    string cmd = "keys " + std::to_string(bucket_id) + "_*";
    redisReply* keys_reply = slave_conn.DoCmd(cmd);
    if (keys_reply && (keys_reply->type == REDIS_REPLY_ARRAY)) {
        uint64_t cost_tick = get_tick_count() - start_tick;
        fprintf(g_config.logfp, "element_size=%d, cost=%ld ms\n", (int)keys_reply->elements, (long)cost_tick);
        for (size_t i = 0; i < keys_reply->elements; i++) {
            redisReply* element = keys_reply->element[i];
            if (element->type == REDIS_REPLY_STRING) {
                string key(element->str, element->len);
                
                // key里面可能含有特殊字符, 比如'\0'，所以需要用DoRawCmd接口
                vector<string> cmd_vec = {"migrate", dst_ip, std::to_string(dst_port), key, "0", "1000"};
                string request;
                build_request(cmd_vec, request);
                redisReply* reply = master_conn.DoRawCmd(request);
                if (!reply || (reply->type == REDIS_REPLY_ERROR)) {
                    fprintf(g_config.logfp, "migrate %s failed\n", key.c_str());
                    if (reply) {
                        string err(reply->str, reply->len);
                        fprintf(g_config.logfp, "err=%s\n", err.c_str());
                    }
                }
            }
        }
        return true;
    } else {
        fprintf(g_config.logfp, "get keys failed\n");
        return false;
    }
}

bool start_migration(uint16_t bucket_id, const string& master_redis_addr, const string& slave_redis_addr,
                     const string& dst_redis_addr)
{
    fprintf(g_config.logfp, "start migrate bucket_id=%d\n", bucket_id);
    uint64_t start_tick = get_tick_count();
    
    string master_ip, slave_ip, dst_ip;
    int master_port, slave_port, dst_port;
    if (!get_ip_port(master_redis_addr, master_ip, master_port) ||
        !get_ip_port(slave_redis_addr, slave_ip, slave_port) ||
        !get_ip_port(dst_redis_addr, dst_ip, dst_port)) {
        fprintf(g_config.logfp, "parse redis addr (%s|%s|%s) failed\n",
                master_redis_addr.c_str(), slave_redis_addr.c_str(), dst_redis_addr.c_str());
        return false;
    }
    
    uint8_t scale_up = (g_config.command == "add") ? 1 : 0;
    PktStartMigration start_pkt(g_config.biz_namespace, bucket_id, dst_redis_addr, scale_up);
    PktBase* resp_pkt = block_request(g_config.cs_host.c_str(), g_config.cs_port, 2000, &start_pkt);
    if (!resp_pkt || (resp_pkt->GetPktId() != PKT_ID_START_MIGRATION_ACK)) {
        fprintf(g_config.logfp, "no StartMigrationAck from cs\n");
        return false;
    }
    
    PktStartMigrationAck* ack_pkt = (PktStartMigrationAck*)resp_pkt;
    uint32_t result = ack_pkt->GetResult();
    delete resp_pkt;
    if (result) {
        fprintf(g_config.logfp, "StartMigrationAck result=%d\n", result);
        return false;
    }
    
    RedisConn slave_conn(slave_ip, slave_port);
    slave_conn.SetPassword(g_config.redis_password);
    if (slave_conn.Init()) {
        fprintf(g_config.logfp, "redis_connect slave failed %s\n", slave_redis_addr.c_str());
        return false;
    }
    
    RedisConn master_conn(master_ip, master_port);
    master_conn.SetPassword(g_config.redis_password);
    if (master_conn.Init()) {
        fprintf(g_config.logfp, "redis_connect master failed %s\n", master_redis_addr.c_str());
        return false;
    }
    
    if (g_config.wait_ms > 0) {
        fprintf(g_config.logfp, "sleep %d milliseconds\n", g_config.wait_ms);
        usleep(g_config.wait_ms * 1000);
    }
    
    if (!redis_migration(bucket_id, master_conn, slave_conn, dst_ip, dst_port)) {
        return false;
    }
    
    PktCompleteMigration complete_pkt(g_config.biz_namespace, bucket_id, dst_redis_addr, scale_up);
    resp_pkt = block_request(g_config.cs_host.c_str(), g_config.cs_port, 2000, &complete_pkt);
    if (!resp_pkt || (resp_pkt->GetPktId() != PKT_ID_COMPLETE_MIGRATION_ACK)) {
        fprintf(g_config.logfp, "no CompleteMigrationAck from cs\n");
        return false;
    }
    
    PktCompleteMigrationAck* complete_ack_pkt = (PktCompleteMigrationAck*)resp_pkt;
    result = complete_ack_pkt->GetResult();
    delete resp_pkt;
    if (result) {
        fprintf(g_config.logfp, "CompleteMigrationAck, result=%d\n", result);
        return false;
    }

    uint64_t cost_tick = get_tick_count() - start_tick;
    fprintf(g_config.logfp, "complete migrate bucket_id=%d, cost=%ld ms\n\n", bucket_id, (long)cost_tick);
    
    return true;
}

void set_redis_password(const set<string>& servers, const string& password)
{
    for (const string addr: servers) {
        string ip;
        int port;
        if (!get_ip_port(addr, ip, port)) {
            return;
        }
        
        RedisConn redis_conn(ip, port);
        if (password.empty()) {
            redis_conn.SetPassword(g_config.redis_password);
        }
        if (!redis_conn.Init()) {
            vector<string> cmd_vec = {"config", "set", "requirepass", password};
            string request;
            build_request(cmd_vec, request);
            redis_conn.DoRawCmd(request);
        }
    }
}

void write_status_file(const string& status)
{
    string log_file = "mt.status." + to_string(getpid());
    FILE* fp = fopen(log_file.c_str(), "w");
    if (!g_config.logfp) {
        fprintf(g_config.logfp, "open status file failed\n");
        return;
    }
    
    fprintf(fp, "%s", status.c_str());
    fclose(fp);
}

int main(int argc, char* argv[])
{
    parse_cmd_line(argc, argv);
    
    // open file for logging
    string log_file = "mt.log." + to_string(getpid());
    g_config.logfp = fopen(log_file.c_str(), "w");
    if (!g_config.logfp) {
        fprintf(stderr, "open log file failed\n");
        return 1;
    }
    
    setlinebuf(g_config.logfp);
    
    // get the bucket table
    map<string, vector<uint16_t>> server_buckets_map;
    uint16_t migrating_bucket_id;
    string migrating_server_addr;
    if (!get_bucket_table(server_buckets_map, migrating_bucket_id, migrating_server_addr)) {
        fprintf(g_config.logfp, "get_bucket_table failed\n");
        write_status_file("FAIL");
        return 1;
    }
    
    fprintf(g_config.logfp, "old table: migrating_bucket_id=%d, migrating_server_addr=%s\n",
           migrating_bucket_id, migrating_server_addr.c_str());
    print_bucket_table(server_buckets_map);
    
    map<uint16_t, string> bucket_server_map = transform_table(server_buckets_map);
    uint16_t bucket_cnt = (uint16_t)bucket_server_map.size();
    
    // get the new bucket table
    map<string, vector<uint16_t>> new_server_buckets_map;
    if (g_config.command == "add") {
        scale_up_table(g_config.redis_addr, bucket_cnt, server_buckets_map, new_server_buckets_map);
    } else {
        scale_down_table(g_config.redis_addr, bucket_cnt, server_buckets_map, new_server_buckets_map);
    }
    
    if (new_server_buckets_map.empty()) {
        fprintf(g_config.logfp, "failed to generate new bucket table\n");
        write_status_file("FAIL");
        return 1;
    }
    
    fprintf(g_config.logfp, "\nnew table\n");
    print_bucket_table(new_server_buckets_map);
    
    map<uint16_t, string> new_bucket_server_map = transform_table(new_server_buckets_map);
    if (!is_valid_migrating_addr(migrating_bucket_id, migrating_server_addr, bucket_server_map, new_bucket_server_map)) {
        write_status_file("FAIL");
        return 1;
    }
    
    set<string> servers;
    if (g_config.command == "add") {
        fetch_servers(new_server_buckets_map, servers);
    } else {
        fetch_servers(server_buckets_map, servers);
    }
    
    map<string, string> addr_map;
    if (!get_master_slave_redis_map(servers, addr_map)) {
        fprintf(g_config.logfp, "get slave redis map failed\n");
        write_status_file("FAIL");
        return 1;
    }
    
    write_status_file("IN_PROCESS");
    if (!g_config.redis_password.empty()) {
        // empty all password of master redis, or migrate keys from redis to redis will fail
        set_redis_password(servers, "");
    }
    
    int ret = 0;
    int migrated_count = 0;
    for (uint16_t bucket_id = 0; bucket_id < bucket_cnt; ++bucket_id) {
        string& old_addr = bucket_server_map[bucket_id];
        string& new_addr = new_bucket_server_map[bucket_id];
        if (old_addr != new_addr) {
            if (g_config.command == "add") {
                if (new_addr != g_config.redis_addr) {
                    fprintf(g_config.logfp, "migrating address is not the added address\n");
                    ret = 1;
                    break;
                }
            } else {
                if (old_addr != g_config.redis_addr) {
                    fprintf(g_config.logfp, "migrating address is not the deleted address\n");
                    ret = 1;
                    break;
                }
            }
            
            if (!start_migration(bucket_id, old_addr, addr_map[old_addr], new_addr)) {
                ret = 1;
                break;
            }
            
            ++migrated_count;
            if (migrated_count >= g_config.count) {
                fprintf(g_config.logfp, "!!!complete %d bucket migration\n", migrated_count);
                break;
            }
        }
    }
    
    if (!g_config.redis_password.empty()) {
        set_redis_password(servers, g_config.redis_password);
    }
    
    if (ret == 0) {
        fprintf(g_config.logfp, "!!!Complete all migration\n");
        write_status_file("SUCCESS");
    } else {
        write_status_file("FAIL");
    }
    
    return ret;
}
