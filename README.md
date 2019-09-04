## SplitFS

[SplitFS](https://github.com/rohankadekodi/SplitFS) is a file system for Persistent Memory (PM) which is aimed at reducing the software overhead of applications accessing Persistent Memory. SplitFS presents a novel split of responsibilities between a user-space library file system and an existing kernel PM file system. The user-space library file system handles data operations by intercepting POSIX calls, memory mapping the underlying file, and serving the reads and overwrites using processor loads and stores. Metadata operations are handled by the kernel file system (ext4 DAX). 

SplitFS introduces a new primitive termed **relink** to efficiently support file appends and atomic data operations. SplitFS provides three consistency modes,which different applications can choose from without interfering with each other.

The [Experiments
page](https://github.com/rohankadekodi/SplitFS/blob/master/experiments.md)
has a list of experiments evaluating SplitFS(strict, sync and POSIX) vs ext4 DAX, NOVA-strict, NOVA-relaxed and PMFS. The summary is that SplitFS outperforms the other file systems on the data intensive workloads, while incurring a modest overhead on metadata heavy workloads. Please see the paper for more details. This repository contains all the information needed to reproduce the main results from our paper.

Please cite the following paper if you use SplitFS:

**SplitFS: Reducing Software Overhead in File Systems for Persistent Memory**. Rohan Kadekodi, Se Kwon Lee, Sanidhya Kashyap, Taesoo Kim, Aasheesh Kolli, Vijay Chidambaram. *Proceedings of the The 27th ACM Symposium on Operating Systems Principles (SOSP 19)*. [Paper PDF](https://www.cs.utexas.edu/~vijay/papers/sosp19-splitfs.pdf). [Bibtex](https://www.cs.utexas.edu/~vijay/bibtex/sosp19-splitfs.bib)

## Features

1. **Low software overhead**. SplitFS tries to obtain performance that is close to the maximum provided by persistent-memory hardware. The overhead due to SplitFS software is significantly lower (by 4-12x) than state-of-the-art file systems such as NOVA or ext4 DAX. As a result, performance on some applications is increased by as much as **2x**.  

2. **Flexible guarantees**. SplitFS is the only persistent-memory file system that allows simultaneously running applications to receive different guarantees from the file system. SplitFS offers three modes: POSIX, Sync, and Strict. Application A may in Strict mode, obtaining atomic, synchronous operations from SplitFS, while Application B may simultaneously run in POSIX mode and obtain higher performance. This is possible due to the novel split architecture used in SplitFS. 

3.**Portability and Stability**. SplitFS uses ext4 DAX as its kernel component, so it works with any kernel where ext4 DAX is supported. ext4 DAX is a mature, robust code base that is actively being maintained and developed; as ext4 DAX performance increases over time, SplitFS performance increases as well. This is contrast to research file systems for persistent memory, which do not see development at the same rate as ext4 DAX.  

## Contents

1. `splitfs/` contains the source code for SplitFS-strict
2. `dependencies/` contains packages and scripts to resolve dependencies
3. `kernel/` contains the Linux 4.13.0 kernel
4. `leveldb/` contains LevelDB source code
5. `rsync/` contains the rsync source code
6. `scripts/` contains scripts to compile and run workloads and kernel
7. `splitfs-so/` contains the SplitFS-strict shared libraries for running different workloads
8. `sqlite3-trace/` contains SQLite3 source code
9. `tpcc-sqlite/` contains TPCC source code
10. `ycsb/` contains YCSB source code


## System Requirements

1. Ubuntu 16.04
2. At least 32 GB DRAM
3. At least 4 cores
4. Baremetal machine (Not a VM)
5. Intel Processor supporting `clflushopt` instruction (Introduced in Intel processor family -- Broadwell). This can be verified with `lscpu | grep clflushopt`

## Dependencies

1. kernel: Installing the linux kernel 4.13.0 involves installing `bc`, `libelf-dev` and `libncurses5-dev`. For ubuntu, please run the script `cd dependencies; ./kernel_deps.sh; cd ..`
2. LevelDB: Compiling LevelDB requires installing cmake version > 3.9. For ubuntu, please run `cd dependencies; ./leveldb_deps.sh; cd ..`
3. YCSB: Compiling YCSB requires installing JDK 8 as well as installing maven version 3. Please follow the steps below:
    * `$ sudo add-apt-repository ppa:openjdk-r/ppa`
    * `$ sudo apt update`
    * `$ sudo apt install openjdk-8-jdk`
    * `$ export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64`
    * `$ export PATH=$PATH:$JAVA_HOME/bin`
    * Check installation using `java -version`
    * `$ sudo apt install maven`
4. SplitFS: Compiling SplitFS requires installing `Boost`. For Ubuntu, please run `cd dependencies; ./splitfs_deps.sh; cd ..`

## License

SplitFS has two parts: a kernel component and a user-space component. The kernel component uses the Liunx ext4 file system, which uses the GPL license. The user-space component is released under the Apache License.

Copyright for SplitFS is held by the University of Texas at Austin. Please contact us if you would like to obtain a license to use SplitFS in your commercial product. 

## Acknowledgements

We thank the National Science Foundation, VMware, Google, and Facebook for partially funding this project.

## Contact

Please e-mail `vijayc@utexas.edu` and `rak@cs.utexas.edu` with any questions.
