//
//  redis_monitor_thread.h
//  kv-store
//
//  Created by ziteng on 16/7/21.
//

#ifndef __PROXY_REDIS_MONITOR_THREAD_H__
#define __PROXY_REDIS_MONITOR_THREAD_H__

#include "thread_pool.h"

class RedisMonitorThread : public Thread {
public:
    virtual void OnThreadRun(void);
    
    void SetPingSwitch(bool on) {
        is_ping_ = on;
        stop_ping_cnt_ = 0;
    }
private:
    bool    is_ping_;
    int     stop_ping_cnt_;
};

extern RedisMonitorThread g_monitor_thread;

#endif
