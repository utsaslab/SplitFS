/*
 * =====================================================================================
 *
 *       Filename:  mmap_cache.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:46:02 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include "mmap_cache.h"
#include "handle_mmaps.h"

void nvp_add_to_inode_mapping(struct NVNode *node, ino_t serialno)
{
	struct InodeToMapping *mappingToBeAdded;
	
	int index = serialno % 1024;
	int i, dirty_index;

	if (serialno == 0)
		return;

	DEBUG("Cleanup: root 0x%x, height %u\n", root, height);
	mappingToBeAdded = &_nvp_ino_mapping[index];
	if(mappingToBeAdded->serialno != 0 && mappingToBeAdded->serialno != serialno) {
		// Replacing some mmap() in that global mmap() cache. So must munmap() all the mmap() ranges in that cache. 
		nvp_free_btree(mappingToBeAdded->root, mappingToBeAdded->merkle_root, mappingToBeAdded->height, mappingToBeAdded->root_dirty_cache, mappingToBeAdded->root_dirty_num, mappingToBeAdded->total_dirty_mmaps);		

		mappingToBeAdded->serialno = 0;
	}

	// Check if many mmap()s need to be copied. If total_dirty_mmaps is set, that means all the mmap()s need to be copied. 
	if(node->total_dirty_mmaps) {
		memcpy(mappingToBeAdded->root, node->root, 1024 * sizeof(unsigned long));
		memcpy(mappingToBeAdded->merkle_root, node->merkle_root, 1024 * sizeof(unsigned long));
		
	} else {
		// Only copy the dirty mmaps. The indexes can be found in the root_dirty_cache. 
		for(i = 0; i < node->root_dirty_num; i++) {
			dirty_index = node->root_dirty_cache[i];
			if(node->root && node->root[dirty_index])
				mappingToBeAdded->root[dirty_index] = node->root[dirty_index];

			if(node->merkle_root && node->merkle_root[dirty_index])
				mappingToBeAdded->merkle_root[dirty_index] = node->merkle_root[dirty_index];
		}
	}

	mappingToBeAdded->serialno = serialno;
	
	if(node->root_dirty_num)
		memcpy(mappingToBeAdded->root_dirty_cache, node->root_dirty_cache, 20 * sizeof(unsigned long));

	mappingToBeAdded->root_dirty_num = node->root_dirty_num;
	mappingToBeAdded->total_dirty_mmaps = node->total_dirty_mmaps;
	mappingToBeAdded->height = node->height;      	
}

/* 
 * This function is responsible for copying all the mapping from the global mmap() cache on to the mmap tree of the node. 
 */
int nvp_retrieve_inode_mapping(struct NVNode *node) {

	struct InodeToMapping *mappingToBeRetrieved;
	int index = node->serialno % 1024;
	int dirty_index, i;
	
	DEBUG("Cleanup: root 0x%x, height %u\n", root, height);

	/* 
	 * Get the mapping from the global mmap() cache, based on the inode number of the node whose mapping it should
         * be retrieved from. 
	 */
	mappingToBeRetrieved = &_nvp_ino_mapping[index];
	
	if(mappingToBeRetrieved->serialno == node->serialno) {

		/* 
		 * Copy the file backed mmap()s and the merkle roots. total_dirty_mmaps suggests that there are more than
		 * 20 mmaps that need to be copied.
		 */
		if(mappingToBeRetrieved->total_dirty_mmaps) {
			memcpy(node->root, mappingToBeRetrieved->root, 1024 * sizeof(unsigned long));
			memcpy(node->merkle_root, mappingToBeRetrieved->merkle_root, 1024 * sizeof(unsigned long));
			
		} else {
	
			for(i = 0; i < mappingToBeRetrieved->root_dirty_num; i++) {
				dirty_index = mappingToBeRetrieved->root_dirty_cache[i];
				if(mappingToBeRetrieved->root && mappingToBeRetrieved->root[dirty_index])
					node->root[dirty_index] = mappingToBeRetrieved->root[dirty_index];

				if(mappingToBeRetrieved->merkle_root && mappingToBeRetrieved->merkle_root[dirty_index])
					node->merkle_root[dirty_index] = mappingToBeRetrieved->merkle_root[dirty_index];
			}
		}
		
		// Copy the root_dirty_cache from the global mmap() cache on to the node mmap() cache
		//if(mappingToBeRetrieved->root_dirty_num)
		memcpy(node->root_dirty_cache, mappingToBeRetrieved->root_dirty_cache, 20 * sizeof(unsigned long));
		
		node->root_dirty_num = mappingToBeRetrieved->root_dirty_num;
		node->total_dirty_mmaps = mappingToBeRetrieved->total_dirty_mmaps;
		node->height = mappingToBeRetrieved->height;      	

		//printf("%s: end: node->root[0] = %lu, mapping root = %lu, mapping root dirty num = %d, node->serialno = %lu, index = %d, node reference = %d, thread_id = %lu\n", __func__, node->root[0], mappingToBeRetrieved->root[0], mappingToBeRetrieved->root_dirty_num, node->serialno, index, node->reference, pthread_self());

		goto out;
	}

	return -1;
 out:
	return 0;
}


