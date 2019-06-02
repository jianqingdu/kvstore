//
//  cmd_line_parser.h
//  kv-store
//
//  Created by ziteng on 16-7-1.
//

#ifndef __REDIS_PORT_CMD_LINE_PARSER_H__
#define __REDIS_PORT_CMD_LINE_PARSER_H__

#include "util.h"
#include "rdb.h"

struct Config {
    string  src_redis_host;
    int     src_redis_port;
    int     src_redis_db;   // select from which db, -1 means any db
    string  src_redis_password;
    string  dst_redis_host;
    int     dst_redis_port;
    int     dst_redis_db;   // write to which db, -1 means do not need to select db
    string  dst_redis_password;
    int     dst_rdb_version;
    int     pipeline_cnt;
    string  rdb_file;
    string  aof_file;
    string  prefix;
    int     network_limit;  // limit network speed when receive RDB file
    bool    with_replace;
    bool    src_from_rdb;
    bool    daemon;
    FILE*   logfp;
    
    Config() {
        src_redis_host = "127.0.0.1";
        src_redis_port = 6375;
        src_redis_db = -1;
        src_redis_password = "";
        dst_redis_host = "127.0.0.1";
        dst_redis_port = 7400;
        dst_redis_db = -1;
        dst_redis_password = "";
        dst_rdb_version = REDIS_RDB_VERSION;
        pipeline_cnt = 32;
        rdb_file = "dump.rdb";
        aof_file = "dump.aof";
        prefix = "";
        network_limit = 50 * 1024 * 1024;
        with_replace = false;
        src_from_rdb = false;
        daemon = false;
        logfp = NULL;
    }
};

extern Config g_config;

void parse_cmd_line(int argc, char* argv[]);

#endif
