#!/bin/bash
set -x

cur_dir=$(pwd)
root_dir=`readlink -f ../..`
lmdb_dir=$root_dir/lmdb

cd $lmdb_dir/libraries/liblmdb
make clean
make
sudo make install

cd $lmdb_dir/dbbench
make clean
make

cd $cur_dir
