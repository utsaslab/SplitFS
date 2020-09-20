#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"

# Single threaded asynchronous random writes (8KB I/Os) on a 1GB file.
# A fsync is issued after 16K ($iters) worth of writes.
# Stops after one ($count) fsync.

set $dir=/mnt/pmem_emul
set $cached=false
set $count=1000000
set $filesize=4g
set $iosize=4k
set $iters=1
set $nthreads=1

set mode quit firstdone
      
define file name=bigfile,path=$dir,size=$filesize,prealloc

define process name=filewriter,instances=1
{
  thread name=filewriterthread,memsize=10m,instances=$nthreads
  {
    flowop write name=write-file,filename=bigfile,random,iosize=$iosize,iters=$iters
    flowop fsync name=sync-file
    #flowop finishoncount name=finish,value=$count,target=sync-file
  }
}

echo  "FileMicro-WriteRandFsync Version 2.1 personality successfully loaded"
run 20
