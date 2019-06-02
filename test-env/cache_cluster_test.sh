echo "1. scale up cache cluster"
curl -d '{"namespace":"test", "action":"add", "addr":"127.0.0.1:6375"}' http://127.0.0.1:7000/kvstore/cs/scale_cache_cluster 
sleep 1

echo "2. inject 120000 keys to cluster to test if bucket table changed"
./redis-benchmark -p 7400 -d 64 -r 10000000 -n 120000 -t set


echo "3. caculate key count expection"
# if key count difference between 6371 and 6375 is less than 1/10 of total, we believe migration is successful
v1=$(./redis-cli -p 6375 -a kvstore123 info keyspace|grep keys|awk -F , '{print $1}'|awk -F = '{print $2}')
echo keys of 6375: $v1
let diff=40000-v1
if [ $diff -lt 0 ]; then
    let diff=-diff
fi
echo difference between 6375 and expected: $diff

let v2=v1/10
if [ $diff -lt $v2 ]; then
    echo -e "\033[48;32m SCALE UP PASSED \033[0m"
else
    echo -e "\033[48;31m SCALE UP FAILED \033[0m"
fi

echo "4. kill a redis 6375 to test HA"
kill `cat redis5/redis.pid`
sleep 32

echo "5. inject another 120000 keys to test if bucket table changed successfully"
./redis-benchmark -p 7400 -d 64 -r 10000000 -n 120000 -t set

echo "6. caculate key count expection"
v1=$(./redis-cli -p 6371 -a kvstore123 info keyspace|grep keys|awk -F , '{print $1}'|awk -F = '{print $2}')
echo keys of 6371: $v1
let diff=100000-v1
if [ $diff -lt 0 ]; then
    let diff=-diff
fi
echo difference between 6371 and expected: $diff

let v2=v1/10
if [ $diff -lt $v2 ]; then
    echo -e "\033[48;32m HA PASSED \033[0m"
else
    echo -e "\033[48;31m HA FAILED \033[0m"
fi


