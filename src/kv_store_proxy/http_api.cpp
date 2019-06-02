/*
 *  http_api.cpp
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#include "http_api.h"
#include "http_handler_map.h"
#include "stats_info.h"
#include "json/json.h"
#include "simple_log.h"
#include "config_parser.h"
#include "bucket_table.h"
#include "table_transform.h"

void handle_stats_info(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    log_message(kLogLevelInfo,"url=%s, ip=%s\n", http_parser_wrapper->GetUrl(), http_conn->GetPeerIP());
    
    uint64_t total_cmd_count, slow_cmd_count;
    uint32_t client_count, average_rt;
    uint32_t qps;
    map<string, uint64_t> cmd_count_map;
    g_stats_info.GetStatsInfo(total_cmd_count, slow_cmd_count, client_count, average_rt, qps, cmd_count_map);
    
    Json::Value json_resp, json_cmd_map;
    json_resp["code"] = OK_CODE;
    json_resp["msg"] = OK_MSG;
    json_resp["client_count"] = client_count;
    json_resp["average_rt"] = average_rt;
    json_resp["qps"] = qps;
    json_resp["total_cmd_count"] = (Json::Value::UInt64)total_cmd_count;
    json_resp["slow_cmd_count"] = (Json::Value::UInt64)slow_cmd_count;
    
    for (auto it = cmd_count_map.begin(); it != cmd_count_map.end(); ++it) {
        json_cmd_map[it->first] = (Json::Value::UInt64)it->second;
    }
    json_resp["cmds"] = json_cmd_map;
    
    send_http_response(http_conn, json_resp);
}

void handle_reset_stats(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    log_message(kLogLevelInfo, "url=%s, ip=%s\n", http_parser_wrapper->GetUrl(), http_conn->GetPeerIP());
    
    g_stats_info.Reset();
    
    Json::Value json_resp;
    json_resp["code"] = OK_CODE;
    json_resp["msg"] = OK_MSG;
    send_http_response(http_conn, json_resp);
}

void handle_set_config(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    const char* content = http_parser_wrapper->GetBodyContent();
    const char* url = http_parser_wrapper->GetUrl();
    log_message(kLogLevelInfo, "url=%s, content=%s, ip=%s\n", url, content, http_conn->GetPeerIP());
    
    Json::Value root, json_resp;
    Json::Reader reader;
    if (!reader.parse(content, root)) {
        log_message(kLogLevelError, "json format invalid\n");
        json_resp["code"] = 1002;
        json_resp["msg"] = "json format invalid";
        send_http_response(http_conn, json_resp);
        return;
    }
    
    json_resp["code"] = OK_CODE;
    json_resp["msg"] = OK_MSG;
    vector<string> member_names = root.getMemberNames();
    for (const string& name : member_names) {
        if ((name == "max_qps") && root[name].isInt()) {
            g_config.max_qps = root[name].asInt();
        } else if ((name == "max_client_num") && root[name].isInt()) {
            g_config.max_client_num = root[name].asInt();
        } else if ((name == "slow_cmd_time") && root[name].isInt()) {
            g_config.slow_cmd_time = root[name].asInt();
        } else if ((name == "client_timeout") && root[name].isInt()) {
            g_config.client_timeout = root[name].asInt();
            if (g_config.client_timeout < 30) {
                g_config.client_timeout = 30;    // at lease 30 seconds
            }
        } else if ((name == "redis_down_timeout") && root[name].isInt()) {
            g_config.redis_down_timeout = root[name].asInt();
            if (g_config.redis_down_timeout < 10) {
                g_config.redis_down_timeout = 10; // at least 10 seconds
            }
        } else if ((name == "request_timeout") && root[name].isInt()) {
            g_config.request_timeout = root[name].asInt();
            if (g_config.request_timeout < 1000) {
                g_config.request_timeout = 1000; // at least 1 seconds
            }
        } else if ((name == "update_slave_interval") && root[name].isInt()) {
            g_config.update_slave_interval = root[name].asInt();
        } else if ((name == "master_cs_ip") && root[name].isString()) {
            g_config.master_cs_ip = root[name].asString();
        } else if ((name == "master_cs_port") && root[name].isInt()) {
            g_config.master_cs_port = root[name].asInt();
        } else if ((name == "slave_cs_ip") && root[name].isString()) {
            g_config.slave_cs_ip = root[name].asString();
        } else if ((name == "slave_cs_port") && root[name].isInt()) {
            g_config.slave_cs_port = root[name].asInt();
        } else if ((name == "redis_password") && root[name].isString()) {
            g_config.redis_password = root[name].asString();
        } else {
            json_resp["code"] = 1003;
            json_resp["msg"] = "no suck config parameter";
            break;
        }
    }
    
    send_http_response(http_conn, json_resp);
    save_config_file();
}

void handle_bucket_table(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    log_message(kLogLevelInfo,"url=%s, ip=%s\n", http_parser_wrapper->GetUrl(), http_conn->GetPeerIP());
    
    uint32_t version;
    map<uint16_t, string> bucket_map;
    uint16_t migrating_bucket_id;
    string migrating_server_addr;
    g_bucket_table.GetBucketTalbe(version, bucket_map, migrating_bucket_id, migrating_server_addr);
    
    server_buckets_map_t server_map = transform_table(bucket_map);
    Json::Value json_table;
    for (auto it_server = server_map.begin(); it_server != server_map.end(); ++it_server) {
        string server_addr = it_server->first;
        Json::Value json_buckets;
        
        for (uint16_t bucket_id : it_server->second) {
            json_buckets.append(bucket_id);
        }
        
        json_table[server_addr] = json_buckets;
    }
    
    Json::Value json_resp;
    json_resp["code"] = OK_CODE;
    json_resp["msg"] = OK_MSG;
    
    json_resp["table"] = json_table;
    json_resp["version"] = version;
    json_resp["migrating_bucket_id"] = migrating_bucket_id;
    json_resp["migrating_server_addr"] = migrating_server_addr;
    
    send_http_response(http_conn, json_resp, true);
}

bool is_pattern_param_ok(Json::Value& root)
{
    return root["pattern"].isString();
}

void handle_add_pattern(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    Json::Value root;
    if (!parse_http_param(http_conn, http_parser_wrapper, is_pattern_param_ok, root)) {
        return;
    }
    
    string pattern = root["pattern"].asString();
    g_bucket_table.AddPattern(pattern);
    
    send_success_http_response(http_conn);
}

void handle_del_pattern(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    Json::Value root;
    if (!parse_http_param(http_conn, http_parser_wrapper, is_pattern_param_ok, root)) {
        return;
    }
    
    string pattern = root["pattern"].asString();
    g_bucket_table.DelPattern(pattern);
    
    send_success_http_response(http_conn);
}

void register_http_handler()
{
    HttpHandlerMap* handler_map = HttpHandlerMap::getInstance();
    handler_map->AddHandler(URL_STATS_INFO, handle_stats_info);
    handler_map->AddHandler(URL_RESET_STATS, handle_reset_stats);
    handler_map->AddHandler(URL_SET_CONFIG, handle_set_config);
    handler_map->AddHandler(URL_BUCKET_TABLE, handle_bucket_table);
    handler_map->AddHandler(URL_ADD_PATTERN, handle_add_pattern);
    handler_map->AddHandler(URL_DEL_PATTERN, handle_del_pattern);
}
