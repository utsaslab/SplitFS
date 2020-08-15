#!/bin/bash

set -x

cur_dir=$(pwd)
root_dir=`readlink -f ../..`
ltp_dir=$root_dir/ltp-master
fsstress_dir=$ltp_dir/testcases/kernel/fs/fsstress

#get ltp with wget
cd $root_dir
wget https://github.com/linux-test-project/ltp/archive/master.zip
unzip master.zip
rm master.zip

#prepare for installing fsstress
cd $ltp_dir
make autotools
./configure

#installing fsstress
cd $fsstress_dir
rm fsstress
make

cd $cur_dir
