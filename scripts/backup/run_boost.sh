#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Usage: sudo ./run_boost.sh <fs> <run_id>"
    exit 1
fi

set -x

workload=redis
fs=$1
run_id=$2
current_dir=$(pwd)
redis_dir=`readlink -f ../../redis-4.0.10`
workload_dir=$redis_dir/workload
pmem_dir=/mnt/pmem_emul
boost_dir=`readlink -f ../../boost-redis`
result_dir=`readlink -f ../../results`
fs_results=$result_dir/$fs/$workload

ulimit -c unlimited

echo Sleeping for 5 seconds . .
sleep 5

run_workload()
{

    echo ----------------------- REDIS WORKLOAD ---------------------------

    mkdir -p $fs_results
    rm $fs_results/run$run_id

    rm -rf $pmem_dir/* && sync

    sleep 5

    date

    $boost_dir/run_boost.sh -p $boost_dir -t nvp_nvp.tree $redis_dir/src/redis-server $redis_dir/redis.conf &
    sleep 2
    $redis_dir/src/redis-benchmark -t set -n 1000000 -d 1024 -c 1 -s /tmp/redis.sock 2>&1 | tee $fs_results/run$run_id

    date

    kill -9 `pgrep redis-server`
    cd $current_dir

    echo Sleeping for 5 seconds . .
    sleep 5

}


run_workload

cd $current_dir
