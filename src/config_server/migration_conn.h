/*
 * migration_conn.h
 *
 *  Created on: 2016-5-17
 *      Author: ziteng
 */

#ifndef __CS_MIGRATION_CONN_H__
#define __CS_MIGRATION_CONN_H__

#include "base_conn.h"
#include "pkt_definition.h"

const int kMigrationConnTimeout = 8000;  // 8 seconds

// migration conneciton is request/response type short-live connection like HTTP
class MigrationConn : public BaseConn
{
public:
    MigrationConn() {  m_conn_timeout = kMigrationConnTimeout; }
    virtual ~MigrationConn() {}
    
    virtual void OnTimer(uint64_t curr_tick);
    virtual void HandlePkt(PktBase* pkt);
private:
    void _HandleBucketTableReq(PktBucketTableReq* pkt);
    void _HandleStartMigration(PktStartMigration* pkt);
    void _HandleCompleteMigration(PktCompleteMigration* pkt);
};

#endif
