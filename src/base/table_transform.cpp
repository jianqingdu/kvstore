/*
 *  table_transform.cpp
 *
 *  Created on: 2016-5-18
 *      Author: ziteng
 */

#include <algorithm>
#include "table_transform.h"

bucket_server_map_t transform_table(server_buckets_map_t old_map)
{
    bucket_server_map_t new_map;
    
    for (auto it_server = old_map.begin(); it_server != old_map.end(); ++it_server) {
        string server_addr = it_server->first;
        
        for (uint16_t bucket_id : it_server->second) {
            new_map[bucket_id] = server_addr;
        }
    }
    
    return new_map;
}

server_buckets_map_t transform_table(bucket_server_map_t old_map)
{
    server_buckets_map_t new_map;
    
    for (auto it = old_map.begin(); it != old_map.end(); ++it) {
        new_map[it->second].push_back(it->first);
    }
    
    return new_map;
}


void fetch_servers(const server_buckets_map_t& server_buckets_map, set<string>& servers)
{
    for (auto it = server_buckets_map.begin(); it != server_buckets_map.end(); ++it) {
        servers.insert(it->first);
    }
}

static void calculate_server_cnt_map(uint16_t bucket_cnt, const string& new_server_addr, const set<string>& servers,
                              map<string, uint16_t>& server_cnt_map)
{
    uint16_t server_cnt = (uint16_t)servers.size();
    uint16_t average_cnt = bucket_cnt / server_cnt;
    uint16_t remain_cnt = bucket_cnt % server_cnt;
    
    for (const string& server : servers) {
        if (server == new_server_addr) {
            server_cnt_map[server] = average_cnt;
        } else {
            server_cnt_map[server] = average_cnt;
            if (remain_cnt > 0) {
                server_cnt_map[server]++;
                remain_cnt--;
            }
        }
    }
}

void scale_up_table(const string& new_server_addr, uint16_t bucket_cnt,
                    const server_buckets_map_t& old_map, server_buckets_map_t& new_map)
{
    new_map = old_map;
    if (new_map.find(new_server_addr) == new_map.end()) {
        // join the new server address to the map, empty the server's bucket vector
        vector<uint16_t> buckets;
        new_map[new_server_addr] = buckets;
    }
    
    // fetch all server address
    set<string> servers;
    fetch_servers(new_map, servers);
    
    // calculate bucket count that every server need to hold the data
    map<string, uint16_t> server_cnt_map;
    calculate_server_cnt_map(bucket_cnt, new_server_addr, servers, server_cnt_map);
    
    // iterate every server, move exceed bucket to new server
    vector<uint16_t>& new_server_buckets = new_map[new_server_addr];
    for (auto it = new_map.begin(); it != new_map.end(); ++it) {
        if (it->first == new_server_addr)
            continue;
        
        uint16_t remain_cnt = server_cnt_map[it->first];
        while (it->second.size() > remain_cnt) {
            uint16_t bucket_id = it->second.back();
            it->second.pop_back();
            new_server_buckets.push_back(bucket_id);
        }
    }
    
    std::sort(new_server_buckets.begin(), new_server_buckets.end());
}

void scale_down_table(const string del_server_addr, uint16_t bucket_cnt,
                      const server_buckets_map_t& old_map, server_buckets_map_t& new_map)
{
    if (old_map.find(del_server_addr) == old_map.end()) {
        fprintf(stderr, "no this redis server in the old bucket table: %s\n", del_server_addr.c_str());
        return;
    }
    
    if (old_map.size() == 1) {
        fprintf(stderr, "only one redis server left, can not delete any more\n");
        return;
    }
    
    new_map = old_map;
    vector<uint16_t> migration_buckets = new_map[del_server_addr];
    printf("migration_buckets = %d\n", (int)migration_buckets.size());
    
    new_map.erase(del_server_addr);
    
    // fetech all server address
    set<string> servers;
    fetch_servers(new_map, servers);
    
    // calculate bucket count that every server need to hold the data
    map<string, uint16_t> server_cnt_map;
    calculate_server_cnt_map(bucket_cnt, del_server_addr, servers, server_cnt_map);
    
    // iterate every server, move buckets in the removed server to other server  
    for (auto it = new_map.begin(); it != new_map.end(); ++it) {
        string server_addr = it->first;
        uint16_t new_bucket_cnt = server_cnt_map[server_addr];
        while (it->second.size() < new_bucket_cnt) {
            if (migration_buckets.empty()) {
                fprintf(stderr, "migration_buckets empty\n");
                break;
            }
            
            uint16_t bucket_id = migration_buckets.back();
            migration_buckets.pop_back();
            it->second.push_back(bucket_id);
        }
        
        std::sort(it->second.begin(), it->second.end());
    }
    
    if (!migration_buckets.empty()) {
        fprintf(stderr, "not reblance all bucket, some terrible thing happened\n");
        new_map.clear();
    }
}

