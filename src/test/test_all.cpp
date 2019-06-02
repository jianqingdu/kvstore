//
//  test_all.cpp
//  kv-store
//
//  Created by ziteng on 16-6-23
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "redis_conn.h"

void ExpectResult(const redisReply* reply, const string& method_name)
{
    if (!reply || (reply->type == REDIS_REPLY_ERROR)) {
        printf("\033[031m %s failed\033[0m\n", method_name.c_str());
        if (reply) {
            printf("\033[031m %s \033[0m\n", reply->str);
        }
        exit(1);
    } else {
        printf("\033[032m %s success \033[0m\n", method_name.c_str());
    }
}

void ExpectEqual(long value1, long value2, const string& method_name)
{
    if (value1 == value2) {
        printf("\033[032m %s passed \033[0m\n", method_name.c_str());
    } else {
        printf("\033[031m %s failed (%ld, %ld)\033[0m\n", method_name.c_str(), value1, value2);
        exit(1);
    }
}

void ExpectEqual(const string& value1, const string& value2, const string& method_name)
{
    if (value1 == value2) {
        printf("\033[032m %s passed \033[0m\n", method_name.c_str());
    } else {
        printf("\033[031m %s failed (%s, %s) \033[0m\n", method_name.c_str(), value1.c_str(), value2.c_str());
        exit(1);
    }
}

void ExpectErrorReply(const redisReply* reply, const string& cmd)
{
    if (reply->type == REDIS_REPLY_ERROR) {
        printf("\n\033[032m command %s reply with error: %s \033[0m\n", cmd.c_str(), reply->str);
    } else {
        printf("\033[031m command %s not reply with error\033[0m\n", cmd.c_str());
        exit(1);
    }
}

void TestErrorCommand(const string& redis_host, int redis_port)
{
    printf("\n==tests for error command===\n");
    
    // test inline parser
    string cmd;
    RedisConn* redis_conn;
    redisReply* reply;
    
    cmd = "*1\r\n$0\r\n\r\n";
    redis_conn = new RedisConn(redis_host, redis_port);
    reply = redis_conn->DoRawCmd(cmd);
    ExpectErrorReply(reply, cmd);
    delete redis_conn;
    
    cmd = "*1\r\n12\r\n";
    redis_conn = new RedisConn(redis_host, redis_port);
    reply = redis_conn->DoRawCmd(cmd);
    ExpectErrorReply(reply, cmd);
    delete redis_conn;
    
    cmd = "*1\r\n$1234567891011121314\r\n...\r\n";
    redis_conn = new RedisConn(redis_host, redis_port);
    reply = redis_conn->DoRawCmd(cmd);
    ExpectErrorReply(reply, cmd);
    delete redis_conn;
    
    cmd = "no_such_cmd param\r\n";
    redis_conn = new RedisConn(redis_host, redis_port);
    reply = redis_conn->DoRawCmd(cmd);
    ExpectErrorReply(reply, cmd);
    delete redis_conn;
    
    cmd = "get\r\n";
    redis_conn = new RedisConn(redis_host, redis_port);
    reply = redis_conn->DoRawCmd(cmd);
    ExpectErrorReply(reply, cmd);
    delete redis_conn;
}

// 把key随机化，这样可以测试不同key的效果, key不同会打到不同的redis后端上
void TestString(RedisConn* redis_conn)
{
    printf("\n===tests for string===\n");

    string str_key = "str_key_" + to_string(rand());
    string cmd = "set " + str_key + " str_value";
    redisReply* reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "set");
    ExpectEqual(reply->str, "OK", "set");
    
    cmd = "get " + str_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "get");
    ExpectEqual(reply->str, "str_value", "get");
    
    cmd = "append " + str_key + " _append";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "append");
    ExpectEqual(reply->integer, 16, "append");
    
    cmd = "strlen " + str_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "strlen");
    ExpectEqual(reply->integer, 16, "strlen");
    
    cmd = "getrange " + str_key + " 0 3";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "getrange");
    ExpectEqual(reply->str, "str_", "getrange");
    
    cmd = "setrange " + str_key + " 4 test";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "setrange");
    ExpectEqual(reply->integer, 16, "setrange");
    
    string key1 = "test1_" + to_string(rand());
    string key2 = "test2_" + to_string(rand());
    string key3 = "test3_" + to_string(rand());
    cmd = "mset " + key1 + " value1 " + key2 + " value2 " + key3 + " value3";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "mset");
    ExpectEqual(reply->str, "OK", "mset");
    
    cmd = "mget " + key1 + " " + key2 + " " + key3;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "mget");
    ExpectEqual(reply->element[0]->str, "value1", "mget");
    ExpectEqual(reply->element[1]->str, "value2", "mget");
    ExpectEqual(reply->element[2]->str, "value3", "mget");

    string str_key_ex = "str_key_ex_" + to_string(rand());
    cmd = "setex " + str_key_ex + " 30 str_value_ex";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "setex");

    cmd = "get " + str_key_ex;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "setex"); // test setex use get cmd
    ExpectEqual(reply->str, "str_value_ex", "setex");
    
    cmd = "setnx " + str_key + " other_value";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "setnx");
    ExpectEqual(reply->integer, 0, "setnx");
    
    string incr_key = "incr_key_" + to_string(rand());
    cmd = "incr " + incr_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "incr");
    ExpectEqual(reply->integer, 1, "incr");
    
    cmd = "incrby " + incr_key + " 7";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "incrby");
    ExpectEqual(reply->integer, 8, "incrby");
    
    cmd = "decr " + incr_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "decr");
    ExpectEqual(reply->integer, 7, "decr");
    
    cmd = "decrby " + incr_key + " 2";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "decrby");
    ExpectEqual(reply->integer, 5, "decrby");
    
    cmd = "getset " + incr_key + " 2";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "getset");
    ExpectEqual(reply->str, "5", "getset");
    
    cmd = "expire " + str_key + " 30";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "expire");
    ExpectEqual(reply->integer, 1, "expire");
    
    cmd = "ttl " + str_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "ttl");
    //ExpectEqual(reply->integer, 10, "ttl");
    
    cmd = "type " + str_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "type");
    ExpectEqual(reply->str, "string", "type");
    
    cmd = "exists " + str_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "exists");
    ExpectEqual(reply->integer, 1, "exists");
    
    string bit_key = "bit_key_" + to_string(rand());
    cmd = "setbit " + bit_key + " 7 1";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "setbit");
    ExpectEqual(reply->integer, 0, "setbit");
    
    cmd = "getbit " + bit_key + " 7";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "getbit");
    ExpectEqual(reply->integer, 1, "getbit");
    
    cmd = "bitcount " + bit_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "bitcount");
    ExpectEqual(reply->integer, 1, "bitcount");
    
    
    // delete all test keys when test complete
    cmd = "del " + str_key + " " + key1 + " " + key2 + " " + key3 + " " + str_key_ex + " " + incr_key + " " + bit_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectEqual(reply->integer, 7, "del");
}

void TestHash(RedisConn* redis_conn)
{
    printf("\n===tests for hash===\n");
    
    string hash_key = "hash_key_" + to_string(rand());
    string cmd = "hset " + hash_key + " hash_field1 hash_value1";
    redisReply* reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hset");
    ExpectEqual(reply->integer, 1, "hset");
    
    cmd = "hset " + hash_key + " hash_field2 hash_value2";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hset");
    ExpectEqual(reply->integer, 1, "hset");
    
    cmd = "hget " + hash_key + " hash_field1";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hget");
    ExpectEqual(reply->str, "hash_value1", "hget");
    
    cmd = "hget " + hash_key + " hash_nonexist_field1";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hget");
    ExpectEqual(reply->type, REDIS_REPLY_NIL, "hget");
    
    cmd = "hlen " + hash_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hlen");
    ExpectEqual(reply->integer, 2, "hlen");
    
    cmd = "hexists " + hash_key + " hash_field1";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hexists");
    ExpectEqual(reply->integer, 1, "hexists");
    
    cmd = "hdel " + hash_key + " hash_field1 hash_field2";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hdel");
    ExpectEqual(reply->integer, 2, "hdel");
    
    cmd = "hmset " + hash_key + " field1 value1 field2 value2 field3 value3";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hmset");
    ExpectEqual(reply->str, "OK", "hmset");
    
    cmd = "hmget " + hash_key + " field1 field2 nofield";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hmget");
    ExpectEqual(reply->element[0]->str, "value1", "hmget");
    ExpectEqual(reply->element[1]->str, "value2", "hmget");
    ExpectEqual(reply->element[2]->type, REDIS_REPLY_NIL, "hmget");
    
    cmd = "hgetall " + hash_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hgetAll");
    ExpectEqual(reply->elements, 6, "hgetall");
    ExpectEqual(reply->element[0]->str, "field1", "hgetAll");
    ExpectEqual(reply->element[1]->str, "value1", "hgetAll");
    
    string hash_incr_key = "hash_incr_key_" + to_string(rand());
    cmd = "hincrby " + hash_incr_key + " hash_incr_field 2";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hincrBy");
    ExpectEqual(reply->integer, 2, "hincrBy");
    
    cmd = "hincrbyfloat " + hash_incr_key + " hash_incr_field 1.5";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hincrbyfloat");
    ExpectEqual(reply->str, "3.5", "hincrbyfloat");
    
    cmd = "hsetnx " + hash_key + " field4 value4";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hsetnx");
    ExpectEqual(reply->integer, 1, "hsetnx");
    
    cmd = "hsetnx " + hash_key + " field1 value_new";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hsetnx");
    ExpectEqual(reply->integer, 0, "hsetnx");
    
    cmd = "hkeys " + hash_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hkeys");
    ExpectEqual(reply->elements, 4, "hkeys");
    
    cmd = "hvals " + hash_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "hvals");
    ExpectEqual(reply->elements, 4, "hvals");
    
    cmd = "del " + hash_key + " " + hash_incr_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectEqual(reply->integer, 2, "del");
}

void TestSet(RedisConn* redis_conn)
{
    printf("\n===test for set===\n");
    
    string set_key = "set_key_" + to_string(rand());
    string cmd = "sadd " + set_key + " aaa bbb ccc";
    redisReply* reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "sadd");
    ExpectEqual(reply->integer, 3, "sadd");
    
    cmd = "scard " + set_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "scard");
    ExpectEqual(reply->integer, 3, "scard");
    
    cmd = "smembers " + set_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "smembers");
    ExpectEqual(reply->elements, 3, "smembers");
    
    cmd = "sismember " + set_key + " aaa";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "sismember");
    ExpectEqual(reply->integer, 1, "sismember");
    
    cmd = "spop " + set_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "spop");
    printf("spop %s\n", reply->str);
    
    cmd = "srandmember " + set_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "srandmember");
    printf("srandmenber %s\n", reply->str);
    
    cmd = "srem " + set_key + " aaa bbb ccc";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "srem");
    ExpectEqual(reply->integer, 2, "srem");
    
    cmd = "del " + set_key;
    redis_conn->DoCmd(cmd);
}

void TestSortedSet(RedisConn* redis_conn)
{
    printf("\n===test for sorted set===\n");
    
    string zset_key = "zset_key_" + to_string(rand());
    string cmd = "zadd " + zset_key + " 1 one 2 two 3 three 4 four 5 five 6 six";
    redisReply* reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "zadd");
    ExpectEqual(reply->integer, 6, "zadd");
    
    cmd = "zcard " + zset_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "zcard");
    ExpectEqual(reply->integer, 6, "zcard");
    
    cmd = "zcount " + zset_key + " 1 3";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "zcount");
    ExpectEqual(reply->integer, 3, "zcount");
    
    cmd = "zremrangeByRank " + zset_key + " 0 1";
    reply = redis_conn->DoCmd(cmd); // remove first, two
    ExpectResult(reply, "zremrangeByRank");
    ExpectEqual(reply->integer, 2, "zremrangeByRank");
    
    cmd = "zrank " + zset_key + " four";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "zrank");
    ExpectEqual(reply->integer, 1, "zrank");
    
    cmd = "zrem " + zset_key + " three";
    reply = redis_conn->DoCmd(cmd); // remove three
    ExpectResult(reply, "zrem");
    ExpectEqual(reply->integer, 1, "zrem");
    
    cmd = "zrange " + zset_key + " 0 1";
    reply = redis_conn->DoCmd(cmd); // take the first two member
    ExpectResult(reply, "zrange");
    ExpectEqual(reply->elements, 2, "zrange");
    ExpectEqual(reply->element[0]->str, "four", "zrange");
    ExpectEqual(reply->element[1]->str, "five", "zrange");
    
    cmd = "zrange " + zset_key + " 0 1 withscores";
    reply = redis_conn->DoCmd(cmd); // take the first two member
    ExpectResult(reply, "zrange");
    ExpectEqual(reply->elements, 4, "zrange");
    ExpectEqual(reply->element[0]->str, "four", "zrange");
    ExpectEqual(reply->element[1]->str, "4", "zrange");
    
    cmd = "zrangebyscore " + zset_key + " 0 10";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "zrangebyscore");
    ExpectEqual(reply->elements, 3, "zrangebyscore");

    cmd = "zremrangebyscore " + zset_key + " 1 4";
    reply = redis_conn->DoCmd(cmd); // remove four
    ExpectResult(reply, "zremrangebyscore");
    ExpectEqual(reply->integer, 1, "zremrangebyscore");
    
    cmd = "zrevrange " + zset_key + " 0 -1";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "zrevrange");
    ExpectEqual(reply->elements, 2, "zrevrange");
    ExpectEqual(reply->element[0]->str, "six", "zrevrange");
    
    cmd = "zrevrangebyscore " + zset_key + " 5 4";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "zrevrangebyscore");
    ExpectEqual(reply->elements, 1, "zrevrangebyscore");
    ExpectEqual(reply->element[0]->str, "five", "zrevrangebyscore");
    
    cmd = "zrevrank " + zset_key + " five";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "zrevrank");
    ExpectEqual(reply->integer, 1, "zrevrank");
    
    cmd = "zscore " + zset_key + " five";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "zscore");
    ExpectEqual(reply->str, "5", "zscore");
    
    cmd = "zincrby " + zset_key + " 2 six";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "zincrby");
    ExpectEqual(reply->str, "8", "zincrby");
    
    cmd = "del " + zset_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectEqual(reply->integer, 1, "del");
}

void TestList(RedisConn* redis_conn)
{
    printf("\n===test for list===\n");
    
    string list_key = "list_key_" + to_string(rand());
    string cmd = "lpush " + list_key + " two";
    redisReply* reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "lpush");
    ExpectEqual(reply->integer, 1, "lpush");
    
    cmd = "lpush " + list_key + " one";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "lpush");
    ExpectEqual(reply->integer, 2, "lpush");
    
    cmd = "rpush " + list_key + " three";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "rpush");
    ExpectEqual(reply->integer, 3, "rpush");
    
    cmd = "rpush " + list_key + " four";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "rpush");
    ExpectEqual(reply->integer, 4, "rpush");
    
    cmd = "llen " + list_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "llen");
    ExpectEqual(reply->integer, 4, "llen");
    
    cmd = "lindex " + list_key + " 1";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "lindex");
    ExpectEqual(reply->str, "two", "lindex");
    
    cmd = "lpop " + list_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "lpop");
    ExpectEqual(reply->str, "one", "lpop");
    
    cmd = "rpop " + list_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "rpop");
    ExpectEqual(reply->str, "four", "rpop");
    
    cmd = "lrange " + list_key + " 0 1";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "lrange");
    ExpectEqual(reply->elements, 2, "lrange");
    ExpectEqual(reply->element[0]->str, "two", "lrange");
    ExpectEqual(reply->element[1]->str, "three", "lrange");
    
    cmd = "linsert " + list_key + " before two one";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "linsert");
    ExpectEqual(reply->integer, 3, "linsert");
    
    cmd = "lrem " + list_key + " 1 three";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "lrem");
    ExpectEqual(reply->integer, 1, "lrem");
    
    cmd = "lset " + list_key + " 0 zero";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "lset");
    ExpectEqual(reply->str, "OK", "lset");
    
    cmd = "ltrim " + list_key + " 1 2";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "ltrim");
    ExpectEqual(reply->str, "OK", "ltrim");
    
    cmd = "rpushx " + list_key + " ten";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "rpushx");
    ExpectEqual(reply->integer, 2, "rpushx");
    
    reply = redis_conn->DoCmd("lpushx list_no_key nine");
    ExpectResult(reply, "lpushx");
    ExpectEqual(reply->integer, 0, "lpushx");
    
    cmd = "del " + list_key;
    reply = redis_conn->DoCmd(cmd);
    ExpectEqual(reply->integer, 1, "del");
}


void TestHyperLogLog(RedisConn* redis_conn)
{
    printf("\n===test for hyperloglog===\n");
    
    string pf_key1 = "pf_key_" + to_string(rand());
    string pf_key2 = "pf_key_" + to_string(rand());
    
    string cmd = "pfadd " + pf_key1 + " a b c d e f g";
    redisReply* reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "pfadd");
    ExpectEqual(reply->integer, 1, "pfadd");
    
    cmd = "pfadd " + pf_key2 + " h i j k l m n o p q";
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "pfadd");
    ExpectEqual(reply->integer, 1, "pfadd");
    
    cmd = "pfcount " + pf_key1 + " " + pf_key2;
    reply = redis_conn->DoCmd(cmd);
    ExpectResult(reply, "pfcount");
    ExpectEqual(reply->integer, 17, "pfcount");
    
    cmd = "del " + pf_key1 + " " + pf_key2;
    reply = redis_conn->DoCmd(cmd);
    ExpectEqual(reply->integer, 2, "del");
}

void TestPipeline(RedisConn* redis_conn)
{
    printf("\n===test for pipeline===\n");
    
    string pl_key1 = "pl_key1_" + to_string(rand());
    string pl_key2 = "pl_key2_" + to_string(rand());
    
    string cmd = "set " + pl_key1 + " 1";
    redis_conn->PipelineCmd(cmd);
    cmd = "incr " + pl_key1;
    redis_conn->PipelineCmd(cmd);
    cmd = "incrby " + pl_key1 + " 6";
    redis_conn->PipelineCmd(cmd);
    
    cmd = "set " + pl_key2 + " 2";
    redis_conn->PipelineCmd(cmd);
    cmd = "incr " + pl_key2;
    redis_conn->PipelineCmd(cmd);
    cmd = "incrby " + pl_key2 + " 6";
    redis_conn->PipelineCmd(cmd);
    
    redisReply* reply = redis_conn->GetReply();
    ExpectEqual(reply->str, "OK", "pipeline set");
    reply = redis_conn->GetReply();
    ExpectEqual(reply->integer, 2, "pipeline incr");
    reply = redis_conn->GetReply();
    ExpectEqual(reply->integer, 8, "pipeline incrby");
    
    reply = redis_conn->GetReply();
    ExpectEqual(reply->str, "OK", "pipeline set");
    reply = redis_conn->GetReply();
    ExpectEqual(reply->integer, 3, "pipeline incr");
    reply = redis_conn->GetReply();
    ExpectEqual(reply->integer, 9, "pipeline incrby");
    
    cmd = "del " + pl_key1 + " " + pl_key2;
    reply = redis_conn->DoCmd(cmd);
    ExpectEqual(reply->integer, 2, "del");
}

int main(int argc, char* argv[])
{
    string redis_host = "127.0.0.1";
    int redis_port = 7400;
    int loop_cnt = 1;
    bool daemon = false;
    
    int ch;
    while ((ch = getopt(argc, argv, "h:p:l:d")) != -1) {
        switch (ch) {
            case 'h':
                redis_host = optarg;
                break;
            case 'p':
                redis_port = atoi(optarg);
                break;
            case 'l':
                loop_cnt = atoi(optarg);
                break;
            case 'd':
                daemon = true;
                break;
            case '?':
            default:
                printf("usage: %s -h ip -p port -l loop_cnt\n", argv[0]);
                return 1;
        }
    }
    
    if (daemon) {
        run_as_daemon();
    }
    
    srand((uint32_t)time(NULL));
    
    RedisConn redis_conn(redis_host, redis_port);
    if (redis_conn.Init()) {
        fprintf(stderr, "connect to redis server failed\n");
        return 1;
    }
    
    TestErrorCommand(redis_host, redis_port);
    
    for (int i = 1; i <= loop_cnt; ++i) {
        printf("\nThe %dth Loop Test\n", i);
        
        redisReply* reply = redis_conn.DoCmd("ping");
        ExpectResult(reply, "ping");
        ExpectEqual(reply->str, "PONG", "ping");
        
        reply = redis_conn.DoCmd("randomkey");
        ExpectResult(reply, "randomkey");
        
        TestString(&redis_conn);
        TestHash(&redis_conn);
        TestSet(&redis_conn);
        TestSortedSet(&redis_conn);
        TestList(&redis_conn);
        TestHyperLogLog(&redis_conn);
        TestPipeline(&redis_conn);
    }
    
    printf("\n");
    redisReply* reply = redis_conn.DoCmd("quit");
    ExpectResult(reply, "quit");
    ExpectEqual(reply->str, "OK", "quit");
    
    printf("\n GREAT! PASSED ALL TEST\n");
    
    return 0;
}
