#include "timers.h"
#include "tbl_mmaps.h"

#if DATA_JOURNALING_ENABLED

static void shift_last_entry(int region, struct NVTable_regions *regions)
{
	int src_end_index = regions[region].tbl_mmap_index - 1;

	regions[region].tbl_mmaps[src_end_index+1].file_start_off = regions[region].tbl_mmaps[src_end_index].file_start_off;
	regions[region].tbl_mmaps[src_end_index+1].dr_start_off = regions[region].tbl_mmaps[src_end_index].dr_start_off;
	regions[region].tbl_mmaps[src_end_index+1].file_end_off = regions[region].tbl_mmaps[src_end_index].file_end_off;
	regions[region].tbl_mmaps[src_end_index+1].dr_end_off = regions[region].tbl_mmaps[src_end_index].dr_end_off;
	regions[region].tbl_mmaps[src_end_index+1].buf_start = regions[region].tbl_mmaps[src_end_index].buf_start;

	regions[region].tbl_mmap_index++;
	
}

static void remove_entry_from_region(int region, int tbl_idx, struct NVTable_regions *regions)
{
	int src_end_index = regions[region].tbl_mmap_index - 1;
	int second_last_index = 0;

	if (src_end_index != 0) {
		src_end_index--;
		second_last_index = 1;
	}

	regions[region].tbl_mmaps[tbl_idx].file_start_off = regions[region].tbl_mmaps[src_end_index].file_start_off;
	regions[region].tbl_mmaps[tbl_idx].dr_start_off = regions[region].tbl_mmaps[src_end_index].dr_start_off;
	regions[region].tbl_mmaps[tbl_idx].file_end_off = regions[region].tbl_mmaps[src_end_index].file_end_off;
	regions[region].tbl_mmaps[tbl_idx].dr_end_off = regions[region].tbl_mmaps[src_end_index].dr_end_off;
	regions[region].tbl_mmaps[tbl_idx].buf_start = regions[region].tbl_mmaps[src_end_index].buf_start;

	if (second_last_index == 1) {
		regions[region].tbl_mmaps[src_end_index].file_start_off = regions[region].tbl_mmaps[src_end_index+1].file_start_off;
		regions[region].tbl_mmaps[src_end_index].dr_start_off = regions[region].tbl_mmaps[src_end_index+1].dr_start_off;
		regions[region].tbl_mmaps[src_end_index].file_end_off = regions[region].tbl_mmaps[src_end_index+1].file_end_off;
		regions[region].tbl_mmaps[src_end_index].dr_end_off = regions[region].tbl_mmaps[src_end_index+1].dr_end_off;
		regions[region].tbl_mmaps[src_end_index].buf_start = regions[region].tbl_mmaps[src_end_index+1].buf_start;
		src_end_index++;
	}

	memset((void *) &regions[region].tbl_mmaps[src_end_index], 0, sizeof(struct table_mmaps));
	
	regions[region].tbl_mmap_index--;
}

static void exchange_regions(int from_region, int to_region, int tbl_idx, struct NVTable_regions *regions)
{
	int target_mmap_idx = regions[to_region].tbl_mmap_index;
	int src_end_index = regions[from_region].tbl_mmap_index - 1;
	
	regions[to_region].tbl_mmaps[target_mmap_idx].file_start_off = regions[from_region].tbl_mmaps[tbl_idx].file_start_off;
	regions[to_region].tbl_mmaps[target_mmap_idx].dr_start_off = regions[from_region].tbl_mmaps[tbl_idx].dr_start_off;
	regions[to_region].tbl_mmaps[target_mmap_idx].file_end_off = regions[from_region].tbl_mmaps[tbl_idx].file_end_off;
	regions[to_region].tbl_mmaps[target_mmap_idx].dr_end_off = regions[from_region].tbl_mmaps[tbl_idx].dr_end_off;
	regions[to_region].tbl_mmaps[target_mmap_idx].buf_start = regions[from_region].tbl_mmaps[tbl_idx].buf_start;

	regions[to_region].tbl_mmap_index++;

	if (regions[to_region].lowest_off > regions[to_region].tbl_mmaps[target_mmap_idx].file_start_off)
		regions[to_region].lowest_off = regions[to_region].tbl_mmaps[target_mmap_idx].file_start_off;
	if (regions[to_region].highest_off < regions[to_region].tbl_mmaps[target_mmap_idx].file_end_off)
		regions[to_region].highest_off = regions[to_region].tbl_mmaps[target_mmap_idx].file_end_off;
			
	remove_entry_from_region(from_region, tbl_idx, regions);
}

static void set_lowest_and_highest(struct NVTable_regions *regions, int region_id) {
	int num_entries = regions[region_id].tbl_mmap_index;
	off_t lowest = (REGION_COVERAGE)*(region_id + 1), highest = 0;
	int i = 0;

	for (i = 0; i < num_entries; i++) {
		if (lowest > regions[region_id].tbl_mmaps[i].file_start_off)
			lowest = regions[region_id].tbl_mmaps[i].file_start_off;
		if (highest < regions[region_id].tbl_mmaps[i].file_end_off)
			highest = regions[region_id].tbl_mmaps[i].file_end_off;
	}

	regions[region_id].lowest_off = lowest;
	regions[region_id].highest_off = highest;
}

static int clear_overlapping_entry_large(off_t file_off_start, size_t length, struct NVTable_regions *regions)
{
	int region_id = file_off_start / REGION_COVERAGE;
	int cur_region = region_id;
	int i = 0, j = 0;
	off_t file_off_end = file_off_start + length - 1;
	int tbl_idx = 0;
	int highest_region = 0, lowest_region = 0, adjusted_lower_region = 0, adjusted_higher_region = 0;
	int num_added_regions = 0;
	size_t shift_len = 0;
	
	if ((region_id - 1 >= 0) && (regions[region_id-1].highest_off >= file_off_start) && regions[region_id-1].tbl_mmap_index > 0) {
		adjusted_lower_region = 1;
		lowest_region = region_id - 1;
	} else
		lowest_region = region_id;
	
	if (regions[region_id+1].lowest_off <= file_off_end && regions[region_id+1].lowest_off > 0 && regions[region_id+1].tbl_mmap_index > 0) {
		adjusted_higher_region = 1;
	        highest_region = region_id + 1;
	} else
		highest_region = region_id;
	
	cur_region = lowest_region;
	while (cur_region <= highest_region) {
		tbl_idx = regions[cur_region].tbl_mmap_index;
		i = 0;
		DEBUG_FILE("%s: STARTING TO CHECK OVERLAP. Cur_region = %d, num_entries in region = %d, lowest region = %d, highest region = %d\n", __func__, cur_region, tbl_idx, lowest_region, highest_region);
		while (i < tbl_idx) {
			if (regions[cur_region].tbl_mmaps[i].file_end_off > file_off_start) {
				if (regions[cur_region].tbl_mmaps[i].file_start_off > file_off_start + length)
					goto while_end;			
				if (regions[cur_region].tbl_mmaps[i].file_start_off == file_off_start) {
					if (regions[cur_region].tbl_mmaps[i].file_end_off > file_off_end) {
						/* 
						 *          |----------------|------------|
						 *     tstart = fstart     fend          tend
						 */

						DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						if (cur_region != region_id) {
							assert(0);
						}
 
						off_t prev_start_off = regions[cur_region].tbl_mmaps[i].file_start_off;
						off_t prev_end_off = regions[cur_region].tbl_mmaps[i].file_end_off;
						
						shift_len = file_off_end + 1 - regions[cur_region].tbl_mmaps[i].file_start_off;
						regions[cur_region].tbl_mmaps[i].file_start_off = file_off_end + 1;
						regions[cur_region].tbl_mmaps[i].buf_start += shift_len;
						regions[cur_region].tbl_mmaps[i].dr_start_off += shift_len;

						if ((regions[cur_region].tbl_mmaps[i].file_start_off / REGION_COVERAGE) != cur_region) {
							if (regions[cur_region].highest_off == prev_end_off)
								regions[cur_region].highest_off = file_off_end;
							exchange_regions(cur_region, cur_region+1, i, regions);
						}						
							
						if (regions[cur_region].tbl_mmaps[i].dr_end_off < 0) {
							MSG("%s: i = %d, tbl idx = %d. tbl fstart = %lld, tbl fend = %lld, tbl dr start = %lld, tbl dr end = %lld. Shift len = %lu. file fstart = %lld, file fend = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, regions[cur_region].tbl_mmaps[i].dr_start_off, regions[cur_region].tbl_mmaps[i].dr_end_off, shift_len, file_off_start, file_off_end);
							assert(0);
						}
					
						DEBUG_FILE("%s: OVERLAPPING HANDLED. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						goto end;
						//break;
					} else if (regions[cur_region].tbl_mmaps[i].file_end_off < file_off_end) {
						/* 
						 *         |-----------------|----------|
						 *  tstart = fstart        tend       fend
						 */

						DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						if (cur_region != region_id) {
							assert(0);
						}

						// Remove element from the region						
						if (regions[cur_region].highest_off == regions[cur_region].tbl_mmaps[i].file_end_off)
							regions[cur_region].highest_off = file_off_end;

						remove_entry_from_region(cur_region, i, regions);
						tbl_idx--;
						
						DEBUG_FILE("%s: OVERLAPPING HANDLED. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						i--;
						num_added_regions--;
												
						goto while_end;
					} else {
						/* 
						 *        |----------------------|
						 * tstart = fstart         tend = fend
						 */

						DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						if (cur_region != region_id) {
							assert(0);
						}

						// Remove element from the region
						remove_entry_from_region(cur_region, i, regions);       							
						tbl_idx--;

						DEBUG_FILE("%s: OVERLAPPING HANDLED. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						i--;
						num_added_regions--;
						
						goto end;
						//break;
					}
				} else if (regions[cur_region].tbl_mmaps[i].file_start_off < file_off_start) {
					if (regions[cur_region].tbl_mmaps[i].file_end_off > file_off_end) {
						/* 
						 *           |-------|------------|----------|
						 *      tstart   fstart         fend       tend
						 */

						DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						int end_index = 0;
						shift_last_entry(cur_region, regions);
						if (i != regions[cur_region].tbl_mmap_index - 2) {
							end_index = regions[cur_region].tbl_mmap_index - 2;
						} else {
							end_index = regions[cur_region].tbl_mmap_index - 1;
						}						
						
						shift_len = file_off_end + 1 - regions[cur_region].tbl_mmaps[i].file_start_off;
						regions[cur_region].tbl_mmaps[end_index].file_start_off = file_off_end + 1;
						regions[cur_region].tbl_mmaps[end_index].dr_start_off = regions[cur_region].tbl_mmaps[i].dr_start_off + shift_len;
						regions[cur_region].tbl_mmaps[end_index].file_end_off = regions[cur_region].tbl_mmaps[i].file_end_off;
						regions[cur_region].tbl_mmaps[end_index].dr_end_off = regions[cur_region].tbl_mmaps[i].dr_end_off;
						regions[cur_region].tbl_mmaps[end_index].buf_start = regions[cur_region].tbl_mmaps[i].buf_start + shift_len;

						shift_len = regions[cur_region].tbl_mmaps[i].file_end_off - file_off_start + 1;
						regions[cur_region].tbl_mmaps[i].file_end_off = file_off_start - 1;
						if (regions[cur_region].tbl_mmaps[i].dr_end_off != 0)
							regions[cur_region].tbl_mmaps[i].dr_end_off -= shift_len;

						if (regions[cur_region].tbl_mmaps[i].dr_end_off < 0)
							assert(0);
						
						if ((regions[cur_region].tbl_mmaps[end_index].file_start_off / REGION_COVERAGE) != cur_region) {
							if (regions[cur_region].highest_off == regions[cur_region].tbl_mmaps[end_index].file_end_off)
								regions[cur_region].highest_off = file_off_start - 1;								
							exchange_regions(cur_region, cur_region+1, end_index, regions);
						}

						num_added_regions++;
						tbl_idx++;

						DEBUG_FILE("%s: OVERLAPPING HANDLED. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						goto end;
						//break;
					} else if (regions[cur_region].tbl_mmaps[i].file_end_off < file_off_end) {
						/* 
						 *           |-------|---------------|------------|
						 *        tstart   fstart         tend           fend
						 */

						if (regions[cur_region].highest_off == regions[cur_region].tbl_mmaps[i].file_end_off)
							regions[cur_region].highest_off = file_off_start - 1;
						
						shift_len = regions[cur_region].tbl_mmaps[i].file_end_off - file_off_start + 1;
						regions[cur_region].tbl_mmaps[i].file_end_off = file_off_start - 1;
						if (regions[cur_region].tbl_mmaps[i].dr_end_off != 0)
							regions[cur_region].tbl_mmaps[i].dr_end_off -= shift_len;
					
						if (regions[cur_region].tbl_mmaps[i].dr_end_off < 0) 
							assert(0);

						DEBUG_FILE("%s: OVERLAPPING HANDLED.\n", __func__);

						goto while_end;
					} else {
						/* 
						 *           |------|----------------|
						 *        tstart   fstart         tend = fend
						 */
						DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						if (regions[cur_region].highest_off == regions[cur_region].tbl_mmaps[i].file_end_off)
							regions[cur_region].highest_off = file_off_start - 1;
						
						shift_len = regions[cur_region].tbl_mmaps[i].file_end_off - file_off_start + 1;
						regions[cur_region].tbl_mmaps[i].file_end_off = file_off_start - 1;
						if (regions[cur_region].tbl_mmaps[i].dr_end_off != 0)
							regions[cur_region].tbl_mmaps[i].dr_end_off -= shift_len;

						if (regions[cur_region].tbl_mmaps[i].dr_end_off < 0)
							assert(0);

						DEBUG_FILE("%s: OVERLAPPING HANDLED. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						goto end;
						//break;
					} 
				} else if (regions[cur_region].tbl_mmaps[i].file_start_off > file_off_start) {
					if (regions[cur_region].tbl_mmaps[i].file_end_off > file_off_end) {
						/* 
						 *           |-------|------------|-------------|
						 *      fstart    tstart        fend          tend
						 */

						DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						off_t prev_start_off = regions[cur_region].tbl_mmaps[i].file_start_off;
						off_t prev_end_off = regions[cur_region].tbl_mmaps[i].file_end_off;
						
						shift_len = file_off_end + 1 - regions[cur_region].tbl_mmaps[i].file_start_off;
						regions[cur_region].tbl_mmaps[i].file_start_off = file_off_end + 1;
						regions[cur_region].tbl_mmaps[i].dr_start_off += shift_len;
						regions[cur_region].tbl_mmaps[i].buf_start += shift_len;

						if (regions[cur_region].tbl_mmaps[i].dr_end_off < 0)
							assert(0);

						if ((regions[cur_region].tbl_mmaps[i].file_start_off / REGION_COVERAGE) != cur_region) {
							if (regions[cur_region].lowest_off == prev_start_off)
								regions[cur_region].lowest_off = file_off_start;
							if (regions[cur_region].highest_off == prev_end_off)
								regions[cur_region].highest_off = file_off_end;
							exchange_regions(cur_region, cur_region+1, i, regions);
						} else {
							if (regions[cur_region].lowest_off == prev_start_off)
								regions[cur_region].lowest_off = file_off_end + 1;
						}
						
						DEBUG_FILE("%s: OVERLAPPING HANDLED. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						goto while_end;
						//break;
					} else if (regions[cur_region].tbl_mmaps[i].file_end_off < file_off_end) {
						/* 
						 *           |-------|---------------|-----------|
						 *        fstart   tstart         tend          fend
						 */
						// Remove element from the region

						DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						if (cur_region != region_id) {
							if (regions[cur_region].lowest_off == regions[cur_region].tbl_mmaps[i].file_start_off)
								regions[cur_region].lowest_off = regions[cur_region].tbl_mmaps[i].file_end_off + 1;
							if (regions[cur_region].highest_off == regions[cur_region].tbl_mmaps[i].file_end_off)
								regions[cur_region].highest_off = regions[cur_region].tbl_mmaps[i].file_start_off - 1;
						} else {
							if (regions[cur_region].lowest_off == regions[cur_region].tbl_mmaps[i].file_start_off)
								regions[cur_region].lowest_off = file_off_start;
							if (regions[cur_region].highest_off == regions[cur_region].tbl_mmaps[i].file_end_off)
								regions[cur_region].highest_off = file_off_end;	
						}
							
						remove_entry_from_region(cur_region, i, regions);       							
						tbl_idx--;

						DEBUG_FILE("%s: OVERLAPPING HANDLED. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						i--;
						num_added_regions--;

						goto while_end;
					} else {
						/* 
						 *           |------|----------------|
						 *        fstart   tstart         tend = fend
						 */
						// Remove element from the region

						DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						if (cur_region != region_id) {
							if (regions[cur_region].lowest_off == regions[cur_region].tbl_mmaps[i].file_start_off)
								regions[cur_region].lowest_off = regions[cur_region].tbl_mmaps[i].file_end_off + 1;
							if (regions[cur_region].highest_off == regions[cur_region].tbl_mmaps[i].file_end_off)
								regions[cur_region].highest_off = regions[cur_region].tbl_mmaps[i].file_start_off - 1;
						} else {
							if (regions[cur_region].lowest_off == regions[cur_region].tbl_mmaps[i].file_start_off)
								regions[cur_region].lowest_off = file_off_start;
							if (regions[cur_region].highest_off == regions[cur_region].tbl_mmaps[i].file_end_off)
								regions[cur_region].highest_off = file_off_end;	
						}
						
						remove_entry_from_region(cur_region, i, regions);       							
						tbl_idx--;

						DEBUG_FILE("%s: OVERLAPPING HANDLED. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, i, tbl_idx, regions[cur_region].tbl_mmaps[i].file_start_off, regions[cur_region].tbl_mmaps[i].file_end_off, file_off_start, file_off_end);

						i--;
						num_added_regions--;						

						goto while_end;
						//break;
					}
				} else {
					MSG("%s: Weird values in table and file offsets\n", __func__);
					assert(0);
				}
			}
		while_end:
			i++;
		}
		cur_region++;
	}

 end:	
	if (adjusted_lower_region) 
		set_lowest_and_highest(regions, region_id - 1);
	if (adjusted_higher_region)
		set_lowest_and_highest(regions, region_id + 1);
	set_lowest_and_highest(regions, region_id);
	
	return num_added_regions;
}



static void clear_overlapping_entry(off_t file_off_start,
			     size_t length, 
			     struct NVTable_maps *tbl)
{
	int tbl_idx = tbl->tbl_mmap_index;
	int i = 0, j = 0, idx_bin = 0;
	int left = 0, right = tbl->tbl_mmap_index - 1, mid = 0;
	off_t file_off_end = file_off_start + length - 1;
	off_t shift_len = 0;
	int handle_overlaps = 0;
	
	if (right >= NUM_OVER_TBL_MMAP_ENTRIES - 1)
		assert(0);

	if (right < left) {
		return;
	}
	
 bin_search:
	while (left <= right) {
		mid = (right + left) / 2;

		if (tbl->tbl_mmaps[mid].file_end_off > file_off_start) {			
			if (tbl->tbl_mmaps[mid].file_start_off >= file_off_start + length) {
				right = mid - 1;
				continue;
			}
			if (tbl->tbl_mmaps[mid].file_start_off < file_off_start + length) {
				if (tbl->tbl_mmaps[mid].file_start_off == file_off_start) {
					if (tbl->tbl_mmaps[mid].file_end_off > file_off_end) {
						handle_overlaps = 1;
						break;
					}
					else if (tbl->tbl_mmaps[mid].file_end_off < file_off_end) {
						handle_overlaps = 2;
						break;
					}
					else {
						handle_overlaps = 3;
						break;
					}
				} else if (tbl->tbl_mmaps[mid].file_start_off < file_off_start) {
					if (tbl->tbl_mmaps[mid].file_end_off > file_off_end) {
						handle_overlaps = 4;
						break;
					}
					else if (tbl->tbl_mmaps[mid].file_end_off < file_off_end) {
						handle_overlaps = 5;
						break;
					}
					else {
						handle_overlaps = 6;
						break;
					}
				} else if (tbl->tbl_mmaps[mid].file_start_off > file_off_start) {
					if (tbl->tbl_mmaps[mid].file_end_off > file_off_end) {
						handle_overlaps = 7;
						break;
					}
					else if (tbl->tbl_mmaps[mid].file_end_off < file_off_end) {
						handle_overlaps = 8;
						break;
					}
					else {
						handle_overlaps = 9;
						break;
					}
				} else {
					assert(0);
				}
			}				
		} else { 
			left = mid + 1;
			continue;
		}
	}

	if (left > right)
		return;
	
	switch (handle_overlaps) {

	case 1:
		/* 
		 *          |----------------|------------|
		 *     tstart = fstart     fend          tend
		 */
		shift_len = file_off_end + 1 - tbl->tbl_mmaps[mid].file_start_off;
		tbl->tbl_mmaps[mid].file_start_off = file_off_end + 1;
		tbl->tbl_mmaps[mid].buf_start += shift_len;
		tbl->tbl_mmaps[mid].dr_start_off += shift_len;

		if (tbl->tbl_mmaps[mid].dr_end_off < 0) {
			MSG("%s: i = %d, tbl idx = %d. tbl fstart = %lld, tbl fend = %lld, tbl dr start = %lld, tbl dr end = %lld. Shift len = %lu. file fstart = %lld, file fend = %lld\n", __func__, mid, tbl->tbl_mmap_index, tbl->tbl_mmaps[mid].file_start_off, tbl->tbl_mmaps[mid].file_end_off, tbl->tbl_mmaps[mid].dr_start_off, tbl->tbl_mmaps[mid].dr_end_off, shift_len, file_off_start, file_off_end);
			assert(0);
		}
					
		DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, mid, tbl->tbl_mmap_index, tbl->tbl_mmaps[mid].file_start_off, tbl->tbl_mmaps[mid].file_end_off, file_off_start, file_off_end);
		return;

	case 2:
		/* 
		 *         |-----------------|----------|
		 *  tstart = fstart        tend       fend
		 */
		for (j = mid; j < tbl->tbl_mmap_index-1; j++) {
			tbl->tbl_mmaps[j].file_start_off = tbl->tbl_mmaps[j+1].file_start_off;
			tbl->tbl_mmaps[j].dr_start_off = tbl->tbl_mmaps[j+1].dr_start_off;
			tbl->tbl_mmaps[j].file_end_off = tbl->tbl_mmaps[j+1].file_end_off;
			tbl->tbl_mmaps[j].dr_end_off = tbl->tbl_mmaps[j+1].dr_end_off;
			tbl->tbl_mmaps[j].buf_start = tbl->tbl_mmaps[j+1].buf_start;
		}
		memset(&tbl->tbl_mmaps[tbl->tbl_mmap_index-1], 0, sizeof(struct table_mmaps));					
		tbl->tbl_mmap_index--;

		left = 0;
		right = tbl->tbl_mmap_index - 1;
		
		DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, mid, tbl->tbl_mmap_index, tbl->tbl_mmaps[mid].file_start_off, tbl->tbl_mmaps[mid].file_end_off, file_off_start, file_off_end);			
		break;

	case 3:
		/* 
		 *        |----------------------|
		 * tstart = fstart         tend = fend
		 */
		for (j = mid; j < tbl->tbl_mmap_index-1; j++) {
			tbl->tbl_mmaps[j].file_start_off = tbl->tbl_mmaps[j+1].file_start_off;
			tbl->tbl_mmaps[j].dr_start_off = tbl->tbl_mmaps[j+1].dr_start_off;
			tbl->tbl_mmaps[j].file_end_off = tbl->tbl_mmaps[j+1].file_end_off;
			tbl->tbl_mmaps[j].dr_end_off = tbl->tbl_mmaps[j+1].dr_end_off;
			tbl->tbl_mmaps[j].buf_start = tbl->tbl_mmaps[j+1].buf_start;
		}
		memset(&tbl->tbl_mmaps[tbl->tbl_mmap_index-1], 0, sizeof(struct table_mmaps));					
		tbl->tbl_mmap_index--;

		DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, mid, tbl->tbl_mmap_index, tbl->tbl_mmaps[mid].file_start_off, tbl->tbl_mmaps[mid].file_end_off, file_off_start, file_off_end);
		return;

	case 4:
		/* 
		 *           |-------|------------|----------|
		 *      tstart   fstart         fend       tend
		 */
		for (j = tbl_idx-1; j >= mid; j--) {
			tbl->tbl_mmaps[j+1].file_start_off = tbl->tbl_mmaps[j].file_start_off;
			tbl->tbl_mmaps[j+1].dr_start_off = tbl->tbl_mmaps[j].dr_start_off;
			tbl->tbl_mmaps[j+1].file_end_off = tbl->tbl_mmaps[j].file_end_off;
			tbl->tbl_mmaps[j+1].dr_end_off = tbl->tbl_mmaps[j].dr_end_off;
			tbl->tbl_mmaps[j+1].buf_start = tbl->tbl_mmaps[j].buf_start;
		}
		shift_len = tbl->tbl_mmaps[mid].file_end_off - file_off_start + 1;
		tbl->tbl_mmaps[mid].file_end_off = file_off_start - 1;
		if (tbl->tbl_mmaps[mid].dr_end_off != 0) 
			tbl->tbl_mmaps[mid].dr_end_off -= shift_len;
					
		if (tbl->tbl_mmaps[mid].dr_end_off < 0)
			assert(0);
					
		shift_len = file_off_end + 1 - tbl->tbl_mmaps[mid+1].file_start_off;
		tbl->tbl_mmaps[mid+1].file_start_off = file_off_end + 1;
		tbl->tbl_mmaps[mid+1].buf_start += shift_len;
		tbl->tbl_mmaps[mid+1].dr_start_off += shift_len;

		if (tbl->tbl_mmaps[mid+1].dr_end_off < 0)
			assert(0);
					
		tbl->tbl_mmap_index++;

		DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, mid, tbl->tbl_mmap_index, tbl->tbl_mmaps[mid].file_start_off, tbl->tbl_mmaps[mid].file_end_off, file_off_start, file_off_end);
		return;

	case 5:
		/* 
		 *           |-------|---------------|------------|
		 *        tstart   fstart         tend           fend
		 */
		shift_len = tbl->tbl_mmaps[mid].file_end_off - file_off_start + 1;
		tbl->tbl_mmaps[mid].file_end_off = file_off_start - 1;
		if (tbl->tbl_mmaps[mid].dr_end_off != 0)
			tbl->tbl_mmaps[mid].dr_end_off -= shift_len;
					
		if (tbl->tbl_mmaps[mid].dr_end_off < 0) 
			assert(0);
		
		left = 0;
		right = tbl->tbl_mmap_index - 1;

		DEBUG_FILE("%s: OVERLAPPING\n", __func__);
		break;

	case 6:
		/* 
		 *           |------|----------------|
		 *        tstart   fstart         tend = fend
		 */
		shift_len = tbl->tbl_mmaps[mid].file_end_off - file_off_start + 1;
		tbl->tbl_mmaps[mid].file_end_off = file_off_start - 1;
		if (tbl->tbl_mmaps[mid].dr_end_off != 0)
			tbl->tbl_mmaps[mid].dr_end_off -= shift_len;

		if (tbl->tbl_mmaps[mid].dr_end_off < 0)
			assert(0);

		DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, mid, tbl->tbl_mmap_index, tbl->tbl_mmaps[mid].file_start_off, tbl->tbl_mmaps[mid].file_end_off, file_off_start, file_off_end);
		return;
		
	case 7:
		/* 
		 *           |-------|------------|-------------|
		 *      fstart    tstart        fend          tend
		 */
		shift_len = file_off_end + 1 - tbl->tbl_mmaps[mid].file_start_off;
		tbl->tbl_mmaps[mid].file_start_off = file_off_end + 1;
		tbl->tbl_mmaps[mid].dr_start_off += shift_len;
		tbl->tbl_mmaps[mid].buf_start += shift_len;

		if (tbl->tbl_mmaps[mid].dr_end_off < 0)
			assert(0);
					
		DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, mid, tbl->tbl_mmap_index, tbl->tbl_mmaps[mid].file_start_off, tbl->tbl_mmaps[mid].file_end_off, file_off_start, file_off_end);
		return;

	case 8:
		/* 
		 *           |-------|---------------|-----------|
		 *        fstart   tstart         tend          fend
		 */
		for (j = mid; j < tbl->tbl_mmap_index-1; j++) {
			tbl->tbl_mmaps[j+1].file_start_off = tbl->tbl_mmaps[j].file_start_off;
			tbl->tbl_mmaps[j+1].dr_start_off = tbl->tbl_mmaps[j].dr_start_off;
			tbl->tbl_mmaps[j+1].file_end_off = tbl->tbl_mmaps[j].file_end_off;
			tbl->tbl_mmaps[j+1].dr_end_off = tbl->tbl_mmaps[j].dr_end_off;
			tbl->tbl_mmaps[j+1].buf_start = tbl->tbl_mmaps[j].buf_start;
		}
		memset(&tbl->tbl_mmaps[tbl->tbl_mmap_index-1], 0, sizeof(struct table_mmaps));					
		tbl->tbl_mmap_index--;

		left = 0;
		right = tbl->tbl_mmap_index - 1;
		
		DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, mid, tbl->tbl_mmap_index, tbl->tbl_mmaps[mid].file_start_off, tbl->tbl_mmaps[mid].file_end_off, file_off_start, file_off_end);
		break;

	case 9:
		/* 
		 *           |------|----------------|
		 *        fstart   tstart         tend = fend
		 */
		for (j = mid; j < tbl->tbl_mmap_index-1; j++) {
			tbl->tbl_mmaps[j+1].file_start_off = tbl->tbl_mmaps[j].file_start_off;
			tbl->tbl_mmaps[j+1].dr_start_off = tbl->tbl_mmaps[j].dr_start_off;
			tbl->tbl_mmaps[j+1].file_end_off = tbl->tbl_mmaps[j].file_end_off;
			tbl->tbl_mmaps[j+1].dr_end_off = tbl->tbl_mmaps[j].dr_end_off;
			tbl->tbl_mmaps[j+1].buf_start = tbl->tbl_mmaps[j].buf_start;
		}
		memset(&tbl->tbl_mmaps[tbl->tbl_mmap_index-1], 0, sizeof(struct table_mmaps));					
		tbl->tbl_mmap_index--;

		DEBUG_FILE("%s: OVERLAPPING. i = %d, Tbl Idx = %d, Tbl_start = %lld, Tbl_end = %lld, File_start = %lld, File_end = %lld\n", __func__, mid, tbl->tbl_mmap_index, tbl->tbl_mmaps[mid].file_start_off, tbl->tbl_mmaps[mid].file_end_off, file_off_start, file_off_end);
		return;
	};

	goto bin_search;
}

static int find_idx_to_insert_large(off_t file_off_start,
				    struct NVTable_regions *regions)
{
	int region_id = file_off_start / REGION_COVERAGE;
	return regions[region_id].tbl_mmap_index;	
}

static int find_idx_to_read_large(off_t file_off_start,
				  struct NVTable_regions *regions,
				  int *region_num)
{
	int region_id = file_off_start / REGION_COVERAGE;
	int idx = 0, idx_seq = 0;
	int cur_region = 0;

	idx = region_id;
	while (idx >= 0 && idx >= region_id - 1) {
		if ((regions[idx].lowest_off <= file_off_start) && (regions[idx].highest_off >= file_off_start)) {
			cur_region = idx;
			break;
		}
		idx--;
	}

	if (idx == -1 || idx == region_id - 2)
		return -1;

	idx = 0;
	while (idx < regions[cur_region].tbl_mmap_index) {
		if (regions[cur_region].tbl_mmaps[idx].file_end_off >= file_off_start) {
			if (regions[cur_region].tbl_mmaps[idx].file_start_off <= file_off_start) {
				idx_seq = idx;
				break;
			}
		}
		idx++;
	}
	if (idx == regions[cur_region].tbl_mmap_index)
		idx_seq = -1;

	*region_num = cur_region;
	return idx_seq;
}



static int find_idx_to_insert(off_t file_off_start,
			      struct NVTable_maps *tbl)
{
	int i = 0, idx_bin = 0, idx_seq = 0;
	int left = 0, right = tbl->tbl_mmap_index - 1;
	int mid;
		
	if (right >= NUM_OVER_TBL_MMAP_ENTRIES - 1)
		assert(0);

	if (right < left) {
		idx_bin = 0;
		return idx_bin;
	}
	
	while (left <= right) {
		mid = (right + left) / 2;

		if (tbl->tbl_mmaps[mid].file_end_off >= file_off_start) {			
			if (mid == 0) {
				idx_bin = mid;
				return idx_bin;
			}				
			if (tbl->tbl_mmaps[mid-1].file_end_off < file_off_start) {
				idx_bin = mid;
				return idx_bin;
			}
			if (tbl->tbl_mmaps[mid-1].file_end_off >= file_off_start) {
				right = mid - 1;
				continue;
			}
		} else { 
			left = mid + 1;
			continue;
		}
	}

	idx_bin = tbl->tbl_mmap_index;

	return idx_bin;
}

#endif // DATA_JOURNALING_ENABLED

static int find_idx_to_read(off_t file_off_start,
		     struct NVTable_maps *tbl)
{
	int i = 0, idx_bin = 0, idx_seq = 0;
	int left = 0, right = tbl->tbl_mmap_index - 1;
	int mid = (right + left) / 2;
	
	if (right < left) {
		return -1;
	}
	
	if (mid < 0)
		assert(0);
	if (left < 0)
		assert(0);
	if (right < 0)
		assert(0);

	if (right >= NUM_OVER_TBL_MMAP_ENTRIES - 1)
		assert(0);
	
	while (left <= right) {
		mid = (right + left) / 2;

		if (tbl->tbl_mmaps[mid].file_end_off < file_off_start) {
			left = mid + 1;
			continue;
		}
		
		if (tbl->tbl_mmaps[mid].file_end_off >= file_off_start &&
		    tbl->tbl_mmaps[mid].file_start_off <= file_off_start) {
			idx_bin = mid;
			goto out;
		}

		if (tbl->tbl_mmaps[mid].file_end_off >= file_off_start &&
		    tbl->tbl_mmaps[mid].file_start_off > file_off_start) {
			right = mid - 1;
			continue;
		}					    
	}

	idx_bin = -1;

 out:
	return idx_bin;
}

void insert_tbl_mmap_entry(struct NVNode *node,
			   off_t file_off_start,
			   off_t dr_off_start,
			   size_t length,
			   unsigned long buf_start)
{
	off_t prev_off_start = 0, prev_off_end = 0, prev_size, file_off_end = 0, dr_off_end = 0;
	unsigned long prev_buf_start = 0, prev_buf_end = 0;
	int index = node->serialno % APPEND_TBL_MAX;
	int newest_tbl_idx = _nvp_tbl_mmaps[index].tbl_mmap_index;

	DEBUG_FILE("%s: Requesting Insert of file start = %lu, length = %lu, buf_start = %p. Inode = %lu\n",
		   __func__, file_off_start, length, buf_start, node->serialno);
	
	if (newest_tbl_idx == 0)
		goto add_entry;
	
	prev_off_start = _nvp_tbl_mmaps[index].tbl_mmaps[newest_tbl_idx-1].file_start_off;
	prev_off_end = _nvp_tbl_mmaps[index].tbl_mmaps[newest_tbl_idx-1].file_end_off;
	prev_buf_start = _nvp_tbl_mmaps[index].tbl_mmaps[newest_tbl_idx-1].buf_start;
	prev_size = prev_off_end - prev_off_start + 1;
	prev_buf_end = prev_buf_start + prev_size - 1;
	file_off_end = file_off_start + length - 1;
	dr_off_end = dr_off_start + length - 1;

	if ((buf_start == prev_buf_end + 1) &&
	    (file_off_start == prev_off_end + 1)) {
		DEBUG_FILE("%s: extending previous table mmap to include the next region in file\n", __func__);
		_nvp_tbl_mmaps[index].tbl_mmaps[newest_tbl_idx-1].file_end_off = file_off_end;
		_nvp_tbl_mmaps[index].tbl_mmaps[newest_tbl_idx-1].dr_end_off = dr_off_end;
		return;
	}
 
 add_entry:	
	file_off_end = file_off_start + length - 1;
	_nvp_tbl_mmaps[index].tbl_mmaps[newest_tbl_idx].file_start_off = file_off_start;
	_nvp_tbl_mmaps[index].tbl_mmaps[newest_tbl_idx].dr_start_off = dr_off_start;	
	_nvp_tbl_mmaps[index].tbl_mmaps[newest_tbl_idx].file_end_off = file_off_end;
	_nvp_tbl_mmaps[index].tbl_mmaps[newest_tbl_idx].dr_end_off = dr_off_end;	
	_nvp_tbl_mmaps[index].tbl_mmaps[newest_tbl_idx].buf_start = buf_start;
	_nvp_tbl_mmaps[index].tbl_mmap_index++;
	if (_nvp_tbl_mmaps[index].tbl_mmap_index >= NUM_APP_TBL_MMAP_ENTRIES)
		assert(0);
	DEBUG_FILE("%s: Inserting entry. address = %p, tbl file start = %lu, tbl file end = %lu. Tbl IDX = %d. Inode = %lu\n",
		   __func__, buf_start, file_off_start, file_off_end, newest_tbl_idx, node->serialno);
}

#if DATA_JOURNALING_ENABLED

void insert_over_tbl_mmap_entry(struct NVNode *node,
				off_t file_off_start,
				off_t dr_off_start,
				size_t length,
				unsigned long buf_start)
{
	int index = node->serialno % APPEND_TBL_MAX;
	int reg_index = node->serialno % LARGE_TBL_MAX;
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[index];
	struct NVTable_maps *tbl_append = &_nvp_tbl_mmaps[index];
	struct NVTable_regions *regions = NULL, *regions_2 = NULL;
	int region_id = 0;
	size_t overlap_len = length;
	off_t file_off_end = file_off_start + length - 1;
	off_t dr_off_end = dr_off_start + length - 1;
	int idx_to_insert_large = 0, idx_to_insert_small = 0, idx_to_insert_large_2 = -1, idx_to_insert = 0, i = 0;
	off_t prev_off_start = 0, prev_off_end = 0;
	off_t prev_buf_start = 0, prev_buf_end = 0;
	off_t prev_dr_end = 0;
	size_t prev_size = 0;
	int tbl_entries_added = 0, tbl_entries_added_2 = 0;
	off_t prev_off_start_large = 0, prev_off_end_large = 0;
	off_t prev_buf_start_large = 0, prev_buf_end_large = 0;
	off_t prev_dr_end_large = 0;

	
	DEBUG_FILE("%s: Requesting Insert of file start = %lu, length = %lu, buf_start = %p. Inode = %lu\n",
		   __func__, file_off_start, length, buf_start, node->serialno);

	if (node->is_large_file) {
		regions = _nvp_tbl_regions[reg_index].regions;
		region_id = file_off_start / REGION_COVERAGE;
		if (region_id > _nvp_tbl_regions[reg_index].num_regions) {
			_nvp_tbl_regions[reg_index].num_regions = region_id;
		}
	}
	
	clear_overlapping_entry(file_off_start, length, tbl_append);
	if (node->is_large_file) {
		DEBUG_FILE("%s: Checked overlap for appends, now overwrites\n", __func__);
		tbl_entries_added = clear_overlapping_entry_large(file_off_start, length, regions);
		_nvp_tbl_regions[reg_index].num_tbl_mmaps += tbl_entries_added;
	}
	else
		clear_overlapping_entry(file_off_start, length, tbl_over);		

	if (node->is_large_file) {
		idx_to_insert_large = find_idx_to_insert_large(file_off_start, regions);
		if (idx_to_insert_large == 0)
			goto shift_and_add;
		else
			goto merge_entries;
	} else {
		idx_to_insert = find_idx_to_insert(file_off_start, tbl_over);
		if (idx_to_insert == 0)
			goto shift_and_add;
	}
	
 merge_entries:
	if (node->is_large_file) {		
		prev_off_start_large = regions[region_id].tbl_mmaps[idx_to_insert_large-1].file_start_off;
		prev_off_end_large = regions[region_id].tbl_mmaps[idx_to_insert_large-1].file_end_off;
		prev_size = prev_off_end_large - prev_off_start_large + 1;
		prev_buf_start_large = regions[region_id].tbl_mmaps[idx_to_insert_large-1].buf_start;
		prev_buf_end_large = prev_buf_start_large + prev_size - 1;
		prev_dr_end_large = regions[region_id].tbl_mmaps[idx_to_insert_large-1].dr_end_off;

		if (dr_off_start > dr_off_end) {
			MSG("%s: index of entry = %d, dr_off_start = %lld, dr_off_end = %lld\n", __func__, dr_off_start, dr_off_end);
			assert(0);
		}

		if ((file_off_start == prev_off_end_large + 1) &&
		    (buf_start == prev_buf_end_large + 1) &&
		    (prev_dr_end_large != 0)) {
		        regions[region_id].tbl_mmaps[idx_to_insert_large-1].file_end_off = file_off_end;
		        regions[region_id].tbl_mmaps[idx_to_insert_large-1].dr_end_off = dr_off_end;

			if (regions[region_id].highest_off < file_off_end)
				regions[region_id].highest_off = file_off_end;

			regions[region_id].region_dirty = 1;
			if (_nvp_tbl_regions[reg_index].max_dirty_region < region_id)
				_nvp_tbl_regions[reg_index].max_dirty_region = region_id;
			if (_nvp_tbl_regions[reg_index].min_dirty_region > region_id)
				_nvp_tbl_regions[reg_index].min_dirty_region = region_id;
			
			DEBUG_FILE("%s: Merging\n", __func__);
			return;			
		}
	}

	if (!node->is_large_file) {	
		prev_off_start = tbl_over->tbl_mmaps[idx_to_insert-1].file_start_off;
		prev_off_end = tbl_over->tbl_mmaps[idx_to_insert-1].file_end_off;
		prev_size = prev_off_end - prev_off_start + 1;
		prev_buf_start = tbl_over->tbl_mmaps[idx_to_insert-1].buf_start;
		prev_buf_end = prev_buf_start + prev_size - 1;
		prev_dr_end = tbl_over->tbl_mmaps[idx_to_insert-1].dr_end_off;
	
		if (dr_off_start > dr_off_end) {
			MSG("%s: index of entry = %d, dr_off_start = %lld, dr_off_end = %lld\n", __func__, dr_off_start, dr_off_end);
			assert(0);
		}
	
		if ((file_off_start == prev_off_end + 1) &&
		    (buf_start == prev_buf_end + 1) &&
		    (prev_dr_end != 0)) {
			tbl_over->tbl_mmaps[idx_to_insert-1].file_end_off = file_off_end;
			tbl_over->tbl_mmaps[idx_to_insert-1].dr_end_off = dr_off_end;
			return;
		}
	}

 shift_and_add:
	if (node->is_large_file) {
		regions[region_id].tbl_mmaps[idx_to_insert_large].file_start_off = file_off_start;
		regions[region_id].tbl_mmaps[idx_to_insert_large].dr_start_off = dr_off_start;
		regions[region_id].tbl_mmaps[idx_to_insert_large].file_end_off = file_off_end;
		regions[region_id].tbl_mmaps[idx_to_insert_large].dr_end_off = dr_off_end;
		regions[region_id].tbl_mmaps[idx_to_insert_large].buf_start = buf_start;
		regions[region_id].tbl_mmap_index++;
		_nvp_tbl_regions[index].num_tbl_mmaps++;

		if (regions[region_id].lowest_off > file_off_start)
			regions[region_id].lowest_off = file_off_start;
		if (regions[region_id].highest_off < file_off_end)
			regions[region_id].highest_off = file_off_end;

		DEBUG_FILE("%s: No Merge\n", __func__);
		regions[region_id].region_dirty = 1;
		if (_nvp_tbl_regions[reg_index].max_dirty_region < region_id)
			_nvp_tbl_regions[reg_index].max_dirty_region = region_id;
		if (_nvp_tbl_regions[reg_index].min_dirty_region > region_id)
			_nvp_tbl_regions[reg_index].min_dirty_region = region_id;
		
		return;
	}	

	for (i = tbl_over->tbl_mmap_index-1; i >= idx_to_insert; i--) {
		tbl_over->tbl_mmaps[i+1].file_start_off = tbl_over->tbl_mmaps[i].file_start_off;
	        tbl_over->tbl_mmaps[i+1].dr_start_off = tbl_over->tbl_mmaps[i].dr_start_off;
	        tbl_over->tbl_mmaps[i+1].file_end_off = tbl_over->tbl_mmaps[i].file_end_off;
	        tbl_over->tbl_mmaps[i+1].dr_end_off = tbl_over->tbl_mmaps[i].dr_end_off;
	        tbl_over->tbl_mmaps[i+1].buf_start = tbl_over->tbl_mmaps[i].buf_start;
	}

	tbl_over->tbl_mmaps[idx_to_insert].file_start_off = file_off_start;
        tbl_over->tbl_mmaps[idx_to_insert].dr_start_off = dr_off_start;
        tbl_over->tbl_mmaps[idx_to_insert].file_end_off = file_off_end;
        tbl_over->tbl_mmaps[idx_to_insert].dr_end_off = dr_off_end;
        tbl_over->tbl_mmaps[idx_to_insert].buf_start = buf_start;
	tbl_over->tbl_mmap_index++;
	
	DEBUG_FILE("%s: Inserting entry. address = %p, tbl file start = %lu, tbl file end = %lu. Tbl IDX = %d. IDX to insert = %d. Inode = %lu\n",
		   __func__, buf_start, file_off_start, file_off_end, tbl_over->tbl_mmap_index, idx_to_insert, node->serialno);
}

#endif // DATA_JOURNALING_ENABLED

int read_tbl_mmap_entry(struct NVNode *node,
			off_t file_off_start,
			size_t length,
			unsigned long *mmap_addr,
			size_t *extent_length,
			int check_append_entry)
{
	off_t tbl_mmap_off_start = 0, tbl_mmap_off_end = 0,
		off_start_diff = 0, effective_tbl_mmap_off_start = 0;
	size_t tbl_mmap_entry_len = 0;
	int i = 0;
	int app_index = node->serialno % APPEND_TBL_MAX;
	int over_index = node->serialno % OVER_TBL_MAX;
	int reg_index = node->serialno % LARGE_TBL_MAX;
	int idx = 0, idx_2 = 0, idx_small = 0, region_id = 0, region_id_2 = 0;
	size_t extent_length_2 = 0, extent_length_small = 0;
	unsigned long mmap_addr_2 = 0, mmap_addr_small = 0;
	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[app_index];
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[over_index];
	struct NVTable_regions *regions = NULL, *regions_2 = NULL;;
		
	DEBUG_FILE("%s: inode number = %lu. offset to read = %lld\n",
		   __func__, node->serialno, file_off_start);

#if DATA_JOURNALING_ENABLED
	
	if (node->is_large_file) {
		regions = _nvp_tbl_regions[reg_index].regions;
		region_id = file_off_start / REGION_COVERAGE;
	}

	if (node->is_large_file) {
		idx = find_idx_to_read_large(file_off_start, regions, &region_id);
	}
	else
		idx = find_idx_to_read(file_off_start, tbl_over);
	
	if (idx != -1) {		
		if (node->is_large_file) {
			off_start_diff = file_off_start - regions[region_id].tbl_mmaps[idx].file_start_off;
			effective_tbl_mmap_off_start = regions[region_id].tbl_mmaps[idx].file_start_off + off_start_diff;
			tbl_mmap_entry_len = regions[region_id].tbl_mmaps[idx].file_end_off - effective_tbl_mmap_off_start + 1;
			if (tbl_mmap_entry_len > length)
				tbl_mmap_entry_len = length;
			
			*extent_length = tbl_mmap_entry_len;
			*mmap_addr = regions[region_id].tbl_mmaps[idx].buf_start + off_start_diff;

			DEBUG_FILE("%s: LARGE reading address = %p, size = %lu, tbl file start = %lu, tbl file end = %lu, Tbl IDX = %d. Inode = %lu\n",
				   __func__, *mmap_addr, *extent_length, regions[region_id].tbl_mmaps[idx].file_start_off, regions[region_id].tbl_mmaps[idx].file_end_off, idx, node->serialno);

			return 0;

		} else {
			off_start_diff = file_off_start - tbl_over->tbl_mmaps[idx].file_start_off;
			effective_tbl_mmap_off_start = tbl_over->tbl_mmaps[idx].file_start_off + off_start_diff;
			tbl_mmap_entry_len = tbl_over->tbl_mmaps[idx].file_end_off - effective_tbl_mmap_off_start + 1;
			if (tbl_mmap_entry_len > length)
				tbl_mmap_entry_len = length;
			*extent_length = tbl_mmap_entry_len;
			*mmap_addr = tbl_over->tbl_mmaps[idx].buf_start + off_start_diff;
			return 0;
			DEBUG_FILE("%s: reading address = %p, size = %lu, tbl file start = %lu, tbl file end = %lu, Tbl IDX = %d. Inode = %lu\n",
				   __func__, *mmap_addr, *extent_length, tbl_mmap_off_start, tbl_mmap_off_end, i, node->serialno);
		}
	}

#endif // DATA_JOURNALING_ENABLED
	
	if (check_append_entry) {
		idx = find_idx_to_read(file_off_start, tbl_app);
		if (idx != -1) {		
			off_start_diff = file_off_start - tbl_app->tbl_mmaps[idx].file_start_off;
			effective_tbl_mmap_off_start = tbl_app->tbl_mmaps[idx].file_start_off + off_start_diff;
			tbl_mmap_entry_len = tbl_app->tbl_mmaps[idx].file_end_off - effective_tbl_mmap_off_start + 1;
			if (tbl_mmap_entry_len > length)
				tbl_mmap_entry_len = length;
			*extent_length = tbl_mmap_entry_len;
			*mmap_addr = tbl_app->tbl_mmaps[idx].buf_start + off_start_diff;
			DEBUG_FILE("%s: reading address = %p, size = %lu, tbl file start = %lu, tbl file end = %lu, Tbl IDX = %d. Inode = %lu\n",
				   __func__, *mmap_addr, *extent_length, tbl_app->tbl_mmaps[idx].file_start_off, tbl_app->tbl_mmaps[idx].file_end_off, idx, node->serialno);
			return 0;
		}
	}

	*mmap_addr = 0;
	return 0;
}

int clear_tbl_mmap_entry(struct NVTable_maps *tbl)
{
	int i = 0;
	size_t len = 0;
	off_t offset_in_page = 0;
	
	DEBUG_FILE("%s: Number of mmap entries = %d\n", __func__, tbl->tbl_mmap_index);
	if (tbl->tbl_mmap_index > 0) { 
		deleted_size += tbl->tbl_mmaps[tbl->tbl_mmap_index-1].file_end_off;
		DEBUG_FILE("%s: Total size deleted = %lu\n", __func__, deleted_size);
		memset((void *)tbl->tbl_mmaps, 0, NUM_OVER_TBL_MMAP_ENTRIES*sizeof(struct table_mmaps));
		tbl->tbl_mmap_index = 0;
	}
	
	return 0;

#if 0
	int i = 0;
	size_t len = 0;
	off_t offset_in_page = 0;
	
	DEBUG_FILE("%s: Number of mmap entries = %d\n", __func__, tbl->tbl_mmap_index);
	for (i = 0; i < tbl->tbl_mmap_index; i++) {
		len = tbl->tbl_mmaps[i].file_end_off - tbl->tbl_mmaps[i].file_start_off + 1;
		munmap((void *)tbl->tbl_mmaps[i].buf_start, len);
		deleted_size += len;
	}
	DEBUG_FILE("%s: Total size deleted = %lu\n", __func__, deleted_size);
	if (tbl->tbl_mmap_index) {
		memset((void *)tbl->tbl_mmaps, 0, NUM_APP_TBL_MMAP_ENTRIES*sizeof(struct table_mmaps));
		tbl->tbl_mmap_index = 0;
	}
	return 0;
#endif
}
