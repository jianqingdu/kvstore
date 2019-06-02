/*
 *  pkt_base.cpp
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#include "pkt_base.h"
#include "pkt_definition.h"

PktBase::PktBase()
{
	m_incoming_buf = NULL;
	m_incoming_len = 0;

	m_pkt_header.version = PKT_VERSION;
	m_pkt_header.flag = 0;
	m_pkt_header.pkt_id = 0;
    m_pkt_header.seq_no = 0;
}

uchar_t* PktBase::GetBuffer()
{
	if (m_incoming_buf)
		return m_incoming_buf;
	else
		return m_buf.GetBuffer();
}

uint32_t PktBase::GetLength()
{
	if (m_incoming_buf)
		return m_incoming_len;
	else
		return m_buf.GetWriteOffset();
}

void PktBase::WriteHeader()
{
	uchar_t* buf = GetBuffer();

	ByteStream::WriteInt32(buf, GetLength());
	ByteStream::WriteUint16(buf + 4, m_pkt_header.version);
	ByteStream::WriteUint16(buf + 6, m_pkt_header.flag);
	ByteStream::WriteUint32(buf + 8, m_pkt_header.pkt_id);
    ByteStream::WriteUint32(buf + 12, m_pkt_header.seq_no);
}

void PktBase::SetVersion(uint16_t version)
{
	uchar_t* buf = GetBuffer();
	ByteStream::WriteUint16(buf + 4, version);
    m_pkt_header.version = version;
}

void PktBase::SetFlag(uint16_t flag)
{
	uchar_t* buf = GetBuffer();
	ByteStream::WriteUint16(buf + 6, flag);
    m_pkt_header.flag = flag;
}

void PktBase::SetSeqNo(uint32_t seq_no)
{
    uchar_t* buf = GetBuffer();
	ByteStream::WriteUint32(buf + 12, seq_no);
    m_pkt_header.seq_no = seq_no;
}

int PktBase::ReadPktHeader(uchar_t* buf, uint32_t len, pkt_header_t* header)
{
	int ret = -1;
	if (len >= PKT_HEADER_LEN && buf && header) {
		ByteStream is(buf, len);

		is >> header->length;
		is >> header->version;
		is >> header->flag;
		is >> header->pkt_id;
        is >> header->seq_no;

		ret = 0;
	}

	return ret;
}

PktBase* PktBase::ReadPacket(uchar_t *buf, uint32_t len)
{
	uint32_t pkt_len = 0;
	if (!IsPktAvailable(buf, len, pkt_len))
		return NULL;

	uint32_t pkt_id = ByteStream::ReadUint32(buf + 8);
	PktBase* pkt = NULL;

	switch (pkt_id) {
        case PKT_ID_HEARTBEAT:
            pkt = new PktHeartBeat(buf, pkt_len);
            break;
        case PKT_ID_NAMESPACE_LIST_REQ:
            pkt = new PktNamespaceListReq(buf, pkt_len);
            break;
        case PKT_ID_NAMESPACE_LIST_RESP:
            pkt = new PktNamespaceListResp(buf, pkt_len);
            break;
        case PKT_ID_BUCKET_TABLE_REQ:
            pkt = new PktBucketTableReq(buf, pkt_len);
            break;
        case PKT_ID_BUCKET_TABLE_RESP:
            pkt = new PktBucketTableResp(buf, pkt_len);
            break;
        case PKT_ID_START_MIGRATION:
            pkt = new PktStartMigration(buf, pkt_len);
            break;
        case PKT_ID_START_MIGRATION_ACK:
            pkt = new PktStartMigrationAck(buf, pkt_len);
            break;
        case PKT_ID_COMPLETE_MIGRATION:
            pkt = new PktCompleteMigration(buf, pkt_len);
            break;
        case PKT_ID_COMPLETE_MIGRATION_ACK:
            pkt = new PktCompleteMigrationAck(buf, pkt_len);
            break;
        case PKT_ID_DEL_NAMESPACE:
            pkt = new PktDelNamespace(buf, pkt_len);
            break;
        case PKT_ID_FREEZE_PROXY:
            pkt = new PktFreezeProxy(buf, pkt_len);
            break;
        case PKT_ID_STORAGE_SERVER_DOWN:
            pkt = new PktStorageServerDown(buf, pkt_len);
            break;
        case PKT_ID_STORAGE_SERVER_UP:
            pkt = new PktStorageServerUp(buf, pkt_len);
            break;
        default:
            throw PktException(ERROR_CODE_UNKNOWN_PKT_ID, "unknown pkt_id");
	}

	pkt->_SetIncomingLen(pkt_len);
	pkt->_SetIncomingBuf(buf);
	return pkt;
}

bool PktBase::IsPktAvailable(uchar_t* buf, uint32_t len, uint32_t& pkt_len)
{
    if (len < PKT_HEADER_LEN)
		return false;

	pkt_len = ByteStream::ReadUint32(buf);
    if (pkt_len < PKT_HEADER_LEN)
        throw PktException(ERROR_CODE_WRONG_PKT_LEN, "wrong pkt length");
    
    if (pkt_len > len)
		return false;

	return true;
}

PktBase* PktBase::DuplicatePacket(PktBase *pkt)
{
    PktBase* new_pkt = new PktBase();
    new_pkt->SetPktBuf(pkt->GetBuffer(), pkt->GetLength());
    return new_pkt;
}
