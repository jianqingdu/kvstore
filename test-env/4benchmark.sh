if [ $# -ne 3 ]; then
	echo $0 port clients cmd
	exit
fi

port=$1
clients=$2
cmd=$3
./redis-benchmark -p $port -c $clients -n 500000 -r 10485760 -d 64 -t $cmd &
./redis-benchmark -p $port -c $clients -n 500000 -r 10485760 -d 64 -t $cmd &
./redis-benchmark -p $port -c $clients -n 500000 -r 10485760 -d 64 -t $cmd &
./redis-benchmark -p $port -c $clients -n 500000 -r 10485760 -d 64 -t $cmd &
