/*
 * =====================================================================================
 *
 *       Filename:  table_mmaps.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/09/2019 01:18:44 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <stdbool.h>
#include "table_mmaps.h"
#include <sys/mman.h>

struct table_entry {
    long fd_start_offset;
    long fd_end_offset;
    long st_start_offset;
    long st_end_offset;
    unsigned long buf_start;
};

struct table_mmap {
    struct table_entry *mmaps;
    int tbl_mmap_idx;
};

static int find_idx_to_read(off_t start_offset,
		     struct table_mmap *tbl)
{
	int idx_bin = 0;
	int left = 0, right = tbl->tbl_mmap_idx - 1;
	int mid = (right + left) / 2;

	if (right < left) {
		return -1;
	}

	if (mid < 0)
		ASSERT(false);
	if (left < 0)
		ASSERT(false);
	if (right < 0)
		ASSERT(false);

	while (left <= right) {
		mid = (right + left) / 2;

		if (tbl->mmaps[mid].fd_end_offset < start_offset) {
			left = mid + 1;
			continue;
		}

		if (tbl->mmaps[mid].fd_end_offset >= start_offset &&
		    tbl->mmaps[mid].fd_start_offset <= start_offset) {
			idx_bin = mid;
			goto out;
		}

		if (tbl->mmaps[mid].fd_end_offset >= start_offset &&
		    tbl->mmaps[mid].fd_start_offset > start_offset) {
			right = mid - 1;
			continue;
		}
	}

	idx_bin = -1;

 out:
	return idx_bin;
}

void splitfs_insert_tbl_entry(struct table_mmap *tbl, off_t fd_start_offset,
        off_t st_start_offset, size_t length, unsigned long buf_start) {

    off_t st_prev_start = 0, st_prev_end = 0, fd_prev_end = 0, fd_end_offset = 0, st_end_offset = 0;
    size_t prev_size;
    unsigned long prev_buf_start = 0, prev_buf_end = 0;
    int max_idx = 0;

    fd_end_offset = fd_start_offset + (off_t)length - 1;
    st_end_offset = st_start_offset + (off_t)length - 1;

    LOG(0, "Doing insert entry. tbl_mmap_idx = %d", tbl->tbl_mmap_idx);
    max_idx = tbl->tbl_mmap_idx;
    if (max_idx == 0)
        goto add_entry;

    st_prev_start = tbl->mmaps[max_idx-1].st_start_offset;
    st_prev_end = tbl->mmaps[max_idx-1].st_end_offset;
    fd_prev_end = tbl->mmaps[max_idx-1].fd_end_offset;
    prev_buf_start = tbl->mmaps[max_idx-1].buf_start;
    prev_size = (size_t)st_prev_end - (size_t)st_prev_start + 1;
    prev_buf_end = (unsigned long) (prev_buf_start + prev_size - 1);

    if ((buf_start == prev_buf_end + 1) &&
        (fd_start_offset == fd_prev_end + 1)) {
        tbl->mmaps[max_idx-1].fd_end_offset = fd_end_offset;
        tbl->mmaps[max_idx-1].st_end_offset = st_end_offset;
        return;
    }

add_entry:
    tbl->mmaps[max_idx].fd_start_offset = fd_start_offset;
	tbl->mmaps[max_idx].st_start_offset = st_start_offset;
    tbl->mmaps[max_idx].fd_end_offset = fd_end_offset;
    tbl->mmaps[max_idx].st_end_offset = st_end_offset;
    tbl->mmaps[max_idx].buf_start = buf_start;
    LOG(0, "inserted at idx = %d, buf_start = %p, fd_start = %ld, st_start = %ld, count = %lu",
            max_idx, (void*)buf_start, fd_start_offset, st_start_offset, length);
    tbl->tbl_mmap_idx++;
}

void *splitfs_get_tbl_entry(struct table_mmap *tbl, off_t start_offset, size_t *extent_length) {

    void *mmap_addr = NULL;
    *extent_length = 0;
    int idx = 0;

    idx = find_idx_to_read(start_offset, tbl);

    LOG(0, "find_idx_to_read returned id = %d", idx);
    if (idx != -1) {
        off_t start_off_diff = start_offset - tbl->mmaps[idx].fd_start_offset;
        off_t tbl_entry_start_off = tbl->mmaps[idx].st_start_offset + start_off_diff;
        size_t tbl_entry_len = (size_t)tbl->mmaps[idx].st_end_offset - (size_t)tbl_entry_start_off + 1;
        *extent_length = tbl_entry_len;
        mmap_addr = (void *) ((unsigned long)tbl->mmaps[idx].buf_start + (unsigned long)start_off_diff);
        LOG(0, "tbl_entry_len = %lu, st_end_offset = %ld, tbl_entry_start_offset = %ld, tbl entry file start off = %ld, tbl entry file end off = %ld", tbl_entry_len,
                tbl->mmaps[idx].st_end_offset, tbl_entry_start_off, tbl->mmaps[idx].fd_start_offset, tbl->mmaps[idx].fd_end_offset);
    }

    LOG(0, "returning mmap_addr = %p, extent_length = %lu", mmap_addr, *extent_length);

    return mmap_addr;
}

struct table_mmap *splitfs_alloc_tbl() {

    struct table_mmap *entry = NULL;
    entry = (struct table_mmap *) calloc(1, sizeof(struct table_mmap));
    entry->mmaps = (struct table_entry *) calloc(PER_NODE_MMAPS, sizeof(struct table_entry));
    entry->tbl_mmap_idx = 0;
    return entry;
}

void splitfs_clear_tbl_mmaps(struct table_mmap *tbl) {

    for (long idx = 0; idx < tbl->tbl_mmap_idx; idx++) {
        size_t count = (size_t) (tbl->mmaps[idx].fd_end_offset - tbl->mmaps[idx].fd_start_offset + 1);
        munmap((char *)(tbl->mmaps[idx].buf_start), count);
    }
    tbl->tbl_mmap_idx = 0;
}
