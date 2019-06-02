### 1. 主要功能点:

```
	1. 多namespace的支持，但每个redis只支持一个namespace，
	这样可以做到业务隔离和基于各自业务的统计
	2. 提供每个namespace的bucket对照表
	3. 将某个namespace下的bucket变更通知给该namespace下的每个KvStoreSDK
	4. 配合MigrationTool来进行扩容和缩容
	5. 提供HTTP后台接口方便管理
	6. 提供主备功能防止单点故障
```

### 2. 主备功能

ConfigServer支持一主多备

ConfigServer唯一需要持久化保持的数据是每个namespace的bucket对照表信息，对照表更新的来源有两个，一个是HTTP请求初始化对照表，另一个是MigrationTool对一个namespace下的一个bucket的更新，只有主ConfigServer能接收对照表更新请求，备ConfigServer需要拒绝更新请求

更新对照表流程如下：

```
	1. 主ConfigServer接收到bucket对照表更新请求
	2. 主ConfigServer更新对照表，如果更新失败，则直接返回失败信息给请求者，退出更新流程
	3. 主ConfigServer更新成功，返回更新成功信息给请求者
	4. 如果有备ConfigServer，异步更新到所有备ConfigServer
	5. 如果更新失败，肯定需要人工排除原因，是网络故障，硬盘满了，还是软件故障
```

ConfigServer的主备是由配置文件决定的，只有主备同时功能正常，才能提供更新对照表服务，如果主挂了，则备只能提供读取对照表服务。

各种异常情况分析

``` 
	1. 主进程挂了或者主所在的服务器宕机，备检查到主的连接断了，而且重连不上主，这时备提供
	读取对照表服务，proxy启动时，连接主失败，则连接备，获取bucket对照表信息，对于已经启动
	的proxy，则不会再连接备，因为主宕机前的对照表信息都有
	2. 备进程挂了或者备所在的服务器宕机，则主不受影响，继续可以提供读取和更新对照表服务
	3. 主备直接的网络断开，和情况2一样
	4. 部分proxy到主的网络断开，只能用备的信息，由于主备同步更新状态也没有问题
	5. 部分proxy到主的网络断开，并且主备之间的网络也断开，这时如果更新对照表，会有脑裂问题
	发生，对于这种情况应该报警，人工干预修复问题
	
	
```
