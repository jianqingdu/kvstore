//
//  config_parser.h
//
//  Created by ziteng on 19-5-28.
//

#ifndef __PROXY_CONFIG_PARSER_H__
#define __PROXY_CONFIG_PARSER_H__

#include "util.h"
#include "simple_log.h"

typedef struct {
    string      listen_http_ip;
    uint16_t    listen_http_port;
    string      listen_client_ip;
    uint16_t    listen_client_port;
    
    uint32_t    io_thread_num;
    uint32_t    max_qps;
    uint32_t    max_client_num;
    uint32_t    slow_cmd_time; // unit millisecond
    uint32_t    client_timeout; // unit second
    uint32_t    request_timeout;   // unit millisecond
    string      redis_ip;
    uint16_t    redis_port;
    uint16_t    redis_dbnum;
    string      redis_password;
    
    LogLevel    log_level;  // 0--error, 1--warning, 2--info, 3--debug
    string      log_path;
} GlobalConfig;

// no return value, if parse failed the program terminated
void parse_config_file(const char* config_file);

void save_config_file();

extern GlobalConfig g_config;


#endif
