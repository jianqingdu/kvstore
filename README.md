# Kvstore
----------------------------------
Kvstore is a proxy based Redis cluster solution written in C++

## Why Another Redis Cluster Solution

There are three open source Redis cluster solution as we known, so why another? Here are the main pain points:

* ### Twemproxy 
	* can not resharding without restarting cluster 
	* use only one thread so can not use multiple core in modern server machine
	
* ### Codis 
	* write in go so there is some GC problem, this is undesirable for a cache service, cause most requests will be served in less than 1 millisecond
	* need to change Redis source code, it's painful to update to the latest Redis version
	* it used much more memory than the original Redis server, cause the key will be duplicated in the slot

* ### Redis Cluster 
	* need to update redis client driver
	* do not widely deployed in production
	* do not support commands with multiple keys. e.g. MSET, MGET
	* it also used too much memory caused by duplicated key in the slot


## Feature

* multiput thread to process client request
* pipeline support
* resharding without restarting cluster
* support most of redis commands, see [not-support-redis-cmd](doc/not-support-redis-cmd.md) for detailed commands
* support two mode: 
	* cache mode
	
	-- Used as a pure cache, with redis sharding but without replication, so it can save half memory. For high availability, Redis node will be removed from the sharding nodes when it is failed
	* data-store mode 
	
	-- Used as a data source, with redis sharding and replication. For high availability Slave Redis node will be promoted to master when the old master node failed

	
## Architecture

![](doc/kvstore-arch.png =500x500)


## Benchmark

Need to be done in the future

## How to Build

Kvstore can be compile under Linux with epoll and MacOS X with kqueue, kvstore need a C++ compiler with C++11 feature, no other dependencies are required. 

To compile the entire project, run the following commands

	$ ./build.sh version 1.0

This will generate a package named kvstore-1.0.tar.gz in the current directory


## How to Clean the Build


	$./build.sh clean

## How to Setup a Test Environment

Copy the build package to any directory, unzip it, run with command

	$ ./kv_store.sh start
	
If no error report in this command, then kvstore cluster is already runing in this machine, with ** one kv_store_proxy and two redis-server sharding **. Use test_all to test that everything is OK
	
	$ ./test_all 

After test, use this command to shutdown the cluster
	
	$ ./kv_store.sh stop  
	

## Have Fun!


