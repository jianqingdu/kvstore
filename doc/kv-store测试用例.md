首先根据[README](../README.md)文档搭建测试环境 

### 正常测试流程
#### 1. 通过测试程序test_all来测试proxy所支持的Redis命令集是否符合期望
运行命令: ./test_all -h 127.0.0.1 -p 7400

如果某个接口有问题，会有红色报错信息，然后退出测试程序，如果接口通过测试，则显示绿色信息，最后显示 PASSED ALL TEST

#### 2. 测试扩容和缩容
* 注入测试数据: ./redis-benchmark -p 7400 -d 64 -r 10000000 -n 300000 -t set 
	
* 进行扩容: ./migration_tool --cs 127.0.0.1:7200 --add 127.0.0.1:6375 --ns test
	
	扩容后用下面的命令查看是否有大概1/3的数据迁移到新的Redis，
	
	./redis-cli -p 6375 info keyspace
	
* 进行缩容: ./migration_tool --cs 127.0.0.1:7200 --del 127.0.0.1:6375 --ns test

	缩容后查看127.0.0.1:6375上的数据是否都迁移到其他的Redis
	
	./redis-cli -p 6375 info keyspace

可以用脚本**scale_test.sh**测试上面的流程，通过显示绿色信息，不通过显示红色信息

#### 3. 扩容和缩容时有其他Redis请求测试
同时启动以下三个测程序

* ./scale_test.sh
* ./redis-benchmark -p 7400 -d 64 -r 10000000 -n 2000000 -t set
* ./test_all -l 10000 > output.txt

如果test_all生成的输出文件output.txt显示PASS ALL TEST，则测试通过

#### 4. HA测试
* kill 127.0.0.1:6371的Redis进程
* 等待30秒，cs会通知proxy更新bucket对照表，通过查看cs1/table/test对照表文件，127.0.0.1:6372替换了127.0.0.1:6371，并且变成为主

#### 5. 测试proxy和config_server的HTTP接口
1. 重置proxy统计信息

		curl http://127.0.0.1:7500/kvstore/proxy/reset_stats
	
		期望返回 
		{
   			"code" : 1001,
   			"msg" : "OK"
		}	
	
2. 获取proxy统计信息

		注入数据: ./redis-benchmark -p 7400 -d 64 -r 10000000 -n 100000 -t set
		
		curl http://127.0.0.1:7500/kvstore/proxy/stats_info
	
		期望返回 
		{
   			"client_count" : 0,
   			"cmds" : {
      			"SET" : 100000
   			},
   			"code" : 1001,
   			"msg" : "OK",
   			"slow_cmd_count" : 2,
   			"total_cmd_count" : 100000
		}

		备注: "slow_cmd_count"的值可能每次测试都不一样

3. 创建bucket对照表
	
	备注：该命令在运行./kv_store.sh start时已经使用了，不用单独测试
	 
4. 获取业务名列表

		curl http://127.0.0.1:7000/kvstore/cs/namespace_list

		期望返回 
		{
   			"code" : 1001,
   			"msg" : "OK",
   			"namespace_list" : [ "test" ]
		}

5. 获取对照表信息

		curl -d '{"namespace": "test"}' http://127.0.0.1:7000/kvstore/cs/bucket_table
	
		期望返回对照表信息，该信息应该和cs/table/test文件信息基本一致

6. 获取某个业务方的所有proxy信息
	
		curl -d '{"namespace": "test"}' http://127.0.0.1:7000/kvstore/cs/proxy_info
	
		期望返回 
		{
   			"code" : 1001,
   			"msg" : "OK",
   			"proxy_info" : [
      			{
         			"info" : "0.0.0.0:7400",
         			"state" : "ONLINE"
      			}
   			]
		}
  
7. 替换一个Redis服务器的地址

		curl -d '{"namespace": "test", "old_addr":"127.0.0.1:6373", "new_addr":"127.0.0.1:6374"}' http://127.0.0.1:7000/kvstore/cs/replace_server
	
		期望返回 
		{
			"code" : 1001, 
			"msg" : "OK"
		} 
		同时cs/table/test里面的地址127.0.0.1:6371应该变成127.0.0.1:6372

8. 删除一个namespace

		curl -d '{"namespace":"test"}' http://127.0.0.1:7000/kvstore/cs/del_namespace
	
		期望返回
		{
			"code" : 1001, 
			"msg" : "OK"
		}
		同时cs/table/test文件被删除, kv_store_proxy进程退出

可以通过**http_test.sh**测试以上几个接口

#### 6. cache cluster模式的测试
* ./kv_store.sh stop关闭data_store模式的测试集群
* 修改cs1, cs2下的配置文件，"cluster_type"设置为“cache”模式
* ./kv_store.sh start开启测试集群
* 扩容测试 curl -d '{"namespace":"test", "action":"add", "addr":"127.0.0.1:6375"}' http://127.0.0.1:7000/kvstore/cs/scale_cache_cluster 
* ./redis-benchmark -p 7400 -d 64 -r 10000000 -n 120000 -t set
* ./redis-cli -p 6375 info keyspace 期望返回大概4万左右的key
* kill 127.0.0.1:6375的redis进程，等待30秒，cs会把该redis从bucket对照表删除
* ./redis-benchmark -p 7400 -d 64 -r 10000000 -n 120000 -t set
* ./redis-cli -p 6373 info keyspace 期望返回大概10万左右的key

从第4步开始，可以用**cache_cluster_test.sh**来测试这些步骤

#### 7. key rewrite测试
* 启动集群后，运行./key_rewrite_test.sh


### 性能测试流程
通过redis-benchmark压测性能，通过比较上个版本的压测数据，测试是否符合性能期望
	
### 异常测试流程
* 所有config_server进程挂了，整个集群仍能提供正常服务，但不能添加proxy
* 一个业务的多个proxy进程中挂了一个，对该业务仍然没有影响

### 网络分区测试
* 设置好双备份测试环境，包括两个proxy和两个主redis，设置iptables规则，让一个proxy到一个redis的连接断开，待proxy上报redis宕机状态后，kill该proxy。设置iptables规则，让proxy到redis的连接可用。最后再启动proxy，设置iptables规则，等到proxy再次上报redis宕机状态后，检查是否会更新路由表，如果没有更新了路由表，测试通过，否则，测试没有通过
* 设置好双备份测试环境，包括两个proxy和两个主redis，设置iptables规则，让一个proxy到一个redis的连接断开，待proxy上报redis宕机状态后，再次设置iptables规则，让proxy到redis的连接正常，设置iptables规则让另一个proxy2和该redis的连接断开，等proxy2上报redis宕机状态后，检查是否会更新路由表，如果没有更新路由表，测试通过，否则，测试没有通过
* 设置好双备份测试环境，包括两个proxy和两个主redis，设置iptables规则，让一个proxy到一个redis的连接断开，等待proxy的日志显示proxy到redis的连接出现broken状态，一直在重连为止(一般需要15分钟以上)，然后设置iptables规则，让proxy到redis的连接可用，然后过一会检查RT是否恢复，如果恢复则测试通过，否则测试没有通过


