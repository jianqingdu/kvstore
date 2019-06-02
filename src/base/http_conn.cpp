/*
 * http_conn.cpp
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#include "http_conn.h"
#include "http_handler_map.h"
#include "io_thread_resource.h"
#include "simple_log.h"

static __thread char g_http_buf[MAX_BUF_SIZE];

static HttpHandlerMap* g_http_handler_map;
IoThreadResource<HttpParserWrapper> g_http_parser_wrappers;

void init_thread_http_conn(int io_thread_num)
{
    if (g_http_parser_wrappers.IsInited()) {
        return;
    }
    
	g_http_handler_map = HttpHandlerMap::getInstance();
    g_http_parser_wrappers.Init(io_thread_num);
}

void send_http_response(HttpConn* pHttpConn, Json::Value& root, bool compact)
{
    string str_resp;
    if (compact) {
        // use FastWriter to remove space in json format, reduce bytes on network
        Json::FastWriter writer;
        str_resp = writer.write(root);
    } else {
        str_resp = root.toStyledString();
    }
    
    snprintf(g_http_buf, MAX_BUF_SIZE, HTTP_HEADER, (int)str_resp.size(), str_resp.c_str());
    pHttpConn->Send(g_http_buf, (int)strlen(g_http_buf));
    pHttpConn->Close();
}

void send_success_http_response(HttpConn* http_conn)
{
    Json::Value root;
    root["code"] = OK_CODE;
    root["msg"] = OK_MSG;
    
    send_http_response(http_conn, root);
}

void send_failure_http_response(HttpConn* http_conn, uint32_t code, const char* msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    
    send_http_response(http_conn, root);
}

bool parse_http_param(HttpConn* http_conn, HttpParserWrapper* http_parser_wrapper,
                      is_param_ok_t check_func, Json::Value& root)
{
    const char* content = http_parser_wrapper->GetBodyContent();
    const char* url = http_parser_wrapper->GetUrl();
    log_message(kLogLevelInfo, "url=%s, content=%s, ip=%s\n", url, content, http_conn->GetPeerIP());
    
    Json::Reader reader;
    if (!reader.parse(content, root)) {
        log_message(kLogLevelError, "json format invalid\n");
        send_failure_http_response(http_conn, 1002, "invalid json format");
        return false;
    }
    
    if (check_func && !check_func(root)) {
        log_message(kLogLevelError, "invalid input format");
        send_failure_http_response(http_conn, 1003, "invalid input format");
        return false;
    }
    
    return true;
}

//////////////////////////
HttpConn::HttpConn()
{

}

HttpConn::~HttpConn()
{

}

void HttpConn::OnRead()
{
	_RecvData();

    char* in_buf = (char*)m_in_buf.GetBuffer();
    uint32_t buf_len = m_in_buf.GetWriteOffset();
    in_buf[buf_len] = '\0';
    
    HttpParserWrapper* parser_wrapper = g_http_parser_wrappers.GetIOResource(m_handle);
    parser_wrapper->ParseHttpContent(in_buf, buf_len);
    
    if (parser_wrapper->IsReadAll()) {
        string handler_url;
        char* url = parser_wrapper->GetUrl();
        
        char* url_end = strchr(url, '?');
        if (url_end) {
            handler_url.append(url, url_end - url);
        } else {
            handler_url = url;
        }
        
        http_handler_t handler = g_http_handler_map->GetHandler(handler_url);
        if (handler) {
            handler(this, parser_wrapper);
        } else {
            printf("no handler for: %s\n", url);
            string resp_str = "{\"code\": 404, \"msg\": \"no such method\"}\n";
            snprintf(g_http_buf, MAX_BUF_SIZE, HTTP_HEADER, (int)resp_str.size(), resp_str.c_str());
            Send(g_http_buf, (int)strlen(g_http_buf));
            Close();
        }
    }
}
