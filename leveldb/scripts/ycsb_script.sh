#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters; Please provide run as the parameter;"
    exit 1
fi

set -x

runId=$1
databaseDir=/mnt/pmem_emul/leveldbtest-1000
ycsbWorkloadsDir=/home/rohan/projects/ycsb_workloads
levelDbDir=/home/rohan/projects/leveldb
ext4DAXResultsDir=/home/rohan/projects/ext4DAX/Results/YCSB
scriptsDir=/home/rohan/projects/leveldb/scripts
storingFileInfo=/home/rohan/projects/quill-modified/Results/YCSB
pmemDir=/mnt/pmem_emul
quillDir=/home/rohan/projects/quill-ycsb
quillAnonDir=/home/rohan/projects/quill-anon/quill-modified
quillSyscallsDir=/home/rohan/projects/quill-syscalls/quill-modified
quillAnonResultsDir=/home/rohan/projects/ycsb_output/anon_ledger
quillDRResultsDir=/home/rohan/projects/ycsb_output/dr_ledger
quillSyscallsResultsDir=/home/rohan/projects/ycsb_output/dax

parameters=' --open_files=1000 --max_file_size=2097152'
echo Configuration: 20, 24, 64MB

ulimit -c unlimited

mkdir -p $quillAnonResultsDir
mkdir -p $quillDRResultsDir
mkdir -p $ext4DAXResultsDir
mkdir -p $quillSyscallsResultsDir

echo Sleeping for 5 seconds . . 
sleep 5

load_workload()
{
    workloadName=$1
    tracefile=$2
    setup=$5
    appends=$6
    
    if [ "$appends" = "anon" ]; then
	resultDir=$quillAnonResultsDir/Load$workloadName
    elif [ "$appends" = "dr" ]; then
	resultDir=$quillDRResultsDir/Load$workloadName
    else
	resultDir=$quillSyscallsResultsDir/Load$workloadName
    fi
    
    mkdir -p $resultDir
    
    echo ----------------------- LevelDB YCSB Load $workloadName ---------------------------
    date
    export trace_file=$tracefile
    echo Trace file is $trace_file
    cd $levelDbDir/build
    
#nohup echo alohomora | sudo -S iotop -btoqa | grep --line-buffered bench > ycsb$run/iotop_loada_50M1.log &
#nohup ~/pmap_script.sh > ycsb"$run"/pmap_loada_50M"$run".log &

    sudo rm -rf $resultDir/*$runId

    date
        if [ "$setup" = "quill" ]; then
        if [ "$appends" = "anon" ]; then
	    $quillAnonDir/run_quill.sh -p $quillAnonDir/ -t nvp_nvp.tree ./db_bench --use_existing_db=0 --benchmarks=ycsb,stats,printdb --db=$databaseDir --threads=1 --open_files=1000 2>&1 | tee $resultDir/Run$runId
	else
	    $quillDir/run_quill.sh -p $quillDir/ -t nvp_nvp.tree ./db_bench --use_existing_db=0 --benchmarks=ycsb,stats,printdb --db=$databaseDir --threads=1 --open_files=1000 2>&1 | tee $resultDir/Run$runId
	fi
    else
        ./db_bench --use_existing_db=0 --benchmarks=ycsb,stats,printdb --db=$databaseDir --threads=1 --open_files=1000 2>&1 | tee $resultDir/Run$runId
	#$quillSyscallsDir/run_quill.sh -p $quillSyscallsDir/ -t nvp_nvp.tree ./db_bench --use_existing_db=0 --benchmarks=ycsb,stats,printdb --db=$databaseDir --threads=1 --open_files=1000 >> $resultDir/Run$runId

    fi
    date

    echo Sleeping for 5 seconds . .
    sleep 5

    ls -lah $databaseDir/* >> $resultDir/FileInfo$runId
    echo "--------------------------------" >> $resultDir/FileInfo$runId
    ls $databaseDir/ | wc -l >> $resultDir/FileInfo$runId
    echo "--------------------------------" >> $resultDir/FileInfo$runId
    du -sh $databaseDir >> $resultDir/FileInfo$runId

    #mkdir -p /mnt/ssd/flsm/leveldbtest-original-loada-50M$run
    #cp -r /mnt/ssd/flsm/leveldbtest-original-1000/* /mnt/ssd/flsm/leveldbtest-original-loada-50M$run/
    #echo Killing iotop process `pgrep iotop`
    #echo alohomora | sudo -S kill `pgrep iotop`
    #echo alohomora | sudo -S kill `pgrep pmap_script`

    echo -----------------------------------------------------------------------

    echo Sleeping for 5 seconds . . 
    sleep 5
}

run_workload()
{
    workloadName=$1
    tracefile=$2
    setup=$5
    appends=$6


    if [ "$appends" = "anon" ]; then
	resultDir=$quillAnonResultsDir/Run$workloadName
    elif [ "$appends" = "dr" ]; then
	resultDir=$quillDRResultsDir/Run$workloadName
    else
	resultDir=$quillSyscallsResultsDir/Run$workloadName
    fi
    
    mkdir -p $resultDir
    
    echo ----------------------- LevelDB YCSB Run $workloadName ---------------------------
    date
    export trace_file=$tracefile
    echo Trace file is $trace_file
    cd $levelDbDir/build
    #nohup echo alohomora | sudo -S iotop -btoqa | grep --line-buffered bench > ycsb$run/iotop_loada_50M1.log &
    #nohup ~/pmap_script.sh > ycsb"$run"/pmap_loada_50M"$run".log &

    sudo rm -rf $resultDir/*$runId

    date
    if [ "$setup" = "quill" ]; then
	if [ "$appends" = "anon" ]; then
	    $quillAnonDir/run_quill.sh -p $quillAnonDir/ -t nvp_nvp.tree ./db_bench --use_existing_db=1 --benchmarks=ycsb,stats,printdb --db=$databaseDir --threads=1 --open_files=1000 2>&1 | tee $resultDir/Run$runId
	else
	    $quillDir/run_quill.sh -p $quillDir/ -t nvp_nvp.tree ./db_bench --use_existing_db=1 --benchmarks=ycsb,stats,printdb --db=$databaseDir --threads=1 --open_files=1000 2>&1 | tee $resultDir/Run$runId
	fi
    else
        ./db_bench --use_existing_db=1 --benchmarks=ycsb,stats,printdb --db=$databaseDir --threads=1 --open_files=1000 2>&1 | tee $resultDir/Run$runId
	#$quillSyscallsDir/run_quill.sh -p $quillSyscallsDir/ -t nvp_nvp.tree ./db_bench --use_existing_db=0 --benchmarks=ycsb,stats,printdb --db=$databaseDir --threads=1 --open_files=1000 >> $resultDir/Run$runId

    fi
    date

    echo Sleeping for 5 seconds . .
    sleep 5

    ls -lah $databaseDir/* >> $resultDir/FileInfo$runId
    echo "--------------------------------" >> $resultDir/FileInfo$runId
    ls $databaseDir/ | wc -l >> $resultDir/FileInfo$runId
    echo "--------------------------------" >> $resultDir/FileInfo$runId
    du -sh $databaseDir >> $resultDir/FileInfo$runId
    
    #mkdir -p /mnt/ssd/flsm/leveldbtest-original-loada-50M$run
    #cp -r /mnt/ssd/flsm/leveldbtest-original-1000/* /mnt/ssd/flsm/leveldbtest-original-loada-50M$run/
    #echo Killing iotop process `pgrep iotop`
    #echo alohomora | sudo -S kill `pgrep iotop`
    #echo alohomora | sudo -S kill `pgrep pmap_script`

    echo -----------------------------------------------------------------------

    echo Sleeping for 5 seconds . . 
    sleep 5
}

setup_expt()
{
    setup=$1
    appends=$4
    
    sudo rm -rf $pmemDir/*

    load_workload a $ycsbWorkloadsDir/loada_5M $parameters $setup $appends
    $scriptsDir/pause_script.sh 10

    sudo rm -rf $pmemDir/DR*

    run_workload a $ycsbWorkloadsDir/runa_5M_5M $parameters $setup $appends
    $scriptsDir/pause_script.sh 10
    
    sudo rm -rf $pmemDir/DR*

    run_workload b $ycsbWorkloadsDir/runb_5M_10M $parameters $setup $appends
    $scriptsDir/pause_script.sh 10

    sudo rm -rf $pmemDir/DR*

    run_workload c $ycsbWorkloadsDir/runc_5M_10M $parameters $setup $appends
    $scriptsDir/pause_script.sh 10

    sudo rm -rf $pmemDir/DR*

    :'
    run_workload f $ycsbWorkloadsDir/runf_15M_15M $parameters $setup $appends
    $scriptsDir/pause_script.sh 10

    sudo rm -rf $pmemDir/DR*

    run_workload d $ycsbWorkloadsDir/rund_15M_5M $parameters $setup $appends
    $scriptsDir/pause_script.sh 10

    sudo rm -rf $pmemDir/*

    load_workload e $ycsbWorkloadsDir/loade_15M $parameters $setup $appends
    $scriptsDir/pause_script.sh 10

    sudo rm -rf $pmemDir/DR*

    run_workload e $ycsbWorkloadsDir/rune_15M_1M $parameters $setup $appends
    $scriptsDir/pause_script.sh 10
    '
}

setup_expt quill $parameters dr
#setup_expt quill $parameters anon
#setup_expt ext4DAX $parameters
