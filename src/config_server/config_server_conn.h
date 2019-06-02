/*
 * config_server_conn.h
 *
 *  Created on: 2016-3-17
 *      Author: ziteng
 */

#ifndef __CS_CONFIG_SERVER_CONN_H__
#define __CS_CONFIG_SERVER_CONN_H__

#include "base_conn.h"
#include "pkt_definition.h"

class ConfigServerConn : public BaseConn
{
public:
    ConfigServerConn() {}
    virtual ~ConfigServerConn() {}
    
    virtual void Close();
    
    virtual void OnConnect(BaseSocket* base_socket);
    virtual void OnConfirm();
    virtual void OnTimer(uint64_t curr_tick);
    virtual void HandlePkt(PktBase* pkt);
private:
    void _HandleNamespaceListReq(PktNamespaceListReq* pkt);
    void _HandleNamespaceListResp(PktNamespaceListResp* pkt);
    void _HandleBucketTableReq(PktBucketTableReq* pkt);
    void _HandleBucketTableResp(PktBucketTableResp* pkt);
    void _HandleDelNamespace(PktDelNamespace* pkt);
};

void init_connection_to_master();

// caller must delete the pkt self
void broadcast_pkt_to_slaves(PktBase* pkt);

#endif
