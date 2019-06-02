/*
 *  table_transform.h
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#ifndef __BASE_TABLE_TRANSFORM_H__
#define __BASE_TABLE_TRANSFORM_H__

// bucket table transform tool, used in these scenario:
// 1. map from bucket_id to server address  -- used in program
// 2. map from server address to bucket_id vector -- used for network transmision，to save bytes in network

#include "util.h"

typedef map<uint16_t, string> bucket_server_map_t;
typedef map<string, vector<uint16_t>> server_buckets_map_t;

bucket_server_map_t transform_table(server_buckets_map_t old_map);
server_buckets_map_t transform_table(bucket_server_map_t old_map);

void fetch_servers(const server_buckets_map_t& server_buckets_map, set<string>& servers);

void scale_up_table(const string& new_server_addr, uint16_t bucket_cnt,
                    const server_buckets_map_t& old_map, server_buckets_map_t& new_map);

void scale_down_table(const string del_server_addr, uint16_t bucket_cnt,
                      const server_buckets_map_t& old_map, server_buckets_map_t& new_map);

#endif
