#!/bin/sh
#
# 线上kv_store_proxy进程操作脚本(启动/停止/重启某个版本)
#
start() {
    path=`pwd`
    $path/kv_store_proxy -d
}

stop() {
    kill `cat server.pid`
    rm -fr server.pid log
}

restart() {
    kill `cat server.pid`
    rm -fr server.pid log
    cp ../../../kvstore-$1/proxy/kv_store_proxy ./
    path=`pwd`
    $path/kv_store_proxy -d
}

case $1 in
    start)
        start
        ;;
    stop)
        stop
        ;;
    restart)
		if [ $# != 2 ]; then 
			echo "missing version number: $0 restart version"
			exit
		fi
        restart $2
        ;;
    *)
        echo "usage: $0 start|stop|restart version"
        ;;
esac

