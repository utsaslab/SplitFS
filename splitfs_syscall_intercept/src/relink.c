/*
 * =====================================================================================
 *
 *       Filename:  relink.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:39:05 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include "timers.h"
#include "add_delay.h"
#include "handle_mmaps.h"
#include "tbl_mmaps.h"
#include "nvp_lock.h"
#include "staging.h"
#include "log.h"
#include "relink.h"
#include "utils.h"

#if DATA_JOURNALING_ENABLED

static inline size_t dynamic_remap_large(int file_fd, struct NVNode *node, int close)
{
	size_t len_to_write = 0, len_written = 0, len_to_swap = 0, len_swapped = 0;
	off_t app_start_addr = 0;
	off_t app_start_off = 0;
	off_t file_start_off = node->true_length;
	off_t over_file_start = 0, over_file_end = 0;
	off_t over_dr_start = 0, over_dr_end = 0;
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[node->serialno % OVER_TBL_MAX];
	struct NVTable_regions *regions = _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].regions;
	int region_id = 0;
	int valid = 0, i = 0, tbl_idx = 0;
	int max_region_id = 0;
	instrumentation_type swap_extents_time, insert_tbl_mmap_time;

	DEBUG_FILE("%s: START: file_fd = %d. dr start addr = %p, dr over start addr = %p, true_length = %lu, length = %lu, Inode number = %lu\n",
		   __func__, file_fd, node->dr_info.start_addr, node->dr_over_info.start_addr, node->true_length, node->length, node->serialno);

	if (node->dr_info.start_addr == 0 && node->dr_over_info.start_addr == 0)
		return 0;

	if (node->dr_info.start_addr != 0) {
		app_start_addr = node->dr_info.start_addr;
		app_start_off = node->dr_info.dr_offset_start;
	}

	i = _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region;
	while (i < _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].max_dirty_region) {
		if (regions[i].highest_off >= node->true_length) {
			break;
		}
		i++;
	}

	max_region_id = _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].max_dirty_region;
	region_id = _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region;
	while (region_id <= max_region_id) {
		tbl_idx = 0;
		while (tbl_idx < tbl_over->tbl_mmap_index) {
			get_tbl_elem_large(&over_file_start,
					   &over_file_end,
					   &over_dr_start,
					   &over_dr_end,
					   regions[region_id].tbl_mmaps,
					   tbl_idx);

			if (over_dr_end == 0) {
				tbl_idx++;
				continue;
			}

			if (over_dr_start > over_dr_end) {
				MSG("%s: over_file_start = %lld, over_file_end = %lld, over_dr_start = %lld, over_dr_end = %lld\n", __func__, over_file_start, over_file_end, over_dr_start, over_dr_end);
				assert(0);
			}
			if (over_file_start > over_file_end)
				assert(0);
			if (over_dr_start > node->dr_over_info.dr_offset_end)
				assert(0);

			len_to_swap = over_file_end - over_file_start + 1;
			START_TIMING(swap_extents_t, swap_extents_time);
			DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, over_dr fd = %d, file_start = %lld, over_dr start = %lld, over_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_over_info.dr_fd, over_file_start, over_dr_start, (const char *) node->dr_over_info.start_addr, len_to_swap);
			len_swapped = syscall(335, file_fd,
					      node->dr_over_info.dr_fd,
					      over_file_start,
					      over_dr_start,
					      (const char *) node->dr_over_info.start_addr,
					      len_to_swap);

			tbl_over->tbl_mmaps[tbl_idx].dr_end_off = 0;
			END_TIMING(swap_extents_t, swap_extents_time);
			num_appendfsync++;
			tbl_idx++;
		}
		regions[region_id].region_dirty = 0;
		if (_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region == region_id)
			_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region++;

		region_id++;
	}

	while (region_id < _nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].max_dirty_region) {
		tbl_idx = 0;
		while (regions[region_id].region_dirty == 1 && tbl_idx < regions[region_id].tbl_mmap_index) {
			valid = get_lowest_tbl_elem_large(&over_file_start,
							  &over_file_end,
							  &over_dr_start,
							  &over_dr_end,
							  regions[region_id].tbl_mmaps,
							  regions[region_id].tbl_mmap_index,
							  regions[region_id].highest_off);

			if (valid == 0)
				break;

			if (over_dr_start > over_dr_end)
				assert(0);
			if (over_file_start > over_file_end)
				assert(0);
			if (over_dr_start > node->dr_over_info.dr_offset_end)
				assert(0);

			if (file_start_off < over_file_start && app_start_addr != 0) {
				len_to_swap = over_file_start - file_start_off + 1;
				app_start_off = node->dr_info.dr_offset_start +
					file_start_off - node->true_length;
				app_start_addr = node->dr_info.start_addr +
					app_start_off;

				if (app_start_off > node->dr_info.dr_offset_end)
					assert(0);

				// Perform swap extents from append DR file
				START_TIMING(swap_extents_t, swap_extents_time);
				DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, app_dr fd = %d, file_start = %lld, app_dr start = %lld, app_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_info.dr_fd, file_start_off, app_start_off, (const char *) node->dr_info.start_addr, len_to_swap);
				len_swapped = syscall(335, file_fd,
						      node->dr_info.dr_fd,
						      file_start_off,
						      app_start_off,
						      (const char *) node->dr_info.start_addr,
						      len_to_swap);

				END_TIMING(swap_extents_t, swap_extents_time);
				num_appendfsync++;
				len_written += len_swapped;
				file_start_off += len_swapped;
				START_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
				insert_tbl_mmap_entry(node,
						      file_start_off,
						      app_start_off,
						      len_swapped,
						      app_start_addr);
				END_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			}

			if (over_dr_start > over_dr_end)
				assert(0);
			if (over_file_start > over_file_end)
				assert(0);
			if (over_file_start != file_start_off)
				assert(0);
			if (over_dr_start > node->dr_over_info.dr_offset_end)
				assert(0);

			// Perform swap extents based on over file
			START_TIMING(swap_extents_t, swap_extents_time);
			len_to_swap = over_file_end - over_file_start + 1;
			DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, over_dr fd = %d, file_start = %lld, over_dr start = %lld, over_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_over_info.dr_fd, file_start_off, over_dr_start, (const char *) node->dr_over_info.start_addr, len_to_swap);
			len_swapped = syscall(335, file_fd,
					      node->dr_over_info.dr_fd,
					      over_file_start,
					      over_dr_start,
					      (const char *) node->dr_over_info.start_addr,
					      len_to_swap);

			tbl_over->tbl_mmaps[tbl_idx].dr_end_off = 0;
			END_TIMING(swap_extents_t, swap_extents_time);
			num_appendfsync++;

			if (over_file_start > node->true_length)
				file_start_off += len_swapped;
			len_written += len_swapped;

			tbl_idx++;
		}

		regions[region_id].region_dirty = 0;
		region_id++;
		if (_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region == region_id)
			_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region++;
	}

	_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].min_dirty_region = LARGE_TBL_REGIONS;
	_nvp_tbl_regions[node->serialno % LARGE_TBL_MAX].max_dirty_region = 0;


	if (app_start_addr != 0) {
		app_start_off = node->dr_info.dr_offset_start +
			file_start_off - node->true_length;

		if (node->dr_info.dr_offset_start > node->dr_info.dr_offset_end)
			assert(0);
		if (app_start_off > node->dr_info.dr_offset_end)
			assert(0);
		if ((app_start_off % MMAP_PAGE_SIZE) != (file_start_off % MMAP_PAGE_SIZE))
			assert(0);

		len_to_swap = node->dr_info.dr_offset_end - app_start_off;

		if (len_written < len_to_swap) {
			app_start_addr = node->dr_info.start_addr + app_start_off;

			DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, app_dr fd = %d, file_start = %lld, app_dr start = %lld, app_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_info.dr_fd, file_start_off, app_start_off, (const char *) node->dr_info.start_addr, len_to_swap);
			// Perform swap extents from append DR file
			START_TIMING(swap_extents_t, swap_extents_time);
			len_swapped = syscall(335, file_fd,
					      node->dr_info.dr_fd,
					      file_start_off,
					      app_start_off,
					      (const char *) node->dr_info.start_addr,
					      len_to_swap);

			if (len_swapped != len_to_swap)
				assert(0);

			END_TIMING(swap_extents_t, swap_extents_time);
			num_appendfsync++;
			START_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			insert_tbl_mmap_entry(node,
					      file_start_off,
					      app_start_off,
					      len_swapped,
					      app_start_addr);
			END_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			len_written += len_swapped;
		}
	}
	return len_written;
}

static inline size_t dynamic_remap_updates(int file_fd, struct NVNode *node, int close, off_t *file_start_off)
{
	size_t len_to_write = 0, len_written = 0, len_to_swap = 0, len_swapped = 0;
	off_t app_start_addr = 0;
	off_t app_start_off = 0;
	off_t over_file_start = 0, over_file_end = 0;
	off_t over_dr_start = 0, over_dr_end = 0;
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[node->serialno % OVER_TBL_MAX];
	int idx_in_over = 0;
	instrumentation_type swap_extents_time, insert_tbl_mmap_time;

	if (node->dr_info.start_addr != 0) {
		app_start_addr = node->dr_info.start_addr;
		app_start_off = node->dr_info.dr_offset_start;
	}

	DEBUG_FILE("%s: START: file_fd = %d. dr start addr = %p, dr over start addr = %p, true_length = %lu, length = %lu, Inode number = %lu\n",
		   __func__, file_fd, node->dr_info.start_addr, node->dr_over_info.start_addr, node->true_length, node->length, node->serialno);

	if (node->dr_over_info.start_addr == 0)
		return 0;

	if (node->is_large_file)
		return dynamic_remap_large(file_fd, node, close);

	while (idx_in_over < tbl_over->tbl_mmap_index) {
		get_lowest_tbl_elem(&over_file_start,
				    &over_file_end,
				    &over_dr_start,
				    &over_dr_end,
				    tbl_over,
				    idx_in_over);

		if (over_file_start >= node->true_length)
			break;

		if (over_dr_end == 0) {
			idx_in_over++;
			continue;
		}

		if (over_dr_start > over_dr_end) {
			MSG("%s: over_file_start = %lld, over_file_end = %lld, over_dr_start = %lld, over_dr_end = %lld\n", __func__, over_file_start, over_file_end, over_dr_start, over_dr_end);
			assert(0);
		}
		if (over_file_start > over_file_end)
			assert(0);
		if (over_dr_start > node->dr_over_info.dr_offset_end)
			assert(0);

		len_to_swap = over_file_end - over_file_start + 1;
		START_TIMING(swap_extents_t, swap_extents_time);
		DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, over_dr fd = %d, file_start = %lld, over_dr start = %lld, over_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_over_info.dr_fd, over_file_start, over_dr_start, (const char *) node->dr_over_info.start_addr, len_to_swap);
		len_swapped = syscall(335, file_fd,
				      node->dr_over_info.dr_fd,
				      over_file_start,
				      over_dr_start,
				      (const char *) node->dr_over_info.start_addr,
				      len_to_swap);

		tbl_over->tbl_mmaps[idx_in_over].dr_end_off = 0;
		END_TIMING(swap_extents_t, swap_extents_time);
		num_appendfsync++;
		idx_in_over++;
	}

	while (idx_in_over < tbl_over->tbl_mmap_index) {
		get_lowest_tbl_elem(&over_file_start,
				    &over_file_end,
				    &over_dr_start,
				    &over_dr_end,
				    tbl_over,
				    idx_in_over);

		if (over_dr_end == 0) {
			idx_in_over++;
			continue;
		}

		if (over_dr_start > over_dr_end)
			assert(0);
		if (over_file_start > over_file_end)
			assert(0);
		if (over_dr_start > node->dr_over_info.dr_offset_end)
			assert(0);

		if (*file_start_off < over_file_start && app_start_addr != 0) {
			len_to_swap = over_file_start - *file_start_off + 1;
			app_start_off = node->dr_info.dr_offset_start +
				*file_start_off - node->true_length;
			app_start_addr = node->dr_info.start_addr +
				app_start_off;

			if (app_start_off > node->dr_info.dr_offset_end)
				assert(0);

			// Perform swap extents from append DR file
			START_TIMING(swap_extents_t, swap_extents_time);
			DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, app_dr fd = %d, file_start = %lld, app_dr start = %lld, app_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_info.dr_fd, file_start_off, app_start_off, (const char *) node->dr_info.start_addr, len_to_swap);
			len_swapped = syscall(335, file_fd,
					      node->dr_info.dr_fd,
					      *file_start_off,
					      app_start_off,
					      (const char *) node->dr_info.start_addr,
					      len_to_swap);

			END_TIMING(swap_extents_t, swap_extents_time);
			num_appendfsync++;
			len_written += len_swapped;
			*file_start_off += len_swapped;
			START_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			insert_tbl_mmap_entry(node,
					      *file_start_off,
					      app_start_off,
					      len_swapped,
					      app_start_addr);
			END_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
		}

		if (over_dr_start > over_dr_end)
			assert(0);
		if (over_file_start > over_file_end)
			assert(0);
		if (over_file_start != *file_start_off)
			assert(0);
		if (over_dr_start > node->dr_over_info.dr_offset_end)
			assert(0);

		// Perform swap extents based on over file
		START_TIMING(swap_extents_t, swap_extents_time);
		len_to_swap = over_file_end - over_file_start + 1;
		DEBUG_FILE("%s: Dynamic remap args: file_fd = %d, over_dr fd = %d, file_start = %lld, over_dr start = %lld, over_dr start addr = %p, len_to_swap = %lu\n", __func__, file_fd, node->dr_over_info.dr_fd, file_start_off, over_dr_start, (const char *) node->dr_over_info.start_addr, len_to_swap);
	        len_swapped = syscall(335, file_fd,
				      node->dr_over_info.dr_fd,
				      over_file_start,
				      over_dr_start,
				      (const char *) node->dr_over_info.start_addr,
				      len_to_swap);

		tbl_over->tbl_mmaps[idx_in_over].dr_end_off = 0;
		END_TIMING(swap_extents_t, swap_extents_time);
		num_appendfsync++;
		*file_start_off += len_swapped;
		len_written += len_swapped;

		idx_in_over++;
	}
	return 0;
}

#endif

size_t dynamic_remap(int file_fd, struct NVNode *node, int close) {
	size_t len_to_write = 0, len_written = 0, len_to_swap = 0, len_swapped = 0;
	off_t app_start_addr = 0;
	off_t app_start_off = 0;
	off_t file_start_off = node->true_length;
	off_t over_file_start = 0, over_file_end = 0;
	off_t over_dr_start = 0, over_dr_end = 0;
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[node->serialno % OVER_TBL_MAX];
	int idx_in_over = 0;
	instrumentation_type swap_extents_time, insert_tbl_mmap_time;

	DEBUG_FILE("%s: START: file_fd = %d. dr start addr = %p, dr over start addr = %p, true_length = %lu, length = %lu, Inode number = %lu\n",
		   __func__, file_fd, node->dr_info.start_addr, node->dr_over_info.start_addr, node->true_length, node->length, node->serialno);

#if DATA_JOURNALING_ENABLED

	len_written = dynamic_remap_updates(file_fd, node, close, &file_start_off);

#endif // DATA_JOURNALING_ENABLED

	len_written = 0;

	if (node->dr_info.start_addr == 0)
		return len_written;

	if (node->dr_info.start_addr != 0) {
		app_start_addr = node->dr_info.start_addr;
		app_start_off = node->dr_info.dr_offset_start;
	}

	if (node->dr_info.dr_offset_end - node->dr_info.dr_offset_start == 0)
		return len_written;

	if (node->dr_info.dr_offset_end < node->dr_info.dr_offset_start)
		return len_written;

	if (app_start_addr != 0) {
		app_start_off = node->dr_info.dr_offset_start +
			file_start_off - node->true_length;

		if (app_start_off > node->dr_info.dr_offset_end)
			assert(0);
		if ((app_start_off % MMAP_PAGE_SIZE) != (file_start_off % MMAP_PAGE_SIZE))
			assert(0);
		if (app_start_off < node->dr_info.valid_offset)
			assert(0);

		len_to_swap = node->dr_info.dr_offset_end - app_start_off;

		if (len_to_swap) {
			app_start_addr = node->dr_info.start_addr + app_start_off;

			DEBUG_FILE("%s: file_inode = %lu, dr_inode = %lu, file_fd = %d, dr_fd = %d, "
				   "valid_offset = %lld, file_offset = %lld, dr_offset = %lld, len = %lu\n",
				   __func__, node->serialno, node->dr_info.dr_serialno, file_fd,
				   node->dr_info.dr_fd, node->dr_info.valid_offset, file_start_off,
				   app_start_off, len_to_swap);

			// Perform swap extents from append DR file
			len_swapped = syscall(335, file_fd,
					      node->dr_info.dr_fd,
					      file_start_off,
					      app_start_off,
					      (const char *) node->dr_info.start_addr,
					      len_to_swap);

			if (len_swapped != len_to_swap) {
				MSG("%s: len_swapped = %lu. Len to swap = %lu\n", __func__, len_swapped, len_to_swap);
				if (len_swapped == -1) {
					MSG("%s: Swap extents failed. Err = %s\n", __func__, strerror(errno));
				}
				assert(0);
			}

			END_TIMING(swap_extents_t, swap_extents_time);
			num_appendfsync++;
			START_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			insert_tbl_mmap_entry(node,
					      file_start_off,
					      app_start_off,
					      len_swapped,
					      app_start_addr);
			END_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
			len_written += len_swapped;
		}
	}

	return len_written;
}


size_t swap_extents(struct NVFile *nvf, int close)
{
	size_t len_swapped = 0;
	off_t offset_in_page = 0;

	DEBUG_FILE("%s: before dynamic_remap, staging file inode = %lu, nvf->node->dr_info.valid_offset = %lld\n",
		   __func__, nvf->node->dr_info.dr_serialno, nvf->node->dr_info.valid_offset);

	len_swapped = dynamic_remap(nvf->fd, nvf->node, close);

	if (len_swapped > 0) {
		nvf->node->true_length = nvf->node->length;

		if (nvf->node->true_length >= LARGE_FILE_THRESHOLD)
			nvf->node->is_large_file = 1;

		nvf->node->dr_info.valid_offset = align_next_page(nvf->node->dr_info.dr_offset_end);

		if (nvf->node->dr_info.valid_offset < DR_SIZE) {
			offset_in_page = nvf->node->true_length % PAGE_SIZE;
			nvf->node->dr_info.valid_offset += offset_in_page;
		}

		DEBUG_FILE("%s: Setting offset_start to DR_SIZE. FD = %d. Valid offset = %lu\n", __func__, nvf->fd, nvf->node->dr_info.valid_offset);
		DEBUG_FILE("%s: -------------------------------\n", __func__);

		if (nvf->node->dr_info.valid_offset > DR_SIZE)
			nvf->node->dr_info.valid_offset = DR_SIZE;

		DEBUG_FILE("%s: after dynamic_remap, staging file inode = %lu, nvf->node->dr_info.valid_offset = %lld\n",
			   __func__, nvf->node->dr_info.dr_serialno, nvf->node->dr_info.valid_offset);

		nvf->node->dr_info.dr_offset_start = DR_SIZE;
		nvf->node->dr_info.dr_offset_end = nvf->node->dr_info.valid_offset;

		if (nvf->node->dr_info.valid_offset > DR_SIZE)
			assert(0);
		if (nvf->node->dr_info.dr_offset_start > DR_SIZE)
			assert(0);
		if (nvf->node->dr_info.dr_offset_end > DR_SIZE)
			assert(0);
	}


	return len_swapped;
}


