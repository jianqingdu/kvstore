/*
 * http_api.h
 *
 *  Created on: 2016-3-17
 *      Author: ziteng
 */

#ifndef __CS_HTTP_API_H__
#define __CS_HTTP_API_H__

#include "http_conn.h"
#include "http_parser_wrapper.h"

#define URL_INIT_TABLE          "/kvstore/cs/init_table"
#define URL_DEL_NAMEPACE        "/kvstore/cs/del_namespace"
#define URL_NAMESPACE_LIST      "/kvstore/cs/namespace_list"
#define URL_BUCKET_TABLE        "/kvstore/cs/bucket_table"
#define URL_PROXY_INFO          "/kvstore/cs/proxy_info"
#define URL_REPLACE_SERVER      "/kvstore/cs/replace_server"
#define URL_SCALE_CACHE_CLUSTER "/kvstore/cs/scale_cache_cluster"
#define URL_SET_CONFIG          "/kvstore/cs/set_config"
#define URL_GET_CONFIG          "/kvstore/cs/get_config"

void register_http_handler();

#endif /* __HTTP_API_H__ */
