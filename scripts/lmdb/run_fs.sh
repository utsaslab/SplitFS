#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Usage: sudo ./run_fs.sh <fs> <run_id>"
    exit 1
fi

set -x

workload=lmdb
fs=$1
run_id=$2
current_dir=$(pwd)
lmdb_dir=`readlink -f ../../lmdb`
workload_dir=$lmdb_dir/dbbench/bin
pmem_dir=/mnt/pmem_emul
boost_dir=`readlink -f ../../splitfs`
result_dir=`readlink -f ../../results`
fs_results=$result_dir/$fs/$workload

if [ "$fs" == "splitfs-posix" ]; then
    run_boost=1
elif [ "$fs" == "splitfs-sync" ]; then
    run_boost=1
elif [ "$fs" == "splitfs-strict" ]; then
    run_boost=1
else
    run_boost=0
fi

ulimit -c unlimited

run_workload()
{

    echo ----------------------- LMDB WORKLOAD ---------------------------

    mkdir -p $fs_results
    rm $fs_results/run$run_id

    rm -rf $pmem_dir/*

    date

    if [ $run_boost -eq 1 ]; then
        $boost_dir/run_boost.sh -p $boost_dir -t nvp_nvp.tree $workload_dir/t_lmdb --benchmarks="fillseqbatch" --num=1000000 --value_size=1024 --batch=100 2>&1 | tee $fs_results/run$run_id
    else
	$workload_dir/t_lmdb --benchmarks="fillseqbatch" --num=1000000 --value_size=1024 --batch=100 2>&1 | tee $fs_results/run$run_id
    fi

    date

    cd $current_dir
}

run_workload

cd $current_dir
