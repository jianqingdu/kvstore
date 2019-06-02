### 1. 单备份集群(cache)扩容方法

* 启动redis进程
* 用一个http请求扩容, 假设redis监听地址是10.11.6.204:6370，naespace是test，cs的http地址是10.11.6.204:6000, 那么命令是 

```
curl -d '{"namespace":"test", "action":"add", "addr":"10.11.6.204:6370"}' 

http://10.11.6.204:6000/kvstore/cs/scale_cache_cluster 
```
* 缩容的话，action参数是del

需要注意的是，单备份扩缩容时，由于bucket对照表的重建，会丢失部分数据，比如如果原来有2个redis，添加一个redis，那么会丢失1/3的数据

### 2. 双备份集群(data-store)扩容方法
* 启动redis主备进程
* 用migration_tool工具扩容，假设扩容的主redis地址是10.11.6.204:6370，naespace是test，cs的迁移地址是10.11.6.204:7200, 那么命令是

```
./migration_tool --cs 10.11.6.204:7200 --add 10.11.6.204:6370 --ns test
```
* 一般先可以迁移一个bucket试试是否一切正常，添加参数--count 1
然后因为迁移比较耗时，需要用nohup命令让迁移进程在后台执行，最好的迁移命令大概是下面的这个样子

```
nohup unbuffer ./migration_tool --cs 10.11.6.204:7200 --add 10.11.6.204:6370 --ns test > output.log 2>&1 &
```

双备份集群迁移时，不会丢失数据，而且对于用户透明，迁移时仍能提供正常服务

### 3. 用redis_port工具迁移数据方法
redis_port可以用于把数据从redis迁移到kv-store，或者从一个kv-store迁移到另一个kv-store
可以用./redis_port --help查看使用参数

* 从redis迁到kv-store，假如要从redis 10.11.6.203:6379迁移到kv-store的proxy 10.11.6.203:6400, 那么命令是

```
./redis_port --src_addr 10.11.6.203:6379 --dst_addr 10.11.6.203:6400
```

* 从kv-store迁到kv-store，需要对源kv-store的每个redis迁移数据，而且由于kvstore会对key加上bucketId_的前缀，需要加上--prefix '_' 选项，表示对key去掉第一个下划线前面的数据

```
./redis_port --src_addr 10.11.6.203:6379 --dst_addr 10.11.6.203:6400 --prefix '_' 
```

由于迁移会耗时比较长一般也要用nohup命令让迁移命令在后台运行

