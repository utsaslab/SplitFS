#!/bin/bash

set -x

cur_dir=$(pwd)
root_dir=`readlink -f ../..`
fsstress_dir=$root_dir/fsstress

cd $fsstress_dir
rm fsstress
make

cd $cur_dir
