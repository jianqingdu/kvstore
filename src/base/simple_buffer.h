/*
 * simple_buffer.h
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#ifndef __BASE_SIMPLE_BUFFER_H__
#define __BASE_SIMPLE_BUFFER_H__

#include "ostype.h"

class SimpleBuffer
{
public:
	SimpleBuffer();
	~SimpleBuffer();
	uchar_t* GetBuffer() { return m_buffer; }
    uchar_t* GetWriteBuffer() { return m_buffer + m_write_offset; }
    uchar_t* GetReadBuffer() { return m_buffer + m_read_offset; }
	uint32_t GetAllocSize() { return m_alloc_size; }
	uint32_t GetWriteOffset() { return m_write_offset; }
    uint32_t GetReadOffset() { return m_read_offset; }
    uint32_t GetWritableLen() { return m_alloc_size - m_write_offset; }
    uint32_t GetReadableLen() { return m_write_offset - m_read_offset; }
	void IncWriteOffset(uint32_t len) { m_write_offset += len; }
    
	void Extend(uint32_t len);
	uint32_t Write(void* buf, uint32_t len);
	uint32_t Read(void* buf, uint32_t len);
    void ResetOffset();
    void Clear();
private:
	uchar_t*	m_buffer;
	uint32_t	m_alloc_size;
	uint32_t	m_write_offset;
    uint32_t    m_read_offset;
};

#endif
