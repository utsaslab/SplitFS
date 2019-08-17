#!/bin/bash

set -x

cur_dir=$(pwd)
root_dir=`readlink -f ..`
pmem_dir=/mnt/pmem_emul
linux_tar=linux-4.18.10.tar.gz
linux_dir=linux-4.18.10

mkdir -p $pmem_dir/repo
cp $root_dir/$linux_tar $pmem_dir/repo
cd $pmem_dir/repo

for i in {1..9}
do
    mkdir folder$i
done

tar -xf $linux_tar
for i in {1..9}
do
    cp -r $linux_dir folder$i/
done

rm -rf $linux_dir
rm $linux_tar

cd $cur_dir
cp -r $pmem_dir/repo $root_dir/git/workload/

