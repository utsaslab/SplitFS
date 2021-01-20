#!/bin/bash

# install boost
sudo apt-get install libboost-dev libcapstone-dev cmake pandoc clang

# install syscall_intercept
git submodule init
git submodule update
syscall_dir=$PWD/syscall_intercept
install_dir=$PWD/sysint_install

mkdir $install_dir 
cd $install_dir 
cmake $syscall_dir -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
if [ "$?" -ne 0 ]; then
    echo "Failed to build syscall_intercept"
    exit 1
fi

make
if [ "$?" -ne 0 ]; then
    echo "Failed to build syscall_intercept"
    exit 1
fi

sudo make install
rm -rf $install_dir
