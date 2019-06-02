if [ -f ./redis1/redis-server ]; then
    echo "redis-server already exist"
	exit
fi

version=3.2.8
tar zxvf redis-$version.tar.gz
cd redis-$version
make -j

for ((i=1;i<7;i++)) {
	cp src/redis-server ../redis$i/
}

cp -f src/redis-cli src/redis-benchmark ../

cd ..
rm -fr redis-$version
