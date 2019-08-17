#!/bin/bash

cur_dir=`readlink -f ./`

# Run YCSB
cd ycsb
taskset -c 0-7 ./run_ycsb.sh
cd $cur_dir

# Run TPCC
cd tpcc
taskset -c 0-7 ./run_tpcc.sh
cd $cur_dir

# Run RSYNC
cd rsync
taskset -c 0-7 ./run_rsync.sh
cd $cur_dir

