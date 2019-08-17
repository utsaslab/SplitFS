#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Usage: sudo ./run_fs.sh <fs> <run_id>"
    exit 1
fi

set -x

workload=rsync
fs=$1
run_id=$2
src_dir=`readlink -f ../../`
cur_dir=`readlink -f ./`
rsync_dir=$src_dir/rsync
workload_dir=$rsync_dir/workload
pmem_dir=/mnt/pmem_emul
boost_dir=$src_dir/splitfs
result_dir=$src_dir/results
fs_results=$result_dir/$fs/$workload

if [ "$fs" == "boost" ]; then
    run_boost=1
elif [ "$fs" == "sync_boost" ]; then
    run_boost=1
elif [ "$fs" == "posix_boost" ]; then
    run_boost=1
else
    run_boost=0
fi

ulimit -c unlimited

echo Sleeping for 5 seconds . . 
sleep 5

run_workload()
{

    echo ----------------------- RSYNC WORKLOAD ---------------------------

    mkdir -p $fs_results
    rm $fs_results/run$run_id

    rm -rf $pmem_dir/*
    cp -r $workload_dir/src $pmem_dir && sync

    cd $pmem_dir
    mkdir dest

    if [ $run_boost -eq 1 ]; then
        export LD_LIBRARY_PATH=$src_dir/splitfs-so/rsync/sync
        export NVP_TREE_FILE=$boost_dir/bin/nvp_nvp.tree
    fi

    sleep 5

    date

    if [ $run_boost -eq 1 ]; then
        ( time LD_PRELOAD=$src_dir/splitfs-so/rsync/sync/libnvp.so $rsync_dir/rsync -Wr ./src dest/ ) 2>&1 | tee $fs_results/run$run_id
    else
        ( time $rsync_dir/rsync -Wr ./src dest/ ) 2>&1 | tee $fs_results/run$run_id
    fi

    date

    cd $current_dir

    echo Sleeping for 5 seconds . .
    sleep 5

}


run_workload

cd $cur_dir
