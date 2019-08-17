#!/bin/bash

# Run git
#cd ./git
#sudo ./run_git.sh
#cd ..

# Run rsync
#cd ./rsync
#sudo ./run_rsync.sh
#cd ..

# Run TPCC
#cd ./tpcc
#sudo ./run_tpcc.sh
#cd ..

# Run Redis
#cd ./redis
#sudo ./run_redis.sh
#cd ..

# Run TPCC Softover
cd ./tpcc-soft
sudo ./run_tpcc.sh
cd ..

# Run YCSB
cd ./ycsb
sudo ./run_ycsb.sh
cd ..

# Run YCSB Softover
cd ./ycsb-soft
sudo ./run_ycsb.sh
cd ..


# Run Redis Softover
#cd ./redis-soft
#sudo ./run_redis.sh
#cd ..
