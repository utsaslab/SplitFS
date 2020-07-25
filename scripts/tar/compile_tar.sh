#!/bin/bash

set -x

cur_dir=$(pwd)
root_dir=`readlink -f ../..`
tar_dir=$root_dir/tar

cd $tar_dir
make clean
./configure
make

cd $cur_dir
