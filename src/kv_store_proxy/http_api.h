/*
 *  http_api.h
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#ifndef __PROXY_HTTP_API_H__
#define __PROXY_HTTP_API_H__

#include "http_conn.h"
#include "http_parser_wrapper.h"

#define URL_STATS_INFO      "/kvstore/proxy/stats_info"
#define URL_RESET_STATS     "/kvstore/proxy/reset_stats"
#define URL_SET_CONFIG      "/kvstore/proxy/set_config"
#define URL_BUCKET_TABLE    "/kvstore/proxy/bucket_table"
#define URL_ADD_PATTERN     "/kvstore/proxy/add_pattern"
#define URL_DEL_PATTERN     "/kvstore/proxy/del_pattern"

void register_http_handler();

#endif
