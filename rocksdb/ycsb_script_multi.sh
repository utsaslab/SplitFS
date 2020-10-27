#!/bin/bash

if [ "$#" -ne 10 ]; then
    echo "Illegal number of parameters; Please provide run, fs, <mount_point>, rec_val, a_op_val, bd_op_val, c_op_val, f_op_val, e_op_val, num_threads  as the parameter;"
    exit 1
fi

set -x

runId=$1
fs=$2
mountpoint=$3
rec_values=$4
a_op_values=$5
bd_op_values=$6
c_op_values=$7
f_op_values=$8
e_op_values=$9
num_of_threads=${10}
homeDir=/home/cc
ycsbWorkloadsDir=$homeDir/ycsb_workloads
rocksDbDir=$homeDir/rocksdb
ResultsDir=$homeDir/results/$fs/ycsb
pmemDir=$mountpoint
databaseDir=$pmemDir/rocksdbtest-1000
splitfsDir=$homeDir/SplitFS/splitfs

echo Configuration: 20, 24, 64MB
parameters=' --write_buffer_size=67108864 --open_files=32768 --level0_slowdown_writes_trigger=20 --level0_stop_writes_trigger=24 --mmap_read=false --mmap_write=false --allow_concurrent_memtable_write=true --disable_wal=false --num_levels=7 --memtable_use_huge_page=true --target_file_size_base=67108864 --max_bytes_for_level_base=268435456 --max_bytes_for_level_multiplier=10 --max_background_compactions=4'
echo parameters: $parameters

ulimit -c unlimited
ulimit -n 32768

mkdir -p $ResultsDir

echo Sleeping for 5 seconds ...
sleep 5

load_workload()
{
    workloadName=$1
    tracefile=$2
    setup=$3

    echo workloadName: $workloadName, tracefile: $tracefile, parameters: $parameters, setup: $setup

    resultDir=$ResultsDir/Load$workloadName

    mkdir -p $resultDir

    echo ----------------------- RocksDB YCSB Load $workloadName ---------------------------
    date
    export trace_file=$tracefile
    echo Trace file is $trace_file
    cd $rocksDbDir

    sudo rm -rf $resultDir/*${runId}_${num_of_threads}

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" 2>&1 | tee $resultDir/pg_faults_before_Run${runId}_${num_of_threads}

    sudo dmesg -c > /dev/null

    #source /opt/intel/vtune_profiler_2020.1.0.607630/vtune-vars.sh

    rm -rf $resultDir/Run${runId}_${num_of_threads}_vtune_analysis

    date
    if [ "$fs" == "splitfs" ]; then
        taskset -c 0-15 $splitfsDir/run_boost.sh -p $splitfsDir -t nvp_nvp.tree ./db_bench --use_existing_db=0 --benchmarks=ycsb,stats,levelstats,sstables --db=$databaseDir --compression_type=none --threads=${num_of_threads} $parameters 2>&1 | tee $resultDir/Run${runId}_${num_of_threads}
    else
        taskset -c 0-15 ./db_bench --use_existing_db=0 --benchmarks=ycsb,stats,levelstats,sstables --db=$databaseDir --compression_type=none --threads=${num_of_threads} $parameters 2>&1 | tee $resultDir/Run${runId}_${num_of_threads}
    fi
    #vtune -collect memory-access -result-dir $resultDir/Run${runId}_${num_of_threads}_vtune_analysis ./db_bench --use_existing_db=0 --benchmarks=ycsb,stats,levelstats,sstables --db=$databaseDir --compression_type=none --threads=${num_of_threads} $parameters 2>&1 | tee $resultDir/Run${runId}_${num_of_threads}
    date

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" 2>&1 | tee $resultDir/pg_faults_after_Run${runId}_${num_of_threads}

    sudo dmesg -c > $resultDir/dmesg_log_${runId}_${num_of_threads}

    echo Sleeping for 5 seconds . .
    sleep 5

    ls -lah $databaseDir/* >> $resultDir/FileInfo${runId}_${num_of_threads}
    echo "--------------------------------" >> $resultDir/FileInfo${runId}_${num_of_threads}
    ls $databaseDir/ | wc -l >> $resultDir/FileInfo${runId}_${num_of_threads}
    echo "--------------------------------" >> $resultDir/FileInfo${runId}_${num_of_threads}
    du -sh $databaseDir >> $resultDir/FileInfo${runId}_${num_of_threads}

    echo -----------------------------------------------------------------------

    #vtune -report summary -r $resultDir/Run${runId}_${num_of_threads}_vtune_analysis 2>&1 | tee $resultDir/vtune${runId}_${num_of_threads}
}

run_workload()
{
    workloadName=$1
    tracefile=$2
    setup=$3

    echo "workloadName: $workloadName, tracefile: $tracefile, parameters: $parameters, setup: $setup"

    resultDir=$ResultsDir/Run$workloadName

    mkdir -p $resultDir

    echo ----------------------- RocksDB YCSB Run $workloadName ---------------------------
    date
    export trace_file=$tracefile
    echo Trace file is $trace_file
    cd $rocksDbDir

    sudo rm -rf $resultDir/*${runId}_${num_of_threads}

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" 2>&1 | tee $resultDir/pg_faults_before_Run${runId}_${num_of_threads}

    sudo dmesg -c > /dev/null

    #source /opt/intel/vtune_profiler_2020.1.0.607630/vtune-vars.sh

    rm -rf $resultDir/Run${runId}_${num_of_threads}_vtune_analysis

    date
    if [ "$fs" == "splitfs" ]; then
        taskset -c 0-15 $splitfsDir/run_boost.sh -p $splitfsDir -t nvp_nvp.tree ./db_bench --use_existing_db=1 --benchmarks=ycsb,stats,levelstats,sstables --db=$databaseDir --compression_type=none --threads=${num_of_threads} $parameters 2>&1 | tee $resultDir/Run${runId}_${num_of_threads}
    else
        taskset -c 0-15 ./db_bench --use_existing_db=1 --benchmarks=ycsb,stats,levelstats,sstables --db=$databaseDir --compression_type=none --threads=${num_of_threads} $parameters 2>&1 | tee $resultDir/Run${runId}_${num_of_threads}
    fi
    #vtune -collect memory-access -result-dir $resultDir/Run${runId}_${num_of_threads}_vtune_analysis ./db_bench --use_existing_db=1 --benchmarks=ycsb,stats,levelstats,sstables --db=$databaseDir --compression_type=none --threads=${num_of_threads} $parameters 2>&1 | tee $resultDir/Run${runId}_${num_of_threads}
    date

    sudo dmesg -c > $resultDir/dmesg_log_Run${runId}_${num_of_threads}

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" 2>&1 | tee $resultDir/pg_faults_after_Run${runId}_${num_of_threads}

    echo Sleeping for 5 seconds . .
    sleep 5

    ls -lah $databaseDir/* >> $resultDir/FileInfo${runId}_${num_of_threads}
    echo "--------------------------------" >> $resultDir/FileInfo${runId}_${num_of_threads}
    ls $databaseDir/ | wc -l >> $resultDir/FileInfo${runId}_${num_of_threads}
    echo "--------------------------------" >> $resultDir/FileInfo${runId}_${num_of_threads}
    du -sh $databaseDir >> $resultDir/FileInfo${runId}_${num_of_threads}

    echo -----------------------------------------------------------------------

    #vtune -report summary -r $resultDir/Run${runId}_${num_of_threads}_vtune_analysis 2>&1 | tee $resultDir/vtune${runId}_${num_of_threads}
}

create_load_trace_files()
{
    wkld=$1
    threads=$2
    rec=$3
    ops=$4

    load_files=$ycsbWorkloadsDir/load${wkld}_$rec

    if [ $threads -eq 1 ]; then
        echo $load_files
        return
    fi

    load_files=${load_files}_1_${threads}

    for ((i = 2 ; i <= $threads ; i++))
    do
        load_files=${load_files},${ycsbWorkloadsDir}/load${wkld}_${rec}_${i}_${threads}
    done

    echo $load_files
}

create_run_trace_files()
{
    wkld=$1
    threads=$2
    rec=$3
    ops=$4

    run_files=$ycsbWorkloadsDir/run${wkld}_${rec}_${ops}

    if [ $threads -eq 1 ]; then
        echo $run_files
        return
    fi

    run_files=${run_files}_1_${threads}

    for ((i = 2 ; i <= $threads ; i++))
    do
        run_files=${run_files},${ycsbWorkloadsDir}/run${wkld}_${rec}_${ops}_${i}_${threads}
    done

    echo $run_files
}

setup_expt()
{
    setup=$1

    sudo rm -rf $pmemDir/rocksdbtest-1000

    loada_files=`create_load_trace_files a $num_of_threads $rec_values $a_op_values`
    runa_files=`create_run_trace_files a $num_of_threads $rec_values $a_op_values`
    runb_files=`create_run_trace_files b $num_of_threads $rec_values $bd_op_values`
    runc_files=`create_run_trace_files c $num_of_threads $rec_values $c_op_values`
    rund_files=`create_run_trace_files d $num_of_threads $rec_values $bd_op_values`
    runf_files=`create_run_trace_files f $num_of_threads $rec_values $f_op_values`
    loade_files=`create_load_trace_files e $num_of_threads $rec_values $e_op_values`
    rune_files=`create_run_trace_files e $num_of_threads $rec_values $e_op_values`

    load_workload a $loada_files $setup
    $scriptsDir/pause_script.sh 10

    run_workload a $runa_files $setup
    $scriptsDir/pause_script.sh 10

    :'
    run_workload b $runb_files $setup
    $scriptsDir/pause_script.sh 10

    run_workload c $runc_files $setup
    $scriptsDir/pause_script.sh 10

    run_workload f $runf_files $setup
    $scriptsDir/pause_script.sh 10

    run_workload d $rund_files $setup
    $scriptsDir/pause_script.sh 10

    ./setup_duofs.sh
    load_workload e $loade_files $setup
    $scriptsDir/pause_script.sh 10

    run_workload e $rune_files $setup
    $scriptsDir/pause_script.sh 10
    '
}

setup_expt $fs
