//
//  config_parser.cpp
//
//  Created by ziteng on 19-5-28.
//

#include "config_parser.h"
#include "json/json.h"

GlobalConfig g_config;
static const char* g_config_file;

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
    
    g_config_file = config_file;
    if (get_file_content(config_file, config_content)) {
        log_error_msg_and_exit("get config file content failed\n");
    }
    
    if (!reader.parse(config_content, root)) {
        log_error_msg_and_exit("json parse static content failed\n");
    }
    
    if (!root["io_thread_num"].isInt() || !root["max_qps"].isInt() ||
        !root["slow_cmd_time"].isInt() || !root["client_timeout"].isInt() ||
        !root["log_level"].isInt() || !root["log_path"].isString() ||
        !root["listen"].isObject() || !root["redis_password"].isString()) {
        log_error_msg_and_exit("json format failed\n");
    }
    
    g_config.listen_http_ip = root["listen"]["http"]["ip"].asString();
    g_config.listen_http_port = root["listen"]["http"]["port"].asInt();
    g_config.listen_client_ip = root["listen"]["client"]["ip"].asString();
    g_config.listen_client_port = root["listen"]["client"]["port"].asInt();
    
    
    g_config.io_thread_num = root["io_thread_num"].asInt();
    g_config.max_qps = root["max_qps"].asInt();
    g_config.slow_cmd_time = root["slow_cmd_time"].asInt();
    g_config.client_timeout = root["client_timeout"].asInt();
    g_config.redis_ip = root["redis_ip"].asString();
    g_config.redis_port = root["redis_port"].asInt();
    g_config.redis_dbnum = root["redis_dbnum"].asInt();
    g_config.redis_password = root["redis_password"].asString();
    
    g_config.log_level = (LogLevel)root["log_level"].asInt();
    g_config.log_path = root["log_path"].asString();
    
    g_config.max_client_num = 10000;
    if (root["max_client_num"].isInt()) {
        g_config.max_client_num = root["max_client_num"].asInt();
    }
    
    g_config.request_timeout = 2000;
    if (root["request_timeout"].isInt()) {
        g_config.request_timeout = root["request_timeout"].asInt();
    }
    
}

// use raw string constant in C++11 instead of Json::Write() to rewrite config file, so as to keep comment and sequence
void save_config_file()
{
    string config_pattern =
R"({
    "listen": {
        "client": {
            "ip": "%s",
            "port": %d
        },
        "http": {
            "ip": "%s",
            "port": %d
        }
    },
    
    "io_thread_num": %d,
    "max_qps": %d,
    "max_client_num": %d, // the max number of connected clients at the same time
    "slow_cmd_time": %d, // unit millisecond
    "client_timeout": %d, // unit second
    "request_timeout": %d, // unit second
    "redis_ip": "%s",
    "redis_port": %d,
    "redis_dbnum": %d,
    "redis_password": "%s",
    
    "log_level": %d, // 0-error,1-warning, 2-info, 3-debug
    "log_path": "log"
}
)";

    FILE* fp = fopen(g_config_file, "w");
    if (!fp) {
        log_message(kLogLevelError, "open config file failed\n");
        return;
    }

    fprintf(fp, config_pattern.c_str(),
            g_config.listen_client_ip.c_str(), g_config.listen_client_port,
            g_config.listen_http_ip.c_str(), g_config.listen_http_port,
            g_config.io_thread_num, g_config.max_qps, g_config.max_client_num,
            g_config.slow_cmd_time, g_config.client_timeout, g_config.request_timeout,
            g_config.redis_ip.c_str(), g_config.redis_port, g_config.redis_dbnum,
            g_config.redis_password.c_str(), g_config.log_level);
    fclose(fp);
}
