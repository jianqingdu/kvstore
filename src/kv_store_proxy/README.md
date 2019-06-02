### 主要功能点：

```
	1. 以长连接的方式连接到ConfigServer，获取bucket对照表
	2. 维持到每个shard的主Redis服务器的连接池
	3. 接收来自ConfigServer的Redis服务器变更，如果是主备变更，则需要修改Redis连接池，
	如果是Redis扩容或缩容的变更，还需要配合数据迁移方案，如果某个bucket正在迁移，
	则对所有这个bucket的key的操作，需要先用migrate命令把key迁移到新的Redis服务器，
	然后在新Redis服务器再进行操作
	4. 接收Redis客户端请求，按key对请求做分片，bucket_id = mur_mur_hash(key) % 
	bucket_number，重写key为bucket_id_key, 然后把请求发给对应于bucket_id的Redis服务器
	5. QPS限流，请求大小限制
```
