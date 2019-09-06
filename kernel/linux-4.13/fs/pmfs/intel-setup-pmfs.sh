#!/bin/sh

umount /mnt/ramdisk
rmmod pmfs
rmmod pmem
insmod pmfs.ko measure_timing=0

sleep 1

mount -t pmfs -o physaddr=0x10000000000,init=64G none /mnt/ramdisk

#cp test1 /mnt/ramdisk/
#dd if=/dev/zero of=/mnt/ramdisk/test1 bs=1M count=1024 oflag=direct
