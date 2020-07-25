#!/bin/bash

set -x

cur_dir=$(pwd)
root_dir=`readlink -f ../..`
pmem_dir=/mnt/pmem_emul
linux_tar=linux-4.18.10.tar.gz
linux_dir=linux-4.18.10

cd $root_dir
rm $linux_tar
wget https://cdn.kernel.org/pub/linux/kernel/v4.x/linux-4.18.10.tar.gz

rm -rf $pmem_dir/repo
mkdir -p $pmem_dir/repo
cp $root_dir/$linux_tar $pmem_dir/repo
cd $pmem_dir/repo

for i in {1..10}
do
    mkdir folder$i
done

tar -xf $linux_tar
for i in {1..5}
do
    cp -r $linux_dir folder$i/
done

rm -rf $linux_dir

for i in {6..10}
do
    cd folder$i
    dd if=/dev/urandom of=temp1.txt bs=1M count=50
    dd if=/dev/urandom of=temp2.txt bs=1M count=50
    dd if=/dev/urandom of=temp3.txt bs=1M count=50
    dd if=/dev/urandom of=temp4.txt bs=1M count=50
    dd if=/dev/urandom of=temp5.txt bs=1M count=50
    cd ..
done

cd $pmem_dir
tar -zcf tar_workload.tar.gz repo
rm -rf repo

cd $cur_dir
mkdir -p $root_dir/tar/workload
cp -r $pmem_dir/tar_workload.tar.gz $root_dir/tar/workload/
