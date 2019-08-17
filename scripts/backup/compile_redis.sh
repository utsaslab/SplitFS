#!/bin/bash

set -x

redis_path=`readlink -f ../redis-4.0.10`
root_path=$(pwd)/..

cd $redis_path
make distclean
cd deps
make hiredis jemalloc linenoise lua geohash-int
cd ..
make install

cd $root_path
