/*
 *  config_parser.cpp
 *
 *  Created on: 2016-4-13
 *      Author: ziteng
 */

#include "config_parser.h"
#include "json/json.h"

GlobalConfig g_config;

static void log_error_msg_and_exit(const char* error_msg)
{
    init_simple_log(kLogLevelError, "error-log");
    log_message(kLogLevelError, error_msg);
    exit(1);
}

void parse_config_file(const char* config_file)
{
    Json::Value root;
    Json::Reader reader;
    string config_content;
    if (get_file_content(config_file, config_content)) {
        log_error_msg_and_exit("get config file content failed\n");
    }
    
    if (!reader.parse(config_content, root)) {
        log_error_msg_and_exit("json parse static content failed\n");
    }
    
    if (!root["io_thread_num"].isInt() || !root["log_level"].isInt() || !root["log_path"].isString() ||
        !root["bucket_table_path"].isString() || !root["cluster_type"].isString() ||
        !root["support_ha"].isBool() || !root["listen"].isArray()) {
        log_error_msg_and_exit("invalid config json format\n");
    }
    
    g_config.io_thread_num = root["io_thread_num"].asInt();
    g_config.log_level = (LogLevel)root["log_level"].asInt();
    g_config.log_path = root["log_path"].asString();
    g_config.bucket_table_path = root["bucket_table_path"].asString();
    g_config.cluster_type = root["cluster_type"].asString();
    g_config.support_ha = root["support_ha"].asBool();
    
    // the difference between data store and cache, see section Data store or cache? in http://redis.io/topics/partitioning
    if ((g_config.cluster_type != "data_store") && (g_config.cluster_type != "cache")) {
        log_error_msg_and_exit("cluster_type must be either cache or data_store\n");
    }
   
    if (root["master_cs_ip"].isString() && root["master_cs_port"].isInt()) {
        g_config.master_cs_ip = root["master_cs_ip"].asString();
        g_config.master_cs_port = root["master_cs_port"].asInt();
        g_config.is_master = false;
    } else {
        g_config.is_master = true;
    }
    
    Json::Value& listen_addr_list = root["listen"];
    int listen_addr_cnt = listen_addr_list.size();
    for (int i = 0; i < listen_addr_cnt; ++i) {
        if (listen_addr_list[i]["ip"].isString() && listen_addr_list[i]["port"].isInt() &&
            listen_addr_list[i]["type"].isString()) {
            string ip = listen_addr_list[i]["ip"].asString();
            uint16_t port = listen_addr_list[i]["port"].asInt();
            string type = listen_addr_list[i]["type"].asString();
            
            if (type == "http") {
                g_config.http_listen_ip = ip;
                g_config.http_listen_port = port;
            } else if (type == "proxy") {
                g_config.proxy_listen_ip = ip;
                g_config.proxy_listen_port = port;
            } else if (type == "migration") {
                g_config.migration_listen_ip = ip;
                g_config.migration_listen_port = port;
            } else if (type == "config_server") {
                g_config.cs_listen_ip = ip;
                g_config.cs_listen_port = port;
            } else {
                string err_msg = "unknown listen type: " + type + "\n";
                log_error_msg_and_exit(err_msg.c_str());
            }
        }
    }
}
