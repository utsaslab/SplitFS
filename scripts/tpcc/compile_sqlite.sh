#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <num_threads>"
    exit 1
fi

set -x

src_dir=`readlink -f ../../`
cur_dir=`readlink -f ./`
sqlite_path=$src_dir/sqlite3-trace

cd $sqlite_path
./configure
make clean
make
sudo make install
