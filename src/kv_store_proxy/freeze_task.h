/*
 *  freeze_task.h
 *
 *  Created on: 2016-8-30
 *      Author: ziteng
 */

#ifndef __PROXY_FREEZE_TASK_H__
#define __PROXY_FREEZE_TASK_H__

#include "simple_log.h"
#include "util.h"
#include "thread_pool.h"
#include "config_parser.h"
#include "config_server_conn.h"
#include "redis_conn_table.h"
#include "pkt_definition.h"

class FreezeTask : public Task {
public:
    FreezeTask() {}
    virtual ~FreezeTask() {}
    
    virtual void run() {
        uint64_t start_tick = get_tick_count();
        
        while (true) {
            int pending_request_cnt = 0;
            for (uint32_t i = 0; i < g_config.io_thread_num; i++) {
                // 由于在freeze状态的时候bucket对照表不会变化，所以不加锁获取每个IO线程的pending请求个数也是可以的
                pending_request_cnt += g_redis_conn_tables.GetIOResource(i)->GetPendingRequestCnt();
            }
            
            // 只有当所有已经发送到Redis的请求都处理完，而且没有新的请求进来时，才返回ACK给CS
            // 目的是为了避免在处理请求的中间，bucket对照表进行了变更
            if ((pending_request_cnt == 0) && (g_in_request_thread_cnt == 0)) {
                break;
            }
            
            // 如果1s内还没有处理完所有请求，应该是一个redis被block了，直接退出freeze状态
            if (get_tick_count() > start_tick + 1000) {
                log_message(kLogLevelError, "block more than 1 second, just break out\n");
                break;
            }
            
            log_message(kLogLevelInfo, "pending_request_cnt=%d, in_request_thread_cnt=%d\n",
                        pending_request_cnt, (int)g_in_request_thread_cnt);
            usleep(500);
        }
        
        uint64_t stop_tick = get_tick_count();
        log_message(kLogLevelInfo, "block %llu milliseconds\n", stop_tick - start_tick);
        
        PktFreezeProxy* pkt = new PktFreezeProxy(g_config.biz_namespace);
        send_to_config_server(pkt);
    }
};

#endif 
