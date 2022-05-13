/*
 * =====================================================================================
 *
 *       Filename:  table_mmaps.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/09/2019 03:01:44 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */

#ifndef SPLITFS_TBL_MMAPS_H
#define SPLITFS_TBL_MMAPS_H

#include "handle_mmaps.h"

struct table_mmap;

void *splitfs_get_tbl_entry(struct table_mmap *tbl, off_t start_offset, size_t *extent_length);
void splitfs_insert_tbl_entry(struct table_mmap *tbl, off_t fd_start_offset,
        off_t st_start_offset, size_t length, unsigned long buf_start);
struct table_mmap *splitfs_alloc_tbl(void);
void splitfs_clear_tbl_mmaps(struct table_mmap *tbl);

#endif
