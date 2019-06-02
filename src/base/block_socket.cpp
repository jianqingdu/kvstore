/*
 * block_socket.cpp
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#include "block_socket.h"
#include "base_socket.h"
#include "pkt_base.h"
#include "simple_buffer.h"
#include <poll.h>

const int kMaxSocketBufSize = 128 * 1024;

static int wait_connect_complete(int sock_fd, uint32_t timeout)
{
    struct pollfd wfd[1];
    
    wfd[0].fd = sock_fd;
    wfd[0].events = POLLOUT;
    wfd[0].revents = 0;
    
    int cnt = poll(wfd, 1, timeout);
    if (cnt <= 0) {
        printf("poll failed or timeout: errno=%d, socket=%d\n", errno, sock_fd);
        return -1;
    }
    
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &len);
    if (error != 0) {
        printf("connecting socket error, socket=%d, error=%d\n", sock_fd, error);
        return -1;
    }
    
    return 0;
}

net_handle_t connect_with_timeout(const char* server_ip, uint16_t port, uint32_t timeout)
{
    printf("connect_with_timeout, server_addr=%s:%d, timeout=%d\n", server_ip, port, timeout);
    
	net_handle_t sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_fd == INVALID_SOCKET) {
		printf("socket failed, err_code=%d\n", errno);
		return NETLIB_INVALID_HANDLE;
	}
    
    BaseSocket::SetNoDelay(sock_fd);
    BaseSocket::SetNonblock(sock_fd, true);
    
	sockaddr_in serv_addr;
    BaseSocket::SetAddr(server_ip, port, &serv_addr);
    
    int ret = connect(sock_fd, (sockaddr*)&serv_addr, sizeof(serv_addr));
	if (ret == -1) {
        if ((errno != EINPROGRESS) || wait_connect_complete(sock_fd, timeout)) {
            printf("connect failed, err_code=%d\n", errno);
            close(sock_fd);
            return NETLIB_INVALID_HANDLE;
        }
	}
	
    BaseSocket::SetNonblock(sock_fd, false);
    
	return sock_fd;
}

int block_close(net_handle_t handle)
{
    return close(handle);
}

bool is_socket_closed(net_handle_t handle)
{
    if (handle < 0) {
        return true;
    }
    
    struct timeval tv = {0, 0};
    
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(handle, &fdset);
    
    int cnt = select(handle + 1, &fdset, NULL, NULL, &tv);
    if (cnt < 0) {
        return true;
    } else if (cnt > 0) {
        int avail = 0;
        if ( (ioctl(handle, FIONREAD, &avail) == -1) || (avail == 0)) {
            printf("peer close, socket closed\n");
            return true;
        }
    }
    
    return false;
}

int block_set_timeout(net_handle_t handle, uint32_t timeout)
{
    struct timeval tv = { timeout / 1000, ((int)timeout % 1000) * 1000};
    
    if (setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
        printf("setsockopt(SO_RCVTIMEO) failed: %d\n", errno);
        return 1;
    }
    
    if (setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1) {
        printf("setsockopt(SO_SNDTIMEO) failed: %d\n", errno);
        return 1;
    }
    
    return 0;
}

int block_send(net_handle_t handle, void* buf, int len)
{
    return (int)send(handle, buf, len, 0);
}

int block_recv(net_handle_t handle, void* buf, int len)
{
    return (int)recv(handle, buf, len, 0);
}

int block_send_all(net_handle_t handle, void* buf, int len)
{
    if (handle == NETLIB_INVALID_HANDLE) {
        return 0;
    }
    
    int offset = 0;
	int remain = len;
	while (remain > 0) {
		int send_size = remain;
		if (send_size > kMaxSocketBufSize) {
			send_size = kMaxSocketBufSize;
		}
        
		int ret = (int)send(handle, (char*)buf + offset, send_size, 0);
		if (ret < 0) {
            printf("send failed, errno=%d\n", errno);
			break;
		} else if (ret == 0) {
            printf("send len = 0\n");
            break;
        }
        
		offset += ret;
		remain -= ret;
	}
    
    return len - remain;
}

int block_recv_all(net_handle_t handle, void* buf, int len)
{
    if (handle == NETLIB_INVALID_HANDLE) {
        return 0;
    }
    
    int received_len = 0;
    while (received_len != len) {
        int ret = (int)recv(handle, (char*)buf + received_len, len - received_len, 0);
        if (ret < 0) {
            printf("recv failed, errno=%d\n", errno);
            return -1;
        } else if (ret == 0) {
            printf("peer close\n");
            return 0;
        } else {
            received_len += ret;
        }
    }
    
    return received_len;
}

PktBase* block_request(const char* server_ip, uint16_t port, uint32_t timeout, PktBase* req_pkt)
{
    net_handle_t conn_handle = connect_with_timeout(server_ip, port, timeout);
    if (conn_handle == NETLIB_INVALID_HANDLE) {
        printf("connect failed\n");
        return NULL;
    }
    
    block_set_timeout(conn_handle, timeout);
    
    if (block_send_all(conn_handle, req_pkt->GetBuffer(), req_pkt->GetLength()) != (int)req_pkt->GetLength()) {
        printf("send request failed\n");
        block_close(conn_handle);
        return NULL;
    }
    
    // receive header
    uchar_t header_buf[PKT_HEADER_LEN];
    int len = block_recv_all(conn_handle, header_buf, PKT_HEADER_LEN);
    if (len != PKT_HEADER_LEN) {
        printf("receive header failed\n");
        block_close(conn_handle);
        return NULL;
    }
    
    uint32_t pkt_len = (int)ByteStream::ReadUint32(header_buf);
    if (pkt_len < PKT_HEADER_LEN) {
        printf("packet header too small\n");
        block_close(conn_handle);
        return NULL;
    }
    
    // receive body
    SimpleBuffer simple_buf;
    simple_buf.Extend(pkt_len);
    simple_buf.Write(header_buf, PKT_HEADER_LEN);
    len = block_recv_all(conn_handle, simple_buf.GetWriteBuffer(), pkt_len - PKT_HEADER_LEN);
    block_close(conn_handle);
    if (len != (int)(pkt_len - PKT_HEADER_LEN)) {
        printf("read packet body failed\n");
        return NULL;
    }

    simple_buf.IncWriteOffset(len);
    
    PktBase* resp_pkt = NULL;
    try {
        resp_pkt = PktBase::ReadPacket(simple_buf.GetReadBuffer(), simple_buf.GetReadableLen());
    } catch (PktException& ex) {
        printf("ReadPacket failed: %s\n", ex.GetErrorMsg());
    }
    
    return resp_pkt;
}
