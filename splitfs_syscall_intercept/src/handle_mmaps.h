/*
 * =====================================================================================
 *
 *       Filename:  handle_mmaps.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/26/2019 01:11:04 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#ifndef SPLITFS_HANDLE_MMAPS_H
#define SPLITFS_HANDLE_MMAPS_H

#include <stdatomic.h>
#include <nv_common.h>
#include "file.h"
#include "tbl_mmaps.h"
#include "non_temporal.h"

#define MAP_SIZE 16

#if MAP_SIZE == 512
#define MAX_MMAP_SIZE 536870912
#elif MAP_SIZE == 256
#define MAX_MMAP_SIZE 268435456
#elif MAP_SIZE == 128
#define MAX_MMAP_SIZE 134217728
#elif MAP_SIZE == 64
#define MAX_MMAP_SIZE 67108864
#elif MAP_SIZE == 32
#define MAX_MMAP_SIZE 33554432
#elif MAP_SIZE == 16
#define MAX_MMAP_SIZE 16777216
#elif MAP_SIZE == 8
#define MAX_MMAP_SIZE 8388608
#elif MAP_SIZE == 4
#define MAX_MMAP_SIZE 4194304
#elif MAP_SIZE == 2
#define MAX_MMAP_SIZE 2097152
#else
#define MAX_MMAP_SIZE 536870912
#endif

#define ANON_MAP_SIZE 16

#if ANON_MAP_SIZE == 512
#define ANON_MAX_MMAP_SIZE 536870912
#elif ANON_MAP_SIZE == 256
#define ANON_MAX_MMAP_SIZE 268435456
#elif ANON_MAP_SIZE == 128
#define ANON_MAX_MMAP_SIZE 134217728
#elif ANON_MAP_SIZE == 64
#define ANON_MAX_MMAP_SIZE 67108864
#elif ANON_MAP_SIZE == 32
#define ANON_MAX_MMAP_SIZE 33554432
#elif ANON_MAP_SIZE == 16
#define ANON_MAX_MMAP_SIZE 16777216
#elif ANON_MAP_SIZE == 8
#define ANON_MAX_MMAP_SIZE 8388608
#elif ANON_MAP_SIZE == 4
#define ANON_MAX_MMAP_SIZE 4194304
#elif ANON_MAP_SIZE == 2
#define ANON_MAX_MMAP_SIZE 2097152
#else
#define ANON_MAX_MMAP_SIZE 536870912
#endif

int MMAP_PAGE_SIZE;
int MMAP_HUGEPAGE_SIZE;
#define PER_NODE_MAPPINGS 10

#define	ALIGN_MMAP_DOWN(addr)	((addr) & ~(MAX_MMAP_SIZE - 1))

void *intel_memcpy(void * __restrict__ b, const void * __restrict__ a, size_t n);

#define MEMCPY intel_memcpy
#define MEMCPY_NON_TEMPORAL memmove_nodrain_movnt_granularity
#define MMAP mmap

extern atomic_uint_fast64_t dr_mem_allocated;

void create_dr_mmap(struct NVNode *node, int is_overwrite);
void change_dr_mmap(struct NVNode *node, int is_overwrite);
void nvp_free_dr_mmaps();
void nvp_reset_mappings(struct NVNode *node);
int nvp_get_over_dr_address(struct NVFile *nvf,
				   off_t offset,
				   size_t len_to_write, 
				   unsigned long *mmap_addr,
				   off_t *offset_within_mmap,
				   size_t *extent_length,
				   int wr_lock,
				   int cpuid,
				   struct NVTable_maps *tbl_app,
				   struct NVTable_maps *tbl_over);
int nvp_get_mmap_address(struct NVFile *nvf, 
						off_t offset, 
						size_t count, 
						unsigned long *mmap_addr, 
						unsigned long *bitmap_root, 
						off_t *offset_within_mmap, 
						size_t *extent_length, 
						int wr_lock, 
						int cpuid, 
						struct NVTable_maps *tbl_app, 
						struct NVTable_maps *tbl_over);
int nvp_get_dr_mmap_address(struct NVFile *nvf, off_t offset, size_t len_to_write, size_t count, unsigned long *mmap_addr, off_t *offset_within_mmap, size_t *extent_length, int wr_lock, int cpuid, int iswrite, struct NVTable_maps *tbl_app, struct NVTable_maps *tbl_over);
void nvp_free_btree(unsigned long *root, unsigned long *merkle_root, unsigned long height, unsigned long *dirty_cache, int root_dirty_num, int total_dirty_mmaps);

#endif
