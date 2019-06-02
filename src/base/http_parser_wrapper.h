/*
 * http_parser_wrapper.h
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#ifndef __BASE_HTTP_PARSER_WRAPPER_H__
#define __BASE_HTTP_PARSER_WRAPPER_H__

#include "util.h"
#include "http_parser.h"

class HttpParserWrapper {
public:
    HttpParserWrapper();
    virtual ~HttpParserWrapper() {}
    
    void ParseHttpContent(const char* buf, uint32_t len);
    
    bool IsReadAll() { return m_read_all; }

    uint32_t GetTotalLength() { return m_total_length; }
    char* GetUrl() { return (char*)m_url.c_str(); }
    char* GetBodyContent() { return (char*)m_body_content.c_str(); }
    
    void SetUrl(const char* url, size_t length) { m_url.append(url, length); }
    void SetBodyContent(const char* content, size_t length) { m_body_content.append(content, length); }
    void SetTotalLength(uint32_t total_len) { m_total_length = total_len; }
    void SetReadAll() { m_read_all = true; }
   
    static int OnUrl(http_parser* parser, const char *at, size_t length);
    static int OnHeaderField(http_parser* parser, const char *at, size_t length);
    static int OnHeaderValue(http_parser* parser, const char *at, size_t length);
    static int OnHeadersComplete (http_parser* parser);
    static int OnBody (http_parser* parser, const char *at, size_t length);
    static int OnMessageComplete (http_parser* parser);

private:
    http_parser             m_http_parser;
    http_parser_settings    m_settings;
    
    bool                m_read_all;
    uint32_t            m_total_length;
    std::string         m_url;
    std::string         m_body_content;
};

#endif
