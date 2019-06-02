/*
 * http_conn.h
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#ifndef __BASE_HTTP_CONN_H__
#define __BASE_HTTP_CONN_H__

#include "util.h"
#include "base_conn.h"
#include "json/json.h"
#include "http_parser_wrapper.h"

#define HTTP_HEADER "HTTP/1.1 200 OK\r\n"\
    "Cache-Control:no-cache\r\n"\
    "Connection:close\r\n"\
    "Content-Length:%d\r\n"\
    "Content-Type:text/html;charset=utf-8\r\n\r\n%s"

#define OK_CODE		1001
#define OK_MSG		"OK"

#define MAX_BUF_SIZE 819200

class HttpConn : public BaseConn {
public:
	HttpConn();
	virtual ~HttpConn();

	virtual void OnRead(); // http protocol have different format, need to override this
};

void init_thread_http_conn(int io_thread_num);

void send_http_response(HttpConn* pHttpConn, Json::Value& root, bool compact = false);

typedef bool (*is_param_ok_t)(Json::Value& root);

void send_success_http_response(HttpConn* http_conn);
void send_failure_http_response(HttpConn* http_conn, uint32_t code, const char* msg);
bool parse_http_param(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper,
                      is_param_ok_t check_func, Json::Value& root);

#endif
