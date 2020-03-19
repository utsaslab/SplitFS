## SplitFS

[SplitFS](https://github.com/rohankadekodi/SplitFS) is a file system for Persistent Memory (PM) which is aimed at reducing the software overhead of applications accessing Persistent Memory. SplitFS presents a novel split of responsibilities between a user-space library file system and an existing kernel PM file system. The user-space library file system handles data operations by intercepting POSIX calls, memory mapping the underlying file, and serving the reads and overwrites using processor loads and stores. Metadata operations are handled by the kernel file system (ext4 DAX). 

SplitFS introduces a new primitive termed relink to efficiently support file appends and atomic data operations. SplitFS provides three consistency modes,which different applications can choose from without interfering with each other.

SplitFS is built on top of [Quill](https://github.com/NVSL/Quill) by NVSL. We re-use the implementation of Quill to track the glibc calls requested by an application and provide our implementation for the calls. We then run the applications using LD_PRELOAD to intercept the calls during runtime and forward them to SplitFS.

Please cite the following paper if you use SplitFS:

**SplitFS : Reducing Software Overhead in File Systems for Persistent Memory**.
Rohan Kadekodi, Se Kwon Lee, Sanidhya Kashyap, Taesoo Kim, Aasheesh Kolli, Vijay Chidambaram. 
*Proceedings of the The 27th ACM Symposium on Operating Systems Principles (SOSP 19)*. 
[Paper PDF](https://www.cs.utexas.edu/~vijay/papers/sosp19-splitfs.pdf). [Bibtex](https://www.cs.utexas.edu/~vijay/bibtex/sosp19-splitfs.bib).

## Getting Started with SplitFS

This tutorial walks you through the workflow of compiling splitfs, setting up ext4-DAX, compiling an application and running it with ext4-DAX as well as SplitFS, using a simple microbenchmark. The microbenchmark appends 128MB data to an empty file, in chunks of 4KB each, and does an fsync() at the end.

1. [<b>Installing Dependencies</b>](#dependencies)

2. [<b>Setup kernel</b>](https://github.com/utsaslab/SplitFS/blob/master/experiments.md/#kernel-setup)

3. <b>Set up SplitFS</b>
```
$ cd splitfs; make clean; make; cd .. # Compile SplitFS
$ export LD_LIBRARY_PATH=./splitfs
$ export NVP_TREE_FILE=./splitfs/bin/nvp_nvp.tree
```
4. <b>Set up ext4-DAX </b>
```
$ sudo mkfs.ext4 -b 4096 /dev/pmem0
$ sudo mount -o dax /dev/pmem0 /mnt/pmem_emul
$ sudo chown -R $USER:$USER /mnt/pmem_emul
```
5. <b>Setup microbenchmark </b>
```
$ cd micro
$ gcc rw_experiment.c -o rw_expt -O3
$ cd ..
```
6. <b>Run microbenchmark with ext4-DAX </b>
```
$ sync && echo 3 > /proc/sys/vm/drop_caches # Run this with superuser
$ ./micro/rw_expt write seq 4096
$ rm -rf /mnt/pmem_emul/*
```
7. <b>Run microbenchmark with SplitFS</b>
```
$ sync && echo 3 > /proc/sys/vm/drop_caches # Run this with superuser
$ LD_PRELOAD=./splitfs/libnvp.so micro/rw_expt write seq 4096
$ rm -rf /mnt/pmem_emul/*
```
8. <b>Results</b>. The resultes show the throughput of doing appends on ext4 DAX and SplitFS. Appends are **5.8x** faster on SplitFS.
    * ext4-DAX: `0.33M appends/sec`
    * SplitFS: `1.92M appends/sec`

## Features

1. **Low software overhead**. SplitFS tries to obtain performance that is close to the maximum provided by persistent-memory hardware. The overhead due to SplitFS software is significantly lower (by 4-12x) than state-of-the-art file systems such as NOVA or ext4 DAX. As a result, performance on some applications is increased by as much as **2x**.  

2. **Flexible guarantees**. SplitFS is the only persistent-memory file system that allows simultaneously running applications to receive different guarantees from the file system. SplitFS offers three modes: POSIX, Sync, and Strict. Application A may in Strict mode, obtaining atomic, synchronous operations from SplitFS, while Application B may simultaneously run in POSIX mode and obtain higher performance. This is possible due to the novel split architecture used in SplitFS. 

3. **Portability and Stability**. SplitFS uses ext4 DAX as its kernel component, so it works with any kernel where ext4 DAX is supported. ext4 DAX is a mature, robust code base that is actively being maintained and developed; as ext4 DAX performance increases over time, SplitFS performance increases as well. This is contrast to research file systems for persistent memory, which do not see development at the same rate as ext4 DAX.  

## Contents

1. `splitfs/` contains the source code for SplitFS-POSIX
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

The [Experiments
page](https://github.com/utsaslab/SplitFS/blob/master/experiments.md)
has a list of experiments evaluating SplitFS(strict, sync and POSIX) vs ext4 DAX, NOVA-strict, NOVA-relaxed and PMFS. The summary is that SplitFS outperforms the other file systems on the data intensive workloads, while incurring a modest overhead on metadata heavy workloads. Please see the paper for more details.

The kernel patch for the implementation of relink() system call for linux v4.13 is [here](https://github.com/utsaslab/SplitFS/blob/master/kernel/relink_v4.13.patch)

## System Requirements

1. Ubuntu 16.04 / 18.04
2. At least 32 GB DRAM
3. At least 4 cores
4. Baremetal machine (Not a VM)
5. Intel Processor supporting `clflush` (Comes with SSE2) or `clflushopt` (Introduced in Intel processor family -- Broadwell) instruction. This can be verified with `lscpu | grep clflush` and `lscpu | grep clflushopt` respectively.

## Dependencies
1. kernel: Installing the linux kernel 4.13.0 involves installing bc, libelf-dev and libncurses5-dev. For ubuntu, please run the script `cd dependencies; ./kernel_deps.sh; cd ..`
2. SplitFS: Compiling SplitFS requires installing Boost. For Ubuntu, please run `cd dependencies; ./splitfs_deps.sh; cd ..`

## Limitations
SplitFS is under active development.
1. The current implementation of SplitFS handles the following system calls: `open, openat, close, read, pread64, write, pwrite64, fsync, unlink, ftruncate, fallocate, stat, fstat, lstat, dup, dup2, execve and clone`. The rest of the calls are passed through to the kernel.
2. The current implementation of SplitFS works correctly for the following applictions: `LevelDB running YCSB, SQLite running TPCC, tar, git, rsync`. This limitation is purely due to the state of the implementation, and we aim to increase the coverage of applications by supporting more system calls in the future.

## License

Copyright for SplitFS is held by the University of Texas at Austin. Please contact us if you would like to obtain a license to use SplitFS in your commercial product.

## Contributors

1. [Rohan Kadekodi](https://github.com/rohankadekodi), UT Austin
2. [Rui Wang](https://github.com/wraymo), Beijing University of Posts and Telecommunications
3. [Om Saran](https://github.com/OmSaran)

## Acknowledgements

We thank the National Science Foundation, VMware, Google, and Facebook for partially funding this project. We thank Intel and ETRI IITP/KEIT[2014-3-00035] for providing access to Optane DC Persistent Memory to perform our experiments.

## Contact

Please contact us at `rak@cs.utexas.edu` or `vijayc@utexas.edu` with any questions.
