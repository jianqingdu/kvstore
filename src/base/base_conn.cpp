/*
 * base_conn.cpp
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#include "base_conn.h"
#include "event_loop.h"
#include "io_thread_resource.h"
#include "pkt_definition.h"
#include "simple_log.h"

typedef list<pair<net_handle_t, PktBase*>> PendingPktList;
typedef list<net_handle_t>  PendingCloseList;
typedef unordered_map<net_handle_t, BaseConn*> ConnMap_t;

struct PendingEventMgr {
    ConnMap_t           conn_map;
    mutex               mtx;
    PendingPktList      pkt_list;
    PendingCloseList    close_list;
};

IoThreadResource<PendingEventMgr> g_pending_event_mgr;

void loop_callback(void* callback_data, uint8_t msg, uint32_t handle, void* pParam)
{
    PendingEventMgr* event_mgr = (PendingEventMgr*)callback_data;
    if (!event_mgr)
        return;
    
    PendingPktList tmp_pkt_list;
    PendingCloseList tmp_close_list;
    {
        lock_guard<mutex> mg(event_mgr->mtx);
        tmp_pkt_list.swap(event_mgr->pkt_list);
        tmp_close_list.swap(event_mgr->close_list);
    }
    
    for (auto it = tmp_pkt_list.begin(); it != tmp_pkt_list.end(); ++it) {
        net_handle_t handle = it->first;
        PktBase* pkt = it->second;
        ConnMap_t::iterator it_conn = event_mgr->conn_map.find(handle);
        if (it_conn != event_mgr->conn_map.end()) {
            BaseConn* conn = it_conn->second;
            if (conn->IsOpen()) {
                conn->SendPkt(pkt);
                delete pkt;
            } else {
                conn->AddToWaitPktList(pkt);
            }
        } else {
            delete pkt;
        }
    }
    
    for (auto it = tmp_close_list.begin(); it != tmp_close_list.end(); ++it) {
        net_handle_t handle = *it;
        ConnMap_t::iterator it_conn = event_mgr->conn_map.find(handle);
        if (it_conn != event_mgr->conn_map.end()) {
            it_conn->second->Close();
        }
    }
}

void conn_callback(void* callback_data, uint8_t msg, uint32_t handle, void* pParam)
{
    //printf("conn_callback, msg=%d, handle=%d\n", msg, handle);
    
    BaseConn* pConn = (BaseConn*)callback_data;
	if (!pConn)
		return;

    pConn->AddRef();
	switch (msg) {
        case NETLIB_MSG_CONFIRM:
            pConn->OnConfirm();
            break;
        case NETLIB_MSG_READ:
            pConn->OnRead();
            break;
        case NETLIB_MSG_WRITE:
            pConn->OnWrite();
            break;
        case NETLIB_MSG_CLOSE:
            pConn->OnClose();
            break;
        case NETLIB_MSG_TIMER:
            {
                uint64_t curr_tick = *(uint64_t*)pParam;
                pConn->OnTimer(curr_tick);
                break;
            }
        default:
            printf("!!!conn_callback error msg: %d\n", msg);
            break;
	}
    pConn->ReleaseRef();
}

void init_thread_base_conn(int io_thread_num)
{
    if (g_pending_event_mgr.IsInited())
        return;
    
    g_pending_event_mgr.Init(io_thread_num);
    
    if (io_thread_num > 0) {
        for (int i = 0; i < io_thread_num; ++i) {
            EventLoop* el = get_io_event_loop(i);
            el->AddLoop(loop_callback, g_pending_event_mgr.GetIOResource(i));
        }
    } else {
        get_main_event_loop()->AddLoop(loop_callback, g_pending_event_mgr.GetMainResource());
    }
}

BaseConn* get_base_conn(net_handle_t handle)
{
    if (handle == NETLIB_INVALID_HANDLE) {
        return NULL;
    }
    
    PendingEventMgr* event_mgr = g_pending_event_mgr.GetIOResource(handle);
    auto it = event_mgr->conn_map.find(handle);
    if (it != event_mgr->conn_map.end()) {
        return it->second;
    } else {
        return NULL;
    }
}

//////////////////////////
BaseConn::BaseConn()
{
    m_thread_index = -1;
    m_base_socket = NULL;
    m_open = false;
	m_busy = false;
	m_handle = NETLIB_INVALID_HANDLE;
    m_heartbeat_interval = kHeartBeartInterval;
    m_conn_timeout = kConnTimeout;
    
	m_last_send_tick = m_last_recv_tick = get_tick_count();
}

BaseConn::~BaseConn()
{
    if (!m_wait_pkt_list.empty()) {
        for (auto it = m_wait_pkt_list.begin(); it != m_wait_pkt_list.end(); ++it) {
            PktBase* pkt = *it;
            delete pkt;
        }

        m_wait_pkt_list.clear();
    }
}

net_handle_t BaseConn::Connect(const string& server_ip, uint16_t server_port, int thread_index)
{
    m_peer_ip = server_ip;
    m_peer_port = server_port;
    
    EventLoop* event_loop = NULL;
    if (thread_index != -1) {
        event_loop = get_io_event_loop(thread_index);
    }
    
    m_base_socket = new BaseSocket();
    assert(m_base_socket);
    m_handle = m_base_socket->Connect(server_ip.c_str(), server_port, conn_callback, this, event_loop);
    if (thread_index != -1) {
        m_thread_index = thread_index;
    } else {
        m_thread_index = m_handle;
    }
    
    if (m_handle == NETLIB_INVALID_HANDLE) {
        delete m_base_socket;
        delete this;
    } else {
        if (g_pending_event_mgr.IsInited()) {
            PendingEventMgr* event_mgr = g_pending_event_mgr.GetIOResource(m_thread_index);
            event_mgr->conn_map.insert(make_pair(m_handle, this));
        }
    }
    
    return m_handle;
}

void BaseConn::Close()
{
    if (m_handle == NETLIB_INVALID_HANDLE) {
        return;
    }
    
    if (g_pending_event_mgr.IsInited()) {
        PendingEventMgr* event_mgr = g_pending_event_mgr.GetIOResource(m_thread_index);
        event_mgr->conn_map.erase(m_handle);
    }
    
    printf("Client Close: handle=%d\n", m_handle);
    m_base_socket->Close();
    m_open = false;
    m_busy = false;
    m_in_buf.Clear();
    m_out_buf.Clear();
    m_handle = NETLIB_INVALID_HANDLE;
    ReleaseRef();
}

int BaseConn::SendPkt(PktBase* pkt)
{
    return Send(pkt->GetBuffer(), pkt->GetLength());
}

int BaseConn::Send(void* data, int len)
{
	m_last_send_tick = get_tick_count();

    if (!m_open) {
        printf("connection do not open yet\n");
        return 0;
    }
    
	if (m_busy) {
		m_out_buf.Write(data, len);
		return len;
	}

	int offset = 0;
	int remain = len;
	while (remain > 0) {
		int send_size = remain;
		if (send_size > kMaxSendSize) {
			send_size = kMaxSendSize;
		}

		int ret = m_base_socket->Send((char*)data + offset , send_size);
		if (ret <= 0) {
			ret = 0;
			break;
		}

		offset += ret;
		remain -= ret;
	}

	if (remain > 0) {
		m_out_buf.Write((char*)data + offset, remain);
		m_busy = true;
		printf("send busy, remain=%d\n", m_out_buf.GetReadableLen());
	}

	return len;
}

void BaseConn::OnConnect(BaseSocket *base_socket)
{
    m_open = true;
    m_base_socket = base_socket;
    m_handle = base_socket->GetHandle();
    m_thread_index = m_handle;
    
    if (g_pending_event_mgr.IsInited()) {
        PendingEventMgr* event_mgr = g_pending_event_mgr.GetIOResource(m_thread_index);
        event_mgr->conn_map.insert(make_pair(m_handle, this));
    }
    
    base_socket->SetCallback(conn_callback);
    base_socket->SetCallbackData((void*)this);
    m_peer_ip = base_socket->GetRemoteIP();
    m_peer_port = base_socket->GetRemotePort();
}

void BaseConn::OnConfirm()
{
    m_open = true;
    m_busy = false;
    
    if (!m_wait_pkt_list.empty()) {
        for (auto it = m_wait_pkt_list.begin(); it != m_wait_pkt_list.end(); ++it) {
            PktBase* pkt = *it;
            SendPkt(pkt);
            delete pkt;
        }
        
        m_wait_pkt_list.clear();
    }
}

void BaseConn::OnRead()
{
	_RecvData();

	_ParsePkt();
}

void BaseConn::OnWrite()
{
	if (!m_busy)
		return;

	while (m_out_buf.GetReadableLen() > 0) {
		int send_size = m_out_buf.GetReadableLen();
		if (send_size > kMaxSendSize) {
			send_size = kMaxSendSize;
		}

		int ret = m_base_socket->Send(m_out_buf.GetReadBuffer(), send_size);
		if (ret <= 0) {
			ret = 0;
			break;
		}

		m_out_buf.Read(NULL, ret);
	}

    m_out_buf.ResetOffset();
    
	if (m_out_buf.GetReadableLen() == 0) {
		m_busy = false;
	}

	printf("onWrite, remain=%d\n", m_out_buf.GetReadableLen());
}

void BaseConn::OnClose()
{
    Close();
}

void BaseConn::OnTimer(uint64_t curr_tick)
{
    //printf("OnTimer, curr_tick=%llu\n",curr_tick);
    if (curr_tick > m_last_recv_tick + m_conn_timeout) {
        printf("connection timeout, handle=%d\n", m_handle);
        Close();
        return;
    }
}

void BaseConn::_RecvData()
{
    for (;;) {
		uint32_t free_buf_len = m_in_buf.GetWritableLen();
		if (free_buf_len < (kReadBufSize + 1))  // reserve 1 byte for text protocol to add '\0'
			m_in_buf.Extend(kReadBufSize + 1);
        
		int ret = m_base_socket->Recv(m_in_buf.GetWriteBuffer(), kReadBufSize);
		if (ret <= 0)
			break;
        
		m_in_buf.IncWriteOffset(ret);
		m_last_recv_tick = get_tick_count();
	}
}

void BaseConn::_ParsePkt()
{
    try {
		PktBase* pPkt = NULL;
		while ( (pPkt = PktBase::ReadPacket(m_in_buf.GetReadBuffer(), m_in_buf.GetReadableLen())) ) {
			uint32_t pkt_len = pPkt->GetLength();

			HandlePkt(pPkt);
            
			m_in_buf.Read(NULL, pkt_len);
			delete pPkt;
            
            if (m_handle == NETLIB_INVALID_HANDLE) {
                // for the case that the packet handler close the connection
                break;
            }
		}
        
        m_in_buf.ResetOffset();
	} catch (PktException& ex) {
        if (ex.GetErrorCode() == ERROR_CODE_WRONG_PKT_LEN) {
            printf("!!!exception, err_msg=%s\n", ex.GetErrorMsg());
        } else {
            // get the error pkt_id
            pkt_header_t header;
            PktBase::ReadPktHeader(m_in_buf.GetReadBuffer(), PKT_HEADER_LEN, &header);
            printf("!!!exception: pkt_len=%d, pkt_id=%d, err_msg=%s\n",
                   header.length, header.pkt_id, ex.GetErrorMsg());
        }
        
        Close();
	}
}

// static methods
// if Connect has specified thread_index，then can't use the following two static method
int BaseConn::SendPkt(net_handle_t handle, PktBase* pkt)
{
    if (handle == NETLIB_INVALID_HANDLE) {
        return 0;
    }
    
    EventLoop* el = get_io_event_loop(handle);
    PendingEventMgr* event_mgr = g_pending_event_mgr.GetIOResource(handle);
    
    if (el->IsInLoopThread()) {
        ConnMap_t::iterator it_conn = event_mgr->conn_map.find(handle);
        if (it_conn != event_mgr->conn_map.end()) {
            BaseConn* conn = it_conn->second;
            if (conn->IsOpen()) {
                conn->SendPkt(pkt);
                delete pkt;
            } else {
                conn->AddToWaitPktList(pkt);
            }
        } else {
            delete pkt;
        }
    } else {
        event_mgr->mtx.lock();
        event_mgr->pkt_list.push_back(make_pair(handle, pkt));
        event_mgr->mtx.unlock();
        
        el->Wakeup();
    }
    
    return 0;
}

int BaseConn::CloseHandle(net_handle_t handle)
{
    if (handle == NETLIB_INVALID_HANDLE) {
        return 0;
    }
    
    EventLoop* el = get_io_event_loop(handle);
    PendingEventMgr* event_mgr = g_pending_event_mgr.GetIOResource(handle);
    
    if (el->IsInLoopThread()) {
        ConnMap_t::iterator it_conn = event_mgr->conn_map.find(handle);
        if (it_conn != event_mgr->conn_map.end()) {
            it_conn->second->Close();
        }
    } else {
        event_mgr->mtx.lock();
        event_mgr->close_list.push_back(handle);
        event_mgr->mtx.unlock();
        
        el->Wakeup();
    }
    
    return 0;
}

