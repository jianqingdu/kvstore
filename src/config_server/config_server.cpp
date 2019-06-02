/*
 * config_server.cpp
 *
 *  Created on: 2016-3-17
 *      Author: ziteng
 */

#include "config_parser.h"
#include "http_api.h"
#include "proxy_conn.h"
#include "migration_conn.h"
#include "config_server_conn.h"
#include "event_loop.h"
#include "simple_log.h"
#include "base_listener.h"
#include "file_operation.h"

#ifndef CONFIG_SERVER_VERSION
#define CONFIG_SERVER_VERSION "1.0.0" // major.minor.patch
#endif

int main(int argc, char* argv[])
{
    string config_file = "ConfigServer.conf";
    parse_command_args(argc, argv, CONFIG_SERVER_VERSION, config_file);
    parse_config_file(config_file.c_str());
    init_simple_log(g_config.log_level, g_config.log_path);
    log_message(kLogLevelInfo, "config_server starting...\n");
    
    init_thread_event_loops(g_config.io_thread_num);
    init_thread_base_conn(g_config.io_thread_num);
    init_thread_http_conn(g_config.io_thread_num);
    
    register_http_handler();
    
    create_path(g_config.bucket_table_path);
    
    if (!load_bucket_tables())
        return 1;
    
    init_connection_to_master();
    
    if (start_listen<HttpConn>(g_config.http_listen_ip, g_config.http_listen_port)) {
        log_message(kLogLevelError, "listen on port %d failed, exit...\n", g_config.http_listen_port);
        return 1;
    }
    
    if (start_listen<ProxyConn>(g_config.proxy_listen_ip, g_config.proxy_listen_port)) {
        log_message(kLogLevelError, "listen on port %d failed, exit...\n", g_config.proxy_listen_port);
        return 1;
    }
    
    if (start_listen<MigrationConn>(g_config.migration_listen_ip, g_config.migration_listen_port)) {
        log_message(kLogLevelError, "listen on port %d failed, exit...\n", g_config.migration_listen_port);
        return 1;
    }

    if (start_listen<ConfigServerConn>(g_config.cs_listen_ip, g_config.cs_listen_port)) {
        log_message(kLogLevelError, "listen on port %d failed, exit...\n", g_config.cs_listen_port);
        return 1;
    }
    
    write_pid();
    get_main_event_loop()->Start();
    
    return 0;
}
