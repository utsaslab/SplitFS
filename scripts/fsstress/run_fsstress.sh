#!/bin/bash

set -x

cur_dir=$(pwd)
root_dir=`readlink -f ../..`
fsstress_dir=$root_dir/fsstress

cd $fsstress_dir
sudo LD_PRELOAD=$root_dir/splitfs/libnvp.so ./fsstress -d /mnt/pmem_emul -n $1 -v

cd $cur_dir
