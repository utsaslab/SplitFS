#!/bin/bash

current_dir=$(pwd)
setup_dir=`readlink -f ..`
pmem_dir=/mnt/pmem_emul

run_ycsb()
{
    fs=$1
    for run in 1 2 3
    do
        sudo rm -rf $pmem_dir/*
        sudo ./run_fs.sh LoadA $fs $run
        sleep 5
        sudo ./run_fs.sh RunA $fs $run
        sleep 5
        sudo ./run_fs.sh RunB $fs $run
        sleep 5
        sudo ./run_fs.sh RunC $fs $run
        sleep 5
        sudo ./run_fs.sh RunF $fs $run
        sleep 5
        sudo ./run_fs.sh RunD $fs $run
        sleep 5
        sudo ./run_fs.sh LoadE $fs $run
        sleep 5
        sudo ./run_fs.sh RunE $fs $run
        sleep 5
    done
}

run_ycsb_boost()
{
    fs=$1
    for run in 1
    do
        sudo rm -rf $pmem_dir/*
        sudo ./run_boost.sh LoadA $fs $run
        sleep 5
        sudo ./run_boost.sh RunA $fs $run
        sleep 5
        sudo ./run_boost.sh RunB $fs $run
        sleep 5
        sudo ./run_boost.sh RunC $fs $run
        sleep 5
        sudo ./run_boost.sh RunF $fs $run
        sleep 5
        sudo ./run_boost.sh RunD $fs $run
        sleep 5
        sudo ./run_boost.sh LoadE $fs $run
        sleep 5
        sudo ./run_boost.sh RunE $fs $run
        sleep 5
    done
}

:'
sudo $setup_dir/dax_config.sh
run_ycsb dax_2M_new

cd $setup_dir
sudo ./nova_relaxed_config.sh
cd $current_dir
run_ycsb relaxed_nova_2M_new

cd $setup_dir
sudo $setup_dir/pmfs_config.sh
cd $current_dir
run_ycsb pmfs_2M_new

cd $setup_dir
sudo $setup_dir/nova_config.sh
cd $current_dir
run_ycsb nova_2M_new
'

sudo $setup_dir/dax_config.sh
run_ycsb_boost boost

:'
cd $setup_dir
sudo $setup_dir/nova_config.sh
cd $current_dir

sudo $setup_dir/dax_config.sh
run_ycsb_boost sync_boost

cd $setup_dir
sudo $setup_dir/nova_config.sh
cd $current_dir

sudo $setup_dir/dax_config.sh
run_ycsb_boost posix_boost
'
