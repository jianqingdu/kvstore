/*
 * http_handler_map.h
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#ifndef __BASE_HTTP_HANDLER_MAP_H__
#define __BASE_HTTP_HANDLER_MAP_H__

#include "util.h"
#include "http_conn.h"
#include "http_parser_wrapper.h"

typedef void (*http_handler_t)(HttpConn* pHttpConn, HttpParserWrapper* pHttpParser);
typedef map<string, http_handler_t> HttpHandlerMap_t;

class HttpHandlerMap {
public:
	virtual ~HttpHandlerMap();

	static HttpHandlerMap* getInstance();

	void AddHandler(const string& url, http_handler_t handler);
	http_handler_t GetHandler(const string& url);
  
private:
	HttpHandlerMap();

private:
	static HttpHandlerMap*  s_handler_instance;
	HttpHandlerMap_t        m_handler_map;
};

#endif
