
### 约定
* code = 1001表示成功
* XXX 表示数字
* “XXX” 表示字符串
* bool 表示布尔值true/false

### 1. ConfigServer HTTP接口

1. /kvstore/cs/init_table

		用于创建bucket对照表
		输入格式
		{
			"namespace": "XXX", 
			"bucket_count": XXX, 
			"address_list": ["ip:port"]
		}
		
		namespace: 业务名
		bucket_count: 对照表的bucket个数
		address_list: 集群部署的主redis地址列表(地址包括IP和port) 
		
		输出格式
		{"code": XXX, "msg": "XXX"}
	
2. /kvstore/cs/del_namespace

		用于删除一个业务的集群
		输入格式
		{"namespace": "xxx"}
		
		输出格式
		{"code": XXX, "msg": "XXX"}
		
3. /kvstore/cs/namespace_list

		用于查询该config_server管理的所以kv-store集群的业务名列表
		没有输入参数
		
		输出格式
		{
			"code": XXX, 
			"msg": "XXX", 
			"namespace_list": ["XXX", "XXX"]
		}
		
4. /kvstore/cs/bucket_table

		用于查询某个业务名的bucket对照表信息
		输入格式
		{"namespace": "XXX"}

		输出格式
		{
			"code": XXX, 
			"msg": "XXX", 
			"table": [{"ip:port": [XX, XX, XX]}], 
			"version": XXX,
			"migrating_bucket_id": XXX, 
			"migrating_server_addr": "XXX"
		}
		
5. /kvstore/cs/proxy_info

		用于查询某个业务的所有kv_store_proxy的信息
		输入格式
		{"namespace": "XXX"}
		
		输出格式
		{
			"code": XXX, 
			"msg": "XXX", 
			"proxy_info": [ 
				{
					"info": "ip:port", 		
					"state": "XXX"
				}
			]
		}
		
6. /kvstore/cs/replace_server

		用于替换后端Redis服务器地址
		输入格式
		{
			"namespace": "XXX", 
			"old_addr": "ip:port", 
			"new_addr": "ip:port"
		}
		
		namespace: 需要替换Redis地址的业务名
		old_addr: 旧的Redis地址
		new_addr: 新的Redis地址
		
		输出格式
		{"code": XXX, "msg": "XXX"}
		
7. /kvstore/cs/scale_cache_cluster

		只能在配置为cache模式的集群使用，用于扩容或缩容
		输入格式
		{
			"namespace": "XXX", 
			"action": "add/del", 
			"addr": "ip:port"
		}
		
		namespace: 业务名
		action:	add表示扩容，del表示缩容
		addr: 需要扩容或缩容的Redis地址
		
		输出格式
		{"code": XXX, "msg": "XXX"}
	
8. /kvstore/cs/set_config

		用于动态设置配置参数，目前只能设置一个配置参数
		{"support_ha": bool}
		
		support_ha: 表示是否支持HA，只能设置为true或者false，如果支持HA，
		在data_store模式，主Redis挂了，会自动进行主备切换，更新bucket对照表
		在cache模式，Redis挂了，会自动摘除该Redis，更新bucket对照表
				
		输出格式
		{"code": XXX, "msg": "XXX"}

### 2. KvStoreProxy HTTP接口

1. /kvstore/proxy/stats_info

		查询kv_store_proxy的统计信息
		没有输入参数
	
		输出格式
		{
			"code": XXX, 
			"msg": "XXX", 
			"average_rt": XXX, 
			"client_count": XXX, 
			"slow_cmd_count" : XXX, 
			"total_cmd_count" : XXX, 
			"cmds": {"XXX":XXX}
		}
	
		average_rt: 平均RT时间,单位毫秒
		client_count: 当前连接到kv_store_proxy的客户端个数
		slow_cmd_count: 慢请求计数器，慢请求的时间有配置参数决定
		total_cmd_count: 总请求计数器
		cmds: 细分到每个命令的请求计数器
	
2. /kvstore/proxy/reset_stats

		重置kv_store_proxy的统计信息
		没有输入参数
	
		输出格式
		{"code": XXX, "msg": "XXX"}
	
3. /kvstore/proxy/set_config

		动态设置配置参数
		输入格式，输入参数可以只设置需要设置的，不用全部都填写
		{
			"max_qps": XXX, 
			"max_client_num", XXX, 
			"slow_cmd_time": XXX, 
			"client_timeout", XXX,
			"redis_down_timeout": XXX, 
			"request_timeout": XXX,
			"update_slave_interval": XXX,
			"master_cs_ip": "XXX", 
			"master_cs_port": XXX, 
			"slave_cs_ip": "XXX", 
			"slave_cs_port": XXX,
			"redis_password": "XXX"
		}
		
		max_qps: 最大QPS限制
		max_client_num: 最大客户端并发连接数限制
		slow_cmd_time: 慢请求的时间，单位毫秒
		client_timeout: 客户端空闲断开连接时间，单位秒
		redis_down_timeout: Redis宕机超时时间，单位秒，kv_store_proxy每隔3秒ping一次
			Redis，如果在连续redis_down_timeout的时间都没有响应，则向config_server
			汇报Redis宕机信息
		request_timeout: 客户端请求超时时间，单位秒
		update_slave_interval: 更新主备Redis映射关系的时间间隔，单位秒
		master_cs_ip: 主CS地址
		master_cs_port: 主CS端口
		slave_cs_ip: 备CS地址
		slave_cs_port: 备CS端口
		redis_password: redis认证密码
	
		输出格式
		{"code": XXX, "msg": "XXX"}

4. /kvstore/proxy/bucket_table
	
		获取对照表信息
		
		无输入参数
		
		输出参数
		{
			"code": XXX, 
			"msg": "XXX", 
			"table": [{"ip:port": [XX, XX, XX]}], 
			"version": XXX,
			"migrating_bucket_id": XXX, 
			"migrating_server_addr": "XXX"
		}
	
5. /kvstore/proxy/add_pattern

		添加key_rewrite的pattern
		
		输入参数
		{
			"patten": "XXX"
		}
		
		输出参数
		{
			"code": XXX, 
			"msg": "XXX"
		}
		
6. /kvstore/proxy/del_pattern 

		删除key_rewrite的pattern
		
		输入参数
		{
			"patten": "XXX"
		}
		
		输出参数
		{
			"code": XXX, 
			"msg": "XXX"
		}

		