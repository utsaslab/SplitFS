#!/bin/bash

set -x

tpcc_path=`readlink -f ../tpcc-sqlite`
tpcc_build_path=$tpcc_path/src
root_path=$(pwd)/..

cd $tpcc_build_path
make clean
make

cd $tpcc_path
sudo cp ./tpcc.db /mnt/pmem_emul
sudo ./tpcc_load -w 4

mkdir ./database
sudo cp /mnt/pmem_emul/tpcc.db ./database/
