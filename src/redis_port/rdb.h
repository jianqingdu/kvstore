//
//  rdb.h
//  kv-store
//
//  Created by ziteng on 16-7-1.
//

#ifndef __REDIS_PORT_RDB_H__
#define __REDIS_PORT_RDB_H__

#include "util.h"
#include "redis_conn.h"

/* The current RDB version. When the format changes in a way that is no longer
 * backward compatible this number gets incremented. */
#define REDIS_RDB_VERSION 7

/* Defines related to the dump file format. To store 32 bits lengths for short
 * keys requires a lot of space, so we check the most significant 2 bits of
 * the first byte to interpreter the length:
 *
 * 00|000000 => if the two MSB are 00 the len is the 6 bits of this byte
 * 01|000000 00000000 =>  01, the len is 14 byes, 6 bits + 8 bits of next byte
 * 10|000000 [32 bit integer] => if it's 01, a full 32 bit len will follow
 * 11|000000 this means: specially encoded object will follow. The six bits
 *           number specify the kind of object that follows.
 *           See the REDIS_RDB_ENC_* defines.
 *
 * Lengths up to 63 are stored using a single byte, most DB keys, and may
 * values, will fit inside. */
#define REDIS_RDB_6BITLEN 0
#define REDIS_RDB_14BITLEN 1
#define REDIS_RDB_32BITLEN 2
#define REDIS_RDB_ENCVAL 3
#define REDIS_RDB_LENERR ((uint32_t)-1)

/* When a length of a string object stored on disk has the first two bits
 * set, the remaining two bits specify a special encoding for the object
 * accordingly to the following defines: */
#define REDIS_RDB_ENC_INT8 0        /* 8 bit signed integer */
#define REDIS_RDB_ENC_INT16 1       /* 16 bit signed integer */
#define REDIS_RDB_ENC_INT32 2       /* 32 bit signed integer */
#define REDIS_RDB_ENC_LZF 3         /* string compressed with FASTLZ */

/* Dup object types to RDB object types. Only reason is readability (are we
 * dealing with RDB types or with in-memory object types?). */
#define REDIS_RDB_TYPE_STRING 0
#define REDIS_RDB_TYPE_LIST   1
#define REDIS_RDB_TYPE_SET    2
#define REDIS_RDB_TYPE_ZSET   3
#define REDIS_RDB_TYPE_HASH   4

/* Object types for encoded objects. */
#define REDIS_RDB_TYPE_HASH_ZIPMAP    9
#define REDIS_RDB_TYPE_LIST_ZIPLIST  10
#define REDIS_RDB_TYPE_SET_INTSET    11
#define REDIS_RDB_TYPE_ZSET_ZIPLIST  12
#define REDIS_RDB_TYPE_HASH_ZIPLIST  13
#define REDIS_RDB_TYPE_LIST_QUICKLIST 14

/* Special RDB opcodes (saved/loaded with rdbSaveType/rdbLoadType). */
#define REDIS_RDB_OPCODE_AUX            250
#define REDIS_RDB_OPCODE_RESIZEDB       251
#define REDIS_RDB_OPCODE_EXPIRETIME_MS  252
#define REDIS_RDB_OPCODE_EXPIRETIME     253
#define REDIS_RDB_OPCODE_SELECTDB       254
#define REDIS_RDB_OPCODE_EOF            255

class RdbReader {
public:
    RdbReader();
    ~RdbReader();
    
    // return 0 - success, other - failure
    int Open(const string& rdb_file_path);
    
    // restore all rdb content to redis server
    int RestoreDB(int src_db_num, RedisConn& redis_conn);
private:
    int _LoadType();
    time_t _LoadTime();
    uint64_t _LoadMsTime();
    uint32_t _LoadLen(bool& is_encoded, string* buf = NULL);
    
    string _LoadString();
    string _LoadDumpValue(int& rdbtype);
    
    string _LoadIntegerString(int enctype);
    string _LoadLzfString();
    
    // get value of dump format, append after dump_value, used for RESTORE command
    void _LoadDumpString(string& dump_value);
    void _LoadDumpIntegerString(int enctype, string& dump_value);
    void _LoadDumpLzfString(string& dump_value);
    void _LoadDumpDoubleValue(string& dump_value);
    
    int _ZipTailEntrySize(uchar_t* ziplist);
    string _EncodeZiplistPrevLength(int len);
    string _EncodeLen(int len);
    string _QuickListToZipList(list<string> quicklist);
    string _CreateDumpPayload(int type, const string& value);
private:
    int     cur_db_num_;
    FILE*   rdb_file_;
};

#endif
