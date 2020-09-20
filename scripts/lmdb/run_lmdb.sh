#!/bin/bash

current_dir=$(pwd)
setup_dir=`readlink -f ../configs`
pmem_dir=/mnt/pmem_emul

run_lmdb()
{
    fs=$1
    for run in 1 2 3
    do
        sudo rm -rf $pmem_dir/*
        sudo taskset -c 0-7 ./run_fs.sh $fs $run
        sleep 5
    done
}

echo "-- ext4 DAX --"
cd $setup_dir
sudo ./nova_config.sh
cd $current_dir
sudo $setup_dir/dax_config.sh
run_lmdb dax

echo "-- NOVA Relaxed --"
cd $setup_dir
sudo ./nova_relaxed_config.sh
cd $current_dir
run_lmdb relaxed_nova

echo "-- NOVA --"
cd $setup_dir
sudo ./nova_config.sh
cd $current_dir
run_lmdb nova

echo "-- PMFS --"
cd $setup_dir
sudo $setup_dir/pmfs_config.sh
cd $current_dir
run_lmdb pmfs

echo "-- SplitFS POSIX --"
make splitfs.posix
sudo $setup_dir/dax_config.sh
run_lmdb splitfs-posix
