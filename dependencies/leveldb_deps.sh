#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <num_threads>"
    exit 1
fi

num_threads=$1

set -x

cur_dir=`readlink -f ./`
src_dir=`readlink -f ../`

# install maven
tar -xf cmake-3.15.2.tar.gz

cd cmake-3.15.2
./configure
make -j $num_threads
sudo make install

cd $cur_dir
