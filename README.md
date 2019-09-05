## SplitFS

[SplitFS](https://github.com/rohankadekodi/SplitFS) is a file system for Persistent Memory (PM) which is aimed at reducing the software overhead of applications accessing Persistent Memory. SplitFS presents a novel split of responsibilities between a user-space library file system and an existing kernel PM file system. The user-space library file system handles data operations by intercepting POSIX calls, memory mapping the underlying file, and serving the reads and overwrites using processor loads and stores. Metadata operations are handled by the kernel file system (ext4 DAX). 

SplitFS introduces a new primitive termed relink to efficiently support file appends and atomic data operations. SplitFS provides three consistency modes,which different applications can choose from without interfering with each other.

The [Experiments
page](https://github.com/rohankadekodi/SplitFS/blob/master/experiments.md)
has a list of experiments evaluating SplitFS(strict, sync and POSIX) vs ext4 DAX, NOVA-strict, NOVA-relaxed and PMFS. The summary is that SplitFS outperforms the other file systems on the data intensive workloads, while incurring a modest overhead on metadata heavy workloads. Please see the paper for more details. 

Please cite the following paper if you use SplitFS: 

**SplitFS : Reducing Software Overhead in File Systems for Persistent Memory**.
Rohan Kadekodi, Se Kwon Lee, Sanidhya Kashyap, Taesoo Kim, Aasheesh Kolli, Vijay Chidambaram. 
*Proceedings of the The 27th ACM Symposium on Operating Systems Principles (SOSP 19)*. 
[Paper PDF](https://www.cs.utexas.edu/~vijay/papers/sosp19-splitfs.pdf). [Bibtex](https://www.cs.utexas.edu/~vijay/bibtex/sosp19-splitfs.bib).

---

### Contents

1. `splitfs/` contains the source code for SplitFS-strict
2. `dependencies/` contains packages and scripts to resolve dependencies
3. `kernel/` contains the Linux 4.13.0 kernel
4. `micro/` contains the microbenchmark
4. `leveldb/` contains LevelDB source code
5. `rsync/` contains the rsync source code
6. `scripts/` contains scripts to compile and run workloads and kernel
7. `splitfs-so/` contains the SplitFS-strict shared libraries for running different workloads
8. `sqlite3-trace/` contains SQLite3 source code
9. `tpcc-sqlite/` contains TPCC source code
10. `ycsb/` contains YCSB source code

---

### System Requirements

1. Ubuntu 16.04 / 18.04
2. At least 32 GB DRAM
3. At least 4 cores
4. Baremetal machine (Not a VM)
5. Intel Processor supporting `clflushopt` instruction (Introduced in Intel processor family -- Broadwell). This can be verified with `lscpu | grep clflushopt`

---

### Dependencies

1. kernel: Installing the linux kernel 4.13.0 involves installing `bc`, `libelf-dev` and `libncurses5-dev`. For ubuntu, please run the script `cd dependencies; ./kernel_deps.sh; cd ..`
2. SplitFS: Compiling SplitFS requires installing `Boost`. For Ubuntu, please run `cd dependencies; ./splitfs_deps.sh; cd ..`

---

### Tutorial

This tutorial provides the steps for compiling SplitFS in different guarantee modes (POSIX, sync or strict), and running an application using SplitFS.

1. Set mode for U-Split (strict, sync or POSIX): modify `<path_to_splitfs_src>/common.mk`:
    * `LEDGER_DATAJ=1; LEDGER_POSIX=0` for SplitFS-strict
    * `LEDGER_DATAJ=0; LEDGER_POSIX=0` for SplitFS-sync
    * `LEDGER_DATAJ=0; LEDGER_POSIX=1` for SplitFS-POSIX
1. Create ext4 file system and mount with `-o dax`: `sudo mkfs.ext4 -b 4096 /dev/pmem0; sudo mount -o dax /dev/pmem0 /mnt/pmem_emul`
2. Set the LD_LIBRARY_PATH environment variable: `export LD_LIBRARY_PATH=<path_to_splitfs_src>`
3. Set the NVP_TREE_FILE environment variable: `export NVP_TREE_FILE=<path_to_splitfs_src>/bin/nvp_nvp.tree`
4. Run  application binary: `LD_PRELOAD=<path_to_splitfs_src>/libnvp.so <application_binary>`

---

### Try it out

This tutorial walks you through the workflow of compiling an application and running it with SplitFS, using a simple microbenchmark of appending data to a file.

#### Set up SplitFS
```
$ cd splitfs; make clean; make; cd .. # Compile SplitFS
$ export LD_LIBRARY_PATH=./splitfs
$ export NVP_TREE_FILE=./splitfs/bin/nvp_nvp.tree
```
#### Set up ext4-DAX:
```
$ sudo mkfs.ext4 -b 4096 /dev/pmem0
$ sudo mount -o dax /dev/pmem0 /mnt/pmem_emul
$ sudo chown -R $USER:$USER /mnt/pmem_emul
```
#### Setup microbenchmark:
```
$ cd micro
$ gcc rw_experiment.c -o rw_expt -O3
$ cd ..
```
#### Run microbenchmark with ext4-DAX:
```
$ sync && echo 3 > /proc/sys/vm/drop_caches # Run this with superuser
$ ./micro/rw_expt write seq 4096
$ rm -rf /mnt/pmem_emul/*
```
#### Run microbenchmark with SplitFS:
```
$ sync && echo 3 > /proc/sys/vm/drop_caches # Run this with superuser
$ LD_PRELOAD=./splitfs/libnvp.so micro/rw_expt write seq 4096
$ rm -rf /mnt/pmem_emul/*
```

---

### Contact

In case of any difficulties, please send an e-mail to `rak@cs.utexas.edu`
