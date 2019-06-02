//
//  cmd_line_parser.h
//  kv-store
//
//  Created by ziteng on 16-5-25.
//

#ifndef __MT_CMD_LINE_PARSER_H__
#define __MT_CMD_LINE_PARSER_H__

#include "util.h"

struct Config {
    string  cs_host;
    int     cs_port;
    string  redis_host;
    int     redis_port;
    string  redis_addr;
    string  redis_password;
    string  command;
    string  biz_namespace;
    int     count;          // 指定迁移bucket个数
    int     wait_ms;        // 每次bucket迁移前等待时间，用于等待待迁移bucket的主备同步完成
    FILE*   logfp;
    
    Config() {
        cs_host = "127.0.0.1";
        cs_port = 7200;
        biz_namespace = "test";
        count = 0xFFFF;
        wait_ms = 1000; // 1 seconds
    }
};

extern Config g_config;

void parse_cmd_line(int argc, char* argv[]);

#endif
