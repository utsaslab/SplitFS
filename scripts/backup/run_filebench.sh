#!/bin/bash

filebench_dir=`readlink -f ../filebench`

$filebench_dir/filebench -f $filebench_dir/workloads/fileserver.f
