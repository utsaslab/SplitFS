/*
 * =====================================================================================
 *
 *       Filename:  staging.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:44:43 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include "utils.h"
#include "staging.h"
#include "handle_mmaps.h"

void nvp_transfer_to_free_dr_pool(struct NVNode *node)
{
	int i, num_free_dr_mmaps;
	struct free_dr_pool *free_pool_of_dr_mmap;
	unsigned long offset_in_page = 0;

#if DATA_JOURNALING_ENABLED

	if(node->dr_over_info.start_addr != 0) {
		free_pool_of_dr_mmap = (struct free_dr_pool *) malloc(sizeof(struct free_dr_pool));
		free_pool_of_dr_mmap->dr_offset_start = node->dr_over_info.dr_offset_start;
		free_pool_of_dr_mmap->dr_offset_end = DR_OVER_SIZE;
		free_pool_of_dr_mmap->start_addr = node->dr_over_info.start_addr;
		free_pool_of_dr_mmap->dr_fd = node->dr_over_info.dr_fd;
		free_pool_of_dr_mmap->dr_serialno = node->dr_over_info.dr_serialno;
		free_pool_of_dr_mmap->valid_offset = node->dr_over_info.valid_offset;

		//LFDS711_QUEUE_UMM_SET_VALUE_IN_ELEMENT(free_pool_of_dr_mmap->qe, free_pool_of_dr_mmap);
		//lfds711_queue_umm_enqueue( &qs_over, &(free_pool_of_dr_mmap->qe) );
		if (lfq_enqueue(&staging_over_mmap_queue_ctx, free_pool_of_dr_mmap) != 0)
			assert(0);

		memset((void *)&node->dr_over_info, 0, sizeof(struct free_dr_pool));
		__atomic_fetch_sub(&dr_mem_allocated, DR_OVER_SIZE, __ATOMIC_SEQ_CST);
	}

#endif // DATA_JOURNALING_ENABLED

	if(node->dr_info.start_addr != 0) {
		free_pool_of_dr_mmap = (struct free_dr_pool *) malloc(sizeof(struct free_dr_pool));

		node->dr_info.valid_offset = align_cur_page(node->dr_info.valid_offset);
		if (node->dr_info.valid_offset > DR_SIZE)
			node->dr_info.valid_offset = DR_SIZE;

		free_pool_of_dr_mmap->dr_offset_start = DR_SIZE;
		free_pool_of_dr_mmap->dr_offset_end = node->dr_info.valid_offset;
		free_pool_of_dr_mmap->start_addr = node->dr_info.start_addr;
		free_pool_of_dr_mmap->dr_fd = node->dr_info.dr_fd;
		free_pool_of_dr_mmap->dr_serialno = node->dr_info.dr_serialno;
		free_pool_of_dr_mmap->valid_offset = node->dr_info.valid_offset;

		//LFDS711_QUEUE_UMM_SET_VALUE_IN_ELEMENT(free_pool_of_dr_mmap->qe, free_pool_of_dr_mmap);
		//lfds711_queue_umm_enqueue( &qs, &(free_pool_of_dr_mmap->qe) );
		if (lfq_enqueue(&staging_mmap_queue_ctx, free_pool_of_dr_mmap) != 0)
			assert(0);

		memset((void *)&node->dr_info, 0, sizeof(struct free_dr_pool));
		__atomic_fetch_sub(&dr_mem_allocated, DR_SIZE, __ATOMIC_SEQ_CST);

		DEBUG_FILE("%s: staging inode = %lu. Inserted into global pool with valid offset = %lld\n",
			   __func__, free_pool_of_dr_mmap->dr_serialno, free_pool_of_dr_mmap->valid_offset);
	}
}
