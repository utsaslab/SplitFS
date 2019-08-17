#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <num_threads>"
    exit 1
fi

num_threads=$1

set -x

src_dir=`readlink -f ../../`
cur_dir=`readlink -f ./`
rsync_path=$src_dir/rsync

cd $rsync_path
./configure
make clean
make -j $num_threads

cd $cur_dir
