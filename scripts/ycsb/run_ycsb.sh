#!/bin/bash

src_dir=`readlink -f ../../`
cur_dir=`readlink -f ./`
setup_dir=`readlink -f ../configs`
pmem_dir=/mnt/pmem_emul

run_ycsb()
{
    fs=$1
    for run in 1
    do
        sudo rm -rf $pmem_dir/*
        sudo taskset -c 0-7 ./run_fs.sh LoadA $fs $run
        sleep 5
        sudo taskset -c 0-7 ./run_fs.sh RunA $fs $run
        sleep 5
        sudo taskset -c 0-7 ./run_fs.sh RunB $fs $run
        sleep 5
        sudo taskset -c 0-7 ./run_fs.sh RunC $fs $run
        sleep 5
        sudo taskset -c 0-7 ./run_fs.sh RunF $fs $run
        sleep 5
        sudo taskset -c 0-7 ./run_fs.sh RunD $fs $run
        sleep 5
        sudo taskset -c 0-7 ./run_fs.sh LoadE $fs $run
        sleep 5
        sudo taskset -c 0-7 ./run_fs.sh RunE $fs $run
        sleep 5
    done
}

sudo $setup_dir/dax_config.sh
run_ycsb dax

sudo $setup_dir/nova_relaxed_config.sh
run_ycsb relaxed_nova

sudo $setup_dir/pmfs_config.sh
run_ycsb pmfs

sudo $setup_dir/nova_config.sh
run_ycsb nova

sudo $setup_dir/dax_config.sh
run_ycsb boost

:'
sudo $setup_dir/dax_config.sh
run_ycsb_boost sync_boost

cd $setup_dir
sudo $setup_dir/nova_config.sh
cd $current_dir

sudo $setup_dir/dax_config.sh
run_ycsb_boost posix_boost
'
