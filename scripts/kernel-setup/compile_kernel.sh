#!/bin/bash

if [ "$#" -ne 1 ]; then
	echo "Usage: compile_kernel.sh <num_threads>"
	exit 1
fi	

threads=$1

set -x

cur_path=`readlink -f ./`
src_path=`readlink -f ../../`
kbuild_path=${src_path}/kernel/kbuild

# Go to kernel build path
cd $kbuild_path

make -f Makefile.setup .config
make -f Makefile.setup
sleep 10
make -j $threads # compile kernel
sleep 10
sudo make modules_install ; sudo make install # install modules

cd $script_path

