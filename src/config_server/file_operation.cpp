/*
 * file_operation.cpp
 *
 *  Created on: 2016-5-23
 *      Author: ziteng
 */

#include <dirent.h>
#include "file_operation.h"
#include "simple_log.h"
#include "json/json.h"
#include "namespace_manager.h"
#include "table_transform.h"
#include "config_parser.h"
#include "common.h"

bool load_bucket_tables()
{
    DIR* dirp = opendir(g_config.bucket_table_path.c_str());
    if (!dirp) {
        log_message(kLogLevelError, "opendir table path failed: errno=%d\n", errno);
        return false;
    }
    
    dirent* dp = NULL;
    while ((dp = readdir(dirp)) != NULL) {
        if (dp->d_type != DT_REG) {
            continue;
        }
        
        string biz_namesapce = dp->d_name;
        string table_path = g_config.bucket_table_path + dp->d_name;
        
        string file_content;
        if (get_file_content(table_path.c_str(), file_content)) {
            log_message(kLogLevelError, "get_file_content failed, file=%s\n", dp->d_name);
            closedir(dirp);
            return false;
        }
        
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(file_content, root)) {
            log_message(kLogLevelError, "json format invalid, file=%s\n", dp->d_name);
            closedir(dirp);
            return false;
        }
        
        if (!root["table"].isObject() || !root["version"].isUInt() ||
            !root["migrating_bucket_id"].isUInt() || !root["migrating_server_addr"].isString()) {
            log_message(kLogLevelError, "json item missing, file=%s\n", dp->d_name);
            closedir(dirp);
            return false;
        }
        
        map<string, vector<uint16_t>> server_buckets;
        uint32_t version = root["version"].asUInt();
        uint16_t migrating_bucket_id = root["migrating_bucket_id"].asUInt();
        string migrating_server_addr = root["migrating_server_addr"].asString();
        vector<string> servers = root["table"].getMemberNames();
        for (const string& server : servers) {
            if (!root["table"][server].isArray()) {
                log_message(kLogLevelError, "server addr is not vector, file=%s\n", dp->d_name);
                closedir(dirp);
                return false;
            }
            
            vector<uint16_t> buckets;
            Json::Value& buckets_vec = root["table"][server];
            int cnt = buckets_vec.size();
            for (int i = 0; i < cnt; ++i) {
                uint16_t bucket_id = buckets_vec[i].asUInt();
                buckets.push_back(bucket_id);
            }
            
            server_buckets[server] = buckets;
        }
        
        map<uint16_t, string> bucket_map = transform_table(server_buckets);
        if (!g_namespace_manager.InitTable(biz_namesapce, version, bucket_map, migrating_bucket_id,
                                           migrating_server_addr, false)) {
            log_message(kLogLevelError, "InitTable failed: file=%s\n", dp->d_name);
            closedir(dirp);
            return false;
        }
        
        log_message(kLogLevelInfo, "load %s ok\n", dp->d_name);
    }
    
    closedir(dirp);
    return true;
}

bool save_bucket_table(const string& ns, uint32_t version, const map<uint16_t, string>& bucket_map,
                       uint16_t migrating_bucket_id, const string& migrating_server_addr)
{
    Json::Value root;
    jsonize_bucket_table(root, version, bucket_map, migrating_bucket_id, migrating_server_addr);
    
    Json::FastWriter writer;
    string content = writer.write(root);
    
    string save_path = g_config.bucket_table_path + ns;
    FILE* fp = fopen(save_path.c_str(), "wb");
    if (!fp) {
        log_message(kLogLevelError, "save bucket table failed, open file failed: %s\n", save_path.c_str());
        return false;
    }
    
    size_t ret = fwrite(content.c_str(), 1, content.size(), fp);
    fclose(fp);
    
    if (ret == content.size()) {
        log_message(kLogLevelInfo, "save bucket table success: namespace=%s\n", ns.c_str());
        return true;
    } else {
        log_message(kLogLevelError, "save bucket table failed: ns=%s, content=%s\n", ns.c_str(), content.c_str());
        return false;
    }
}

void del_bucket_table(const string& ns)
{
    string table_path = g_config.bucket_table_path + ns;
    if (remove(table_path.c_str())) {
        log_message(kLogLevelError, "delete file ns=%s failed\n", ns.c_str());
    } else {
        log_message(kLogLevelInfo, "delete file ns=%s success\n", ns.c_str());
    }
}
