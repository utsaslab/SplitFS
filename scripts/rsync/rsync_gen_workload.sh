#!/bin/bash

src_dir=`readlink -f ../../`
cur_dir=`readlink -f ./`
pmem_dir=/mnt/pmem_emul
src=$pmem_dir/src

rm -rf $pmem_dir/*

mkdir $src

for i in {1..10}
do
    mkdir $src/folder$i
done

for i in {1..10}
do
    dd if=/dev/urandom of=$src/file$i.txt bs=1M count=70
done

for i in {11..160}
do
   dd if=/dev/urandom of=$src/file$i.txt bs=1M count=30
done

for i in {161..260}
do
    dd if=/dev/urandom of=$src/file$i.txt bs=1M count=5
done

for i in {261..1010}
do
    dd if=/dev/urandom of=$src/file$i.txt bs=1M count=2
done

for i in {1011..1210}
do
    dd if=/dev/urandom of=$src/file$i.txt bs=256K count=1
done

for i in {1..121}
do
    mv $src/file$i.txt $src/folder1
done

for i in {122..242}
do
    mv $src/file$i.txt $src/folder2
done

for i in {243..363}
do
    mv $src/file$i.txt $src/folder3
done

for i in {364..484}
do
    mv $src/file$i.txt $src/folder4
done

for i in {485..605}
do
    mv $src/file$i.txt $src/folder5
done

for i in {606..726}
do
    mv $src/file$i.txt $src/folder6
done

for i in {727..847}
do
    mv $src/file$i.txt $src/folder7
done

for i in {848..968}
do
    mv $src/file$i.txt $src/folder8
done

for i in {969..1089}
do
    mv $src/file$i.txt $src/folder9
done

for i in {1090..1210}
do
    mv $src/file$i.txt $src/folder10
done

cd $cur_dir

mkdir $src_dir/rsync/workload/
cp -r $pmem_dir/src $src_dir/rsync/workload/

