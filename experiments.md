### Performance Benchmarks

We evaluate and benchmark on SplitFS using different application benchmarks like YCSB (Load A, Run A-F, Load E and Run E), TPC-C on SQLite and rsync. We compare the performance with other similar file systems like ext4 DAX, NOVA-strict, NOVA-relaxed and PMFS. We run YCSB on SplitFS-strict, TPC-C on SplitFS-POSIX and rsync on SplitFS-sync.
1. Running YCSB on SplitFS-strict and comparing it with NOVA-strict supports the claim that SplitFS is able to match or beat the performance of state-of-the-art file systems on data intensive workloads while achieving the same guarantees that these file systems provide
2. Running TPC-C on SplitFS-POSIX and comparing it with ext4 DAX and NOVA-strict supports the claim that SplitFS is able to provide different guarantees to applications according to their needs without remounting the underlying file system. SQLite in WAL mode does not require the strict guarantees that NOVA-strict provides leading to suboptimal performance, while running SQLite on SplitFS-POSIX helps boost performance while only providing the required guarantees from the underlying file system
3. Running rsync on SplitFS-sync and comparing it with NOVA-relaxed and PMFS supports the claim that SplitFS incurs a modest overhead in performance for utility workloads that are metadata intensive


### Experiment Setup

1. kernel: `cd scripts/kernel-setup; ./compile_kernel.sh; cd ..` -- This will compile the Linux 4.13.0 kernel along with loadable modules for NOVA and PMFS. It will also install the kernel after compiling. Run with `sudo` 
2. PM Emulation: 
    * Open `/etc/default/grub`
    * add `GRUB_CMDLINE_LINUX="memmap=24G!4G nokaslr"`
    * Close file
    * `$ sudo update-grub && sudo update-grub2`
    * Reboot system
    * Run `uname -r` to ensure that system is booted with 4.13.0 kernel, and ensure that `/dev/pmem0` exists
    * `$ mkdir /mnt/pmem_emul`
3. SplitFS: `cd scripts/splitfs-setup; ./compile_splitfs.sh; cd ../..` -- This will compile splitfs strict
4. LevelDB: `cd scripts/ycsb; ./compile_leveldb.sh; cd ../..` -- This will compile LevelDB
5. YCSB: `cd scripts/ycsb; ./compile_ycsb.sh; cd ../..` -- This will compile YCSB workload
6. SQLite: `cd scripts/tpcc; ./compile_sqlite.sh; cd ../..` -- This will compile SQLite3
7. TPCC: `cd scripts/tpcc; ./compile_tpcc.sh; cd ../..` -- This will compile TPCC workload
8. rsync: `cd scripts/rsync; ./compile_rsync.sh; cd ../..` -- This will compile rsync

Note: The <num_threads> argument in the compilation scripts performs the compilation with the number of threads given as input to the script, to improve the speed of compilation. 

---

### Workload Generation

1. YCSB: `cd scripts/ycsb; ./gen_workloads.sh; cd ../..` -- This will generate the YCSB workload files to be run with LevelDB, because YCSB does not natively support LevelDB, and has been added to the benchmarks of LevelDB
2. TPCC: `cd scripts/tpcc; ./gen_workload.sh; cd ../..` -- This will create an initial database on SQLite on which to run the TPCC workload
3. rsync: `cd scripts/rsync/; sudo ./rsync_gen_workload.sh; cd ../..` -- This will create the rsync workload according to the backup data distribution as mentioned in the Paper

---

### Run Workloads

1. YCSB: `cd scripts/ycsb; ./run_ycsb.sh; cd ../..` -- This will run all the YCSB workloads on LevelDB (Load A, Run A-F, Load E, Run E) with `ext4-DAX, NOVA strict, NOVA Relaxed, PMFS, SplitFS-strict` 
2. TPCC: `cd scripts/tpcc; ./run_tpcc.sh; cd ../..` -- This will run the TPCC workload on SQLite3 with `ext4-DAX, NOVA strict, NOVA Relaxed, PMFS, SplitFS-POSIX`
3. rsync: `cd scripts/rsync; ./run_rsync.sh; cd ../..` -- This will run the rsync workload with `ext4-DAX, NOVA strict, NOVA Relaxed, PMFS, SplitFS-sync`

---

### Results

Results will be generated in `results/` folder in the repository.
The result parsing script is present in the `scripts/` folder in the repository. It can be run with the command `$ python3 parse_results.py`. The script generates separate CSV files for different applications.
In order to compare results with the paper, YCSB results of SplitFS-Strict should be compared with NOVA, TPC-C results of SplitFS-POSIX should be compared with NOVA and ext4 DAX, rsync results of SplitFS-Sync should be compared with PMFS and NOVA-relaxed.

---

### Replicating Results

The setup used for the Paper was a server with Ubuntu 16.04, 32GB DRAM, 4 cores and 1 socket. Processor used: Intel(R) Xeon(R) CPU E3-1225 v5 @ 3.30GHz. LLC cache = 8 MB.

---
