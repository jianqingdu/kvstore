/*
 *  config_parser.h
 *
 *  Created on: 2016-4-13
 *      Author: ziteng
 */

#ifndef __CS_CONFIG_PARSER_H__
#define __CS_CONFIG_PARSER_H__

#include "util.h"
#include "simple_log.h"

typedef struct {
    bool        is_master;
    uint32_t    io_thread_num;
    LogLevel    log_level;  // 0--error, 1--warning, 2--info, 3--debug
    string      log_path;
    string      bucket_table_path;
    string      cluster_type; // data_store or cache
    bool        support_ha; // automatic support HA(data_store-auto master slave switch, cache-auto reject redis)
    
    string      master_cs_ip;
    uint16_t    master_cs_port;
    
    string      http_listen_ip;
    uint16_t    http_listen_port;
    string      proxy_listen_ip;
    uint16_t    proxy_listen_port;
    string      migration_listen_ip;
    uint16_t    migration_listen_port;
    string      cs_listen_ip;
    uint16_t    cs_listen_port;
} GlobalConfig;

// no return value, if parse failed the program terminated
void parse_config_file(const char* config_file);

extern GlobalConfig g_config;

#endif
