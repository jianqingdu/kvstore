//
//  http_api.cpp
//
//  Created by ziteng on 19-5-28.
//

#include "http_api.h"
#include "http_handler_map.h"
#include "stats_info.h"
#include "json/json.h"
#include "simple_log.h"
#include "config_parser.h"

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
        } else if ((name == "request_timeout") && root[name].isInt()) {
            g_config.request_timeout = root[name].asInt();
            if (g_config.request_timeout < 1000) {
                g_config.request_timeout = 1000; // at least 1 seconds
            }
        } else if ((name == "redis_ip") && root[name].isString()) {
            g_config.redis_ip = root[name].asString();
        } else if ((name == "redis_port") && root[name].isInt()) {
            g_config.redis_port = root[name].asInt();
        } else if ((name == "redis_password") && root[name].isString()) {
            g_config.redis_dbnum = root[name].asInt();
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

void register_http_handler()
{
    HttpHandlerMap* handler_map = HttpHandlerMap::getInstance();
    handler_map->AddHandler(URL_STATS_INFO, handle_stats_info);
    handler_map->AddHandler(URL_RESET_STATS, handle_reset_stats);
    handler_map->AddHandler(URL_SET_CONFIG, handle_set_config);
}
