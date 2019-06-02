/*
 *  config_server_conn.cpp
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#include "config_server_conn.h"
#include "config_parser.h"
#include "simple_log.h"
#include "event_loop.h"
#include "bucket_table.h"
#include "table_transform.h"
#include "freeze_task.h"

// Proxy在启动时，先去连接master cs，如果连接不上master cs, 会去连接一次slave cs来获取bucket对照表，
// 获取完对照表后，需要关闭到slave cs的连接
// Proxy连接上master cs或者连接了一次slave后，不会再去连接slave cs
// Proxy如果连接到master cs则会一直保持连接，以便立刻获得cs的通知
// Proxy去连接一次slave cs是为了处理以下场景: master挂了，proxy刚好也挂了, proxy重启，这时可以去slave cs临时取下对照表
// 但是由于slave cs的状态可能不是最新的，所以不能一直连着slave cs

atomic<int> g_in_request_thread_cnt {0};
atomic<bool> g_is_freeze {true}; // proxy刚启动时，如果还没获取到bucket对照表，客户端请求就过来了，需要把请求放入freeze队列

static ThreadPool g_thread_pool;

static mutex g_handle_mutex;
static net_handle_t g_master_handle = NETLIB_INVALID_HANDLE;
static bool g_can_connect_slave = true;

static void connect_config_server()
{
    if ((g_master_handle == NETLIB_INVALID_HANDLE) && !g_config.master_cs_ip.empty()) {
        ConfigServerConn* master_conn = new ConfigServerConn;
        net_handle_t handle = master_conn->Connect(g_config.master_cs_ip, g_config.master_cs_port);
        if (handle != NETLIB_INVALID_HANDLE) {
            lock_guard<mutex> lg(g_handle_mutex);
            g_master_handle = handle;
        }
    }
}

static void config_server_timer_callback(void* callback_data, uint8_t msg, uint32_t handle, void* pParam)
{
    connect_config_server();
}


void send_to_config_server(PktBase* pkt)
{
    g_handle_mutex.lock();
    net_handle_t handle = g_master_handle;
    g_handle_mutex.unlock();
    
    if (handle != NETLIB_INVALID_HANDLE) {
        BaseConn::SendPkt(handle, pkt);
    }
}

// static methods
void ConfigServerConn::Init()
{
    g_thread_pool.Init(1);
    
    connect_config_server();
    
    get_main_event_loop()->AddTimer(config_server_timer_callback, NULL, 2000);
}

// instance methods
void ConfigServerConn::Close()
{
    if (m_handle != NETLIB_INVALID_HANDLE) {
        if (m_open) {
            log_message(kLogLevelInfo, "connection to %s:%d broken\n", m_peer_ip.c_str(), m_peer_port);
        } else {
            log_message(kLogLevelInfo, "connect to %s:%d failed\n", m_peer_ip.c_str(), m_peer_port);
        }
        
        lock_guard<mutex> lg(g_handle_mutex);
        if (m_handle == g_master_handle) {
            g_master_handle = NETLIB_INVALID_HANDLE;
            
            if (g_can_connect_slave) {
                g_can_connect_slave = false;
                ConfigServerConn* slave_conn = new ConfigServerConn;
                slave_conn->Connect(g_config.slave_cs_ip, g_config.slave_cs_port);
            }
        }
    }
    
    BaseConn::Close();
}

void ConfigServerConn::OnConfirm()
{
    BaseConn::OnConfirm();
    
    g_can_connect_slave = false;
    log_message(kLogLevelInfo, "connect to %s:%d success\n", m_peer_ip.c_str(), m_peer_port);
    
    string addr_info = g_config.listen_client_ip + ":" + std::to_string(g_config.listen_client_port);
    
    PktBucketTableReq pkt(g_config.biz_namespace, TABLE_REQ_FROM_PROXY, addr_info);
    SendPkt(&pkt);
}

void ConfigServerConn::OnTimer(uint64_t curr_tick)
{
    if (curr_tick > m_last_send_tick + m_heartbeat_interval) {
        PktHeartBeat pkt;
        SendPkt(&pkt);
    }
    
    if (curr_tick > m_last_recv_tick + m_conn_timeout) {
        log_message(kLogLevelWarning, "ConfigServer timeout %s:%d\n", m_peer_ip.c_str(), m_peer_port);
        Close();
        return;
    }
}

void ConfigServerConn::HandlePkt(PktBase* pkt)
{
    switch (pkt->GetPktId()) {
        case PKT_ID_HEARTBEAT:
            break;
        case PKT_ID_BUCKET_TABLE_RESP:
            _HandleBucketTableResp((PktBucketTableResp *)pkt);
            break;
        case PKT_ID_DEL_NAMESPACE:
            _HandleDelNamespace((PktDelNamespace *)pkt);
            break;
        case PKT_ID_FREEZE_PROXY:
            _HandleFreezeProxy((PktFreezeProxy *)pkt);
            break;
        default:
            log_message(kLogLevelError, "unknown pkt_id=%d from cs\n", pkt->GetPktId());
            break;
    }
}

void ConfigServerConn::_HandleBucketTableResp(PktBucketTableResp* pkt)
{
    string& ns = pkt->GetNamespace();
    uint32_t result = pkt->GetResult();
    if (result) {
        log_message(kLogLevelError, "BucketTableResp failed, ns=%s, result=%d\n", ns.c_str(), result);
        log_message(kLogLevelInfo, "kv_store_proxy stopping...\n\n");
        _exit(1);    // 没bucket路由表就直接退出进程
    }
    
    uint32_t version = pkt->GetVersion();
    server_buckets_map_t& server_map = pkt->GetServerBucketsMap();
    uint16_t migrating_bucket_id = pkt->GetMigratingBucketId();
    string& migrating_server_addr = pkt->GetMigratingServerAddr();
    bucket_server_map_t bucket_map = transform_table(server_map);
    log_message(kLogLevelInfo, "BucketTableResp, ns=%s, version=%u, migrate_bucket=%d, migrate_server_addr=%s\n",
                ns.c_str(), version, migrating_bucket_id, migrating_server_addr.c_str());
    
    g_bucket_table.Init(version, bucket_map, migrating_bucket_id, migrating_server_addr);
    
    g_is_freeze = false;
    
    if ((m_peer_ip == g_config.slave_cs_ip) && (m_peer_port == g_config.slave_cs_port)) {
        log_message(kLogLevelInfo, "close connection to slave CS after get bucket table\n");
        Close();
    }
}

void ConfigServerConn::_HandleDelNamespace(PktDelNamespace* pkt)
{
    string& ns = pkt->GetNamespace();
    log_message(kLogLevelInfo, "DelNamespace ns=%s\n", ns.c_str());
    
    if (ns == g_config.biz_namespace) {
        log_message(kLogLevelInfo, "kv_store_proxy stopping...\n\n");
        _exit(0); // fix some random crash with exit()
    }
}

void ConfigServerConn::_HandleFreezeProxy(PktFreezeProxy* pkt)
{
    string ns = pkt->GetNamespace();
    log_message(kLogLevelInfo, "FreezeProxy ns=%s\n", ns.c_str());
    
    g_is_freeze = true;
    
    FreezeTask* task = new FreezeTask;
    g_thread_pool.AddTask(task);
}
