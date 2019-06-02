/*
 *  redis_conn.h
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#ifndef __3RD_PARTY_REDIS_CONN_H__
#define __3RD_PARTY_REDIS_CONN_H__

#include "util.h"
#include "hiredis.h"

class RedisConn
{
public:
    RedisConn(const string& ip, int port);
    RedisConn();
    virtual ~RedisConn();
    
    void SetAddr(const string& ip, int port) { ip_ = ip; port_ = port; }
    void SetPassword(const string& password) { password_ = password; }
    int Init();
    
    // Caution: do not freeReplyObject(), reply will be freed by the RedisConn object
    redisReply* DoRawCmd(const string& cmd);
    redisReply* DoCmd(const string& cmd);
    
    void PipelineRawCmd(const string& cmd);
    void PipelineCmd(const string& cmd);
    redisReply* GetReply();
private:
    redisContext*   context_;
    redisReply*     reply_; //reply_对象会在下次请求时销毁，或者在析构函数销毁，这样请求者就不用自己去销毁了
    string          ip_;
    int             port_;
    string          password_;
};

#endif
