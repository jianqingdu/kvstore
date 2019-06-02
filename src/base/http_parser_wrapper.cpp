/*
 * http_parser_wrapper.cpp
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#include "http_parser_wrapper.h"
#include "http_parser.h"

HttpParserWrapper::HttpParserWrapper()
{
	memset(&m_settings, 0, sizeof(m_settings));
	m_settings.on_url = OnUrl;
	m_settings.on_header_field = OnHeaderField;
	m_settings.on_header_value = OnHeaderValue;
	m_settings.on_headers_complete = OnHeadersComplete;
	m_settings.on_body = OnBody;
	m_settings.on_message_complete = OnMessageComplete;
}

void HttpParserWrapper::ParseHttpContent(const char* buf, uint32_t len)
{
    http_parser_init(&m_http_parser, HTTP_REQUEST);
    m_http_parser.data = this;
    
    m_read_all = false;
    
    m_total_length = 0;
    m_url.clear();
    m_body_content.clear();
    
    http_parser_execute(&m_http_parser, &m_settings, buf, len);
}

int HttpParserWrapper::OnUrl(http_parser* parser, const char *at, size_t length)
{
    HttpParserWrapper* parser_wrapper = (HttpParserWrapper*)parser->data;
    parser_wrapper->SetUrl(at, length);
    return 0;
}

int HttpParserWrapper::OnHeaderField(http_parser* parser, const char *at, size_t length)
{
	return 0;
}

int HttpParserWrapper::OnHeaderValue(http_parser* parser, const char *at, size_t length)
{
	return 0;
}

int HttpParserWrapper::OnHeadersComplete (http_parser* parser)
{
    HttpParserWrapper* parser_wrapper = (HttpParserWrapper*)parser->data;
    parser_wrapper->SetTotalLength(parser->nread + (uint32_t)parser->content_length);
    return 0;
}

int HttpParserWrapper::OnBody (http_parser* parser, const char *at, size_t length)
{
    HttpParserWrapper* parser_wrapper = (HttpParserWrapper*)parser->data;
    parser_wrapper->SetBodyContent(at, length);
    return 0;
}

int HttpParserWrapper::OnMessageComplete (http_parser* parser)
{
    HttpParserWrapper* parser_wrapper = (HttpParserWrapper*)parser->data;
    parser_wrapper->SetReadAll();
    return 0;
}
