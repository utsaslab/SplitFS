#!/bin/bash

current_dir=$(pwd)
setup_dir=`readlink -f ../configs`
source_dir=`readlink -f ../../splitfs`
pmem_dir=/mnt/pmem_emul

run_fio()
{
    fs=$1
    for run in 1 2 3
    do
        sudo rm -rf $pmem_dir/*
        sudo taskset -c 0-15 ./run_fs.sh $fs $run
        sleep 5
    done
}

echo "-- ext4 DAX --"
cd $setup_dir
sudo ./nova_config.sh
cd $current_dir
sudo $setup_dir/dax_config.sh
run_filebench dax

echo "-- NOVA Relaxed --"
cd $setup_dir
sudo ./nova_relaxed_config.sh
cd $current_dir
run_filebench relaxed_nova

echo "-- NOVA --"
cd $setup_dir
sudo ./nova_config.sh
cd $current_dir
run_filebench nova

echo "-- PMFS --"
cd $setup_dir
sudo $setup_dir/pmfs_config.sh
cd $current_dir
run_filebench pmfs

echo "-- SplitFS POSIX --"
cd $source_dir
export LEDGER_DATAJ=0
export LEDGER_POSIX=1
export LEDGER_FIO=1 
make clean
make -e
sudo $setup_dir/dax_config.sh
cd $current_dir
run_fio splitfs-posix
