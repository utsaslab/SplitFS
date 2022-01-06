/*
 * =====================================================================================
 *
 *       Filename:  mmap_cache.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:58:38 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#ifndef SPLITFS_MMAP_CACHE_H
#define SPLITFS_MMAP_CACHE

#include <nv_common.h>
#include "inode.h"

struct InodeToMapping
{
	ino_t serialno;
	unsigned long *root;
	unsigned long *merkle_root;
	unsigned long *root_dirty_cache;
	int root_dirty_num;
	int total_dirty_mmaps;
	unsigned int height;
	char buffer[16];
};

extern struct InodeToMapping* _nvp_ino_mapping;

#define MMAP_CACHE_ENTRIES 1024

void nvp_add_to_inode_mapping(struct NVNode *node, ino_t serialno);
int nvp_retrieve_inode_mapping(struct NVNode *node);

#endif
