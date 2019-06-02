/*
 * http_api.cpp
 *
 *  Created on: 2016-3-17
 *      Author: ziteng
 */

#include "http_api.h"
#include "config_server_conn.h"
#include "json/json.h"
#include "simple_log.h"
#include "pkt_definition.h"
#include "namespace_manager.h"
#include "config_parser.h"
#include "http_handler_map.h"
#include "common.h"

#define CHECK_MASTER  \
    if (!g_config.is_master) { \
        send_failure_http_response(http_conn, 1004, "not a master"); \
        return; \
    }

#define CHECK_CLUSTER_TYPE(type) \
    if (g_config.cluster_type != type) { \
        send_failure_http_response(http_conn, 1005, "not match the cluster type"); \
        return; \
    }


///// api fucntions /////
static bool is_init_table_param_ok(Json::Value& root)
{
    return root["namespace"].isString() && root["bucket_count"].isInt() && root["address_list"].isArray();
}

void handle_init_table(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    Json::Value root;
    if (!parse_http_param(http_conn, http_parser_wrapper, is_init_table_param_ok, root)) {
        return;
    }
    
    CHECK_MASTER
        
    string ns = root["namespace"].asString();
    uint16_t bucket_cnt = root["bucket_count"].asInt();
    Json::Value& addr_list = root["address_list"];
    
    vector<string> server_addr_vec;
    int addr_cnt = addr_list.size();
    for (int i = 0; i < addr_cnt; ++i) {
        if (!addr_list[i].isString()) {
            break;
        }
        
        string addr = addr_list[i].asString();
        string ip;
        int port;
        if (!get_ip_port(addr, ip, port)) {
            break;
        }
        
        server_addr_vec.push_back(addr);
    }
    
    if ((int)server_addr_vec.size() != addr_cnt) {
        send_failure_http_response(http_conn, 1005, "invalid redis address");
        return;
    }
    
    bool ret = g_namespace_manager.InitTable(ns, bucket_cnt, server_addr_vec);
    if (ret) {
        send_success_http_response(http_conn);
    } else {
        send_failure_http_response(http_conn, 1006, "InitTable failed");
    }
}

static bool is_del_namespace_param_ok(Json::Value& root)
{
    return root["namespace"].isString();
}

void handle_del_namespace(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    Json::Value root;
    if (!parse_http_param(http_conn, http_parser_wrapper, is_del_namespace_param_ok, root)) {
        return;
    }
    
    CHECK_MASTER
    
    string ns = root["namespace"].asString();
    if (g_namespace_manager.DelTable(ns)) {
        send_success_http_response(http_conn);
    } else {
        send_failure_http_response(http_conn, 1005, "no such namespace");
    }
}

void handle_namespace_list(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    log_message(kLogLevelInfo,"url=%s, ip=%s\n", http_parser_wrapper->GetUrl(), http_conn->GetPeerIP());
    
    Json::Value json_resp, json_ns_list;
    json_resp["code"] = OK_CODE;
    json_resp["msg"] = OK_MSG;
    
    vector<string> ns_vec = g_namespace_manager.GetNamespaceList();
    for (const string& ns : ns_vec) {
        json_ns_list.append(ns);
    }
    json_resp["namespace_list"] = json_ns_list;
    
    send_http_response(http_conn, json_resp);
}

static bool is_table_param_ok(Json::Value& root)
{
    return root["namespace"].isString();
}

void handle_bucket_table(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    Json::Value root;
    if (!parse_http_param(http_conn, http_parser_wrapper, is_table_param_ok, root)) {
        return;
    }
    
    string ns = root["namespace"].asString();
    uint32_t version = 0;
    map<uint16_t, string> bucket_map;
    uint16_t migrating_bucket_id;
    string migrating_server_addr;
    if (!g_namespace_manager.GetBucketTable(ns, version, bucket_map, migrating_bucket_id, migrating_server_addr)) {
        send_failure_http_response(http_conn, 1004, "no such namespace");
        return;
    }
    
    Json::Value json_resp;
    jsonize_bucket_table(json_resp, version, bucket_map, migrating_bucket_id, migrating_server_addr);
    
    json_resp["code"] = OK_CODE;
    json_resp["msg"] = OK_MSG;
    
    send_http_response(http_conn, json_resp, true);
}

static bool is_proxy_info_param_ok(Json::Value& root)
{
    return root["namespace"].isString();
}

void handle_proxy_info(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    Json::Value root;
    if (!parse_http_param(http_conn, http_parser_wrapper, is_proxy_info_param_ok, root)) {
        return;
    }
    
    string ns = root["namespace"].asString();
    map<net_handle_t, ProxyStateInfo_t> proxy_info_map;
    if (!g_namespace_manager.GetProxyInfo(ns, proxy_info_map)) {
        send_failure_http_response(http_conn, 1004, "no such namespace");
        return;
    }
    
    Json::Value json_resp, json_proxy_array;
    for (auto it = proxy_info_map.begin(); it != proxy_info_map.end(); ++it) {
        Json::Value json_proxy;
        json_proxy["state"] = kProxyStateName[it->second.state];
        json_proxy["info"] = it->second.info;
        
        json_proxy_array.append(json_proxy);
    }
    
    json_resp["code"] = OK_CODE;
    json_resp["msg"] = OK_MSG;
    json_resp["proxy_info"] = json_proxy_array;
    
    send_http_response(http_conn, json_resp);
}

static bool is_replace_server_param_ok(Json::Value& root)
{
    return root["namespace"].isString() && root["old_addr"].isString() && root["new_addr"].isString();
}

// 两种场景需要替换Redis服务器地址
//  1. data_store模式的主Redis服务器宕机，需要把备切换到主
//  2. Redis的内存碎片太多，可以把redis数据同步到另一个Redis实例，然后替换Redis地址
void handle_replace_server(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    Json::Value root;
    if (!parse_http_param(http_conn, http_parser_wrapper, is_replace_server_param_ok, root)) {
        return;
    }
    
    CHECK_MASTER
    
    string ns = root["namespace"].asString();
    string old_addr = root["old_addr"].asString();
    string new_addr = root["new_addr"].asString();
    
    string old_ip, new_ip;
    int old_port, new_port;
    if (!get_ip_port(old_addr, old_ip, old_port) || !get_ip_port(new_addr, new_ip, new_port)) {
        send_failure_http_response(http_conn, 1006, "invade addr format");
        return;
    }
    
    uint32_t result = g_namespace_manager.ReplaceServerAddress(ns, old_addr, new_addr);
    if (!result) {
        send_success_http_response(http_conn);
    } else {
        send_failure_http_response(http_conn, 1007, "ReplaceServerAddresss failed");
    }
}

static bool is_scale_cluster_param_ok(Json::Value& root)
{
    return root["namespace"].isString() && root["action"].isString() && root["addr"].isString();
}

void handle_scale_cache_cluster(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    Json::Value root;
    if (!parse_http_param(http_conn, http_parser_wrapper, is_scale_cluster_param_ok, root)) {
        return;
    }
    
    CHECK_MASTER
    CHECK_CLUSTER_TYPE("cache");
    
    string ns = root["namespace"].asString();
    string action = root["action"].asString();
    string addr = root["addr"].asString();
    
    string ip;
    int port;
    if (!get_ip_port(addr, ip, port)) {
        send_failure_http_response(http_conn, 1006, "not a valid addr");
        return;
    }
    
    bool ret = false;
    if (action == "add") {
        ret = g_namespace_manager.ScaleUpTable(ns, addr);
    } else if (action == "del") {
        ret = g_namespace_manager.ScaleDownTable(ns, addr);
    } else {
        send_failure_http_response(http_conn, 1007, "not a valid action");
        return;
    }
    
    if (ret) {
        send_success_http_response(http_conn);
    } else {
        send_failure_http_response(http_conn, 1008, "scale up/down failed");
    }
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
        json_resp["msg"] = "json format invalide";
        send_http_response(http_conn, json_resp);
        return;
    }
    
    json_resp["code"] = OK_CODE;
    json_resp["msg"] = OK_MSG;
    vector<string> member_names = root.getMemberNames();
    for (const string& name : member_names) {
        if ((name == "support_ha") && root[name].isBool()) {
            g_config.support_ha = root[name].asBool();
        } else {
            json_resp["code"] = 1003;
            json_resp["msg"] = "no suck config parameter";
            break;
        }
    }
    
    send_http_response(http_conn, json_resp);
}

void handle_get_config(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper)
{
    log_message(kLogLevelInfo, "url=%s, ip=%s\n", http_parser_wrapper->GetUrl(), http_conn->GetPeerIP());
    Json::Value json_resp, data;
    
    json_resp["code"] = OK_CODE;
    json_resp["msg"] = OK_MSG;
    
    data["support_ha"] = g_config.support_ha;
    data["is_master"] = g_config.is_master;
    data["cluster_type"] = g_config.cluster_type;
    json_resp["data"] = data;
    
    send_http_response(http_conn, json_resp);
}

void register_http_handler()
{
    HttpHandlerMap* pHandleMap = HttpHandlerMap::getInstance();
    pHandleMap->AddHandler(URL_INIT_TABLE, handle_init_table);
    pHandleMap->AddHandler(URL_DEL_NAMEPACE, handle_del_namespace);
    pHandleMap->AddHandler(URL_NAMESPACE_LIST, handle_namespace_list);
    pHandleMap->AddHandler(URL_BUCKET_TABLE, handle_bucket_table);
    pHandleMap->AddHandler(URL_PROXY_INFO, handle_proxy_info);
    pHandleMap->AddHandler(URL_REPLACE_SERVER, handle_replace_server);
    pHandleMap->AddHandler(URL_SCALE_CACHE_CLUSTER, handle_scale_cache_cluster);
    pHandleMap->AddHandler(URL_SET_CONFIG, handle_set_config);
    pHandleMap->AddHandler(URL_GET_CONFIG, handle_get_config);
}
