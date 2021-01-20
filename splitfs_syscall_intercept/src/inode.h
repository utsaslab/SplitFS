/*
 * =====================================================================================
 *
 *       Filename:  inode.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:15:18 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#ifndef SPLITFS_INODE_H
#define SPLITFS_INODE_H

#include <nv_common.h>
#include "nvp_lock.h"
#include "staging.h"

struct NVNode
{
	ino_t serialno;
	ino_t backup_serialno;
	NVP_LOCK_DECL;

	unsigned long true_length;
	volatile size_t length;
	volatile size_t maplength;
	unsigned long *root;
	unsigned long *merkle_root;
	int free_list_idx;
	int async_file_close;
	unsigned long *root_dirty_cache;
	int root_dirty_num;
	int total_dirty_mmaps;
	unsigned int height;
	volatile int reference;
	int isRootSet;
	int index_in_free_list;
	int is_large_file;

	// DR stuff
	struct free_dr_pool dr_info;
	struct free_dr_pool dr_over_info;
	uint64_t dr_mem_used;
};

void nvp_transfer_to_free_dr_pool(struct NVNode*);

#define NUM_NODE_LISTS 1
#define NODE_LOCKING 1
#define LARGE_FILE_THRESHOLD (300*1024*1024)

#if NODE_LOCKING

#define NVP_LOCK_NODE_RD(nvf, cpuid)	NVP_LOCK_RD(nvf->node->lock, cpuid)
#define NVP_UNLOCK_NODE_RD(nvf, cpuid)	NVP_LOCK_UNLOCK_RD(nvf->node->lock, cpuid)
#define NVP_LOCK_NODE_WR(nvf)		NVP_LOCK_WR(	   nvf->node->lock)
#define NVP_UNLOCK_NODE_WR(nvf)		NVP_LOCK_UNLOCK_WR(nvf->node->lock)

#else

#define NVP_LOCK_NODE_RD(nvf, cpuid) {(void)(cpuid);}
#define NVP_UNLOCK_NODE_RD(nvf, cpuid) {(void)(cpuid);}
#define NVP_LOCK_NODE_WR(nvf) {(void)(nvf->node->lock);}
#define NVP_UNLOCK_NODE_WR(nvf)	{(void)(nvf->node->lock);}

#endif

extern int _nvp_ino_lookup[1024];
extern pthread_spinlock_t node_lookup_lock[NUM_NODE_LISTS];

#endif
