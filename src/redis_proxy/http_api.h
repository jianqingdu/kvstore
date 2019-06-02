//
//  http_api.h
//
//  Created by ziteng on 19-5-28.
//

#ifndef __PROXY_HTTP_API_H__
#define __PROXY_HTTP_API_H__

#include "http_conn.h"
#include "http_parser_wrapper.h"

#define URL_STATS_INFO      "/redis_proxy/stats_info"
#define URL_RESET_STATS     "/redis_proxy/reset_stats"
#define URL_SET_CONFIG      "/redis_proxy/set_config"

void register_http_handler();

#endif
