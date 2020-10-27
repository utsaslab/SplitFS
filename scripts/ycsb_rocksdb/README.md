To run ycsb benchmarks on SplitFS:
1. [Setup SplitFS](https://github.com/utsaslab/SplitFS#set-up-ext4-DAX)
2. Compile SplitFS with desired mode (posix, sync, strict). Be sure to set the `WORKLOAD_ROCKSDB` and `WORKLOAD_YCSB` flags
3. `cd scripts/ycsb_rocksdb`
4. `./run_ycsb_rocksdb.sh`
5. `cd ../../`
6. You should be able to see the results under `results/` directory
