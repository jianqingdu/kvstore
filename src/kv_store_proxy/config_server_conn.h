/*
 *  config_server_conn.h
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#ifndef __PROXY_CONFIG_SERVER_CONN_H__
#define __PROXY_CONFIG_SERVER_CONN_H__

#include "base_conn.h"
#include "pkt_definition.h"

class ConfigServerConn : public BaseConn
{
public:
    ConfigServerConn() {}
    virtual ~ConfigServerConn() {}
    
    static void Init(); // 初始化到主/备ConfigServer的连接
    
    virtual void Close();

    virtual void OnConfirm();
    virtual void OnTimer(uint64_t curr_tick);
    virtual void HandlePkt(PktBase* pkt);
    
private:
    void _HandleBucketTableResp(PktBucketTableResp* pkt);
    void _HandleDelNamespace(PktDelNamespace* pkt);
    void _HandleFreezeProxy(PktFreezeProxy* pkt);
};

void send_to_config_server(PktBase* pkt);

extern atomic<int>  g_in_request_thread_cnt;
extern atomic<bool> g_is_freeze;

#endif
