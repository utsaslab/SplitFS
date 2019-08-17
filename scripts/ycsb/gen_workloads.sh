#!/bin/bash

set -x

cur_dir=`readlink -f ./`
src_dir=`readlink -f ../../`
ycsb_dir=${src_dir}/ycsb
leveldb_dir=${src_dir}/leveldb/workloads
mkdir -p ${leveldb_dir}

five_mil=5000000
one_mil=1000000

cd $ycsb_dir
./bin/ycsb load tracerecorder -p recorder.file=${leveldb_dir}/loada_5M -p recordcount=${five_mil} -P workloads/workloada
./bin/ycsb run tracerecorder -p recorder.file=${leveldb_dir}/runa_5M_5M -p recordcount=${five_mil} -p operationcount=${five_mil} -P workloads/workloada
./bin/ycsb run tracerecorder -p recorder.file=${leveldb_dir}/runb_5M_5M -p recordcount=${five_mil} -p operationcount=${five_mil} -P workloads/workloadb
./bin/ycsb run tracerecorder -p recorder.file=${leveldb_dir}/runc_5M_5M -p recordcount=${five_mil} -p operationcount=${five_mil} -P workloads/workloadc
./bin/ycsb run tracerecorder -p recorder.file=${leveldb_dir}/runf_5M_5M -p recordcount=${five_mil} -p operationcount=${five_mil} -P workloads/workloadf
./bin/ycsb run tracerecorder -p recorder.file=${leveldb_dir}/rund_5M_5M -p recordcount=${five_mil} -p operationcount=${five_mil} -P workloads/workloadd

./bin/ycsb load tracerecorder -p recorder.file=${leveldb_dir}/loade_5M -p recordcount=${five_mil} -P workloads/workloade
./bin/ycsb run tracerecorder -p recorder.file=${leveldb_dir}/rune_5M_1M -p recordcount=${five_mil} -p operationcount=${one_mil} -P workloads/workloade

cd $cur_dir
