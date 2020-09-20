#!/bin/bash
set -x

cur_dir=$(pwd)
root_dir=`readlink -f ../..`
filebench_dir=$root_dir/filebench

cd $filebench_dir
make clean
libtoolize
aclocal
autoheader
automake --add-missing
autoconf
./configure
make
cd $cur_dir
