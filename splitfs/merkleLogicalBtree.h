#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "fileops_nvp.h"
#include "timers.h"

//#define MAP_SIZE 512

#define BITMAP_PER_INDEX_NUM_CACHELINES 64

#define SIZE_OF_CACHELINE 64

//#define LEVEL0_BIT_CACHELINE_COVERAGE TOTAL_NUM_CACHELINES/LEVEL0_NUM_CHILDREN
#if MAP_SIZE == 512
#define LEVEL0_NUM_CHILDREN 8
#define LEVEL0_NUM_CHILDREN_INDICES_PER_BIT (8/64)
#define LEVEL1_NUM_CHILDREN 8
#define LEVEL1_NUM_CHILDREN_INDICES_PER_BIT (4/64)
#define LEVEL2_NUM_CHILDREN 4
#define LEVEL0_BIT_CACHELINE_COVERAGE 131072
#define LEVEL1_BIT_CACHELINE_COVERAGE 16384
#define LEVEL2_BIT_CACHELINE_COVERAGE 4096
#define LEAF_BUFFER 131072
#elif MAP_SIZE == 256
#define LEVEL0_NUM_CHILDREN 4
#define LEVEL0_NUM_CHILDREN_INDICES_PER_BIT (4/64)
#define LEVEL1_NUM_CHILDREN 8
#define LEVEL1_NUM_CHILDREN_INDICES_PER_BIT (4/64)
#define LEVEL2_NUM_CHILDREN 4
#define LEVEL0_BIT_CACHELINE_COVERAGE 65536
#define LEVEL1_BIT_CACHELINE_COVERAGE 16384
#define LEVEL2_BIT_CACHELINE_COVERAGE 4096
#define LEAF_BUFFER 65536
#elif MAP_SIZE == 128
#define LEVEL0_NUM_CHILDREN 8
#define LEVEL0_NUM_CHILDREN_INDICES_PER_BIT (8/64)
#define LEVEL1_NUM_CHILDREN 4
#define LEVEL0_BIT_CACHELINE_COVERAGE 32768
#define LEVEL1_BIT_CACHELINE_COVERAGE 4096
#define LEAF_BUFFER 32768
#elif MAP_SIZE == 64
#define LEVEL0_NUM_CHILDREN 4
#define LEVEL0_NUM_CHILDREN_INDICES_PER_BIT (4/64)
#define LEVEL1_NUM_CHILDREN 4
#define LEVEL0_BIT_CACHELINE_COVERAGE 16384
#define LEVEL1_BIT_CACHELINE_COVERAGE 4096
#define LEAF_BUFFER 16384
#elif MAP_SIZE == 32
#define LEVEL0_NUM_CHILDREN 2
#define LEVEL0_NUM_CHILDREN_INDICES_PER_BIT (2/64)
#define LEVEL1_NUM_CHILDREN 4
#define LEVEL0_BIT_CACHELINE_COVERAGE 8192
#define LEVEL1_BIT_CACHELINE_COVERAGE 4096
#define LEAF_BUFFER 8192
#elif MAP_SIZE == 16
#define LEVEL0_NUM_CHILDREN 4
#define LEVEL0_NUM_CHILDREN_INDICES_PER_BIT (4096/64)
#define LEVEL0_BIT_CACHELINE_COVERAGE 4096
#define LEAF_BUFFER 4096
#elif MAP_SIZE == 8
#define LEAF_BUFFER 2048
#elif MAP_SIZE == 4
#define LEAF_BUFFER 1024
#elif MAP_SIZE == 2
#define LEAF_BUFFER 512
#endif

#define LEAF_PER_INDEX_CACHELINE_COVERAGE 64
#define DEFAULT_PAGE_SIZE 4096
#define SIZE_OF_NODE 336
#define NODE_BUFFER DEFAULT_PAGE_SIZE - SIZE_OF_NODE

struct merkleBtreeLeaf {
	uint64_t bitmaps[LEAF_BUFFER];
};

struct merkleBtreeNode {
	uint64_t level0;
	uint64_t level1[8];
	uint64_t level2[32];
	struct merkleBtreeLeaf *leaf;
	uint8_t buffer[NODE_BUFFER];
};


void createTree(struct merkleBtreeNode **node);
void printTree(struct merkleBtreeNode *node);
void modifyBmap(struct merkleBtreeNode *root, uint64_t offset, uint64_t size);
uint64_t traverseTree(struct merkleBtreeNode *root, unsigned long mmap_start_addr);
uint64_t min(uint64_t val1, uint64_t val2);

static inline unsigned char _bittestandset(unsigned long *a, unsigned long b) {
	asm("bts %1,%0" : "+m" (*a) : "r" (b));
	return 0;
}

uint64_t min(uint64_t val1, uint64_t val2) {

	if(val1 <= val2)
		return val1;
	return val2;
}

void createTree(struct merkleBtreeNode **node) {

	int i;

	//printf("%s: doing initial malloc\n", __func__);
	*node = (struct merkleBtreeNode *) malloc(sizeof(struct merkleBtreeNode));
	//printf("%s: initial malloc done\n", __func__);
	
	(*node)->level0 = 0;
	for(i=0; i<8; i++)
		(*node)->level1[i] = 0;
	for(i=0; i<32; i++)
		(*node)->level2[i] = 0;

	//printf("%s: Doing leaf malloc\n", __func__);
	(*node)->leaf = (struct merkleBtreeLeaf *) malloc(sizeof(struct merkleBtreeLeaf));
	//printf("%s: leaf malloc done\n", __func__);
	
	for(i=0; i<LEAF_BUFFER; i++)
		((*node)->leaf)->bitmaps[i] = 0;
}

void printTree(struct merkleBtreeNode *root) {

	uint64_t i, data, index;
	
	if(root == NULL)
		return;

	struct merkleBtreeNode *node = root;
	
	printf("---------------------------------------------\n");
	printf("level0 stats: \n");

	uint64_t num_dirty_bits = 0;
	index = 0;
	data = node->level0;
	while(__builtin_popcountll(data)) {
		asm("bsfq %0, %0" : "=r" (index) : "0" (data));
		//printf("dirty bit = %lu\n", index);
		data &= data - 1;
	}
	
	printf("Number of dirty bits = %d\n", __builtin_popcountll(node->level0));

	printf("---------------------------------------------\n");

#ifdef LEVEL1_NUM_CHILDREN
	
	printf("level1 stats: \n");
	num_dirty_bits = 0;
        for(i=0; i<LEVEL1_NUM_CHILDREN; i++) {
		num_dirty_bits += __builtin_popcountll(node->level1[i]);
		index = 0;
		data = node->level1[i];
		while(__builtin_popcountll(data)) {
			asm("bsfq %0, %0" : "=r" (index) : "0" (data));
			//printf("index = %lu, dirty bit = %lu\n", i, index);
			data &= data - 1;
		}
	}

	printf("Number of dirty bits = %lu\n", num_dirty_bits);
	
	printf("---------------------------------------------\n");

#endif
#ifdef LEVEL2_NUM_CHILDREN
	
	printf("level2 stats: \n");
	num_dirty_bits = 0;
	for(i=0; i<LEVEL1_NUM_CHILDREN*LEVEL2_NUM_CHILDREN; i++) {
		num_dirty_bits += __builtin_popcountll(node->level2[i]);
		index = 0;
		data = node->level2[i];
		while(__builtin_popcountll(data)) {
			asm("bsfq %0, %0" : "=r" (index) : "0" (data));
			//printf("index = %lu, dirty bit = %lu\n", i, index);
			data &= data - 1;
		}
	}

	printf("Number of dirty bits = %lu\n", num_dirty_bits);
	
	printf("---------------------------------------------\n");

#endif

	printf("leaf stats: \n");
	num_dirty_bits = 0;
        for(i=0; i<LEAF_BUFFER; i++)
		num_dirty_bits += __builtin_popcountll((node->leaf)->bitmaps[i]);

	printf("Number of dirty bits = %lu\n", num_dirty_bits);

	printf("---------------------------------------------\n");
}



void modifyBmap(struct merkleBtreeNode *root, uint64_t offset, uint64_t size) {

	int i;
	uint64_t num_dirty_bits_in_index;
	unsigned long long bmask;

	uint64_t numCachelines = (uint64_t) size / SIZE_OF_CACHELINE;
	
	//uint64_t level0_bit_cacheline_coverage = 1024*1024;
	
	uint64_t startCachelineNumber = (uint64_t) (offset / SIZE_OF_CACHELINE);
	uint64_t endCachelineNumber = (uint64_t) ((offset + size - 1) / SIZE_OF_CACHELINE);
	
#ifdef LEVEL0_NUM_CHILDREN

	uint64_t level0_start_dirty_bit = (uint64_t) (startCachelineNumber / LEVEL0_BIT_CACHELINE_COVERAGE);
	uint64_t level0_end_dirty_bit = (uint64_t) (endCachelineNumber / LEVEL0_BIT_CACHELINE_COVERAGE);
	uint64_t level0_num_dirty_bits = level0_end_dirty_bit - level0_start_dirty_bit + 1;

	//printf("numCachelines = %lu, startCachelineNumber = %lu, level0_start_dirty_bit = %lu, level0_num_dirty_bits = %lu, LEVEL0_BIT_CACHELINE_COVERAGE = %d\n", numCachelines, startCachelineNumber, level0_start_dirty_bit, level0_num_dirty_bits, LEVEL0_BIT_CACHELINE_COVERAGE);
	
        bmask = 0;
	if(level0_num_dirty_bits == 64) {
		bmask = UINT64_MAX;
		root->level0 |= bmask;
		level0_num_dirty_bits = 0;
	}

	else if(level0_num_dirty_bits >= 32) {
		bmask |= UINT32_MAX;
		bmask = bmask << (32-(level0_start_dirty_bit));
		root->level0 |= bmask;
		level0_start_dirty_bit += 32;
		level0_num_dirty_bits -= 32;
	}

	//printf("%s: level0 calling setbit\n", __func__);
	for(i=0; i<level0_num_dirty_bits; i++)
		_bittestandset(&root->level0, 63-level0_start_dirty_bit-i);

#endif       
#ifdef LEVEL1_NUM_CHILDREN

	uint64_t level1_start_dirty_bit = (uint64_t) (startCachelineNumber / LEVEL1_BIT_CACHELINE_COVERAGE);
	uint64_t level1_end_dirty_bit = (uint64_t) (endCachelineNumber / LEVEL1_BIT_CACHELINE_COVERAGE);
	uint64_t level1_num_dirty_bits = level1_end_dirty_bit - level1_start_dirty_bit + 1;
        uint64_t level1_start_dirty_index = level1_start_dirty_bit / 64;
	uint64_t level1_end_dirty_index = level1_end_dirty_bit / 64;
        uint64_t level1_start_dirty_bit_in_index = level1_start_dirty_bit - level1_start_dirty_index*64;
	
	while(level1_start_dirty_index < level1_end_dirty_index) {
		num_bits_in_index = (level1_start_dirty_index+1)*64-level1_start_dirty_bit;
		
                bmask = 0;
		if(num_bits_in_index == 64) {
			root->level1[level1_start_dirty_index] |= UINT64_MAX;
			num_bits_in_index = 0;
			level1_num_dirty_bits -= 64;
			level1_start_dirty_bit += 64;
		}

		else if(num_bits_in_index >= 32) {
			bmask |= UINT32_MAX;
			bmask = bmask << (32-(level1_start_dirty_bit_in_index));
			root->level1[level1_start_dirty_index] |= bmask;
			level1_start_dirty_bit += 32;
			level1_num_dirty_bits -= 32;
			num_bits_in_index -= 32;
		}
		
		//printf("%s: level1 calling setbit\n", __func__);
		for(i=0; i<num_bits_in_index; i++)
			_bittestandset(&root->level1[level1_start_dirty_index], 63-(level1_start_dirty_bit_in_index+i));
		
		level1_start_dirty_index++;
		level1_start_dirty_bit += num_bits_in_index;
		level1_num_dirty_bits -= num_bits_in_index;
		level1_start_dirty_bit_in_index = 0;
	}

	bmask = 0;

	if(level1_num_dirty_bits == 64) {
		root->level1[level1_start_dirty_index] |= UINT64_MAX;
		level1_num_dirty_bits -= 64;
		level1_start_dirty_bit += 64;
	}

	else if(level1_num_dirty_bits >= 32) {
		bmask |= UINT32_MAX;
		bmask = bmask << (32-(level1_start_dirty_bit_in_index));
		root->level1[level1_start_dirty_index] |= bmask;
		level1_start_dirty_bit += 32;
		level1_num_dirty_bits -= 32;
	}	

	uint64_t effective_index = level1_start_dirty_bit - level1_start_dirty_index*64;

	//printf("%s: level1 calling setbit\n", __func__);
	for(i=0; i<level1_num_dirty_bits; i++)
		_bittestandset(&root->level1[level1_start_dirty_index], 63-(effective_index+i));
	
	//printf("numCachelines = %lu, startCachelineNumber = %lu, level1_start_dirty_bit = %lu, level1_num_dirty_bits = %lu, LEVEL1_BIT_CACHELINE_COVERAGE = %d\n", numCachelines, startCachelineNumber, level1_start_dirty_bit, level1_num_dirty_bits, LEVEL1_BIT_CACHELINE_COVERAGE);

#endif
#ifdef LEVEL2_NUM_CHILDREN
	
	uint64_t level2_start_dirty_bit = (uint64_t) (startCachelineNumber / LEVEL2_BIT_CACHELINE_COVERAGE);
	uint64_t level2_end_dirty_bit = (uint64_t) (endCachelineNumber / LEVEL2_BIT_CACHELINE_COVERAGE);
	uint64_t level2_num_dirty_bits = level2_end_dirty_bit - level2_start_dirty_bit + 1;

	uint64_t level2_start_dirty_index = level2_start_dirty_bit / 64;
	uint64_t level2_end_dirty_index = level2_end_dirty_bit / 64;
	uint64_t level2_start_dirty_bit_in_index = level2_start_dirty_bit - level2_start_dirty_index*64;
 
        //printf("%s: level2_num_dirty_bits = %lu, level2_start_dirty_bit = %lu, level2_end_dirty_bit = %lu, level2_start_dirty_index = %lu, level2_end_dirty_index = %lu\n", __func__, level2_num_dirty_bits, level2_start_dirty_bit, level2_end_dirty_bit,level2_start_dirty_index, level2_end_dirty_index);


	while(level2_start_dirty_index < level2_end_dirty_index) {
                bmask = 0;
		num_bits_in_index = (level2_start_dirty_index+1)*64-level2_start_dirty_bit;
                if(num_bits_in_index == 64) {
			root->level2[level2_start_dirty_index] |= UINT64_MAX;
			num_bits_in_index = 0;
			level2_num_dirty_bits -= 64;
			level2_start_dirty_bit += 64;
		}

		else if(num_bits_in_index >= 32) {
			bmask |= UINT32_MAX;
			bmask = bmask << (32-(level2_start_dirty_bit_in_index));
			root->level2[level2_start_dirty_index] |= bmask;
			level2_start_dirty_bit += 32;
			level2_num_dirty_bits -= 32;
			num_bits_in_index -= 32;
		}
		
		for(i=0; i<num_bits_in_index; i++)
			_bittestandset(&root->level2[level2_start_dirty_index], 63-(level2_start_dirty_bit_in_index+i));
		
                level2_start_dirty_index++;
                level2_start_dirty_bit += num_bits_in_index;
                level2_num_dirty_bits -= num_bits_in_index;
		level2_start_dirty_bit_in_index = 0;
	}
	
	bmask = 0;

	if(level2_num_dirty_bits == 64) {
		root->level2[level2_start_dirty_index] |= UINT64_MAX;
		level2_num_dirty_bits -= 64;
		level2_start_dirty_bit += 64;
	}

	else if(level2_num_dirty_bits >= 32) {
		bmask |= UINT32_MAX;
		bmask = bmask << (32-(level2_start_dirty_bit_in_index));
		root->level2[level2_start_dirty_index] |= bmask;
		level2_start_dirty_bit += 32;
		level2_num_dirty_bits -= 32;
	}
	
	effective_index = level2_start_dirty_bit - level2_start_dirty_index*64;

	//printf("%s: effective_index = %lu, level2_start_dirty_index = %lu, level2_num_dirty_bits = %lu\n", __func__, effective_index, level2_start_dirty_index, level2_num_dirty_bits);
        for(i=0; i<level2_num_dirty_bits; i++)
                _bittestandset(&root->level2[level2_start_dirty_index], 63-(effective_index+i));

#endif
	
        struct merkleBtreeLeaf *leaf = root->leaf;
	uint64_t leaf_start_dirty_index = (uint64_t) (startCachelineNumber / LEAF_PER_INDEX_CACHELINE_COVERAGE);
	uint64_t leaf_start_dirty_bit_in_index = (uint64_t) (startCachelineNumber - (leaf_start_dirty_index * LEAF_PER_INDEX_CACHELINE_COVERAGE));

	while(numCachelines != 0) {
		num_dirty_bits_in_index = min(64-leaf_start_dirty_bit_in_index, numCachelines);
		bmask = 0;
		if(num_dirty_bits_in_index == 64) {
			bmask = UINT64_MAX;
			leaf->bitmaps[leaf_start_dirty_index] |= bmask;
                        num_dirty_bits_in_index = 0;
			numCachelines -= 64;			
		}

		else if(num_dirty_bits_in_index >= 32) {
			bmask = UINT32_MAX;
			bmask = bmask << (32-leaf_start_dirty_bit_in_index);
			leaf->bitmaps[leaf_start_dirty_index] |= bmask;
			num_dirty_bits_in_index -= 32;
			leaf_start_dirty_bit_in_index += 32;
			numCachelines -= 32;
		}

		//printf("%s: leaf calling setbit\n", __func__);

		for(i=0; i<num_dirty_bits_in_index; i++)
			_bittestandset(&leaf->bitmaps[leaf_start_dirty_index], 63-leaf_start_dirty_bit_in_index-i);

		numCachelines -= num_dirty_bits_in_index;
		leaf_start_dirty_index++;
		leaf_start_dirty_bit_in_index = 0;
	}
	
}

uint64_t traverseTree(struct merkleBtreeNode *root, unsigned long mmap_start_addr) {

	uint64_t i,j;
	uint64_t dirtyCachelines = 0;
	uint64_t index,data;
	uint64_t effective_index;
	instrumentation_type clflushopt_time;
	
#ifdef LEVEL1_NUM_CHILDREN
	uint8_t level0_dirty_children[LEVEL1_NUM_CHILDREN];
#elif defined LEVEL0_NUM_CHILDREN
	uint64_t level0_dirty_children[2048];
#endif
#ifdef LEVEL0_NUM_CHILDREN	
	uint64_t level0_total_dirty_children = 0;

	//printTree(root);
	
	index = 0;
	data = root->level0;
	while(__builtin_popcountll(data)) {
		asm("bsfq %0, %0" : "=r" (index) : "0" (data));
		effective_index = 63-index;
		//printf("%s: effective index = %lu\n", __func__, effective_index);
		if((level0_total_dirty_children == 0) || (level0_dirty_children[level0_total_dirty_children-1] != effective_index*LEVEL0_NUM_CHILDREN_INDICES_PER_BIT)) {
			level0_dirty_children[level0_total_dirty_children] = (effective_index)*LEVEL0_NUM_CHILDREN_INDICES_PER_BIT;
			//printf("%s: level0_dirty_children[level0_total_dirty_children] =  = %lu\n", __func__, effective_index);
			level0_total_dirty_children++;
		}
	        data &= data - 1;
		root->level0 &= root->level0 - 1;
	}

	//printf("%s: level0_total_dirty_children = %u, level0_dirty_children[0] = %u\n", __func__, level0_total_dirty_children, level0_dirty_children[0]);

#endif	
#ifdef LEVEL2_NUM_CHILDREN

	uint64_t level1_dirty_children[LEVEL1_NUM_CHILDREN*LEVEL2_NUM_CHILDREN];
	uint64_t level1_total_dirty_children = 0;

	for(i=0; i<level0_total_dirty_children; i++) {
		index = 0;
		data = root->level1[level0_dirty_children[i]];
		while(__builtin_popcountll(data)) {
			asm("bsfq %0, %0" : "=r" (index) : "0" (data));
			effective_index = (63-index) + (level0_dirty_children[i] * 64); 
			if((level1_total_dirty_children == 0) || (level1_dirty_children[level1_total_dirty_children-1] != effective_index*LEVEL1_NUM_CHILDREN_INDICES_PER_BIT)) {
				level1_dirty_children[level1_total_dirty_children] = effective_index*LEVEL1_NUM_CHILDREN_INDICES_PER_BIT;
				level1_total_dirty_children++;
			}
			data &= data - 1;
			root->level1[level0_dirty_children[i]] &= root->level1[level0_dirty_children[i]] - 1;
		}		
	}
	
#elif defined LEVEL0_NUM_CHILDREN

	uint64_t level1_dirty_children[2048];
	uint64_t level1_total_dirty_children = 0;
	for(i=0; i<level0_total_dirty_children; i++) {
		level1_dirty_children[i] = level0_dirty_children[i];
		level1_total_dirty_children++;
	}

	//printf("%s: level1_total_dirty_children = %lu, level1_dirty_children[0] = %lu\n", __func__, level1_total_dirty_children, level1_dirty_children[0]);

#endif
#ifdef LEVEL2_NUM_CHILDREN
	
	uint64_t level2_dirty_children[2048];
	uint64_t level2_total_dirty_children = 0;
	
	for(i=0; i<level1_total_dirty_children; i++) {
		index = 0;
		data = root->level2[level1_dirty_children[i]];
		while(__builtin_popcountll(data)) {
			asm("bsfq %0, %0" : "=r" (index) : "0" (data));
			effective_index = (63-index) + (level1_dirty_children[i] * 64); 
			level2_dirty_children[level2_total_dirty_children] = effective_index;
			level2_total_dirty_children++;
			data &= data - 1;
			root->level2[level1_dirty_children[i]] &= root->level2[level1_dirty_children[i]] - 1;
			
		}		
	}

#elif defined LEVEL0_NUM_CHILDREN

	uint64_t level2_dirty_children[2048];
	uint64_t level2_total_dirty_children = 0;
	for(i=0; i<level1_total_dirty_children; i++) {
		level2_dirty_children[i] = level1_dirty_children[i];
		level2_total_dirty_children++;
	}

	//printf("%s: level2_total_dirty_children = %lu, level2_dirty_children[0] = %lu\n", __func__, level2_total_dirty_children, level2_dirty_children[0]);
	

#endif	
	
	struct merkleBtreeLeaf *leaf = root->leaf;
	uint64_t leaf_dirty_index[LEAF_BUFFER];
	uint64_t leaf_total_dirty_indexes = 0;
	
#ifdef LEVEL0_NUM_CHILDREN
	
	for(i=0; i<level2_total_dirty_children; i++) {
		for(j=0; j<64; j++) {
			data = leaf->bitmaps[level2_dirty_children[i]+j];
			if(data) {
				leaf_dirty_index[leaf_total_dirty_indexes] = (level2_dirty_children[i])+j;
				leaf_total_dirty_indexes++;
			}
		}
	}
	
	/* Here is when we find out the dirty cachelines in the final level */
	
	for(i=0; i<leaf_total_dirty_indexes; i++) {
		if(leaf->bitmaps[leaf_dirty_index[i]] == UINT64_MAX) {
			leaf->bitmaps[leaf_dirty_index[i]] = 0;
			/* cacheline flush of start_addr + (leaf->bitmaps[leaf_dirty_index[i][j]] * 4096), size = 4*1024 bytes */			
			//printf("%s: flushing 64 cacheline\n", __func__);
			do_cflushopt_len((void *)(mmap_start_addr + (leaf_dirty_index[i] * 4096)), 4096);
			dirtyCachelines += 64;
		} else {
			index = 0;
			data = leaf->bitmaps[leaf_dirty_index[i]];
			while(__builtin_popcountll(data)) {
				asm("bsfq %0, %0" : "=r" (index) : "0" (data));
				effective_index = 63-index;
				data &= data - 1;
				leaf->bitmaps[leaf_dirty_index[i]] &= leaf->bitmaps[leaf_dirty_index[i]] - 1;
				//printf("%s: flushing 1 cacheline\n", __func__);
				do_cflushopt_len((void *)(mmap_start_addr + (leaf_dirty_index[i] * 4096) + (effective_index * 64)), 64);					
				dirtyCachelines++;
			}
		}
	}
	
	//printf("Dirty cachelines = %lu\n", dirtyCachelines);
#else
	leaf_total_dirty_indexes = LEAF_BUFFER;
	for(i=0; i<leaf_total_dirty_indexes; i++) {
		if(leaf->bitmaps[i] == UINT64_MAX) {
			leaf->bitmaps[i] = 0;
			/* cacheline flush of start_addr + (leaf->bitmaps[leaf_dirty_index[i][j]] * 4096), size = 4*1024 bytes */
			
			//printf("%s: flushing 64 cacheline\n", __func__);
			do_cflushopt_len((void *)(mmap_start_addr + (i * 4096)), 4096);
			dirtyCachelines += 64;
		} else {
			index = 0;
			data = leaf->bitmaps[i];
			while(__builtin_popcountll(data)) {
				asm("bsfq %0, %0" : "=r" (index) : "0" (data));
				effective_index = 63-index;
				data &= data - 1;
				leaf->bitmaps[i] &= leaf->bitmaps[i] - 1;
				do_cflushopt_len((void *)(mmap_start_addr + (i * 4096) + (effective_index * 64)), 64); 
				dirtyCachelines++;
			}
		}
	}	

#endif

	return dirtyCachelines;
}


void traverseTreeSequential(struct merkleBtreeNode *root) {

	uint64_t i,j;
	struct merkleBtreeLeaf *leaf = root->leaf;
	uint64_t dirtyCachelines = 0;
	
	for(i=0; i<LEAF_BUFFER; i++) {
		if(leaf->bitmaps[i] != 0) {
			if(leaf->bitmaps[i] == UINT64_MAX)
				dirtyCachelines += 64;
			else {
				for(j=0; j<BITMAP_PER_INDEX_NUM_CACHELINES; j++) {
					if(leaf->bitmaps[i] & (1UL << (63UL-j)))
						dirtyCachelines++;
				}
			}
		}
	}
}
