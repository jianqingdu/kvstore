/*
 * migration_conn.cpp
 *
 *  Created on: 2016-5-17
 *      Author: ziteng
 */

#include "migration_conn.h"
#include "simple_log.h"
#include "namespace_manager.h"
#include "config_parser.h"
#include "common.h"

void MigrationConn::OnTimer(uint64_t curr_tick)
{
    if (curr_tick > m_last_recv_tick + m_conn_timeout) {
        log_message(kLogLevelWarning, "migration conn timeout %s:%d\n", m_peer_ip.c_str(), m_peer_port);
        
        Close();
    }
}

void MigrationConn::HandlePkt(PktBase* pkt)
{
    switch (pkt->GetPktId()) {
        case PKT_ID_HEARTBEAT:
            break;
        case PKT_ID_BUCKET_TABLE_REQ:
            _HandleBucketTableReq((PktBucketTableReq* )pkt);
            break;
        case PKT_ID_START_MIGRATION:
            _HandleStartMigration((PktStartMigration *)pkt);
            break;
        case PKT_ID_COMPLETE_MIGRATION:
            _HandleCompleteMigration((PktCompleteMigration *)pkt);
            break;
        default:
            log_message(kLogLevelError, "unknown pkt_id=%u from migration_tool\n", pkt->GetPktId());
            break;
    }
}

void MigrationConn::_HandleBucketTableReq(PktBucketTableReq* pkt)
{
    handle_bucket_table_req(this, pkt, TABLE_REQ_FROM_MIGRATION_TOOL);
}

void MigrationConn::_HandleStartMigration(PktStartMigration* pkt)
{
    string& ns = pkt->GetNamespace();
    uint16_t bucket_id = pkt->GetBucketId();
    string& new_server_addr = pkt->GetNewServerAddr();
    uint8_t scale_up = pkt->GetScaleUp();
    log_message(kLogLevelInfo, "StartMigration, ns=%s, bucket_id=%d, new_server_addr=%s, scale_up=%d\n",
                ns.c_str(), bucket_id, new_server_addr.c_str(), scale_up);
    
    if (!g_config.is_master) {
        PktStartMigrationAck ack_pkt(ns, bucket_id, MIGRATION_RESULT_NOT_MASETER);
        SendPkt(&ack_pkt);
        return;
    }
    
    uint32_t result = g_namespace_manager.StartMigration(ns, bucket_id, new_server_addr, scale_up, this);
    if (result != MIGRATION_RESULT_SUCCESS) {
        PktStartMigrationAck ack_pkt(ns, bucket_id, result);
        SendPkt(&ack_pkt);
    }
}

void MigrationConn::_HandleCompleteMigration(PktCompleteMigration* pkt)
{
    string& ns = pkt->GetNamespace();
    uint16_t bucket_id = pkt->GetBucketId();
    string& new_server_addr = pkt->GetNewServerAddr();
    uint8_t scale_up = pkt->GetScaleUp();
    log_message(kLogLevelInfo, "CompleteMigration, ns=%s, bucket_id=%d, new_server_addr=%s, scale_up=%d\n",
                ns.c_str(), bucket_id, new_server_addr.c_str(), scale_up);
    
    if (!g_config.is_master) {
        PktCompleteMigrationAck ack_pkt(ns, bucket_id, MIGRATION_RESULT_NOT_MASETER);
        SendPkt(&ack_pkt);
        return;
    }
    
    uint32_t result = g_namespace_manager.CompleteMigration(ns, bucket_id, new_server_addr, scale_up, this);
    if (result != MIGRATION_RESULT_SUCCESS) {
        PktCompleteMigrationAck ack_pkt(ns, bucket_id, result);
        SendPkt(&ack_pkt);
    }
}
