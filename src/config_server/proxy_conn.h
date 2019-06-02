/*
 * proxy_conn.h
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#ifndef __CS_PROXY_CONN_H__
#define __CS_PROXY_CONN_H__

#include "base_conn.h"
#include "pkt_definition.h"

class ProxyConn : public BaseConn {
public:
    ProxyConn() {}
    virtual ~ProxyConn() {}
    
    virtual void Close();
    
    virtual void OnTimer(uint64_t curr_tick);
    virtual void HandlePkt(PktBase* pkt);
private:
    void _HandleBucketTableReq(PktBucketTableReq* pkt);
    void _HandleFreezeProxy(PktFreezeProxy* pkt);
    void _HandleStorageServerDown(PktStorageServerDown* pkt);
    void _HandleStorageServerUp(PktStorageServerUp* pkt);
private:
    string  namespace_;
    string  proxy_addr_;
};

#endif
