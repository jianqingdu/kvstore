/*
 *  config_parser.h
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#ifndef __PROXY_CONFIG_PARSER_H__
#define __PROXY_CONFIG_PARSER_H__

#include "util.h"
#include "simple_log.h"

typedef struct {
    string      master_cs_ip;
    uint16_t    master_cs_port;
    string      slave_cs_ip;
    uint16_t    slave_cs_port;
    
    string      listen_http_ip;
    uint16_t    listen_http_port;
    string      listen_client_ip;
    uint16_t    listen_client_port;
    
    string      biz_namespace;
    uint32_t    io_thread_num;
    bool        set_cpu_affinity;
    uint32_t    max_qps;
    uint32_t    max_client_num;
    uint32_t    slow_cmd_time; // unit millisecond
    uint32_t    client_timeout; // unit second
    uint32_t    redis_down_timeout; // unit second
    uint32_t    update_slave_interval; // unit second
    uint32_t    request_timeout;   // unit millisecond
    string      redis_password;
    
    LogLevel    log_level;  // 0--error, 1--warning, 2--info, 3--debug
    string      log_path;
} GlobalConfig;

// no return value, if parse failed the program terminated
void parse_config_file(const char* config_file);

void save_config_file();

extern GlobalConfig g_config;


#endif
