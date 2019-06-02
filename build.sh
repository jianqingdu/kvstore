build () {
	cd src
	cd base
	make ver=release -j
	cd ../config_server
	make ver=release version=$1 -j
	cd ../kv_store_proxy
	make ver=release version=$1 -j
	cd ../migration_tool
	make ver=release version=$1 -j
	cd ../redis_port
	make ver=release version=$1 -j
	cd ../redis_proxy 
	make ver=release version=$1 -j
	cd ../test
	make -j

	cd ../
	cp config_server/config_server ../test-env/cs1/
	cp config_server/config_server ../test-env/cs2/
	cp kv_store_proxy/kv_store_proxy ../test-env/proxy/
	cp migration_tool/migration_tool ../test-env/
	cp redis_port/redis_port ../test-env/
	cp test/test_all ../test-env/
	
	cd ../test-env
	./build_redis.sh

	cd ../
	cp -fr test-env kvstore-$1
	rm -f kvstore-$1/redis-3.2.8.tar.gz
	rm -f kvstore-$1/build_redis.sh
	tar zcvf kvstore-$1.tar.gz kvstore-$1
	rm -fr kvstore-$1
}

clean() {
	cd src
	cd 3rd_party/hiredis
	make clean
	cd ../jsoncpp
	make clean
	cd ../../base
	make clean
	cd ../config_server
	make clean
	cd ../kv_store_proxy
	make clean
	cd ../migration_tool
	make clean
	cd ../redis_port
	make clean
	cd ../redis_proxy
	make clean
	cd ../test
	make clean
}

case $1 in
	clean)
		clean
		;;
	version)
		build $2
		;;
	*)
		echo "usage: "
		echo "  build version version-number"
		echo "  build clean"
		;;
esac

