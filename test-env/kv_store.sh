#
# 用于测试环境搭建./kvstore.sh start|stop
#
start() {
	for ((i=1;i<7;i++)) {
		cd redis$i
		./redis-server ./redis.conf
		cd ..		
	}

	cd cs1
	./config_server -d
	cd ../cs2
	./config_server -d

	sleep 1
	curl -d '{"namespace": "test", "bucket_count": 1023, "address_list": ["127.0.0.1:6371", "127.0.0.1:6373"]}' http://127.0.0.1:7000/kvstore/cs/init_table
	sleep 1

	cd ../proxy
	./kv_store_proxy -d
}

stop() {
	for ((i=1;i<7;i++)) {
        cd redis$i
        kill `cat redis.pid`
        cd ..
    }

	sleep 1
	for ((i=1;i<7;i++)) {
        cd redis$i
        rm -f redis.log dump.rdb
        cd ..
    }

	cd cs1
	kill `cat server.pid`
	rm -fr log table server.pid

	cd ../cs2
	kill `cat server.pid`
    rm -fr log table server.pid
	
	cd ../proxy
	kill `cat server.pid`
    rm -fr log server.pid
}

case $1 in
	start)
		start
		;;
	stop)
		stop
		;;
	*)
		echo "usage: ./kv_store.sh (start|stop)"
		;;
esac

