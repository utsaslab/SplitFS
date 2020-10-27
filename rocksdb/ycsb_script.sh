#!/bin/bash

if [ "$#" -ne 3 ]; then
    echo "Illegal number of parameters; Please provide run, fs, <mount_point> as the parameter;"
    exit 1
fi

set -x

runId=$1
fs=$2
mountpoint=$3
homeDir=/root/saurabh/devel/kk-nvm
ycsbWorkloadsDir=$homeDir/ycsb_workloads
rocksDbDir=$homeDir/rocksdb-6.6.4
daxResultsDir=$homeDir/results/dax/ycsb
novaResultsDir=$homeDir/results/nova/ycsb
pmfsResultsDir=$homeDir/results/pmfs/ycsb
ramResultsDir=$homeDir/results/ramdisk/ycsb
hugeResultsDir=$homeDir/results/hugetlbfs/ycsb
scriptsDir=$homeDir/rocksdb-6.6.4/scripts
pmemDir=$mountpoint
databaseDir=$pmemDir/rocksdbtest-1000

echo Configuration: 20, 24, 64MB
parameters=' --write_buffer_size=67108864 --open_files=1000 --level0_slowdown_writes_trigger=20 --level0_stop_writes_trigger=24 --mmap_read=true --mmap_write=true --allow_concurrent_memtable_write=true --disable_wal=false --num_levels=7 --memtable_use_huge_page=true --target_file_size_base=67108864 --max_bytes_for_level_base=268435456 --max_bytes_for_level_multiplier=10'
echo parameters: $parameters

ulimit -c unlimited

mkdir -p $daxResultsDir
mkdir -p $novaResultsDir
mkdir -p $pmfsResultsDir
mkdir -p $ramResultsDir
mkdir -p $hugeResultsDir

echo Sleeping for 5 seconds ...
sleep 5

load_workload()
{
    workloadName=$1
    tracefile=$2
    setup=$3

    echo workloadName: $workloadName, tracefile: $tracefile, parameters: $parameters, setup: $setup

    if [ "$setup" = "nova" ]; then
        resultDir=$novaResultsDir/Load$workloadName
    elif [ "$setup" = "dax" ]; then
        resultDir=$daxResultsDir/Load$workloadName
    elif [ "$setup" = "pmfs" ]; then
        resultDir=$pmfsResultsDir/Load$workloadName
    elif [ "$setup" = "huge" ]; then
        resultDir=$hugeResultsDir/Load$workloadName
    else
        resultDir=$ramResultsDir/Load$workloadName
    fi

    mkdir -p $resultDir

    echo ----------------------- RocksDB YCSB Load $workloadName ---------------------------
    date
    export trace_file=$tracefile
    echo Trace file is $trace_file
    cd $rocksDbDir

    sudo rm -rf $resultDir/*$runId

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" 2>&1 | tee $resultDir/pg_faults_before_Run$runId

    date
    if [ "$setup" = "huge" ]; then
        LD_PRELOAD=/usr/lib/libhugetlbfs.so HUGETLB_MORECORE=yes ./db_bench --use_existing_db=0 --benchmarks=ycsb,stats,levelstats,sstables --db=$databaseDir --compression_type=none --threads=1 $parameters 2>&1 | tee $resultDir/Run$runId
    else
        numactl --cpubind=0 ./db_bench --use_existing_db=0 --benchmarks=ycsb,stats,levelstats,sstables --db=$databaseDir --compression_type=none --threads=1 $parameters 2>&1 | tee $resultDir/Run$runId
    fi
    date

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" 2>&1 | tee $resultDir/pg_faults_after_Run$runId

    echo Sleeping for 5 seconds . .
    sleep 5

    ls -lah $databaseDir/* >> $resultDir/FileInfo$runId
    echo "--------------------------------" >> $resultDir/FileInfo$runId
    ls $databaseDir/ | wc -l >> $resultDir/FileInfo$runId
    echo "--------------------------------" >> $resultDir/FileInfo$runId
    du -sh $databaseDir >> $resultDir/FileInfo$runId

    echo -----------------------------------------------------------------------

    echo Sleeping for 5 seconds ...
    sleep 5
}

run_workload()
{
    workloadName=$1
    tracefile=$2
    setup=$3

    echo "workloadName: $workloadName, tracefile: $tracefile, parameters: $parameters, setup: $setup"

    if [ "$setup" = "nova" ]; then
        resultDir=$novaResultsDir/Run$workloadName
    elif [ "$setup" = "dax" ]; then
        resultDir=$daxResultsDir/Run$workloadName
    elif [ "$setup" = "pmfs" ]; then
        resultDir=$pmfsResultsDir/Run$workloadName
    elif [ "$setup" = "huge" ]; then
        resultDir=$hugeResultsDir/Run$workloadName
    else
        resultDir=$ramResultsDir/Run$workloadName
    fi

    mkdir -p $resultDir

    echo ----------------------- RocksDB YCSB Run $workloadName ---------------------------
    date
    export trace_file=$tracefile
    echo Trace file is $trace_file
    cd $rocksDbDir

    sudo rm -rf $resultDir/*$runId

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" 2>&1 | tee $resultDir/pg_faults_before_Run$runId

    sudo dmesg -c

    date
    if [ "$setup" = "huge" ]; then
        LD_PRELOAD=/usr/lib/libhugetlbfs.so HUGETLB_MORECORE=yes ./db_bench --use_existing_db=1 --benchmarks=ycsb,stats,levelstats,sstables --db=$databaseDir --compression_type=none --threads=1 $parameters 2>&1 | tee $resultDir/Run$runId
    else
        numactl --cpubind=0 ./db_bench --use_existing_db=1 --benchmarks=ycsb,stats,levelstats,sstables --db=$databaseDir --compression_type=none --threads=1 $parameters 2>&1 | tee $resultDir/Run$runId
    fi
    date

    sudo dmesg -c > $resultDir/dmesg_log_Run$runId

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" 2>&1 | tee $resultDir/pg_faults_after_Run$runId

    echo Sleeping for 5 seconds . .
    sleep 5

    ls -lah $databaseDir/* >> $resultDir/FileInfo$runId
    echo "--------------------------------" >> $resultDir/FileInfo$runId
    ls $databaseDir/ | wc -l >> $resultDir/FileInfo$runId
    echo "--------------------------------" >> $resultDir/FileInfo$runId
    du -sh $databaseDir >> $resultDir/FileInfo$runId

    echo -----------------------------------------------------------------------

    echo Sleeping for 5 seconds ...
    sleep 5
}

setup_expt()
{
    setup=$1

    sudo rm -rf $pmemDir/rocksdbtest-1000

    load_workload a $ycsbWorkloadsDir/loada_10M $setup
    $scriptsDir/pause_script.sh 10

    #sudo rm -rf $pmemDir/DR*

    run_workload a $ycsbWorkloadsDir/runa_10M_5M $setup
    $scriptsDir/pause_script.sh 10

    #sudo rm -rf $pmemDir/DR*

    run_workload b $ycsbWorkloadsDir/runb_10M_5M $setup
    $scriptsDir/pause_script.sh 10

    #sudo rm -rf $pmemDir/DR*

    run_workload c $ycsbWorkloadsDir/runc_10M_5M $setup
    $scriptsDir/pause_script.sh 10

    #sudo rm -rf $pmemDir/DR*

    run_workload f $ycsbWorkloadsDir/runf_10M_5M $setup
    $scriptsDir/pause_script.sh 10

    #sudo rm -rf $pmemDir/DR*

    run_workload d $ycsbWorkloadsDir/rund_10M_5M $setup
    $scriptsDir/pause_script.sh 10

    sudo rm -rf $pmemDir/rocksdbtest-1000

    load_workload e $ycsbWorkloadsDir/loade_10M $setup
    $scriptsDir/pause_script.sh 10

    run_workload e $ycsbWorkloadsDir/rune_10M_1M $setup
    $scriptsDir/pause_script.sh 10
}

setup_expt $fs
