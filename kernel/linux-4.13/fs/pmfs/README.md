# Porting PMFS to the latest Linux kernel

## Introduction

PMFS is a file system for persistent memory, developed by Intel.
For more details about PMFS, please check the git repository:

https://github.com/linux-pmfs/pmfs

This project ports PMFS to the latest Linux kernel so developers can compare PMFS to other file systems on the new kernel.

## Building PMFS
The master branch works on the 4.15 version of x86-64 Linux kernel.

To build PMFS, simply run a

~~~
#make
~~~

command.

## Running PMFS
PMFS runs on a physically contiguous memory region that is not used by the Linux kernel, and relies on the kernel NVDIMM support.

To run PMFS, first build up your kernel with NVDIMM support enabled (`CONFIG_BLK_DEV_PMEM`), and then you can
reserve the memory space by booting the kernel with `memmap` command line option.

For instance, adding `memmap=16G!8G` to the kernel boot parameters will reserve 16GB memory starting from 8GB address, and the kernel will create a `pmem0` block device under the `/dev` directory.

After the OS has booted, you can initialize a PMFS instance with the following commands:


~~~
#insmod pmfs.ko
#mount -t pmfs -o init /dev/pmem0 /mnt/ramdisk 
~~~

The above commands create a PMFS instance on pmem0 device, and mount on `/mnt/ramdisk`.

To recover an existing PMFS instance, mount PMFS without the init option, for example:

~~~
#mount -t pmfs /dev/pmem0 /mnt/ramdisk 
~~~

There are two scripts provided in the source code, `setup-pmfs.sh` and `remount-pmfs.sh` to help setup PMFS.

## Current limitations

* PMFS only works on x86-64 kernels.
* PMFS does not currently support extended attributes or ACL.
* PMFS requires the underlying block device to support DAX (Direct Access) feature.
* This project cuts some features of the original PMFS, such as memory protection and huge mmap support. If you need these features, please turn to the original PMFS.
