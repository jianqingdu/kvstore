/*
 *  common.h
 *
 *  Created on: 2016-7-20
 *      Author: ziteng
 */

#ifndef __CS_COMMON_H__
#define __CS_COMMON_H__

#include "base_conn.h"
#include "pkt_definition.h"
#include "json/json.h"

void handle_bucket_table_req(BaseConn* conn, PktBucketTableReq* pkt, uint32_t expect_from_type);

void jsonize_bucket_table(Json::Value& root, uint32_t version, const map<uint16_t, string>& bucket_map,
                          uint16_t migrating_bucket_id, const string& migrating_server_addr);

#endif 
