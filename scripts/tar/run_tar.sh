#!/bin/bash

current_dir=$(pwd)
setup_dir=`readlink -f ../configs`
pmem_dir=/mnt/pmem_emul

run_tar()
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
run_tar dax

echo "-- NOVA Relaxed --"
cd $setup_dir
sudo ./nova_relaxed_config.sh
cd $current_dir
run_tar relaxed_nova

echo "-- NOVA --"
cd $setup_dir
sudo ./nova_config.sh
cd $current_dir
run_tar nova

echo "-- PMFS --"
cd $setup_dir
sudo $setup_dir/pmfs_config.sh
cd $current_dir
run_tar pmfs

export LEDGER_DATAJ=0
export LEDGER_POSIX=1
export LEDGER_TAR=1
echo "-- SplitFS POSIX --"
make splitfs.posix
sudo $setup_dir/dax_config.sh
run_tar splitfs-posix
