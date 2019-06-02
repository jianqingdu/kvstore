# data injection
./redis-benchmark -p 7400 -d 64 -r 10000000 -n 600000 -t set

# scale up
echo "scale up migration"

passwd='kvstore123'

./migration_tool --cs 127.0.0.1:7200 --add 127.0.0.1:6375 --password $passwd --ns test --wait_ms 100 > /dev/null
./redis-cli -p 6371 -a $passwd info keyspace
./redis-cli -p 6375 -a $passwd info keyspace

# if key count difference between 6371 and 6375 is less than 1/10 of total, we believe migration is successful
v1=$(./redis-cli -p 6371 -a $passwd info keyspace|grep keys|awk -F, '{print $1}'|awk -F= '{print $2}')
v2=$(./redis-cli -p 6375 -a $passwd info keyspace|grep keys|awk -F, '{print $1}'|awk -F= '{print $2}')
echo keys of 6371: $v1
echo keys of 6375: $v2
let diff=v1-v2
if [ $diff -lt 0 ]; then
    let diff=-diff
fi
echo difference between 6371 and 6375: $diff

let v3=v1/10
if [ $diff -lt $v3 ]; then
    echo -e "\033[48;32m SCALE UP PASSED \033[0m"
else
    echo -e "\033[48;31m SCALE UP FAILED \033[0m"
fi

# scale down
echo 
echo "scale down migration"

./migration_tool --cs 127.0.0.1:7200 --del 127.0.0.1:6375 --password $passwd  --ns test --wait_ms 100 > /dev/null
key_count=`./redis-cli -p 6375 -a $passwd info keyspace|grep keys|wc -l`
if [ $key_count -eq 0 ]; then
    echo -e "\033[48;32m SCALE DOWN PASSED \033[0m"
else
    echo -e "\033[48;31m SCALE DOWN FAILED \033[0m"
fi

