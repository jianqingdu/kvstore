//
//  cmd_line_parser.cpp
//  kv-store
//
//  Created by ziteng on 16-5-25.
//

#include "cmd_line_parser.h"

#ifndef MIGRATION_TOOL_VERSION
#define MIGRATION_TOOL_VERSION "1.0.0" // major.minor.patch
#endif

Config g_config;

static void print_usage(const char* program)
{
    fprintf(stderr, "%s [OPTIONS]\n"
            "  --cs <host:port>                 master config server ip:port (default: 127.0.0.1:7200)\n"
            "  --add|del <redis host:port>      add/del redis server ip:port(must set)\n"
            "  --password <password>            redis password(default: none)\n"          
            "  --ns <namespace>                 business namespace (default: test)\n"
            "  --count <bucket_count>           max migration bucket count (default: all)\n"
            "  --wait_ms <wait_milliseconds>    wait time before every bucket migration in milliseconds (default 1 seconds)\n"
            "  --version                        show version\n"
            "  --help\n", program);
}

void parse_cmd_line(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        bool last_arg = (i == argc - 1);
        
        if (!strcmp(argv[i], "--help")) {
            print_usage(argv[0]);
            exit(0);
        } else if (!strcmp(argv[i], "--cs") && !last_arg) {
            string addr = argv[++i];
            if (!get_ip_port(addr, g_config.cs_host, g_config.cs_port)) {
                fprintf(stderr, "invalid cs addr: %s\n", addr.c_str());
                exit(0);
            }
        } else if (!strcmp(argv[i], "--add") && !last_arg) {
            g_config.redis_addr = argv[++i];
            if (!get_ip_port(g_config.redis_addr, g_config.redis_host, g_config.redis_port)) {
                fprintf(stderr, "invalid cs addr: %s\n", g_config.redis_addr.c_str());
                exit(0);
            }
            g_config.command = "add";
        } else if (!strcmp(argv[i], "--del") && !last_arg) {
            g_config.redis_addr = argv[++i];
            if (!get_ip_port(g_config.redis_addr, g_config.redis_host, g_config.redis_port)) {
                fprintf(stderr, "invalid cs addr: %s\n", g_config.redis_addr.c_str());
                exit(0);
            }
            g_config.command = "del";
        } else if (!strcmp(argv[i], "--password") && !last_arg) {
            g_config.redis_password = argv[++i];
        } else if (!strcmp(argv[i], "--ns") && !last_arg) {
            g_config.biz_namespace = argv[++i];
        } else if (!strcmp(argv[i], "--count") && !last_arg) {
            g_config.count = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--wait_ms") && !last_arg) {
            g_config.wait_ms = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--version")) {
            printf("migration_tool Version: %s\n", MIGRATION_TOOL_VERSION);
            printf("migration_tool Build: %s %s\n", __DATE__, __TIME__);
            exit(0);
        } else {
            print_usage(argv[0]);
            exit(1);
        }
    }
    
    if (g_config.redis_addr.empty()) {
        fprintf(stderr, "error: must set redis address\n");
        exit(1);
    }
}
