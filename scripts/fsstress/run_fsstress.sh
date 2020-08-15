#!/bin/bash

set -x

cur_dir=$(pwd)
root_dir=`readlink -f ../..`
fsstress_dir=$root_dir/ltp-master/testcases/kernel/fs/fsstress

cd $fsstress_dir
LD_PRELOAD=$root_dir/splitfs/libnvp.so ./fsstress -c -d /mnt/pmem_emul -n $1 -v

cd $cur_dir
