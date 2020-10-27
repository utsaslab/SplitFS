#!/bin/bash

set -x

cur_dir=`readlink -f ./`
src_dir=`readlink -f ../../`
rocksdb_path=${src_dir}/rocksdb

cd $rocksdb_path
make release -j8

cd ${cur_dir}
