result=`curl -s http://127.0.0.1:7500/kvstore/proxy/reset_stats|grep OK|wc -l`
if [ $result -eq 1 ]; then
	echo -e "\033[48;32m reset_stats PASSED \033[0m"
else 
	echo -e "\033[48;31m reset_stats FAILED \033[0m"
fi

./redis-benchmark -q -p 7400 -d 64 -r 10000000 -n 100000 -t set
result=`curl -s http://127.0.0.1:7500/kvstore/proxy/stats_info|grep 10000|wc -l`
if [ $result -eq 2 ]; then
    echo -e "\033[48;32m stats_info PASSED \033[0m"
else
    echo -e "\033[48;31m stats_info FAILED \033[0m"
fi

result=`curl -s http://127.0.0.1:7000/kvstore/cs/namespace_list|grep test|wc -l`
if [ $result -eq 1 ]; then
    echo -e "\033[48;32m namespace_list PASSED \033[0m"
else
    echo -e "\033[48;31m namespace_list FAILED \033[0m"
fi

result=`curl -s -d '{"namespace": "test"}' http://127.0.0.1:7000/kvstore/cs/bucket_table|grep OK|wc -l`
if [ $result -eq 1 ]; then
    echo -e "\033[48;32m bucket_table PASSED \033[0m"
else
    echo -e "\033[48;31m bucket_table FAILED \033[0m"
fi

result=`curl -s -d '{"namespace": "test"}' http://127.0.0.1:7000/kvstore/cs/proxy_info|grep OK|wc -l`
if [ $result -eq 1 ]; then
    echo -e "\033[48;32m proxy_info PASSED \033[0m"
else
    echo -e "\033[48;31m proxy_info FAILED \033[0m"
fi

result=`curl -s -d '{"namespace": "test", "old_addr":"127.0.0.1:6373", "new_addr":"127.0.0.1:6374"}' http://127.0.0.1:7000/kvstore/cs/replace_server|grep OK|wc -l`
if [ $result -eq 1 ]; then
    echo -e "\033[48;32m replace_server PASSED \033[0m"
else
    echo -e "\033[48;31m replace_server FAILED \033[0m"
fi

result=`curl -s -d '{"namespace": "test"}' http://127.0.0.1:7000/kvstore/cs/del_namespace|grep OK|wc -l`
if [ $result -eq 1 ]; then
    echo -e "\033[48;32m del_namespace PASSED \033[0m"
else
    echo -e "\033[48;31m del_namespace FAILED \033[0m"
fi
