// Header file for nvmfileops.c

#ifndef __LEDGER_TBL_MMAPS_H_
#define __LEDGER_TBL_MMAPS_H_

#include <inttypes.h>
#include <sys/types.h>
#include <stdint.h>
#include "debug.h"
#include <assert.h>
#include "ledger.h"

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
int clear_tbl_mmap_entry(struct NVTable_maps *tbl, int size);

#endif
