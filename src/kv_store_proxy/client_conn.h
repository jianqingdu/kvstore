/*
 *  client_conn.h
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#ifndef __PROXY_CLIENT_CONN_H__
#define __PROXY_CLIENT_CONN_H__

#include "base_conn.h"
#include "redis_parser.h"
#include "redis_request.h"

class ClientConn : public BaseConn {
public:
    ClientConn();
    virtual ~ClientConn();
    
    uint32_t IncrSeqNo() { return ++seq_no_; }
    
    virtual void OnConnect(BaseSocket* base_socket);
    virtual void OnRead();
    virtual void OnTimer(uint64_t curr_tick);
    
    void HandleResponse(uint32_t seq_no, const RedisReply& reply, char* buf, int len, const string& server_addr);
    void SendResponse(const string& resp);
private:
    void _HandlePipelineCommands(vector<vector<string>>& request_vec);
    void _HandleRedisCommand(vector<string>& cmd_vec, bool is_in_pipeline = false);
    
    void _HandleSingleKeyCommand(vector<string>& cmd_vec);
    void _HandleMultipleKeyCommand(vector<string>& cmd_vec);
    void _HandleMultipleKeyValueCommand(vector<string>& cmd_vec);
    void _HandleFlushDBCommand(vector<string>& cmd_vec);
    void _HandleRandomKeyCommand(vector<string>& cmd_vec);
    
    void _SendCompleteRequest();
    void _ProcessFreezeRequestList();
private:
    uint32_t                seq_no_;
    list<RedisRequest*>     request_list_;
    list<vector<vector<string>>>   freeze_request_list_;
};


#endif
