/*
 * =====================================================================================
 *
 *       Filename:  staging.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 04:00:51 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#ifndef SPLITFS_STAGING_H
#define SPLITFS_STAGING_H

#include <nv_common.h>
#include "lfq.h"
// #include "liblfds711/inc/liblfds711.h"

#define DR_APPEND_PATH "/mnt/pmem_emul/DR-XXXXXX"
#define DR_OVER_PATH "/mnt/pmem_emul/DR-OVER-XXXXXX"

struct free_dr_pool
{
	unsigned long start_addr;
	int dr_fd;
	ino_t dr_serialno;
	unsigned long valid_offset;
	unsigned long dr_offset_start;
	unsigned long dr_offset_end;
};

struct full_dr {
	int dr_fd;
	unsigned long start_addr;
	size_t size;
};

#define INIT_NUM_DR 2
#define INIT_NUM_DR_OVER 2
#define BG_NUM_DR 1

#define DR_SIZE (256*1024*1024)
#define DR_OVER_SIZE (256*1024*1024)

extern struct full_dr* _nvp_full_drs;
extern int full_dr_idx;

struct lfq_ctx staging_mmap_queue_ctx;
struct lfq_ctx staging_over_mmap_queue_ctx;

#endif

