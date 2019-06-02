//
//  redis_parser.h
//  kv-store
//
//  Created by ziteng on 16-5-18.
//

#ifndef __PROXY_REDIS_PARSER_H__
#define __PROXY_REDIS_PARSER_H__

#include "util.h"

enum {
    REDIS_TYPE_STRING   = 1,
    REDIS_TYPE_ARRAY    = 2,
    REDIS_TYPE_INTEGER  = 3,
    REDIS_TYPE_NIL      = 4,
    REDIS_TYPE_STATUS   = 5,
    REDIS_TYPE_ERROR    = 6,
};

class RedisReply {
public:
    RedisReply(): type_(REDIS_TYPE_NIL), int_value_(0) {}
    RedisReply(int type, long value): type_(type), int_value_(value) {}
    RedisReply(int type, string& value): type_(type), int_value_(0), str_value_(value) {}
    RedisReply(int type): type_(type), int_value_(0) {}
    virtual ~RedisReply() {}
    
    void AddRedisReply(const RedisReply& element) { elements_.push_back(element); }
    
    int GetType() const { return type_; }
    long GetIntValue() const { return int_value_; }
    const string& GetStrValue() const { return str_value_; }
    const vector<RedisReply>& GetElements() const { return elements_; }
private:
    int         type_;
    long        int_value_; // The integer when type is INTEGER
    string      str_value_; // value for STATUS, ERROR, STRING
    vector<RedisReply> elements_;   // elements for ARRAY
};

int string2long(const char *s, size_t slen, long& value);

/*
 * 返回值:
 *  -1 -- 解析失败，redis格式出错
 *   0 -- 没有接收到完整的数据包
 *  >0 -- 解析成功，返回redis请求的长度
 */
int parse_redis_request(const char* redis_cmd, int redis_len, vector<string>& cmd_vec, string& err_msg);

/*
 * 返回值:
 *  -1 -- 解析失败，redis格式出错
 *   0 -- 没有接收到完整的数据包
 *  >0 -- 解析成功，返回redis请求的长度
 */
int parse_redis_response(const char* redis_resp, int redis_len, RedisReply& reply);

void build_request(const vector<string>& cmd_vec, string& request);

void build_response(const vector<string>& cmd_vec, string& response);

void parse_slave_addr(const string& replication_info, string& slave_addr);

#endif
