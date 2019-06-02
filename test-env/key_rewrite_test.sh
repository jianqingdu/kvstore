./redis-cli -p 7400 set aaa v1
./redis-cli -p 7400 set aab v2
./redis-cli -p 7400 set bbb v3

curl -d '{"pattern": "aa*"}' http://127.0.0.1:7500/kvstore/proxy/add_pattern

result=`./redis-cli -p 7400 get aaa`
if [ "$result" != "" ]; then
	echo "get aaa failed"
	exit
fi

result=`./redis-cli -p 7400 get aab`
if [ "$result" != "" ]; then
    echo "get aab failed"
	exit
fi

result=`./redis-cli -p 7400 get bbb`
if [ $result != "v3" ]; then
    echo "get bbb failed"
	exit
fi

./redis-cli -p 7400 set aaa v4
./redis-cli -p 7400 set aab v5

result=`./redis-cli -p 7400 get aaa`
if [ $result != "v4" ]; then
    echo "get aaa (v4) failed"
	exit
fi

result=`./redis-cli -p 7400 get aab`
if [ $result != "v5" ]; then
    echo "get aab (v5) failed"
	exit
fi

curl -d '{"pattern": "aa*"}' http://127.0.0.1:7500/kvstore/proxy/del_pattern

result=`./redis-cli -p 7400 get aaa`
if [ $result != "v1" ]; then
    echo "get aaa (v1) failed"
	exit
fi

result=`./redis-cli -p 7400 get aab`
if [ $result != "v2" ]; then
    echo "get aab (v2) failed"
	exit
fi

echo "all key rewrite test passed"

