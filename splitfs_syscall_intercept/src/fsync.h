/*
 * =====================================================================================
 *
 *       Filename:  fsync.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  09/28/2019 11:42:45 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#ifndef SPLITFS_FSYNC_H
#define SPLITFS_FSYNC_H

#include <nv_common.h>
#include "file.h"

#define FSYNC_POLICY_NONE 0
#define FSYNC_POLICY_FLUSH_ON_FSYNC 1
#define FSYNC_POLICY_UNCACHEABLE_MAP 2
#define FSYNC_POLICY_NONTEMPORAL_WRITES 3
#define FSYNC_POLICY_FLUSH_ON_WRITE 4

#define FSYNC_MEMCPY MEMCPY
#define FSYNC_MMAP MMAP
#define FSYNC_FSYNC(nvf,cpuid,close,fdsync) fsync_flush_on_fsync(nvf,cpuid,close,fdsync)

void fsync_flush_on_fsync(struct NVFile* nvf, int cpuid, int close, int fdsync);

#endif
