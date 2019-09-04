## SplitFS

[SplitFS](https://github.com/rohankadekodi/SplitFS) is a file system for Persistent Memory (PM) which is aimed at reducing the software overhead of applications accessing Persistent Memory. SplitFS presents a novel split of responsibilities between a user-space library file system and an existing kernel PM file system. The user-space library file system handles data operations by intercepting POSIX calls, memory mapping the underlying file, and serving the reads and overwrites using processor loads and stores. Metadata operations are handled by the kernel file system (ext4 DAX). 

SplitFS introduces a new primitive termed **relink** to efficiently support file appends and atomic data operations. SplitFS provides three consistency modes,which different applications can choose from without interfering with each other.

The [Experiments
page](https://github.com/rohankadekodi/SplitFS/blob/master/experiments.md)
has a list of experiments evaluating SplitFS(strict, sync and POSIX) vs ext4 DAX, NOVA-strict, NOVA-relaxed and PMFS. The summary is that SplitFS outperforms the other file systems on the data intensive workloads, while incurring a modest overhead on metadata heavy workloads. Please see the paper for more details. 

Please cite the following paper if you use SplitFS:

**SplitFS: Reducing Software Overhead in File Systems for Persistent Memory**. Rohan Kadekodi, Se Kwon Lee, Sanidhya Kashyap, Taesoo Kim, Aasheesh Kolli, Vijay Chidambaram. *Proceedings of the The 27th ACM Symposium on Operating Systems Principles (SOSP 19)*. [Paper PDF](https://www.cs.utexas.edu/~vijay/papers/sosp19-splitfs.pdf). [Bibtex](https://www.cs.utexas.edu/~vijay/bibtex/sosp19-splitfs.bib)

---

### Contents

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

---

### System Requirements

1. Ubuntu 16.04
2. At least 32 GB DRAM
3. At least 4 cores
4. Baremetal machine (Not a VM)
5. Intel Processor supporting `clflushopt` instruction (Introduced in Intel processor family -- Broadwell). This can be verified with `lscpu | grep clflushopt`

---

### Dependencies

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

---

### Contact

In case of any difficulties, please send an e-mail to `rak@cs.utexas.edu`
