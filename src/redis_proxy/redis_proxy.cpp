//
//  redis_proxy.cpp
//
//  Created by ziteng on 19-5-28.
//

#include "util.h"
#include "config_parser.h"
#include "client_conn.h"
#include "http_api.h"
#include "base_listener.h"
#include "event_loop.h"
#include "stats_info.h"
#include "async_redis_conn.h"

#ifndef REDIS_PROXY_VERSION
#define REDIS_PROXY_VERSION "1.0.0" // major.minor.patch
#endif

void rt_timer_callback(void* callback_data, uint8_t msg, uint32_t handle, void* pParam)
{
    g_stats_info.CalculateRtAndQps();
}

int main(int argc, char* argv[])
{
    string config_file = "./RedisProxy.conf";
    parse_command_args(argc, argv, REDIS_PROXY_VERSION, config_file);
    
    parse_config_file(config_file.c_str());
    init_simple_log(g_config.log_level, g_config.log_path);
    log_message(kLogLevelInfo, "redis_proxy starting...\n");
    
    init_thread_event_loops(g_config.io_thread_num);
    init_thread_base_conn(g_config.io_thread_num);
    init_thread_http_conn(g_config.io_thread_num);
    
    g_redis_conns.Init(g_config.io_thread_num);
    for (uint32_t i = 0; i < g_config.io_thread_num; i++) {
        g_redis_conns.GetIOResource(i)->Init(i);
    }

    register_http_handler();
    get_main_event_loop()->AddTimer(rt_timer_callback, NULL, 5000);
    
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
