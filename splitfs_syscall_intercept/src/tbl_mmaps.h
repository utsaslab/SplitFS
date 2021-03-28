// Header file for nvmfileops.c

#ifndef __LEDGER_TBL_MMAPS_H_
#define __LEDGER_TBL_MMAPS_H_

#include <inttypes.h>
#include <sys/types.h>
#include <stdint.h>
#include <nv_common.h>
#include "inode.h"
#include "nvp_lock.h"

struct table_mmaps
{
	off_t file_start_off;
	off_t file_end_off;
	off_t dr_start_off;
	off_t dr_end_off;
	unsigned long buf_start;
};

struct NVTable_maps
{
	NVP_LOCK_DECL;
	struct table_mmaps *tbl_mmaps;
	int tbl_mmap_index;
};

struct NVTable_regions
{
	struct table_mmaps *tbl_mmaps;
	off_t lowest_off;
	off_t highest_off;
	int tbl_mmap_index;
	int region_dirty;
};

struct NVLarge_maps
{
	NVP_LOCK_DECL;
	struct NVTable_regions *regions;
	int num_tbl_mmaps;
	int num_regions;
	int min_dirty_region;
	int max_dirty_region;
};

extern struct NVTable_maps *_nvp_tbl_mmaps;
extern struct NVTable_maps *_nvp_over_tbl_mmaps;
extern struct NVLarge_maps *_nvp_tbl_regions;

void get_lowest_tbl_elem(off_t *over_file_start,
				off_t *over_file_end,
				off_t *over_dr_start,
				off_t *over_dr_end,
				struct NVTable_maps *tbl,
				int idx_in_over);

void get_tbl_elem_large(off_t *over_file_start,
			       off_t *over_file_end,
			       off_t *over_dr_start,
			       off_t *over_dr_end,
			       struct table_mmaps *tbl_mmaps,
			       int idx_in_over);

int get_lowest_tbl_elem_large(off_t *over_file_start,
				     off_t *over_file_end,
				     off_t *over_dr_start,
				     off_t *over_dr_end,
				     struct table_mmaps *tbl_mmaps,
				     int tbl_mmap_index,
				     off_t max_value);

void insert_tbl_mmap_entry(struct NVNode *node,
			   off_t file_off_start,
			   off_t dr_off_start,
			   size_t length,
			   unsigned long buf_start);
void insert_over_tbl_mmap_entry(struct NVNode *node,
				off_t file_off_start,
				off_t dr_off_start,
				size_t length,
				unsigned long buf_start);
int read_tbl_mmap_entry(struct NVNode *node,
			off_t file_off_start,
			size_t length,
			unsigned long *mmap_addr,
			size_t *extent_length,
			int check_append_entry);
int clear_tbl_mmap_entry(struct NVTable_maps *tbl, int num_entries);

#define TBL_MMAP_LOCKING 1
#if TBL_MMAP_LOCKING

#define TBL_ENTRY_LOCK_RD(tbl, cpuid)    {if (tbl) {NVP_LOCK_RD(tbl->lock, cpuid);}}
#define TBL_ENTRY_UNLOCK_RD(tbl, cpuid)  {if (tbl) {NVP_LOCK_UNLOCK_RD(tbl->lock, cpuid);}}
#define TBL_ENTRY_LOCK_WR(tbl)           {if (tbl) {NVP_LOCK_WR(tbl->lock);}}
#define TBL_ENTRY_UNLOCK_WR(tbl)         {if (tbl) {NVP_LOCK_UNLOCK_WR(tbl->lock);}}

#else

#define TBL_ENTRY_LOCK_RD(tbl, cpuid)    {(void)(cpuid);}
#define TBL_ENTRY_UNLOCK_RD(tbl, cpuid)  {(void)(cpuid);}
#define TBL_ENTRY_LOCK_WR(tbl)           {(void)(tbl->lock);}
#define TBL_ENTRY_UNLOCK_WR(tbl)         {(void)(tbl->lock);}

#endif

#define LARGE_TBL_MAX 5
#define APPEND_TBL_MAX 4096
#define OVER_TBL_MAX 4096
#define NUM_APP_TBL_MMAP_ENTRIES 1024
#define NUM_OVER_TBL_MMAP_ENTRIES 1024

#define REGION_COVERAGE (40*1024)
#define LARGE_TBL_REGIONS (500*1024*1024 / REGION_COVERAGE)
#define PER_REGION_TABLES 100 // (REGION_COVERAGE / 1024)

#endif
