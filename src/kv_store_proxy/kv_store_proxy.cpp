/*
 *  kv_store_proxy.cpp
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#include "util.h"
#include "config_parser.h"
#include "client_conn.h"
#include "http_api.h"
#include "base_listener.h"
#include "event_loop.h"
#include "config_server_conn.h"
#include "redis_conn_table.h"
#include "redis_monitor_thread.h"
#include "stats_info.h"

#ifndef KV_STORE_PROXY_VERSION
#define KV_STORE_PROXY_VERSION "1.0.0" // major.minor.patch
#endif

void rt_timer_callback(void* callback_data, uint8_t msg, uint32_t handle, void* pParam)
{
    g_stats_info.CalculateRtAndQps();
}

int main(int argc, char* argv[])
{
    string config_file = "./KvStoreProxy.conf";
    parse_command_args(argc, argv, KV_STORE_PROXY_VERSION, config_file);
    
    parse_config_file(config_file.c_str());
    init_simple_log(g_config.log_level, g_config.log_path);
    log_message(kLogLevelInfo, "kv_store_proxy starting...\n");
    
    init_thread_event_loops(g_config.io_thread_num, g_config.set_cpu_affinity);
    init_thread_base_conn(g_config.io_thread_num);
    init_thread_http_conn(g_config.io_thread_num);
    
    g_redis_conn_tables.Init(g_config.io_thread_num);
    for (uint32_t i = 0; i < g_config.io_thread_num; i++) {
        g_redis_conn_tables.GetIOResource(i)->Init(i);
    }

    register_http_handler();
    get_main_event_loop()->AddTimer(rt_timer_callback, NULL, 5000);
    
    ConfigServerConn::Init();
    
    g_monitor_thread.StartThread();
    
    if (start_listen<HttpConn>(g_config.listen_http_ip, g_config.listen_http_port)) {
        log_message(kLogLevelError, "listen on port %d failed, exit...\n", g_config.listen_http_port);
        return 1;
    }
    
    if (start_listen<ClientConn>(g_config.listen_client_ip, g_config.listen_client_port)) {
        log_message(kLogLevelError, "listen on port %d failed, exit...\n", g_config.listen_client_port);
        return 1;
    }
    
    write_pid();
    get_main_event_loop()->Start();
    
    return 0;
}
