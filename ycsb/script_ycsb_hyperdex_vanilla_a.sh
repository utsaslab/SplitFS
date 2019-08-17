#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters; Please provide run and sub_run as the parameters;"
    exit 1
fi

run=$1
sub_run=$2
data_dir='/mnt/ssd/flsm/hyperdex_data_vanilla_ycsb'$run'_a'
threads=4

ulimit -c unlimited
ulimit -n 3000

echo ------------------------------ Vanilla YCSB HyperDex Load A -------------------------------
export LD_LIBRARY_PATH=/usr/local/lib
nohup echo alohomora | sudo -S iotop -btoqa | grep --line-buffered $data_dir > ycsb"$run"_hyperdex/iotop_vanilla_loada_20M"$run"_"$sub_run".log &
nohup ~/pmap_script.sh > ycsb"$run"_hyperdex/pmap_flsm_loada_20M"$run"_"$sub_run".log &
date
./bin/ycsb load hyperdex -P workloads/workloada -p recordcount=10000000 -p operationcount=10000000 -threads $threads > ycsb"$run"_hyperdex/vanilla_loada_20M"$run"_"$sub_run".log
date
echo alohomora | sudo -S kill `pgrep iotop`
echo alohomora | sudo -S kill `pgrep pmap_script`
echo -------------------------------------------------------------------------------------------

exit 0

echo Sleeping for 120 seconds . . . 
sleep 120

mkdir -p /mnt/ssd/flsm/hyperdex_data_vanilla_ycsb"$run"_a_loada
mkdir -p /mnt/ssd/flsm/hyperdex_coordinator_vanilla_ycsb"$run"_a_loada

rm -r /mnt/ssd/flsm/hyperdex_data_vanilla_ycsb"$run"_a_loada/*
rm -r /mnt/ssd/flsm/hyperdex_coordinator_vanilla_ycsb"$run"_a_loada/*

cp -r /mnt/ssd/flsm/hyperdex_data_vanilla_ycsb"$run"_a/* /mnt/ssd/flsm/hyperdex_data_vanilla_ycsb"$run"_a_loada/
cp -r /mnt/ssd/flsm/hyperdex_coordinator_vanilla_ycsb"$run"_a/* /mnt/ssd/flsm/hyperdex_coordinator_vanilla_ycsb"$run"_a_loada/

echo ------------------------------ Vanilla YCSB HyperDex Run A -------------------------------
export LD_LIBRARY_PATH=/usr/local/lib
nohup echo alohomora | sudo -S iotop -btoqa | grep --line-buffered $data_dir > ycsb"$run"_hyperdex/iotop_vanilla_runa_10M"$run"_"$sub_run".log &
nohup ~/pmap_script.sh > ycsb"$run"_hyperdex/pmap_flsm_runa_10M"$run"_"$sub_run".log &
date
./bin/ycsb run hyperdex -P workloads/workloada -p recordcount=20000000 -p operationcount=10000000 -threads $threads > ycsb"$run"_hyperdex/vanilla_runa_10M"$run"_"$sub_run".log
date
echo alohomora | sudo -S kill `pgrep iotop`
echo alohomora | sudo -S kill `pgrep pmap_script`
echo -------------------------------------------------------------------------------------------

echo Sleeping for 10 seconds . . . 
sleep 10

echo ------------------------------ Vanilla YCSB HyperDex Run B -------------------------------
export LD_LIBRARY_PATH=/usr/local/lib
nohup echo alohomora | sudo -S iotop -btoqa | grep --line-buffered $data_dir > ycsb"$run"_hyperdex/iotop_vanilla_runb_10M"$run"_"$sub_run".log &
nohup ~/pmap_script.sh > ycsb"$run"_hyperdex/pmap_flsm_runb_10M"$run"_"$sub_run".log &
date
./bin/ycsb run hyperdex -P workloads/workloadb -p recordcount=20000000 -p operationcount=10000000 -threads $threads > ycsb"$run"_hyperdex/vanilla_runb_10M"$run"_"$sub_run".log
date
echo alohomora | sudo -S kill `pgrep iotop`
echo alohomora | sudo -S kill `pgrep pmap_script`
echo -------------------------------------------------------------------------------------------

echo Sleeping for 10 seconds . . . 
sleep 10

echo ------------------------------ Vanilla YCSB HyperDex Run C -------------------------------
export LD_LIBRARY_PATH=/usr/local/lib
nohup echo alohomora | sudo -S iotop -btoqa | grep --line-buffered $data_dir > ycsb"$run"_hyperdex/iotop_vanilla_runc_10M"$run"_"$sub_run".log &
nohup ~/pmap_script.sh > ycsb"$run"_hyperdex/pmap_flsm_runc_10M"$run"_"$sub_run".log &
date
./bin/ycsb run hyperdex -P workloads/workloadc -p recordcount=20000000 -p operationcount=10000000 -threads $threads > ycsb"$run"_hyperdex/vanilla_runc_10M"$run"_"$sub_run".log
date
echo alohomora | sudo -S kill `pgrep iotop`
echo alohomora | sudo -S kill `pgrep pmap_script`
echo -------------------------------------------------------------------------------------------

echo Sleeping for 10 seconds . . . 
sleep 10

echo ------------------------------ Vanilla YCSB HyperDex Run F -------------------------------
export LD_LIBRARY_PATH=/usr/local/lib
nohup echo alohomora | sudo -S iotop -btoqa | grep --line-buffered $data_dir > ycsb"$run"_hyperdex/iotop_vanilla_runf_10M"$run"_"$sub_run".log &
nohup ~/pmap_script.sh > ycsb"$run"_hyperdex/pmap_flsm_runf_10M"$run"_"$sub_run".log &
date
./bin/ycsb run hyperdex -P workloads/workloadf -p recordcount=20000000 -p operationcount=10000000 -threads $threads > ycsb"$run"_hyperdex/vanilla_runf_10M"$run"_"$sub_run".log
date
echo alohomora | sudo -S kill `pgrep iotop`
echo alohomora | sudo -S kill `pgrep pmap_script`
echo -------------------------------------------------------------------------------------------

echo Sleeping for 10 seconds . . . 
sleep 10

echo ------------------------------ Vanilla YCSB HyperDex Run D -------------------------------
export LD_LIBRARY_PATH=/usr/local/lib
nohup echo alohomora | sudo -S iotop -btoqa | grep --line-buffered $data_dir > ycsb"$run"_hyperdex/iotop_vanilla_rund_10M"$run"_"$sub_run".log &
nohup ~/pmap_script.sh > ycsb"$run"_hyperdex/pmap_flsm_rund_10M"$run"_"$sub_run".log &
date
./bin/ycsb run hyperdex -P workloads/workloadd -p recordcount=20000000 -p operationcount=10000000 -threads $threads > ycsb"$run"_hyperdex/vanilla_rund_10M"$run"_"$sub_run".log
date
echo alohomora | sudo -S kill `pgrep iotop`
echo alohomora | sudo -S kill `pgrep pmap_script`
echo -------------------------------------------------------------------------------------------

