/*
 * http_handler_map.cpp
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#include "http_handler_map.h"

HttpHandlerMap* HttpHandlerMap::s_handler_instance = NULL;


HttpHandlerMap::HttpHandlerMap()
{

}

HttpHandlerMap::~HttpHandlerMap()
{

}

HttpHandlerMap* HttpHandlerMap::getInstance()
{
	if (!s_handler_instance) {
		s_handler_instance = new HttpHandlerMap();
	}

	return s_handler_instance;
}

void HttpHandlerMap::AddHandler(const string& url, http_handler_t handler)
{
    m_handler_map.insert(make_pair(url, handler));
}

http_handler_t HttpHandlerMap::GetHandler(const string& url)
{
	HttpHandlerMap_t::iterator it = m_handler_map.find(url);
	if (it != m_handler_map.end()) {
		return it->second;
	} else {
		return NULL;
	}
}

