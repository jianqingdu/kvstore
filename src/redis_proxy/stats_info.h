//
//  stats_info.h
//
//  Created by jianqing.du on 19-5-28.
//

#ifndef __PROXY_STATS_INFO_H__
#define __PROXY_STATS_INFO_H__

#include "util.h"

class StatsInfo {
public:
    StatsInfo() {
        Reset();
        client_count_ = 0;
        average_rt_ = 0;
        total_rt_ = 0;
        rt_cmd_count_ = 0;
        qps_ = 0;
    }
    
    virtual ~StatsInfo() {}
    
    void Reset() {
        total_cmd_count_ = 0;
        slow_cmd_count_ = 0;
        last_total_cmd_count_ = 0;
        last_tick_ = get_tick_count();
        
        lock_guard<mutex> lock(map_mutex_);
        cmd_count_map_.clear();
    }
    
    void IncrCmdCount(const string& cmd) {
        lock_guard<mutex> lock(map_mutex_);
        ++cmd_count_map_[cmd];
    }
    
    void IncrTotalCmdCount() { ++total_cmd_count_; }
    void IncrSlowCmdCount() { ++slow_cmd_count_; }
    void IncrClientCount() { ++client_count_; }
    void DecrClientCount() { --client_count_; }
    uint32_t GetClientCount() { return client_count_; }
    
    void IncrRtCount(uint32_t rt) {
        total_rt_ += rt;
        ++rt_cmd_count_;
    }
    
    void CalculateRtAndQps() {
        average_rt_ = (rt_cmd_count_ == 0) ? 0 : (uint32_t)(total_rt_ / rt_cmd_count_);
        total_rt_ = 0;
        rt_cmd_count_ = 0;
        
        uint64_t current_tick = get_tick_count();
        uint64_t tick_diff = current_tick - last_tick_;
        qps_ = (tick_diff == 0) ? 0 : (uint32_t)((total_cmd_count_ - last_total_cmd_count_) * 1000 / tick_diff);
        last_total_cmd_count_ = (uint64_t)total_cmd_count_;
        last_tick_ = current_tick;
    }
    
    void GetStatsInfo(uint64_t& total_cmd_count, uint64_t& slow_cmd_count,
                      uint32_t& client_count, uint32_t& average_rt,
                      uint32_t& qps, map<string, uint64_t>& cmd_count_map) {
        total_cmd_count = total_cmd_count_;
        slow_cmd_count = slow_cmd_count_;
        client_count = client_count_;
        average_rt = average_rt_;
        qps = qps_;
        
        lock_guard<mutex> lock(map_mutex_);
        cmd_count_map = cmd_count_map_;
    }
private:
    mutex                   map_mutex_;
    map<string, uint64_t>   cmd_count_map_;
    
    atomic<uint64_t>        total_rt_;
    atomic<uint64_t>        rt_cmd_count_;
    atomic<uint32_t>        average_rt_;
    
    atomic<uint64_t>        total_cmd_count_;
    atomic<uint64_t>        slow_cmd_count_;
    atomic<uint64_t>        last_total_cmd_count_;
    atomic<uint64_t>        last_tick_;
    atomic<uint32_t>        qps_;
    
    atomic<uint32_t>        client_count_;
};

extern StatsInfo g_stats_info;

#endif
