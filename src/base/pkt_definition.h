/*
 * pkt_definition.h
 *
 *  Created on: 2016-3-14
 *      Author: ziteng
 */

#ifndef __BASE_PKT_DEFINITION_H__
#define __BASE_PKT_DEFINITION_H__

#include "pkt_base.h"

class PktHeartBeat : public PktBase {
public:
    PktHeartBeat();
    PktHeartBeat(uchar_t* buf, uint32_t len);
    virtual ~PktHeartBeat() {}
};

class PktNamespaceListReq : public PktBase {
public:
    PktNamespaceListReq();
    PktNamespaceListReq(uchar_t* buf, uint32_t len);
    virtual ~PktNamespaceListReq() {}
};

class PktNamespaceListResp : public PktBase {
public:
    PktNamespaceListResp(const vector<string>& namespace_list);
    PktNamespaceListResp(uchar_t* buf, uint32_t len);
    virtual ~PktNamespaceListResp() {}
    
    vector<string>& GetNamespaceList() { return namespace_list_; }
private:
    vector<string>    namespace_list_;
};

enum {
    TABLE_REQ_FROM_PROXY = 1,
    TABLE_REQ_FROM_MIGRATION_TOOL = 2,
    TABLE_REQ_FROM_CONFIG_SERVER = 3,
};

class PktBucketTableReq : public PktBase {
public:
    PktBucketTableReq(const string& ns, uint32_t from, const string& info);
    PktBucketTableReq(uchar_t* buf, uint32_t len);
    virtual ~PktBucketTableReq() {}
    
    string& GetNamespace() { return namespace_; }
    uint32_t GetFromType() { return from_; }
    string& GetInfo() { return info_; }
private:
    string      namespace_;
    uint32_t    from_;  // 1--proxy, 2-migration tool, 3-slave config server
    string      info_;  // information for requestor, e.g. proxy listening addr
};

enum {
    TABLE_RESULT_SUCCESS = 0,
    TABLE_RESULT_WRONG_REQUESTOR = 1,
    TABLE_RESULT_NO_NAMESPACE = 2,
};

class PktBucketTableResp : public PktBase {
public:
    PktBucketTableResp(const string& ns, uint32_t result);  // to situation of result != 0
    PktBucketTableResp(const string& ns, uint32_t version,
                       const map<string, vector<uint16_t>>& server_buckets_map,
                       uint16_t migrating_bucket_id, const string& migrating_server_addr);
    PktBucketTableResp(uchar_t* buf, uint32_t len);
    virtual ~PktBucketTableResp() {}
    
    string& GetNamespace() { return namespace_; }
    uint32_t GetResult() { return result_; }
    uint32_t GetVersion() { return version_; }
    // if namespace not exist, ConfigServer will return empty map
    map<string, vector<uint16_t>>& GetServerBucketsMap() { return server_buckets_map_; }
    uint16_t GetMigratingBucketId() { return migrating_bucket_id_; }
    string& GetMigratingServerAddr() { return migrating_server_addr_; }
private:
    string      namespace_;
    uint32_t    result_;    // 0-success, other-failure
    // the following field will not show if result != 0;
    uint32_t    version_;
    map<string, vector<uint16_t>> server_buckets_map_;
    uint16_t    migrating_bucket_id_;
    string      migrating_server_addr_;
};

class PktStartMigration : public PktBase {
public:
    PktStartMigration(const string& ns, uint16_t bucket_id, const string& new_server_addr, uint8_t scale_up);
    PktStartMigration(uchar_t* buf, uint32_t len);
    virtual ~PktStartMigration() {}
    
    string& GetNamespace() { return namespace_; }
    uint16_t GetBucketId() { return bucket_id_; }
    string& GetNewServerAddr() { return new_server_addr_; }
    uint8_t GetScaleUp() { return scale_up_; }
private:
    string      namespace_;
    uint16_t    bucket_id_;
    string      new_server_addr_;
    uint8_t     scale_up_;  // 1-scale up, 0-scale down
};

enum {
    MIGRATION_RESULT_SUCCESS            = 0,
    MIGRATION_RESULT_NO_NAMESPACE       = 1,
    MIGRATION_RESULT_NO_BUCKET_ID       = 2,
    MIGRATION_RESULT_IN_MIGRAING        = 3,
    MIGRATION_RESULT_SAVE_FAILURE       = 4,
    MIGRATION_RESULT_NOT_MASETER        = 5,
    MIGRATION_RESULT_ADDRESS_EXIST      = 6,
    MIGRATION_RESULT_ADDRESS_NOT_EXIST  = 7,
};

class PktStartMigrationAck : public PktBase {
public:
    PktStartMigrationAck(const string& ns, uint16_t bucket_id, uint32_t result);
    PktStartMigrationAck(uchar_t* buf, uint32_t len);
    virtual ~PktStartMigrationAck() {}
    
    string& GetNamespace() { return namespace_; }
    uint16_t GetBucketId() { return bucket_id_; }
    uint32_t GetResult() { return result_; }
private:
    string      namespace_;
    uint16_t    bucket_id_;
    uint32_t    result_;
};

class PktCompleteMigration : public PktBase {
public:
    PktCompleteMigration(const string& ns, uint16_t bucket_id, const string& new_server_addr, uint8_t scale_up);
    PktCompleteMigration(uchar_t* buf, uint32_t len);
    virtual ~PktCompleteMigration() {}
    
    string& GetNamespace() { return namespace_; }
    uint16_t GetBucketId() { return bucket_id_; }
    string& GetNewServerAddr() { return new_server_addr_; }
    uint8_t GetScaleUp() { return scale_up_; }
private:
    string      namespace_;
    uint16_t    bucket_id_;
    string      new_server_addr_;
    uint8_t     scale_up_;
};

class PktCompleteMigrationAck : public PktBase {
public:
    PktCompleteMigrationAck(const string& ns, uint16_t bucket_id, uint32_t result);
    PktCompleteMigrationAck(uchar_t* buf, uint32_t len);
    virtual ~PktCompleteMigrationAck() {}
    
    string& GetNamespace() { return namespace_; }
    uint16_t GetBucketId() { return bucket_id_; }
    uint32_t GetResult() { return result_; }
private:
    string      namespace_;
    uint16_t    bucket_id_;
    uint32_t    result_;
};

class PktDelNamespace : public PktBase {
public:
    PktDelNamespace(const string& ns);
    PktDelNamespace(uchar_t* buf, uint32_t len);
    virtual ~PktDelNamespace() {}
    
    string& GetNamespace() { return namespace_; }
private:
    string  namespace_;
};

// freeze request and ack between cs and proxy
class PktFreezeProxy : public PktBase {
public:
    PktFreezeProxy(const string& ns);
    PktFreezeProxy(uchar_t* buf, uint32_t len);
    virtual ~PktFreezeProxy() {}
    
    string& GetNamespace() { return namespace_; }
public:
    string      namespace_;
};

class PktStorageServerDown : public PktBase {
public:
    PktStorageServerDown(const string& ns, const string& addr, const string& slave_addr);
    PktStorageServerDown(uchar_t* buf, uint32_t len);
    virtual ~PktStorageServerDown() {}
    
    string& GetNamespace() { return namespace_; }
    string& GetServerAddr() { return server_addr_; }
    string& GetSlaveAddr() { return slave_addr_; }
private:
    string  namespace_;
    string  server_addr_;
    string  slave_addr_;
};

class PktStorageServerUp : public PktBase {
public:
    PktStorageServerUp(const string& ns, const string& addr);
    PktStorageServerUp(uchar_t* buf, uint32_t len);
    virtual ~PktStorageServerUp() {}
    
    string& GetNamespace() { return namespace_; }
    string& GetServerAddr() { return server_addr_; }
private:
    string  namespace_;
    string  server_addr_;
};

#endif /* __PKT_DEFINITION_H__ */
