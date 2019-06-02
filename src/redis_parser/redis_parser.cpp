//
//  redis_parser.cpp
//  kv-store
//
//  Created by ziteng on 16-5-18.
//

#include "redis_parser.h"
#include "simple_log.h"
#include "redis_byte_stream.h"
#include <limits.h>

const int kRedisInlineMaxSize = 1024 * 64;
const int kRedisRequestMaxSize = 1024 * 1024 * 512;

/* Helper function for sdssplitargs() that returns non zero if 'c'
 * is a valid hex digit. */
static int is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* Helper function for sdssplitargs() that converts a hex digit into an
 * integer from 0 to 15 */
static int hex_digit_to_int(char c)
{
    switch(c) {
        case '0': return 0;
        case '1': return 1;
        case '2': return 2;
        case '3': return 3;
        case '4': return 4;
        case '5': return 5;
        case '6': return 6;
        case '7': return 7;
        case '8': return 8;
        case '9': return 9;
        case 'a': case 'A': return 10;
        case 'b': case 'B': return 11;
        case 'c': case 'C': return 12;
        case 'd': case 'D': return 13;
        case 'e': case 'E': return 14;
        case 'f': case 'F': return 15;
        default: return 0;
    }
}

/* Split a line into arguments, where every argument can be in the
 * following programming-language REPL-alike form:
 *
 * foo bar "newline are supported\n" and "\xff\x00otherstuff"
 *
 * The number of arguments is stored into *argc, and an array
 * of sds is returned.
 *
 * The function returns 1 on success, even when the
 * input string is empty, or 0 if the input contains unbalanced
 * quotes or closed quotes followed by non space characters
 * as in: "foo"bar or "foo'
 */
static int split_inline_args(const char *line, vector<string>& cmd_vec)
{
    const char *p = line;
    string current;
    
    while (1) {
        /* skip blanks */
        while(*p && isspace(*p)) p++;
        if (*p) {
            /* get a token */
            int inq=0;  /* set to 1 if we are in "quotes" */
            int insq=0; /* set to 1 if we are in 'single quotes' */
            int done=0;
            
            while(!done) {
                if (inq) {
                    if (*p == '\\' && *(p+1) == 'x' &&
                        is_hex_digit(*(p+2)) &&
                        is_hex_digit(*(p+3)))
                    {
                        unsigned char byte;
                        
                        byte = (hex_digit_to_int(*(p+2))*16)+
                        hex_digit_to_int(*(p+3));
                        current.append((char*)&byte,1);
                        p += 3;
                    } else if (*p == '\\' && *(p+1)) {
                        char c;
                        
                        p++;
                        switch(*p) {
                            case 'n': c = '\n'; break;
                            case 'r': c = '\r'; break;
                            case 't': c = '\t'; break;
                            case 'b': c = '\b'; break;
                            case 'a': c = '\a'; break;
                            default: c = *p; break;
                        }
                        current.append(&c,1);
                    } else if (*p == '"') {
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if (*(p+1) && !isspace(*(p+1)))
                            return 0;
                        done=1;
                    } else if (!*p) {
                        /* unterminated quotes */
                        return 0;
                    } else {
                        current.append(p,1);
                    }
                } else if (insq) {
                    if (*p == '\\' && *(p+1) == '\'') {
                        p++;
                        current.append("'",1);
                    } else if (*p == '\'') {
                        /* closing quote must be followed by a space or
                         * nothing at all. */
                        if (*(p+1) && !isspace(*(p+1)))
                            return 0;
                        done=1;
                    } else if (!*p) {
                        /* unterminated quotes */
                        return 1;
                    } else {
                        current.append(p,1);
                    }
                } else {
                    switch(*p) {
                        case ' ':
                        case '\n':
                        case '\r':
                        case '\t':
                        case '\0':
                            done=1;
                            break;
                        case '"':
                            inq=1;
                            break;
                        case '\'':
                            insq=1;
                            break;
                        default:
                            current.append(p,1);
                            break;
                    }
                }
                if (*p) p++;
            }
            /* add the token to the vector */
            cmd_vec.push_back(current);
            current.clear();
        } else {
            /* Even on empty input string return success */
            return 1;
        }
    }
}

/* Convert a string into a long. Returns 1 if the string could be parsed
 * into a (non-overflowing) long long, 0 otherwise. The value will be set to
 * the parsed value when appropriate. */
int string2long(const char *s, size_t slen, long& value)
{
    const char *p = s;
    size_t plen = 0;
    int negative = 0;
    unsigned long v;
    
    if (plen == slen)
        return 0;
    
    /* Special case: first and only digit is 0. */
    if (slen == 1 && p[0] == '0') {
        value = 0;
        return 1;
    }
    
    if (p[0] == '-') {
        negative = 1;
        p++; plen++;
        
        /* Abort on only a negative sign. */
        if (plen == slen)
            return 0;
    }
    
    /* First digit should be 1-9, otherwise the string should just be 0. */
    if (p[0] >= '1' && p[0] <= '9') {
        v = p[0]-'0';
        p++; plen++;
    } else if (p[0] == '0' && slen == 1) {
        value = 0;
        return 1;
    } else {
        return 0;
    }
    
    while (plen < slen && p[0] >= '0' && p[0] <= '9') {
        if (v > (ULONG_MAX / 10)) /* Overflow. */
            return 0;
        v *= 10;
        
        if (v > (ULONG_MAX - (p[0]-'0'))) /* Overflow. */
            return 0;
        v += p[0]-'0';
        
        p++; plen++;
    }
    
    /* Return if not all bytes were used. */
    if (plen < slen)
        return 0;
    
    if (negative) {
        if (v > ((unsigned long)(-(LONG_MIN+1))+1)) /* Overflow. */
            return 0;
        value = -v;
    } else {
        if (v > LONG_MAX) /* Overflow. */
            return 0;
        value = v;
    }
    return 1;
}

// see http://redis.io/topics/protocol for details of redis protocol specification
int parse_inline_redis_request(const char* redis_cmd, int redis_len, vector<string>& cmd_vec, string& err_msg)
{
    // redis inline command
    char* newline = (char*)strchr(redis_cmd, '\n');
    if (!newline) {
        if (redis_len > kRedisInlineMaxSize) {
            err_msg = "Protocol error: too big inline request";
            log_message(kLogLevelError, "Protocol error: too big inline request\n");
            return -1;
        }
        return 0;
    }
    
    int skip_len = (int)(newline - redis_cmd) + 1;
    
    // Handle the \r\n case
    if (newline && newline != redis_cmd && *(newline-1) == '\r') {
        newline--;
    }
    
    int len = (int)(newline - redis_cmd);
    if (len == 0) {
        return skip_len;
    }
    
    *newline = 0;
    int ok = split_inline_args(redis_cmd, cmd_vec);
    if (!ok) {
        err_msg = "Protocol error: unbalanced quotes in request";
        log_message(kLogLevelError, "Protocol error: unbalanced quotes in request\n");
        return -1;
    }
    
    return skip_len;
}

int parse_multibulk_redis_request(const char* redis_cmd, int redis_len, vector<string>& cmd_vec, string& err_msg)
{
    // parse argument count
    char* newline = (char*)strchr(redis_cmd, '\r');
    if (!newline) {
        if (redis_len > kRedisInlineMaxSize) {
            err_msg = "Protocol error: too big mbulk count string";
            log_message(kLogLevelError, "Protocol error: too big mbulk count string\n");
            return -1;
        }
        return 0;
    }
    
    long argc_num;
    string argc_str(redis_cmd + 1, newline - (redis_cmd + 1));
    int ok = string2long(redis_cmd + 1, newline - (redis_cmd + 1), argc_num);
    if (!ok || (argc_num > 1024 * 1024)) {
        err_msg = "Protocol error: invalid multibulk length";
        log_message(kLogLevelError, "Protocol error: invalid multibulk length: %d\n", argc_num);
        return -1;
    }
    
    int pos = (int)(newline - redis_cmd) + 2;
    if (argc_num <= 0) {
        return pos;
    }
    
    cmd_vec.reserve(argc_num);
    while (argc_num > 0) {
        if (pos >= redis_len) {
            return 0;
        }
        
        // parse parameter length
        if (redis_cmd[pos] != '$') {
            err_msg = "Protocol error: expected '$', got '";
            err_msg += redis_cmd[pos];
            err_msg += "'";
            log_message(kLogLevelError, "%s\n", err_msg.c_str());
            return -1;
        }
        
        newline = (char*)strchr(redis_cmd + pos, '\r');
        if (!newline) {
            // parameter length protection
            if (redis_len - pos > kRedisInlineMaxSize) {
                err_msg = "Protocol error: too big bulk count string";
                log_message(kLogLevelError, "Protocol error: too big bulk count string\n");
                return -1;
            }
            return 0;
        }
        
        long len;
        ok = string2long(redis_cmd + pos + 1, newline - (redis_cmd + pos + 1), len);
        if (!ok || (len < 0) || (len > kRedisRequestMaxSize)) {
            err_msg = "Protocol error: invalid bulk length";
            log_message(kLogLevelError, "Protocol error: invalid bulk length: %d\n", len);
            return -1;
        }
        
        pos = (int)(newline - redis_cmd) + 2;
        if (pos + len > redis_len - 2) {
            return 0;
        }
        
        // add the parameter to cmd_vec
        string argv_str(redis_cmd + pos, len);
        cmd_vec.push_back(argv_str);
        pos += len + 2;
        argc_num--;
    }
    
    return pos;
}

int parse_redis_request(const char* redis_cmd, int redis_len, vector<string>& cmd_vec, string& err_msg)
{
    if (redis_len < 1) {
        return 0;
    }
    
    if (redis_cmd[0] != '*') {
        return parse_inline_redis_request(redis_cmd, redis_len, cmd_vec, err_msg);
    } else {
        return parse_multibulk_redis_request(redis_cmd, redis_len, cmd_vec, err_msg);
    }
}


static RedisReply parse_integer(RedisByteStream& byte_stream)
{
    string line;
    byte_stream.ReadLine(line);
    long int_value = atol(line.c_str());
    
    return RedisReply(REDIS_TYPE_INTEGER, int_value);
}

static RedisReply parse_simple_string(int type, RedisByteStream& byte_stream)
{
    string line;
    byte_stream.ReadLine(line);
    
    return RedisReply(type, line);
}

static RedisReply parse_bulk_string(RedisByteStream& byte_stream)
{
    string line;
    byte_stream.ReadLine(line);
    int size = (int)atoi(line.c_str());
    if (size == -1) {
        return RedisReply(REDIS_TYPE_NIL);
    }
    
    line.clear();
    byte_stream.ReadBytes(size, line);
    return RedisReply(REDIS_TYPE_STRING, line);
}

static RedisReply parse_redis_protocol(RedisByteStream& byte_stream);

static RedisReply parse_array(RedisByteStream& byte_stream)
{
    string line;
    byte_stream.ReadLine(line);
    int count = (int)atoi(line.c_str());
    if (count == -1) {
        return RedisReply(REDIS_TYPE_NIL);
    }
    
    RedisReply reply(REDIS_TYPE_ARRAY);
    for (int i = 0; i < count; i++) {
        reply.AddRedisReply(parse_redis_protocol(byte_stream));
    }
    
    return reply;
}

static RedisReply parse_redis_protocol(RedisByteStream& byte_stream)
{
    int b = byte_stream.ReadByte();
    switch (b) {
        case '+':
            return parse_simple_string(REDIS_TYPE_STATUS, byte_stream);
        case '-':
            return parse_simple_string(REDIS_TYPE_ERROR, byte_stream);
        case ':':
            return parse_integer(byte_stream);
        case '$':
            return parse_bulk_string(byte_stream);
        case '*':
            return parse_array(byte_stream);
        default:
            log_message(kLogLevelError, "parse redis protocol failed\n");
            throw ParseRedisException(PARSE_REDIS_ERROR_INVALID_FORMAT, "invalid format");
    }
}

int parse_redis_response(const char* redis_resp, int redis_len, RedisReply& reply)
{
    if (redis_len < 1) {
        return 0;
    }
    
    RedisByteStream byte_stream((char*)redis_resp, redis_len);
    
    try {
        reply = parse_redis_protocol(byte_stream);
        return byte_stream.GetOffset();
    } catch (ParseRedisException& ex) {
        if (ex.GetErrorCode() == PARSE_REDIS_ERROR_NO_MORE_DATA) {
            return 0;
        } else {
            return -1;
        }
    }
}

// 为什么弄这么复杂，而不用snprintf，因为callgrind性能测试显示build_request占了30%多的cpu资源,
// 而且绝大部分cpu都消耗在snprintf，这样改后可以降低到一半多，只有13%多一点
inline int build_prefix(char* buf, int len, char start_char, int size)
{
    buf[len - 1] = 0;
    buf[len - 2] = '\n';
    buf[len - 3] = '\r';
    int pos = len - 4;
    if (size == 0) {
        buf[pos] = '0';
        --pos;
    } else {
        while (size > 0) {
            buf[pos] = size % 10 + '0';
            --pos;
            size /= 10;
        }
    }
    buf[pos] = start_char;
    return pos;
}

void build_request(const vector<string>& cmd_vec, string& request)
{
    char tmp[32];
    int size = (int)cmd_vec.size();
    int pos = build_prefix(tmp, 32, '*', size);
    request.append(tmp + pos, strlen(tmp + pos));
    for (auto it = cmd_vec.begin(); it != cmd_vec.end(); ++it) {
        size = (int)it->size();
        pos = build_prefix(tmp, 32, '$', size);
        request.append(tmp + pos, strlen(tmp + pos));
        
        request.append(it->data(), size);
        request.append("\r\n");
    }
}

void build_response(const vector<string>& cmd_vec, string& response)
{
    char tmp[32];
    int size = (int)cmd_vec.size();
    int pos = build_prefix(tmp, 32, '*', size);
    response.append(tmp + pos, strlen(tmp + pos));
    for (auto it = cmd_vec.begin(); it != cmd_vec.end(); ++it) {
        if (it->empty()) {  // empty string means the value is not exist
            response.append("$-1\r\n");
        } else {
            size = (int)it->size();
            pos = build_prefix(tmp, 32, '$', size);
            response.append(tmp + pos, strlen(tmp + pos));
            response.append(it->data(), size);
            response.append("\r\n");
        }
    }
}

void parse_slave_addr(const string& replication_info, string& slave_addr)
{
    vector<string> info_vec = split(replication_info, "\r\n");
    for (const string& line: info_vec) {
        if (line.find("slave0:ip") != string::npos) {
            string slave_ip;
            string slave_port;
            
            vector<string> item_vec = split(line, ",");
            for (const string& item : item_vec) {
                vector<string> kv_vec = split(item, "=");
                if ((kv_vec.size() == 2) && (kv_vec[0] == "slave0:ip")) {
                    slave_ip = kv_vec[1];
                }
                
                if ((kv_vec.size() == 2) && (kv_vec[0] == "port")) {
                    slave_port = kv_vec[1];
                }
            }
            
            if (!slave_ip.empty() && !slave_port.empty()) {
                slave_addr = slave_ip + ":" + slave_port;
            }
            
            break;
        }
    }
}
