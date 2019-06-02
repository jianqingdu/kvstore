/*
 * proxy_conn.cpp
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#include "proxy_conn.h"
#include "namespace_manager.h"
#include "simple_log.h"
#include "common.h"

void ProxyConn::Close()
{
    if ((m_handle != NETLIB_INVALID_HANDLE) && !namespace_.empty()) {
        log_message(kLogLevelInfo, "Proxy close %s:%d, handle=%d\n", m_peer_ip.c_str(), m_peer_port, m_handle);
        g_namespace_manager.DelProxy(namespace_, m_handle);
    }
    
    BaseConn::Close();
}

void ProxyConn::OnTimer(uint64_t curr_tick)
{
    if (curr_tick > m_last_send_tick + m_heartbeat_interval) {
        PktHeartBeat pkt;
        SendPkt(&pkt);
    }
    
    if (curr_tick > m_last_recv_tick + m_conn_timeout) {
        log_message(kLogLevelWarning, "Proxy timeout %s:%d\n", m_peer_ip.c_str(), m_peer_port);
        Close();
        return;
    }
}

void ProxyConn::HandlePkt(PktBase* pkt)
{
    switch (pkt->GetPktId()) {
        case PKT_ID_HEARTBEAT:
            break;
        case PKT_ID_BUCKET_TABLE_REQ:
            _HandleBucketTableReq((PktBucketTableReq *)pkt);
            break;
        case PKT_ID_FREEZE_PROXY:
            _HandleFreezeProxy((PktFreezeProxy *)pkt);
            break;
        case PKT_ID_STORAGE_SERVER_DOWN:
            _HandleStorageServerDown((PktStorageServerDown *)pkt);
            break;
        case PKT_ID_STORAGE_SERVER_UP:
            _HandleStorageServerUp((PktStorageServerUp *)pkt);
            break;
        default:
            log_message(kLogLevelError, "unknown pkt_id=%d for ProxyConn\n", pkt->GetPktId());
            break;
    }
}

void ProxyConn::_HandleBucketTableReq(PktBucketTableReq* pkt)
{
    namespace_ = pkt->GetNamespace();
    proxy_addr_ = pkt->GetInfo();
    handle_bucket_table_req(this, pkt, TABLE_REQ_FROM_PROXY);
}

void ProxyConn::_HandleFreezeProxy(PktFreezeProxy* pkt)
{
    string& ns = pkt->GetNamespace();
    log_message(kLogLevelInfo, "FreezeProxyAck, ns=%s, proxy_addr=%s\n", ns.c_str(), proxy_addr_.c_str());
    
    g_namespace_manager.HandleFreezeProxyAck(ns, m_handle);
}

void ProxyConn::_HandleStorageServerDown(PktStorageServerDown *pkt)
{
    string& ns = pkt->GetNamespace();
    string& addr = pkt->GetServerAddr();
    string& slave_addr = pkt->GetSlaveAddr();
    log_message(kLogLevelWarning, "StorageServerDown, ns=%s, storage_addr=%s, slave_addr=%s, proxy_addr=%s\n",
                ns.c_str(), addr.c_str(), slave_addr.c_str(), proxy_addr_.c_str());
    
    g_namespace_manager.ReportServerDown(ns, addr, slave_addr, m_handle);
}

void ProxyConn::_HandleStorageServerUp(PktStorageServerUp* pkt)
{
    string& ns = pkt->GetNamespace();
    string& addr = pkt->GetServerAddr();
    log_message(kLogLevelWarning, "StorageServerUp, ns=%s, storage_addr=%s, proxy_addr=%s\n",
                ns.c_str(), addr.c_str(), proxy_addr_.c_str());
    
    g_namespace_manager.ReportServerUp(ns, addr, m_handle);
}
