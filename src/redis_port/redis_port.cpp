//
//  redis_port.cpp
//  kv-store
//
//  Created by ziteng on 16-7-1.
//

#include "util.h"
#include "cmd_line_parser.h"
#include "event_loop.h"
#include "src_redis_conn.h"
#include "sync_task.h"

int main(int argc, char* argv[])
{
    parse_cmd_line(argc, argv);
    
    if (g_config.daemon) {
        run_as_daemon();
        write_pid();
    }
    
    string log_file = "output.log." + to_string(getpid());
    g_config.logfp = fopen(log_file.c_str(), "w");
    if (!g_config.logfp) {
        fprintf(stderr, "open log file failed\n");
        return 1;
    }
    
    setlinebuf(g_config.logfp);
    
    int io_thread_num = 0; // all nonblock network io in main thread
    init_thread_event_loops(io_thread_num);
    init_thread_base_conn(io_thread_num);
    
    g_thread_pool.Init(1);  // 工作线程池只有一个线程，保证所有的task都按顺序执行
    
    if (g_config.src_from_rdb) {
        SyncRdbTask* task = new SyncRdbTask();
        g_thread_pool.AddTask(task);
    } else {
        SrcRedisConn* src_conn = new SrcRedisConn();
        src_conn->Connect(g_config.src_redis_host, g_config.src_redis_port);
    }
    
    get_main_event_loop()->Start();
    
    return 0;
}
