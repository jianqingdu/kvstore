/*
 * block_socket.h
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#ifndef __BASE_BLOCK_SOCKET_H__
#define __BASE_BLOCK_SOCKET_H__

#include "ostype.h"

class PktBase;

#ifdef __cplusplus
extern "C" {
#endif

// timeout in milliseconds
net_handle_t connect_with_timeout(const char* server_ip, uint16_t port, uint32_t timeout);
    
int block_close(net_handle_t handle);
    
bool is_socket_closed(net_handle_t handle);

int block_set_timeout(net_handle_t handle, uint32_t timeout);
    
int block_send(net_handle_t handle, void* buf, int len);
    
int block_recv(net_handle_t handle, void* buf, int len);
    
int block_send_all(net_handle_t handle, void* buf, int len);
    
int block_recv_all(net_handle_t handle, void* buf, int len);
 
// used for block request/response in migration tool
PktBase* block_request(const char* server_ip, uint16_t port, uint32_t timeout, PktBase* req_pkt);
    
#ifdef __cplusplus
}
#endif

#endif
