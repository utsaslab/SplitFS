#!/bin/bash

set -x

cur_dir=`readlink -f ./`
src_dir=`readlink -f ../../`
leveldb_path=${src_dir}/leveldb

mkdir -p $leveldb_path/build
leveldb_build_path=$leveldb_path/build
workload_path=$leveldb_path/workloads

cd $leveldb_build_path
cmake -DCMAKE_BUILD_TYPE=Release .. && make -j4

cd ${cur_dir}
