#!/bin/bash
set -x

cur_dir=$(pwd)
root_dir=`readlink -f ../..`
fio_dir=$root_dir/fio

cd $fio_dir
make clean
./configure
make
cd $cur_dir
