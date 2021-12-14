#!/bin/bash

current_dir=$(pwd)
setup_dir=`readlink -f ../configs`
pmem_dir=/mnt/pmem_emul

run_git()
{
    fs=$1
    for run in 1 2 3
    do
        sudo rm -rf $pmem_dir/*
        sudo taskset -c 0-7 ./run_fs.sh $fs $run
        sleep 5
    done
}

echo "-- ext4-DAX --"
sudo $setup_dir/dax_config.sh
run_git dax

echo "-- NOVA Relaxed --"
cd $setup_dir
sudo ./nova_relaxed_config.sh
cd $current_dir
run_git relaxed_nova

echo "-- PMFS --"
cd $setup_dir
sudo $setup_dir/pmfs_config.sh
cd $current_dir
run_git pmfs

echo "-- NOVA --"
cd $setup_dir
sudo ./nova_config.sh
cd $current_dir
run_git nova

export LEDGER_GIT=1 
echo "-- SplitFS Posix --"
make splitfs.posix
sudo $setup_dir/dax_config.sh
run_git splitfs-posix

export LEDGER_GIT=1 
echo "-- SplitFS Sync --"
make splitfs.sync
sudo $setup_dir/dax_config.sh
run_git splitfs-sync

export LEDGER_GIT=1 
echo "-- SplitFS Strict --"
make splitfs.strict
sudo $setup_dir/dax_config.sh
run_git splitfs-strict
