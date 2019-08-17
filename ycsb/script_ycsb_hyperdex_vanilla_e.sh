#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters; Please provide run and sub_run as the parameters;"
    exit 1
fi

run=$1
sub_run=$2
data_dir='/mnt/ssd/flsm/hyperdex_data_vanilla_ycsb'$run'_e'
threads=4

ulimit -n 3000
ulimit -c unlimited

echo ------------------------------ Vanilla YCSB HyperDex Load E -------------------------------
export LD_LIBRARY_PATH=/usr/local/lib
nohup echo alohomora | sudo -S iotop -btoqa | grep --line-buffered $data_dir > ycsb"$run"_hyperdex/iotop_vanilla_loade_30M"$run"_"$sub_run".log &
nohup ~/pmap_script.sh > ycsb"$run"_hyperdex/pmap_vanilla_loade_30M"$run"_"$sub_run".log &
date
./bin/ycsb load hyperdex -P workloads/workloade -p recordcount=30000000 -p operationcount=30000000 -p hyperclient.scannable=true -p hyperdex.scannable=true -threads $threads > ycsb"$run"_hyperdex/vanilla_loade_30M"$run"_"$sub_run".log
date
echo alohomora | sudo -S kill `pgrep iotop`
echo alohomora | sudo -S kill `pgrep pmap_script`
echo -------------------------------------------------------------------------------------------

echo Sleeping for 10 seconds . . .
sleep 10

#df -h
#du -s -h /mnt/ssd/flsm/*

#echo Copying data to loade directories...
#mkdir -p /mnt/ssd/flsm/hyperdex_data_vanilla_ycsb"$run"_e_loade
#mkdir -p /mnt/ssd/flsm/hyperdex_coordinator_vanilla_ycsb"$run"_e_loade
#cp -r /mnt/ssd/flsm/hyperdex_data_vanilla_ycsb"$run"_e/* /mnt/ssd/flsm/hyperdex_data_vanilla_ycsb"$run"_e_loade/
#cp -r /mnt/ssd/flsm/hyperdex_coordinator_vanilla_ycsb"$run"_e/* /mnt/ssd/flsm/hyperdex_coordinator_vanilla_ycsb"$run"_e_loade/

#df -h
#du -s -h /mnt/ssd/flsm/*

echo ------------------------------ Vanilla YCSB HyperDex Run E -------------------------------
export LD_LIBRARY_PATH=/usr/local/lib
nohup echo alohomora | sudo -S iotop -btoqa | grep --line-buffered $data_dir > ycsb"$run"_hyperdex/iotop_vanilla_rune_250K"$run"_"$sub_run".log &
nohup ~/pmap_script.sh > ycsb"$run"_hyperdex/pmap_vanilla_rune_250K"$run"_"$sub_run".log &
date
./bin/ycsb run hyperdex -P workloads/workloade -p recordcount=30000000 -p operationcount=250000 -p hyperclient.scannable=true -p hyperdex.scannable=true -threads $threads > ycsb"$run"_hyperdex/vanilla_rune_250K"$run"_"$sub_run".log
date
echo alohomora | sudo -S kill `pgrep iotop`
echo alohomora | sudo -S kill `pgrep pmap_script`
echo -------------------------------------------------------------------------------------------


