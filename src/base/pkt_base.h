/*
 *  pkt_base.h
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#ifndef __BASE_PKT_BASE_H__
#define __BASE_PKT_BASE_H__

#include "util.h"
#include "simple_buffer.h"
#include "byte_stream.h"

#define PKT_VERSION     2
#define PKT_HEADER_LEN  16

#define SETUP_PKT_HEADER(PKT_ID) \
    m_pkt_header.pkt_id = PKT_ID; \
    ByteStream os(&m_buf, PKT_HEADER_LEN); \
    m_buf.Write(NULL, PKT_HEADER_LEN);

#define READ_PKT_HEADER \
    ReadPktHeader(buf, PKT_HEADER_LEN, &m_pkt_header); \
    ByteStream is(buf + PKT_HEADER_LEN, len - PKT_HEADER_LEN);

#define PARSE_PACKET_ASSERT \
    if (is.GetPos() != (len - PKT_HEADER_LEN)) { \
        throw PktException(ERROR_CODE_PARSE_FAILED, "parse pkt failed"); \
    }


enum {
    PKT_ID_HEARTBEAT                = 1,
    PKT_ID_NAMESPACE_LIST_REQ       = 2,
    PKT_ID_NAMESPACE_LIST_RESP      = 3,
    PKT_ID_BUCKET_TABLE_REQ         = 4,
    PKT_ID_BUCKET_TABLE_RESP        = 5,
    PKT_ID_START_MIGRATION          = 6,
    PKT_ID_START_MIGRATION_ACK      = 7,
    PKT_ID_COMPLETE_MIGRATION       = 8,
    PKT_ID_COMPLETE_MIGRATION_ACK   = 9,
    PKT_ID_DEL_NAMESPACE            = 10,
    PKT_ID_FREEZE_PROXY             = 11,
    PKT_ID_STORAGE_SERVER_DOWN      = 12,
    PKT_ID_STORAGE_SERVER_UP        = 13,
};

//////////////////////////////
typedef  struct {
    uint32_t    length;     // packet length including the header
    uint16_t    version;    // packet version
    uint16_t    flag;       // reserved flag, use for compression or encryption
    uint32_t    pkt_id;     // packet ID
    uint32_t    seq_no;     // package sequence
} pkt_header_t;

class PktBase {
public:
	PktBase();
	virtual ~PktBase() {}

	uchar_t* GetBuffer();
	uint32_t GetLength();

	uint16_t GetVersion() { return m_pkt_header.version; }
	uint16_t GetFlag() { return m_pkt_header.flag; }
	uint32_t GetPktId() { return m_pkt_header.pkt_id; }
    uint32_t GetSeqNo() { return m_pkt_header.seq_no; }

	void SetVersion(uint16_t version);
	void SetFlag(uint16_t flag);
    void SetSeqNo(uint32_t seq_no);
   
	void WriteHeader();

	static bool IsPktAvailable(uchar_t* buf, uint32_t len, uint32_t& pkt_len);
	static int ReadPktHeader(uchar_t* buf, uint32_t len, pkt_header_t* header);
	static PktBase* ReadPacket(uchar_t* buf, uint32_t len);
    
    void SetPktBuf(uchar_t* buf, uint32_t len) { m_buf.Write(buf, len); }
    static PktBase* DuplicatePacket(PktBase* pkt);
private:
	void _SetIncomingLen(uint32_t len) { m_incoming_len = len; }
	void _SetIncomingBuf(uchar_t* buf) { m_incoming_buf = buf; }

protected:
	SimpleBuffer	m_buf;
	uchar_t*		m_incoming_buf;
	uint32_t		m_incoming_len;
	pkt_header_t    m_pkt_header;
};


#endif /* __PKT_BASE_H__ */
