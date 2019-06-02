//
//  rdb.cpp
//  kv-store
//
//  Created by ziteng on 16-7-1.
//
//  reference:
//  rdb.h rdb.c ziplist.h ziplist.c in redis source code
//  http://rdb.fnordig.de/file_format.html#ziplist-encoding
//

#include "rdb.h"
#include "lzf.h"
#include "crc64.h"
#include "redis_parser.h"
#include "cmd_line_parser.h"
#include "sync_task.h"

// ziplist utility macros
#define ZIP_BIGLEN 254
#define ZIPLIST_BYTES(zl)       (*((uint32_t*)(zl)))
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
#define ZIPLIST_LENGTH(zl)      (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
#define ZIPLIST_HEADER_SIZE     (sizeof(uint32_t)*2+sizeof(uint16_t))
#define ZIPLIST_END_SIZE        (sizeof(uint8_t))

RdbReader::RdbReader()
{
    rdb_file_ = NULL;
    cur_db_num_ = 0;
}

RdbReader::~RdbReader()
{
    if (rdb_file_) {
        fclose(rdb_file_);
        rdb_file_ = NULL;
    }
}

int RdbReader::Open(const string& rdb_file_path)
{
    rdb_file_ = fopen(rdb_file_path.c_str(), "rb");
    if (!rdb_file_) {
        fprintf(g_config.logfp, "fopen failed\n");
        return 1;
    }
    
    char buf[16];
    if (fread(buf, 1, 9, rdb_file_) != 9) {
        fprintf(g_config.logfp, "fread return error\n");
        return 2;
    }
    
    buf[9] = '\0';
    if (memcmp(buf,"REDIS",5) != 0) {
        fprintf(g_config.logfp, "Wrong signature trying to load DB from file\n");
        return 3;
    }
    
    int rdb_version = atoi(buf + 5);
    if ((rdb_version < 1) || (rdb_version > REDIS_RDB_VERSION)) {
        fprintf(g_config.logfp, "Can't handle RDB format version %d\n", rdb_version);
        return 4;
    }
    
    return 0;
}

int RdbReader::RestoreDB(int src_db_num, RedisConn& redis_conn)
{
    int pipeline_cmd_cnt = 0;
    while (true) {
        /* Read type. */
        int type = _LoadType();
        if (type == -1) {
            return -1;
        }
        
        int64_t expire_time = -1;  // expire time in milliseconds
        if (type == REDIS_RDB_OPCODE_EXPIRETIME) {
            if ((expire_time = _LoadTime()) == -1) {
                return -1;
            }
            /* We read the time so we need to read the object type again. */
            if ((type = _LoadType()) == -1) {
                return -1;
            }

            expire_time *= 1000;
        } else if (type == REDIS_RDB_OPCODE_EXPIRETIME_MS) {
            if ((expire_time = _LoadMsTime()) == -1) {
                return -1;
            }
            /* We read the time so we need to read the object type again. */
            if ((type = _LoadType()) == -1) {
                return -1;
            }
        } else if (type == REDIS_RDB_OPCODE_EOF) {
            break; // End of RDB file
        } else if (type == REDIS_RDB_OPCODE_SELECTDB) {
            // SELECTDB: Select the specified database
            bool is_encoded = false;
            if ((cur_db_num_ = _LoadLen(is_encoded)) == (int)REDIS_RDB_LENERR) {
                fprintf(g_config.logfp, "read db number failed\n");
                return -1;
            }
            
            continue; // Read type again
        } else if (type == REDIS_RDB_OPCODE_RESIZEDB) {
            bool is_encoded = false;
            uint32_t db_size = _LoadLen(is_encoded);
            uint32_t expire_size = _LoadLen(is_encoded);
            // discard resize, just log the message
            fprintf(g_config.logfp, "resizedb db_size=%d, expire_size=%d\n", db_size, expire_size);
            continue; // Read type again
        } else if (type == REDIS_RDB_OPCODE_AUX) {
            string key = _LoadString();
            string value = _LoadString();
            fprintf(g_config.logfp, "AUX RDB: %s=%s\n", key.c_str(), value.c_str());
            continue; // Read type again
        }
        
        // read key and value
        string key = _LoadString();
        string val = _LoadDumpValue(type);
        string dump_val = _CreateDumpPayload(type, val);
        
        if ((src_db_num != -1) && (src_db_num != cur_db_num_)) {
            continue;
        }
        
        /* Check if the key already expired. This function is used when loading
         * an RDB file from disk, either at startup, or when an RDB was
         * received from the master. In the latter case, the master is
         * responsible for key expiry. If we would expire keys here, the
         * snapshot taken by the master may not be reflected on the slave. */
        int64_t cur_ms_time = (int64_t)get_tick_count();
        if ((expire_time != -1) && (expire_time < cur_ms_time)) {
            continue;
        }
        
        // remove prefix
        if (!g_config.prefix.empty()) {
            size_t pos = key.find(g_config.prefix);
            if (pos != string::npos) {
                key = key.substr(pos + g_config.prefix.size());
            }
        }
        
        string ttl = (expire_time == -1) ? "0" : to_string(expire_time - cur_ms_time);
        vector<string> cmd_vec = {"RESTORE", key, ttl, dump_val};
        if (g_config.with_replace) {
            cmd_vec.push_back("REPLACE");
        }
        string request;
        build_request(cmd_vec, request);
        
        redis_conn.PipelineRawCmd(request);
        pipeline_cmd_cnt++;
        if (pipeline_cmd_cnt >= g_config.pipeline_cnt) {
            execute_pipeline(pipeline_cmd_cnt, redis_conn);
            pipeline_cmd_cnt = 0;
        }
    }
    
    if (pipeline_cmd_cnt > 0) {
        execute_pipeline(pipeline_cmd_cnt, redis_conn);
    }
    
    return 0;
}

int RdbReader::_LoadType()
{
    unsigned char type;
    if (fread(&type, 1, 1, rdb_file_) != 1) {
        fprintf(g_config.logfp, "LoadType failed, file postion: %ld\n", ftell(rdb_file_));
        return -1;
    }
    return type;
}

time_t RdbReader::_LoadTime()
{
    int32_t t32;
    if (fread(&t32, 4, 1, rdb_file_) != 1) {
        fprintf(g_config.logfp, "LoadTime failed, file postion: %ld\n", ftell(rdb_file_));
        return -1;
    }
    return (time_t)t32;
}

uint64_t RdbReader::_LoadMsTime()
{
    uint64_t t64;
    if (fread(&t64, 8, 1, rdb_file_) != 1) {
        fprintf(g_config.logfp, "LoadMsTime failed, file postion: %ld\n", ftell(rdb_file_));
        return -1;
    }
    return t64;
}

/* Load an encoded length. The "is_encoded" argument is set to true if the length
 * is not actually a length but an "encoding type". See the REDIS_RDB_ENC_*
 * definitions in rdb.h for more information. */
uint32_t RdbReader::_LoadLen(bool& is_encoded, string* len_buf)
{
    unsigned char buf[2];
    uint32_t len;
    int type;
    
    is_encoded = false;
    if (fread(buf, 1, 1, rdb_file_) != 1) {
        fprintf(g_config.logfp, "_LoadLen failed, read 1 byte file postion: %ld\n", ftell(rdb_file_));
        return REDIS_RDB_LENERR;
    }
    
    type = (buf[0] & 0xC0) >> 6;
    if (type == REDIS_RDB_ENCVAL) {
        /* Read a 6 bit encoding type. */
        is_encoded = true;
        
        if (len_buf) {
            len_buf->append((char*)buf, 1);
        }
        return buf[0] & 0x3F;
    } else if (type == REDIS_RDB_6BITLEN) {
        /* Read a 6 bit len. */
        if (len_buf) {
            len_buf->append((char*)buf, 1);
        }
        return buf[0] & 0x3F;
    } else if (type == REDIS_RDB_14BITLEN) {
        /* Read a 14 bit len. */
        if (fread(buf + 1, 1, 1, rdb_file_) != 1) {
            fprintf(g_config.logfp, "_LoadLen failed, read another 1 byte, file postion: %ld\n", ftell(rdb_file_));
            return REDIS_RDB_LENERR;
        }
        if (len_buf) {
            len_buf->append((char*)buf, 2);
        }
        return ((buf[0] & 0x3F) << 8) | buf[1];
    } else {
        /* Read a 32 bit len. */
        if (fread(&len, 4, 1, rdb_file_) != 1) {
            fprintf(g_config.logfp, "_LoadLen failed, read 4 byte, file postion: %ld\n", ftell(rdb_file_));
            return REDIS_RDB_LENERR;
        }
        
        if (len_buf) {
            len_buf->append((char*)buf, 1);
            len_buf->append((char*)&len, 4);
        }
        return ntohl(len);
    }
}

string RdbReader::_LoadString()
{
    bool is_encoded;
    uint32_t len;
    
    len = _LoadLen(is_encoded);
    if (is_encoded) {
        switch(len) {
            case REDIS_RDB_ENC_INT8:
            case REDIS_RDB_ENC_INT16:
            case REDIS_RDB_ENC_INT32:
                return _LoadIntegerString(len);
            case REDIS_RDB_ENC_LZF:
                return _LoadLzfString();
            default:
                fprintf(g_config.logfp, "Unknown RDB encoding type: %d\n", len);
                exit(1);
        }
    }
    
    if (len == REDIS_RDB_LENERR) {
        fprintf(g_config.logfp, "_LoadString REDIS_RDB_LENERR\n");
        exit(1);
    }
    
    char* buf = new char[len];
    if (len && (fread(buf, 1, len, rdb_file_) != len)) {
        fprintf(g_config.logfp, "LoadString failed\n");
        delete [] buf;
        exit(1);
    }
    
    string val(buf, len);
    delete [] buf;
    return val;
}

string RdbReader::_LoadDumpValue(int& rdbtype)
{
    string dump_value;
    if (rdbtype == REDIS_RDB_TYPE_STRING       ||
        rdbtype == REDIS_RDB_TYPE_HASH_ZIPMAP  ||
        rdbtype == REDIS_RDB_TYPE_LIST_ZIPLIST ||
        rdbtype == REDIS_RDB_TYPE_SET_INTSET   ||
        rdbtype == REDIS_RDB_TYPE_ZSET_ZIPLIST ||
        rdbtype == REDIS_RDB_TYPE_HASH_ZIPLIST) {
        _LoadDumpString(dump_value);
    } else if (rdbtype == REDIS_RDB_TYPE_ZSET) {
        size_t zset_len;
        bool is_decoded;
        if ((zset_len = _LoadLen(is_decoded, &dump_value)) == REDIS_RDB_LENERR) {
            fprintf(g_config.logfp, "read zset len failed\n");
            exit(1);
        }
        
        while(zset_len--) {
            _LoadDumpString(dump_value);
            _LoadDumpDoubleValue(dump_value);
        }
    } else if (rdbtype == REDIS_RDB_TYPE_LIST || rdbtype == REDIS_RDB_TYPE_SET || rdbtype == REDIS_RDB_TYPE_LIST_QUICKLIST) {
        size_t len;
        bool is_decoded;
        if ((len = _LoadLen(is_decoded, &dump_value)) == REDIS_RDB_LENERR) {
            fprintf(g_config.logfp, "read list len failed\n");
            exit(1);
        }
        
        if ((g_config.dst_rdb_version <= 6) && (rdbtype == REDIS_RDB_TYPE_LIST_QUICKLIST)) {
            // RDB version of 6 (redis-server version of 2.8) do not support QuickList, so we need to transfer it to ZipList
            rdbtype = REDIS_RDB_TYPE_LIST_ZIPLIST;
            list<string> quicklist;
            while (len--) {
                string ziplist = _LoadString();
                quicklist.push_back(ziplist);
            }
            dump_value = _QuickListToZipList(quicklist);
        } else {
            while(len--) {
                _LoadDumpString(dump_value);
            }
        }
    } else if (rdbtype == REDIS_RDB_TYPE_HASH) {
        size_t hash_len;
        bool is_decoded;
        if ((hash_len = _LoadLen(is_decoded, &dump_value)) == REDIS_RDB_LENERR) {
            fprintf(g_config.logfp, "read zset len failed\n");
            exit(1);
        }
        
        while (hash_len--) {
            _LoadDumpString(dump_value);
            _LoadDumpString(dump_value);
        }
    } else {
        fprintf(g_config.logfp, "(ERROR) Unknown RDB encoding type %d\n", rdbtype);
    }

    return dump_value;
}

string RdbReader::_LoadIntegerString(int enctype)
{
    unsigned char enc[4];
    long long val;
    
    if (enctype == REDIS_RDB_ENC_INT8) {
        if (fread(enc, 1, 1, rdb_file_) != 1) {
            fprintf(g_config.logfp, "fread 1 byte failed\n");
            exit(1);
        }
        
        val = (signed char)enc[0];
    } else if (enctype == REDIS_RDB_ENC_INT16) {
        uint16_t v;
        if (fread(enc, 2, 1, rdb_file_) != 1) {
            fprintf(g_config.logfp, "fread 2 byte failed\n");
            exit(1);
        }
        
        v = enc[0] | (enc[1]<<8);
        val = (int16_t)v;
    } else if (enctype == REDIS_RDB_ENC_INT32) {
        uint32_t v;
        if (fread(enc, 4, 1, rdb_file_) != 1) {
            fprintf(g_config.logfp, "fread 4 byte failed\n");
            exit(1);
        }
        
        v = enc[0] | (enc[1]<<8) | (enc[2]<<16) | (enc[3]<<24);
        val = (int32_t)v;
    } else {
        val = 0; /* anti-warning */
        fprintf(g_config.logfp, "Unknown RDB integer encoding type\n");
        exit(1);
    }

    return std::to_string(val);
}

string RdbReader::_LoadLzfString()
{
    unsigned int len, clen;
    unsigned char *cbuf = NULL;
    unsigned char *buf = NULL;
    bool is_encoded;
    
    if ((clen = _LoadLen(is_encoded)) == REDIS_RDB_LENERR) {
        exit(1);
    }
    
    if ((len = _LoadLen(is_encoded)) == REDIS_RDB_LENERR) {
        exit(1);
    }
    
    cbuf = new unsigned char[clen];
    buf = new unsigned char [len];

    if (fread(cbuf, 1, clen, rdb_file_) != clen) {
        fprintf(g_config.logfp, "read compress string failed\n");
        exit(1);
    }
    
    if (lzf_decompress(cbuf, clen, buf, len) == 0) {
        fprintf(g_config.logfp, "lzf_decompress failed\n");
        exit(1);
    }
    
    string val((char*)buf, len);
    delete [] cbuf;
    delete [] buf;
    return val;
}

void RdbReader::_LoadDumpString(string& dump_value)
{
    bool is_encoded;
    uint32_t len;
    
    len = _LoadLen(is_encoded, &dump_value);
    if (is_encoded) {
        switch(len) {
            case REDIS_RDB_ENC_INT8:
            case REDIS_RDB_ENC_INT16:
            case REDIS_RDB_ENC_INT32:
                return _LoadDumpIntegerString(len, dump_value);
            case REDIS_RDB_ENC_LZF:
                return _LoadDumpLzfString(dump_value);
            default:
                fprintf(g_config.logfp, "Unknown RDB encoding type: %d\n", len);
                exit(1);
        }
    }
    
    if (len == REDIS_RDB_LENERR) {
        fprintf(g_config.logfp, "_LoadDumpString REDIS_RDB_LENERR\n");
        exit(1);
    }
    
    char* buf = new char[len];
    if (len && (fread(buf, 1, len, rdb_file_) != len)) {
        fprintf(g_config.logfp, "LoadString failed\n");
        delete [] buf;
        exit(1);
    }
    
    dump_value.append(buf, len);
    delete [] buf;
}

void RdbReader::_LoadDumpIntegerString(int enctype, string& dump_value)
{
    char enc[4];
    
    if (enctype == REDIS_RDB_ENC_INT8) {
        if (fread(enc, 1, 1, rdb_file_) != 1) {
            fprintf(g_config.logfp, "fread 1 byte failed\n");
            exit(1);
        }
        
        dump_value.append(enc, 1);
    } else if (enctype == REDIS_RDB_ENC_INT16) {
        if (fread(enc, 2, 1, rdb_file_) != 1) {
            fprintf(g_config.logfp, "fread 2 byte failed\n");
            exit(1);
        }
        
        dump_value.append(enc, 2);
    } else if (enctype == REDIS_RDB_ENC_INT32) {
        if (fread(enc, 4, 1, rdb_file_) != 1) {
            fprintf(g_config.logfp, "fread 4 byte failed\n");
            exit(1);
        }
        
        dump_value.append(enc, 4);
    } else {
        fprintf(g_config.logfp, "Unknown RDB integer encoding type\n");
        exit(1);
    }
}

void RdbReader::_LoadDumpLzfString(string& dump_value)
{
    uint32_t len, clen;
    char *cbuf = NULL;
    bool is_encoded;
    
    if ((clen = _LoadLen(is_encoded, &dump_value)) == REDIS_RDB_LENERR) {
        exit(1);
    }
    
    if ((len = _LoadLen(is_encoded, &dump_value)) == REDIS_RDB_LENERR) {
        exit(1);
    }
    
    cbuf = new char[clen];
    if (fread(cbuf, 1, clen, rdb_file_) != clen) {
        fprintf(g_config.logfp, "read compress string failed\n");
        exit(1);
    }
    
    dump_value.append((char*)cbuf, clen);
    delete [] cbuf;
}

void RdbReader::_LoadDumpDoubleValue(string& dump_value)
{
    char buf[256];
    unsigned char len;
    
    if (fread(&len, 1, 1, rdb_file_) != 1) {
        fprintf(g_config.logfp, "LoadDoubleValue failed\n");
        exit(1);
    }
    
    switch (len) {
        case 255:
        case 254:
        case 253:
            dump_value.append((char*)&len, 1);
        default:
            if (fread(buf, 1, len, rdb_file_) != len) {
                fprintf(g_config.logfp, "LoadDoubleValue failed\n");
                exit(1);
            }
            
            dump_value.append((char*)&len, 1);
            dump_value.append(buf, len);
    }
}

int RdbReader::_ZipTailEntrySize(uchar_t* ziplist)
{
    int total_bytes = ZIPLIST_BYTES(ziplist);
    int tail_offset = ZIPLIST_TAIL_OFFSET(ziplist);
    return total_bytes - tail_offset - ZIPLIST_END_SIZE;
}

string RdbReader::_EncodeZiplistPrevLength(int len)
{
    if (len < ZIP_BIGLEN) {
        uchar_t buf[1];
        buf[0] = len;
        return string((char*)buf, 1);
    } else {
        uchar_t buf[5];
        buf[0] = ZIP_BIGLEN;
        memcpy(buf + 1, &len, sizeof(int));
        return string((char*)buf, 5);
    }
}

string RdbReader::_EncodeLen(int len)
{
    unsigned char buf[8];
    size_t nwritten;
        
    if (len < (1<<6)) {
        // Save a 6 bit len
        buf[0] = (len & 0xFF) | (REDIS_RDB_6BITLEN<<6);
        nwritten = 1;
    } else if (len < (1<<14)) {
        // Save a 14 bit len
        buf[0] = ((len>>8) & 0xFF) | (REDIS_RDB_14BITLEN<<6);
        buf[1] = len & 0xFF;
        nwritten = 2;
    } else {
        // Save a 32 bit len
        buf[0] = (REDIS_RDB_32BITLEN<<6);
        len = htonl(len); // write as big endian
        memcpy(buf + 1, &len, sizeof(int));
        nwritten = 1+4;
    }
    
    return string((char*)buf, nwritten);
}

// merge a linked ziplist to a large ziplist
string RdbReader::_QuickListToZipList(list<string> quicklist)
{
    string merged_ziplist;
    int quicklist_size = (int)quicklist.size();
    if (quicklist_size == 1) {
        merged_ziplist = quicklist.front();
    } else {
        int total_length = 0;
        int idx = 0;
        int tail_entry_size = 0;
        for (const string& ziplist : quicklist) {
            uchar_t* ziplist_data = (uchar_t*)ziplist.data();
            int ziplist_bytes = ZIPLIST_BYTES(ziplist_data);
            total_length += ZIPLIST_LENGTH(ziplist_data);
            
            if (idx == 0) {
                // the first ziplist only need to remove the last 1 byte of ending 0xFF
                merged_ziplist = ziplist.substr(0, ziplist_bytes - 1);
            } else {
                // the other ziplist need to remove ziplist header, tail and replace the fist entry's prev length with
                // the last ziplist entry
                string preEntryLength = _EncodeZiplistPrevLength(tail_entry_size);
                merged_ziplist.append(preEntryLength);
                if (idx != (quicklist_size - 1)) {
                    merged_ziplist.append(ziplist.substr(ZIPLIST_HEADER_SIZE + 1,
                                                        ziplist_bytes - ZIPLIST_HEADER_SIZE - 1 - ZIPLIST_END_SIZE));
                } else {
                    // the last ziplist do not need to remove the tail byte 0xFF
                    merged_ziplist.append(ziplist.substr(ZIPLIST_HEADER_SIZE + 1));
                }
            }
            
            tail_entry_size = _ZipTailEntrySize(ziplist_data);
            idx++;
        }
        
        // update total bytes, tail entry offset, and total length of the ziplist
        uchar_t* total_ziplist_data = (uchar_t*)merged_ziplist.data();
        int total_bytes = (int)merged_ziplist.size();
        memcpy(total_ziplist_data, &total_bytes, sizeof(int));
        int tail_entry_offset = total_bytes - tail_entry_size - ZIPLIST_END_SIZE;
        memcpy(total_ziplist_data + 4, &tail_entry_offset, sizeof(int));
        uint16_t ziplist_len = (total_length < UINT16_MAX) ? (uint16_t)total_length : UINT16_MAX;
        memcpy(total_ziplist_data + 8, &ziplist_len, sizeof(uint16_t));
        //printf("total_byte=%d, tail_offset=%d, size=%d\n", total_bytes, tail_entry_offset, ziplist_len);
    }
    
    string len_str = _EncodeLen((int)merged_ziplist.size());
    return len_str + merged_ziplist;
}

string RdbReader::_CreateDumpPayload(int type, const string &value)
{
    int data_size = (int)value.size();
    unsigned char* buf = new unsigned char [1 + data_size + 2 + 8];
    uint64_t crc;
    
    /* Serialize the object in a RDB-like format. It consist of an object type
     * byte followed by the serialized object. This is understood by RESTORE. */
    buf[0] = type;
    memcpy(buf + 1, value.data(), data_size);
    
    /* Write the footer, this is how it looks like:
     * ----------------+---------------------+---------------+
     * ... RDB payload | 2 bytes RDB version | 8 bytes CRC64 |
     * ----------------+---------------------+---------------+
     * RDB version and CRC are both in little endian.
     */
    
    /* RDB version */
    buf[1 + data_size] = g_config.dst_rdb_version & 0xff;
    buf[2 + data_size] = (g_config.dst_rdb_version >> 8) & 0xff;
    
    /* CRC64 */
    crc = crc64(0, buf, 1 + data_size + 2);

    /* append crc in little endian */
    for (int i = 0; i < 8; i++) {
        unsigned digit = (crc >> (i * 8)) & 0xFF;
        buf[3 + data_size + i] = digit;
    }
    
    string dump_value((char*)buf, data_size + 11);
    delete [] buf;
    return dump_value;
}
