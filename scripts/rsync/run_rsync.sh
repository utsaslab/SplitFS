#!/bin/bash

cur_dir=`readlink -f ./`
src_dir=`readlink -f ../../`
pmem_dir=/mnt/pmem_emul
setup_dir=$src_dir/scripts/configs

run_rsync()
{
    fs=$1
    for run in 1 2 3
    do
        sudo rm -rf $pmem_dir/*
        sudo taskset -c 0-7 ./run_fs.sh $fs $run
        sleep 5
    done
}

sudo $setup_dir/dax_config.sh
run_rsync dax

sudo $setup_dir/nova_config.sh
run_rsync nova

sudo $setup_dir/nova_relaxed_config.sh
run_rsync relaxed_nova

sudo $setup_dir/pmfs_config.sh
run_rsync pmfs

export LEDGER_DATAJ=0
export LEDGER_POSIX=1
export LEDGER_RSYNC=1
sudo $setup_dir/dax_config.sh
run_rsync posix_boost
