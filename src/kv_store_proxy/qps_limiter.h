/*
 *  qps_limiter.h
 *
 *  Created on: 2016-5-30
 *      Author: ziteng
 */

#ifndef __PROXY_QPS_LIMITER_H__
#define __PROXY_QPS_LIMITER_H__

#include "util.h"
#include "config_parser.h"

class QpsLimiter {
public:
    QpsLimiter() {
        start_time_ = (uint32_t)time(NULL);
        cmd_count_ = 0;
    }
    virtual ~QpsLimiter() {}
    
    inline bool IncrAndCheckLimit() {
        lock_guard<mutex> lg(check_mutex_);
        ++cmd_count_;
        if (cmd_count_ < g_config.max_qps) {
            return false;
        } else {
            // 先累计QPS计数器, 只有计数器大于最大QPS限制时，才去判断是否在同一秒，
            // 这样可以极大的减少time的调用次数
            uint32_t cur_time = (uint32_t)time(NULL);
            if (start_time_ == cur_time) {
                if (cmd_count_ == g_config.max_qps) {
                    // 只有第一次达到qps才写日志，不然可能会写太多报警日志
                    log_message(kLogLevelWarning, "reach qps limit: time=%u, cmd_count=%u\n", cur_time, cmd_count_);
                }
                return true;
            } else {
                cmd_count_ = 1;
                start_time_ = cur_time;
                return false;
            }
        }
    }
private:
    mutex       check_mutex_;
    uint32_t    start_time_;
    uint32_t    cmd_count_;
};

#endif
