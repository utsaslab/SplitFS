#!/bin/bash

set -x

cur_dir=`readlink -f ./`
src_dir=`readlink -f ../../`

cd $src_dir
# git clone https://github.com/rohankadekodi/ycsb-ledger.git

cd ycsb
mvn install -DskipTests

cd $cur_dir
