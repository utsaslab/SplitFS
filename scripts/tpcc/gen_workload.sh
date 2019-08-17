#!/bin/bash

set -x

src_dir=`readlink -f ../../`
cur_dir=`readlink -f ./`
tpcc_path=$src_dir/tpcc-sqlite
pmem_dir=/mnt/pmem_emul

cd $tpcc_path
sudo cp ./schema2/tpcc.db $pmem_dir/
sudo ./tpcc_load -w 4

mkdir ./database
sudo cp $pmem_dir/tpcc.db ./database/

cd $cur_dir
