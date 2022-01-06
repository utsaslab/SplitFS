/*
 * =====================================================================================
 *
 *       Filename:  handle_mmaps.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:39:26 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <libsyscall_intercept_hook_point.h>
// #include "liblfds711/inc/liblfds711.h"
#include "handle_mmaps.h"
#include "bg_clear_mmap.h"
#include "timers.h"
#include "nvp_lock.h"
#include "fsync.h"

void *intel_memcpy(void * __restrict__ b, const void * __restrict__ a, size_t n){
	char *s1 = b;
	const char *s2 = a;
	for(; 0<n; --n)*s1++ = *s2++;
	return b;
}

static unsigned long calculate_capacity(unsigned int height)
{
	unsigned long capacity = MAX_MMAP_SIZE;

	while (height) {
		capacity *= PER_NODE_MAPPINGS;
		height--;
	}

	return capacity;
}

void nvp_free_btree(unsigned long *root, unsigned long *merkle_root, unsigned long height, unsigned long *dirty_cache, int root_dirty_num, int total_dirty_mmaps)
{
	int i, dirty_index;
	dirty_index = 0;
	if (height == 0) {
		for(i = 0; i < root_dirty_num; i++) {
			dirty_index = dirty_cache[i];
			if(root && root[dirty_index]) {
				munmap((void *)root[dirty_index], MAX_MMAP_SIZE);
				root[dirty_index] = 0;
				merkle_root[dirty_index] = 0;
			}
		}
		root_dirty_num = 0;
		if(total_dirty_mmaps) {
			for (i = 0; i < 1024; i++) {
				if (root && root[i]) {
					DEBUG("munmap: %d, addr 0x%lx\n",
					      i, root[i]);
					munmap((void *)root[i], MAX_MMAP_SIZE);
					root[i] = 0;
					merkle_root[i] = 0;
				}
			}
		}
		return;
	}
	for (i = 0; i < 1024; i++) {
		if (root[i] && merkle_root[i]) {
			nvp_free_btree((unsigned long *)root[i], (unsigned long *)merkle_root[i],
				       height - 1, NULL, 0, 1);
			root[i] = 0;
			merkle_root[i] = 0;
		}
	}
	free(root);
	free(merkle_root);
}

void create_dr_mmap(struct NVNode *node, int is_overwrite)
{
	void *addr = NULL;
	struct stat stat_buf;
	char dr_fname[256];
	int dr_fd = 0, ret = 0;
	num_mmap++;

	struct free_dr_pool *send_to_global = (struct free_dr_pool *) malloc(sizeof(struct free_dr_pool));

	if (is_overwrite) {
		_nvp_full_drs[full_dr_idx].dr_fd = node->dr_over_info.dr_fd;
		_nvp_full_drs[full_dr_idx].start_addr = node->dr_over_info.start_addr;
		_nvp_full_drs[full_dr_idx].size = DR_OVER_SIZE;
		full_dr_idx++;
	} else {
		_nvp_full_drs[full_dr_idx].dr_fd = node->dr_info.dr_fd;
		_nvp_full_drs[full_dr_idx].start_addr = node->dr_info.start_addr;
		_nvp_full_drs[full_dr_idx].size = DR_SIZE;
		full_dr_idx++;
	}

	if (is_overwrite)
		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-OVER-XXXXXX");
	else
		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-XXXXXX");
	dr_fd = syscall_no_intercept(SYS_open, mktemp(dr_fname), O_RDWR | O_CREAT, 0666);
	if (dr_fd < 0) {
		MSG("%s: mkstemp of DR file failed. Err = %s\n",
		    __func__, strerror(-dr_fd));
		assert(0);
	}
	if (is_overwrite)
		ret = posix_fallocate(dr_fd, 0, DR_OVER_SIZE);
	else
		ret = posix_fallocate(dr_fd, 0, DR_SIZE);

	if (ret < 0) {
		MSG("%s: posix_fallocate failed. Err = %s\n",
		    __func__, strerror(errno));
		assert(0);
	}

	syscall_no_intercept(SYS_fstat, dr_fd, &stat_buf);

	if (is_overwrite) {
		node->dr_over_info.dr_fd = dr_fd;
		node->dr_over_info.start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_OVER_SIZE,
			 PROT_READ | PROT_WRITE, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 node->dr_over_info.dr_fd, //fd_with_max_perms,
			 0
			 );

		if (node->dr_over_info.start_addr == 0) {
			MSG("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
			assert(0);
		}
		node->dr_over_info.valid_offset = 0;
		node->dr_over_info.dr_offset_start = 0;
		node->dr_over_info.dr_offset_end = DR_OVER_SIZE;
		node->dr_over_info.dr_serialno = stat_buf.st_ino;

	} else {
		node->dr_info.dr_fd = dr_fd;
		node->dr_info.start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_SIZE,
			 PROT_READ | PROT_WRITE, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 node->dr_info.dr_fd, //fd_with_max_perms,
			 0
			 );

		if (node->dr_info.start_addr == 0) {
			MSG("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
			assert(0);
		}
		node->dr_info.valid_offset = 0;
		node->dr_info.dr_offset_start = DR_SIZE;
		node->dr_info.dr_offset_end = node->dr_info.valid_offset;
		node->dr_info.dr_serialno = stat_buf.st_ino;
	}

	DEBUG_FILE("%s: Unmapped and mapped DR file again\n", __func__);
}

void change_dr_mmap(struct NVNode *node, int is_overwrite) {
	struct free_dr_pool *temp_dr_mmap = NULL;
	unsigned long offset_in_page = 0;

	DEBUG_FILE("%s: Throwing away DR File FD = %d\n", __func__, node->dr_info.dr_fd);

	if (is_overwrite) {
		struct free_dr_pool *temp_dr_info = lfq_dequeue(&staging_over_mmap_queue_ctx);
		if (temp_dr_info != 0) {
		//if( lfds711_queue_umm_dequeue(&qs_over, &qe_over) ) {
			// Found addr in global pool
			//struct free_dr_pool *temp_dr_info = NULL;
			//temp_dr_info = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe_over );
			node->dr_over_info.start_addr = temp_dr_info->start_addr;
			node->dr_over_info.valid_offset = temp_dr_info->valid_offset;
			node->dr_over_info.dr_offset_start = temp_dr_info->dr_offset_start;
			node->dr_over_info.dr_fd = temp_dr_info->dr_fd;
			node->dr_over_info.dr_serialno = temp_dr_info->dr_serialno;
			node->dr_over_info.dr_offset_end = DR_OVER_SIZE;
			DEBUG_FILE("%s: DR found in global pool. Got from global pool. FD = %d\n",
				   __func__, temp_dr_info->dr_fd);
		} else {
			DEBUG_FILE("%s: Global queue empty\n", __func__);
			memset((void *)&node->dr_info, 0, sizeof(struct free_dr_pool));
		}
	} else {
		struct free_dr_pool *temp_dr_info = lfq_dequeue(&staging_mmap_queue_ctx);
		if (temp_dr_info != 0) {
			//if( lfds711_queue_umm_dequeue(&qs, &qe) ) {
			// Found addr in global pool
			//struct free_dr_pool *temp_dr_info = NULL;
			//temp_dr_info = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe );
			node->dr_info.start_addr = temp_dr_info->start_addr;
			node->dr_info.valid_offset = temp_dr_info->valid_offset;
			node->dr_info.dr_offset_start = DR_SIZE;
			node->dr_info.dr_fd = temp_dr_info->dr_fd;
			node->dr_info.dr_serialno = temp_dr_info->dr_serialno;
			node->dr_info.dr_offset_end = temp_dr_info->valid_offset;
			DEBUG_FILE("%s: DR found in global pool. Got from global pool. FD = %d\n",
				   __func__, temp_dr_info->dr_fd);
		} else {
			DEBUG_FILE("%s: Global queue empty\n", __func__);
			memset((void *)&node->dr_info, 0, sizeof(struct free_dr_pool));
		}
	}

	__atomic_fetch_sub(&num_drs_left, 1, __ATOMIC_SEQ_CST);

	callBgCleaningThread(is_overwrite);
}

void nvp_free_dr_mmaps()
{
	unsigned long addr;
	unsigned long offset_in_page = 0;
	struct free_dr_pool *temp_free_pool_of_dr_mmaps;
	int i = 0;
	ssize_t file_name_size = 0;

	while ((temp_free_pool_of_dr_mmaps = lfq_dequeue(&staging_mmap_queue_ctx)) != 0) {
		//while( lfds711_queue_umm_dequeue(&qs, &qe) ) {
		//temp_free_pool_of_dr_mmaps = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe );
		addr = temp_free_pool_of_dr_mmaps->start_addr;
		munmap((void *)addr, DR_SIZE);

		// Fetch the name of the file before closing it.
		char fd_str[256];
		char new_path[256];
		sprintf(fd_str, "/proc/self/fd/%d", temp_free_pool_of_dr_mmaps->dr_fd);
		file_name_size = readlink(fd_str, new_path, sizeof(new_path));
		if (file_name_size == -1)
			assert(0);
		new_path[file_name_size] = '\0';

		close(temp_free_pool_of_dr_mmaps->dr_fd);

		// Remove the file.
		syscall_no_intercept(SYS_unlink, new_path);
		__atomic_fetch_sub(&num_drs_left, 1, __ATOMIC_SEQ_CST);
	}
	//lfds711_queue_umm_cleanup( &qs, NULL );

#if DATA_JOURNALING_ENABLED

	while ((temp_free_pool_of_dr_mmaps = lfq_dequeue(&staging_over_mmap_queue_ctx)) != 0) {
		//while( lfds711_queue_umm_dequeue(&qs_over, &qe_over) ) {
		//temp_free_pool_of_dr_mmaps = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe_over );
		addr = temp_free_pool_of_dr_mmaps->start_addr;
		munmap((void *)addr, DR_OVER_SIZE);

		// Fetch the name of the file before closing it.
		char fd_str[256];
		char new_path[256];
		sprintf(fd_str, "/proc/self/fd/%d", temp_free_pool_of_dr_mmaps->dr_fd);
		file_name_size = readlink(fd_str, new_path, sizeof(new_path));
		if (file_name_size == -1)
			assert(0);
		new_path[file_name_size] = '\0';

		syscall_no_intercept(SYS_close, temp_free_pool_of_dr_mmaps->dr_fd);

		// Remove the file.
		syscall_no_intercept(SYS_unlink, new_path);
		__atomic_fetch_sub(&num_drs_left, 1, __ATOMIC_SEQ_CST);
	}
	// lfds711_queue_umm_cleanup( &qs_over, NULL );

	for (i = 0; i < full_dr_idx; i++) {
		addr = _nvp_full_drs[i].start_addr;
		munmap((void *)addr, _nvp_full_drs[i].size);
		syscall_no_intercept(SYS_close, _nvp_full_drs[i].dr_fd);
	}

#endif // DATA_JOURNALING_ENABLED

}

void nvp_reset_mappings(struct NVNode *node)
{
	int i, dirty_index;
	
	DEBUG("Cleanup: root 0x%x, height %u\n", root, height);

	if(node->root_dirty_num) {		
		// Check if many mmap()s need to be memset. If total_dirty_mmaps is set, that means all the mmap()s need to be copied 
		if(node->total_dirty_mmaps) {
			memset((void *)node->root, 0, 1024 * sizeof(unsigned long));		
			memset((void *)node->merkle_root, 0, 1024 * sizeof(unsigned long));	
		} else {
			// Only copy the dirty mmaps. The indexes can be found in the root_dirty_cache. 
			for(i = 0; i < node->root_dirty_num; i++) {
				dirty_index = node->root_dirty_cache[i];
				if(node->root && node->root[dirty_index]) {
					node->root[dirty_index] = 0;
					node->merkle_root[dirty_index] = 0;
				}
			}
		}
		if(node->root_dirty_num)
			memset((void *)node->root_dirty_cache, 0, 20 * sizeof(unsigned long));	
	}
	node->isRootSet = 0;
	node->height = 0;
	node->total_dirty_mmaps = 0;
	node->root_dirty_num = 0;
}

static unsigned int calculate_new_height(off_t offset)
{
	unsigned int height = 0;
	off_t temp_offset = offset / ((unsigned long)1024 * MAX_MMAP_SIZE);

	while (temp_offset) {
		temp_offset /= 1024;
		height++;
	}

	return height;
}

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
						struct NVTable_maps *tbl_over)
{
	int i;
	int index;
	unsigned int height = nvf->node->height;
	unsigned int new_height;
	unsigned long capacity = MAX_MMAP_SIZE;
	unsigned long *root = nvf->node->root;

#if !NON_TEMPORAL_WRITES	
	unsigned long *merkle_root = nvf->node->merkle_root;
	unsigned long merkle_start_addr;
#endif

	unsigned long start_addr;
	off_t start_offset = offset;
	instrumentation_type nvnode_lock_time, file_mmap_time;
	
	DEBUG("Get mmap address: offset 0x%lx, height %u\n", offset, height);
	DEBUG("root @ %p\n", root);

	do {
		capacity = calculate_capacity(height);
		index = start_offset / capacity;

		DEBUG("index %d\n", index);
#if !NON_TEMPORAL_WRITES	
		if (index >= 1024 || root[index] == 0 || merkle_root[index] == 0) {
#else
		if (index >= 1024 || root[index] == 0) {
#endif
			goto not_found;
		}
		if (height) {
			root = (unsigned long *)root[index];

#if !NON_TEMPORAL_WRITES	
			merkle_root = (unsigned long *)merkle_root[index];
#endif

			DEBUG("%p\n", root);
		} else {
			start_addr = root[index];

#if !NON_TEMPORAL_WRITES
			merkle_start_addr = merkle_root[index];
#endif
			DEBUG("addr 0x%lx\n", start_addr);
		}
		start_offset = start_offset % capacity;
	} while(height--);
	//NVP_END_TIMING(lookup_t, lookup_time);

#if !NON_TEMPORAL_WRITES	
	if (IS_ERR(start_addr) || start_addr == 0 || merkle_start_addr == 0) {
#else
	if (IS_ERR(start_addr) || start_addr == 0) {
#endif
		MSG("ERROR!\n");
		fflush(NULL);
		assert(0);
	}

        (*mmap_addr) = (start_addr + (offset % MAX_MMAP_SIZE));
	*offset_within_mmap = offset % MAX_MMAP_SIZE;

#if !NON_TEMPORAL_WRITES	
	*bitmap_root = merkle_start_addr;
#endif
	(*extent_length) = (MAX_MMAP_SIZE - (offset % MAX_MMAP_SIZE));

	DEBUG("Found: mmap addr 0x%lx, extent length %lu\n",
			*mmap_addr, *extent_length);
	return 0;

not_found:
	DEBUG("Not found, perform mmap\n");

	if (offset >= ALIGN_MMAP_DOWN(nvf->node->true_length)) {
		DEBUG("File length smaller than offset: "
			"length 0x%lx, offset 0x%lx\n",
			nvf->node->length, offset);
		return 1;
	}

	if (!wr_lock) {
		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);

		NVP_UNLOCK_NODE_RD(nvf, cpuid);
		START_TIMING(nvnode_lock_t, nvnode_lock_time);
		NVP_LOCK_NODE_WR(nvf);

		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);

	        END_TIMING(nvnode_lock_t, nvnode_lock_time);
	}

	start_offset = ALIGN_MMAP_DOWN(offset);	
		
	if (start_offset + MAX_MMAP_SIZE > nvf->node->true_length) {
		ERROR("File length smaller than offset: "
			"length 0x%lx, offset 0x%lx\n",
			nvf->node->length, offset);
		MSG("%s: file length smaller than offset\n", __func__);
		return 1;
	}

	START_TIMING(file_mmap_t, file_mmap_time);	
	int max_perms = ((nvf->canRead) ? PROT_READ : 0) | 
			((nvf->canWrite) ? PROT_WRITE : 0);

	start_addr = (unsigned long) FSYNC_MMAP
	(
		NULL,
		MAX_MMAP_SIZE,
		max_perms, //max_perms,
		MAP_SHARED | MAP_POPULATE,
//		MAP_SHARED,
		nvf->fd, //fd_with_max_perms,
		start_offset
		//0
	);

	END_TIMING(file_mmap_t, file_mmap_time);

	DEBUG("%s: created mapping of address = %lu, inode = %lu, thread id = %lu\n", __func__, start_addr, nvf->node->serialno, pthread_self());
	
	/* Bitmap Tree creation */
#if !NON_TEMPORAL_WRITES	
	createTree((struct merkleBtreeNode **)&merkle_start_addr);
	if (IS_ERR(start_addr) || start_addr == 0 || merkle_start_addr == 0) {
#else
	if (IS_ERR(start_addr) || start_addr == 0) {		
#endif       
		MSG("mmap failed for fd %i: %s, mmap count %d, addr %lu, errno is %lu\n",
		    nvf->fd, strerror(errno), num_mmap, start_addr, errno);
		MSG("Open count %d, close count %d\n", num_open, num_close);
		MSG("Use posix operations for fd %i instead.\n", nvf->fd);
		nvf->posix = 1;
		fflush(NULL);
		assert(0);
	}

	DEBUG_FILE("%s: Performed mmap. Start_addr = %p, inode no = %lu\n", __func__, (void *) start_addr, nvf->node->serialno);

	num_mmap++;

	DEBUG("mmap offset 0x%lx, start_offset 0x%lx\n", offset, start_offset);

	height = nvf->node->height;
	new_height = calculate_new_height(offset);

	if (height < new_height) {
		MSG("Increase height from %u to %u\n", height, new_height);

		while (height < new_height) {
			unsigned long old_root = (unsigned long)nvf->node->root;
			nvf->node->root = malloc(1024 * sizeof(unsigned long));

#if !NON_TEMPORAL_WRITES	
			unsigned long old_merkle_root = (unsigned long)nvf->node->merkle_root;
			nvf->node->merkle_root = malloc(1024 * sizeof(unsigned long));
			for (i = 0; i < 1024; i++) {
				nvf->node->root[i] = 0;
				nvf->node->merkle_root[i] = 0;
			}
			nvf->node->merkle_root[0] = (unsigned long)old_merkle_root;
#else
			for (i = 0; i < 1024; i++) {
				nvf->node->root[i] = 0;
			}
#endif
			DEBUG("Malloc new root @ %p\n", nvf->node->root);
			nvf->node->root[0] = (unsigned long)old_root;
			DEBUG("Old root 0x%lx\n", nvf->node->root[0]);
			height++;
		}

		nvf->node->height = new_height;
		height = new_height;
	}

	root = nvf->node->root;
#if !NON_TEMPORAL_WRITES	
	merkle_root = nvf->node->merkle_root;
#endif
	do {
		capacity = calculate_capacity(height);
		index = start_offset / capacity;
		DEBUG("index %d\n", index);
		if (height) {
			if (root[index] == 0) {
				root[index] = (unsigned long)malloc(1024 *
						sizeof(unsigned long));

#if !NON_TEMPORAL_WRITES	
				merkle_root[index] = (unsigned long)malloc(1024 * sizeof(unsigned long));
				root = (unsigned long *)root[index];
				merkle_root = (unsigned long *)merkle_root[index];
				for (i = 0; i < 1024; i++) {
					root[i] = 0;
					merkle_root[i] = 0;
				}
#else
				root = (unsigned long *)root[index];
				for (i = 0; i < 1024; i++) {
					root[i] = 0;
				}
#endif				
			} else {
				root = (unsigned long *)root[index];
#if !NON_TEMPORAL_WRITES	
				merkle_root = (unsigned long *)merkle_root[index];
#endif
			}
		} else {
			root[index] = start_addr;
			nvf->node->root_dirty_cache[nvf->node->root_dirty_num] = index;
			if(!nvf->node->total_dirty_mmaps) {
				nvf->node->root_dirty_num++;
				if(nvf->node->root_dirty_num == 20)
					nvf->node->total_dirty_mmaps = 1;
			}
#if !NON_TEMPORAL_WRITES	
			merkle_root[index] = merkle_start_addr;
#endif
		}
		start_offset = start_offset % capacity;
	} while(height--);

	nvf->node->isRootSet = 1;
	(*mmap_addr) = (start_addr + (offset % MAX_MMAP_SIZE));
	*offset_within_mmap = offset % MAX_MMAP_SIZE;

#if !NON_TEMPORAL_WRITES
	*bitmap_root = merkle_start_addr;
#endif
	(*extent_length) = (MAX_MMAP_SIZE - (offset % MAX_MMAP_SIZE));

	if (!wr_lock) {

		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);

		NVP_UNLOCK_NODE_WR(nvf);
		NVP_LOCK_NODE_RD(nvf, cpuid);

		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
	}

	DEBUG("mmap addr 0x%lx, extent length %lu\n",
			*mmap_addr, *extent_length);

	return 0;
}

#if DATA_JOURNALING_ENABLED

static void nvp_manage_over_dr_memory(struct NVFile *nvf, uint64_t *extent_length,
				      uint64_t len_to_write, off_t start_offset,
				      int index)
{
	int i;
	/* 
	 * Check if the reads are being served from DR. If yes, then all the future reads should
	 * be performed through the file backed memory, for the appended and fsync()ed region. 
	 */

	DEBUG_FILE("%s START: dr_offset_start = %lu, dr_offset_end = %lu\n",
		   __func__, nvf->node->dr_over_info.dr_offset_start, nvf->node->dr_over_info.dr_offset_end);
	if(*extent_length >= len_to_write)
		nvf->node->dr_over_info.dr_offset_start = start_offset + len_to_write;
}

#endif // DATA_JOURNALING_ENABLED

static void nvp_manage_dr_memory(struct NVFile *nvf, uint64_t *extent_length,
				 uint64_t len_to_write, off_t start_offset,
				 int index)
{
	int i;
	unsigned long offset_within_mmap = 0;
	/* 
	 * Check if the reads are being served from DR. If yes, then all the future reads should
	 * be performed through the file backed memory, for the appended and fsync()ed region. 
	 */

	offset_within_mmap = start_offset;

	DEBUG_FILE("%s START: dr_offset_start = %lu, dr_offset_end = %lu, offset_within_mmap = %lu\n",
		   __func__, nvf->node->dr_info.dr_offset_start, nvf->node->dr_info.dr_offset_end,
		   offset_within_mmap);

	if(nvf->node->dr_info.dr_offset_start > offset_within_mmap)
		// Update the portion from which the dirty DR region starts. 
		nvf->node->dr_info.dr_offset_start = offset_within_mmap;
	if(*extent_length > len_to_write) {
		if(nvf->node->dr_info.dr_offset_end < (offset_within_mmap + len_to_write))
			// Update the portion till which the dirty DR region exists
			nvf->node->dr_info.dr_offset_end = offset_within_mmap + len_to_write;
	} else {
		// It is a large write. So finish writing to this mmap. 
		if(nvf->node->dr_info.dr_offset_end < (offset_within_mmap + *extent_length))
			nvf->node->dr_info.dr_offset_end = DR_SIZE;
	}

	DEBUG_FILE("%s END: dr_offset_start = %lu, dr_offset_end = %lu, offset_within_mmap = %lu\n",
		   __func__, nvf->node->dr_info.dr_offset_start, nvf->node->dr_info.dr_offset_end,
		   offset_within_mmap);

	if (nvf->node->dr_info.dr_offset_start < nvf->node->dr_info.valid_offset)
		assert(0);
	if (nvf->node->dr_info.valid_offset > DR_SIZE)
		assert(0);
	if (nvf->node->dr_info.dr_offset_start > DR_SIZE)
		assert(0);
	if (nvf->node->dr_info.dr_offset_end > DR_SIZE)
		assert(0);
	if (nvf->node->dr_info.dr_offset_end < nvf->node->dr_info.dr_offset_start)
		assert(0);
}

#if DATA_JOURNALING_ENABLED

int nvp_get_over_dr_address(struct NVFile *nvf,
				   off_t offset,
				   size_t len_to_write, 
				   unsigned long *mmap_addr,
				   off_t *offset_within_mmap,
				   size_t *extent_length,
				   int wr_lock,
				   int cpuid,
				   struct NVTable_maps *tbl_app,
				   struct NVTable_maps *tbl_over)
{
	int index;
	unsigned long capacity = DR_OVER_SIZE;
	unsigned long start_addr, unaligned_file_end;
	off_t file_offset = offset, offset_within_page = 0;
	off_t start_offset = 0;
	struct stat stat_buf;
	instrumentation_type nvnode_lock_time, dr_mem_queue_time;
	
	DEBUG("Get mmap address: offset 0x%lx, height %u\n",
	      offset, height);
	/* The index of the mmap in the global DR pool.
	 * Max number of entries = 1024. 
	 */
	if (nvf->node->dr_over_info.start_addr == 0) 
		goto not_found;

	/* Anonymous mmap at that index is present for the file.
	 * So get the start address. 
	 */
	start_addr = nvf->node->dr_over_info.start_addr;
	DEBUG("addr 0x%lx\n", start_addr);
	// Get the offset in the mmap to which the memcpy must be performed. 
	if (IS_ERR(start_addr) || start_addr == 0) {
		MSG("%s: ERROR!\n", __func__);
		assert(0);
	}
	/* address we want to perform memcpy(). The start_offset
	 * is the offset with relation to node->true_length. 
	 */
	start_offset = nvf->node->dr_over_info.dr_offset_start;

	DEBUG_FILE("%s: DR valid_offset = %lu. Start offset = %lu, true length = %lu\n",
		   __func__, nvf->node->dr_over_info.valid_offset,
		   start_offset, nvf->node->true_length);

	if ((start_offset % MMAP_PAGE_SIZE) != (file_offset % MMAP_PAGE_SIZE)) {
		offset_within_page = start_offset % MMAP_PAGE_SIZE;
		if (offset_within_page != 0) {
			start_offset += MMAP_PAGE_SIZE - offset_within_page;
		}
		offset_within_page = file_offset % MMAP_PAGE_SIZE;
		if (offset_within_page != 0) {
			start_offset += offset_within_page;
		}
	}

	if (start_offset >= DR_OVER_SIZE) {		
		DEBUG_FILE("%s: start_offset = %lld, DR_OVER_SIZE = %lu, dr_offset_start = %lld\n",
			   __func__, start_offset, DR_OVER_SIZE, nvf->node->dr_over_info.dr_offset_start);     	
	}

	if (nvf->node->dr_over_info.valid_offset > start_offset)
		assert(0);
	
	*mmap_addr = start_addr + start_offset;
	*offset_within_mmap = start_offset;
	/* This gives how much free space is remaining in the
	 * current anonymous mmap. 
	 */
	if (start_offset < DR_OVER_SIZE)
		*extent_length = DR_OVER_SIZE - start_offset;
	else
		*extent_length = 0;
	/* The mmap for that index was not found. Performing mmap 
	 * in this section. 	
	 */
	if (!wr_lock) {
		TBL_ENTRY_UNLOCK_WR(tbl_over);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		NVP_UNLOCK_NODE_RD(nvf, cpuid);

		START_TIMING(nvnode_lock_t, nvnode_lock_time);

		NVP_LOCK_NODE_WR(nvf);
		TBL_ENTRY_LOCK_WR(tbl_app);
		TBL_ENTRY_LOCK_WR(tbl_over);

		END_TIMING(nvnode_lock_t, nvnode_lock_time);
	}

	nvp_manage_over_dr_memory(nvf, extent_length, len_to_write,
				  start_offset, index);

	if (nvf->node->dr_over_info.dr_offset_end != DR_OVER_SIZE)
		assert(0);
	
	return 0;

not_found:	
	/* The mmap for that index was not found. Performing mmap
	 * in this section. 	
	 */
	if (!wr_lock) {

		TBL_ENTRY_UNLOCK_WR(tbl_over);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		NVP_UNLOCK_NODE_RD(nvf, cpuid);

		START_TIMING(nvnode_lock_t, nvnode_lock_time);

		NVP_LOCK_NODE_WR(nvf);
		TBL_ENTRY_LOCK_WR(tbl_app);
		TBL_ENTRY_LOCK_WR(tbl_over);

		END_TIMING(nvnode_lock_t, nvnode_lock_time);
	}

	START_TIMING(dr_mem_queue_t, dr_mem_queue_time);

	struct free_dr_pool *temp_dr_info = lfq_dequeue(&staging_over_mmap_queue_ctx);
	if (temp_dr_info != 0) {
		//if( lfds711_queue_umm_dequeue(&qs_over, &qe_over) ) {
		// Found addr in global pool
		//struct free_dr_pool *temp_dr_info = NULL;
		unsigned long offset_in_page = 0;
		//temp_dr_info = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe_over );
		nvf->node->dr_over_info.start_addr = temp_dr_info->start_addr;
		nvf->node->dr_over_info.valid_offset = temp_dr_info->valid_offset;
		nvf->node->dr_over_info.dr_fd = temp_dr_info->dr_fd;
		nvf->node->dr_over_info.dr_serialno = temp_dr_info->dr_serialno;
		nvf->node->dr_over_info.dr_offset_start = temp_dr_info->dr_offset_start;
		nvf->node->dr_over_info.dr_offset_end = DR_OVER_SIZE;
		__atomic_fetch_sub(&num_drs_left, 1, __ATOMIC_SEQ_CST);
	} else {
		DEBUG_FILE("%s: Allocating new DR\n", __func__);
		// Nothing in global pool
		int dr_fd = 0;
		int i = 0;
		char dr_fname[256];
		unsigned long offset_in_page = 0;
		int num_blocks = DR_OVER_SIZE / MMAP_PAGE_SIZE;
		int max_perms = ((nvf->canRead) ? PROT_READ : 0) | 
			((nvf->canWrite) ? PROT_WRITE : 0);
		DEBUG_FILE("%s: DR not found in global pool. Allocated dr_file variable\n", __func__);

		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-OVER-XXXXXX");
		dr_fd = syscall_no_intercept(SYS_open, mktemp(dr_fname), O_RDWR | O_CREAT, 0666);
		if (dr_fd < 0) {
			MSG("%s: mkstemp of DR file failed. Err = %s\n",
			    __func__, strerror(-dr_fd));
			assert(0);
		}
		posix_fallocate(dr_fd, 0, DR_SIZE);
		num_mmap++;
		num_drs++;
		num_drs_critical_path++;
		nvf->node->dr_over_info.start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_OVER_SIZE,
			 max_perms, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 dr_fd, //fd_with_max_perms,
			 0
			 );

		DEBUG_FILE("%s: Setting offset_start to DR_SIZE. FD = %d\n",
			   __func__, nvf->fd);
		syscall_no_intercept(SYS_fstat, dr_fd, &stat_buf);
		nvf->node->dr_over_info.dr_serialno = stat_buf.st_ino;
		nvf->node->dr_over_info.dr_fd = dr_fd;
		nvf->node->dr_over_info.valid_offset = 0;
		nvf->node->dr_over_info.dr_offset_start = 0;
		nvf->node->dr_over_info.dr_offset_end = DR_OVER_SIZE;
		dr_fname[0] = '\0';
		DEBUG_FILE("%s: DR not found in global pool. Initialized DR_INFO. FD = %d\n", __func__, dr_fd);
	}
	start_addr = nvf->node->dr_over_info.start_addr;
	__atomic_fetch_add(&dr_mem_allocated, DR_OVER_SIZE,
			   __ATOMIC_SEQ_CST);
	nvf->node->dr_mem_used += DR_OVER_SIZE;

	END_TIMING(dr_mem_queue_t, dr_mem_queue_time);
	if (IS_ERR(start_addr) || start_addr == 0)
	{
		MSG("mmap failed for  %s, mmap count %d, addr %lu, errno is %lu\n",
		    strerror(errno), num_mmap, start_addr, errno);
		MSG("Open count %d, close count %d\n",
		    num_open, num_close);
		nvf->posix = 1;
		assert(0);
	}
	/* Get the index of the mmap from the size of mmap and 
	 * from the offset.
	 */ 
	DEBUG_FILE("%s: offset requested = %lu\n", __func__, offset);
	start_offset = nvf->node->dr_over_info.dr_offset_start;
	offset_within_page = start_offset % MMAP_PAGE_SIZE;
	if (offset_within_page != 0) {
		start_offset += MMAP_PAGE_SIZE - offset_within_page;
	}
	offset_within_page = file_offset % MMAP_PAGE_SIZE;
	if (offset_within_page != 0) {
		start_offset += offset_within_page;
	}

	if ((start_offset % MMAP_PAGE_SIZE) != (file_offset % MMAP_PAGE_SIZE))
		assert(0);

	if (start_offset >= DR_OVER_SIZE) {
		DEBUG_FILE("%s: start_offset = %lld, DR_OVER_SIZE = %lu, dr_offset_start = %lld\n",
			   __func__, start_offset, DR_OVER_SIZE, nvf->node->dr_over_info.dr_offset_start);
	}

	if (nvf->node->dr_over_info.valid_offset > start_offset)
		assert(0);

	*mmap_addr = start_addr + start_offset;
	*offset_within_mmap = start_offset;

	if (start_offset < DR_OVER_SIZE)
		*extent_length = DR_OVER_SIZE - start_offset;
	else
		*extent_length = 0;

	DEBUG_FILE("%s: Will do manage DR memory if it is a write\n",
		   __func__);

	nvp_manage_over_dr_memory(nvf, extent_length,
				  len_to_write, start_offset, index);

	if (nvf->node->dr_over_info.dr_offset_end != DR_OVER_SIZE)
		assert(0);

	return 0;
}

#endif // DATA_JOURNALING_ENABLED

int nvp_get_dr_mmap_address(struct NVFile *nvf, off_t offset,
				   size_t len_to_write, size_t count,
				   unsigned long *mmap_addr,
				   off_t *offset_within_mmap,
				   size_t *extent_length, int wr_lock,
				   int cpuid, int iswrite,
				   struct NVTable_maps *tbl_app,
				   struct NVTable_maps *tbl_over)
{
	int index;
	unsigned long capacity = DR_SIZE;
	unsigned long start_addr, unaligned_file_end;
	off_t start_offset = offset;
	struct stat stat_buf;
	instrumentation_type nvnode_lock_time, dr_mem_queue_time;

	DEBUG("Get mmap address: offset 0x%lx, height %u\n",
	      offset, height);
	/* The index of the mmap in the global DR pool.
	 * Max number of entries = 1024. 
	 */
	if (nvf->node->dr_info.start_addr == 0) {
		if(iswrite)
			/* Have to get the mmap from the 
			 * global anonymous pool. 
			 */
			goto not_found;
		else {
			/* If it is a read, then the anonymous mmap 
			 * must be found. Otherwise something is wrong. 
			 */
			ERROR("dr mmap not found\n");
		        MSG("%s: dr mmap not found\n", __func__);
			assert(0);
		}
	}
	/* Anonymous mmap at that index is present for the file.
	 * So get the start address. 
	 */
	start_addr = nvf->node->dr_info.start_addr;
	DEBUG("addr 0x%lx\n", start_addr);
	// Get the offset in the mmap to which the memcpy must be performed. 
	if (IS_ERR(start_addr) || start_addr == 0) {
		MSG("%s: ERROR!\n", __func__);
		assert(0);
	}
	/* address we want to perform memcpy(). The start_offset
	 * is the offset with relation to node->true_length. 
	 */
	DEBUG_FILE("%s: DR valid_offset = %lu. Start offset = %lu, true length = %lu\n",
		   __func__, nvf->node->dr_info.valid_offset,
		   start_offset, nvf->node->true_length);
	start_offset = (start_offset +
			nvf->node->dr_info.valid_offset);
	*mmap_addr = start_addr + start_offset;
	*offset_within_mmap = start_offset;
	/* This gives how much free space is remaining in the
	 * current anonymous mmap. 
	 */
	*extent_length = DR_SIZE - start_offset;
	/* The mmap for that index was not found. Performing mmap 
	 * in this section.
	 */

	if (!wr_lock) {
		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		NVP_UNLOCK_NODE_RD(nvf, cpuid);

		START_TIMING(nvnode_lock_t, nvnode_lock_time);
		NVP_LOCK_NODE_WR(nvf);
		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		END_TIMING(nvnode_lock_t, nvnode_lock_time);
	}
	if(iswrite) {
		nvp_manage_dr_memory(nvf, extent_length, len_to_write,
				     start_offset, index);
	}

	if (!wr_lock && !iswrite) {
		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		NVP_UNLOCK_NODE_WR(nvf);

		NVP_LOCK_NODE_RD(nvf, cpuid);
		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
	}

	return 0;

not_found:
	/* The mmap for that index was not found. Performing mmap
	 * in this section.
	 */
	if (!wr_lock) {
		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		NVP_UNLOCK_NODE_RD(nvf, cpuid);

		START_TIMING(nvnode_lock_t, nvnode_lock_time);
		NVP_LOCK_NODE_WR(nvf);
		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		END_TIMING(nvnode_lock_t, nvnode_lock_time);
	}

	START_TIMING(dr_mem_queue_t, dr_mem_queue_time);

	struct free_dr_pool *temp_dr_info = lfq_dequeue(&staging_mmap_queue_ctx);
	if (temp_dr_info != 0) {
		//if( lfds711_queue_umm_dequeue(&qs, &qe) ) {
		// Found addr in global pool
		//struct free_dr_pool *temp_dr_info = NULL;
		unsigned long offset_in_page = 0;
		//temp_dr_info = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe );
		nvf->node->dr_info.start_addr = temp_dr_info->start_addr;
		nvf->node->dr_info.valid_offset = temp_dr_info->valid_offset;
		nvf->node->dr_info.dr_offset_start = DR_SIZE;
		nvf->node->dr_info.dr_fd = temp_dr_info->dr_fd;
		nvf->node->dr_info.dr_serialno = temp_dr_info->dr_serialno;

		if (nvf->node->dr_info.valid_offset < DR_SIZE) {
			offset_in_page = nvf->node->true_length % MMAP_PAGE_SIZE;
			nvf->node->dr_info.valid_offset += offset_in_page;
		}

		nvf->node->dr_info.dr_offset_end = nvf->node->dr_info.valid_offset;
		__atomic_fetch_sub(&num_drs_left, 1, __ATOMIC_SEQ_CST);

		DEBUG_FILE("%s: staging inode = %lu. Got from global pool with valid offset = %lld\n",
			   __func__, nvf->node->dr_info.dr_serialno, nvf->node->dr_info.valid_offset);

	} else {
		DEBUG_FILE("%s: Allocating new DR\n", __func__);
		// Nothing in global pool
		int dr_fd = 0;
		int i = 0;
		char dr_fname[256];
		unsigned long offset_in_page = 0;
		int num_blocks = DR_SIZE / MMAP_PAGE_SIZE;
		int max_perms = ((nvf->canRead) ? PROT_READ : 0) | 
			((nvf->canWrite) ? PROT_WRITE : 0);
		DEBUG_FILE("%s: DR not found in global pool. Allocated dr_file variable\n", __func__);

		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-XXXXXX");
		dr_fd = syscall_no_intercept(SYS_open, mktemp(dr_fname), O_RDWR | O_CREAT, 0666);
		if (dr_fd < 0) {
			MSG("%s: mkstemp of DR file failed. Err = %s\n",
			    __func__, strerror(-dr_fd));
			assert(0);
		}
		posix_fallocate(dr_fd, 0, DR_SIZE);
		num_mmap++;
		num_drs++;
		num_drs_critical_path++;
		nvf->node->dr_info.start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_SIZE,
			 max_perms, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 dr_fd, //fd_with_max_perms,
			 0
			 );

		DEBUG_FILE("%s: Setting offset_start to DR_SIZE. FD = %d\n",
			   __func__, nvf->fd);
		fstat(dr_fd, &stat_buf);
		nvf->node->dr_info.dr_serialno = stat_buf.st_ino;
		nvf->node->dr_info.dr_fd = dr_fd;
		nvf->node->dr_info.valid_offset = 0;
		nvf->node->dr_info.dr_offset_start = DR_SIZE;
		offset_in_page = nvf->node->true_length % MMAP_PAGE_SIZE;
		if (offset_in_page != 0)
			nvf->node->dr_info.valid_offset += offset_in_page;
		nvf->node->dr_info.dr_offset_end = nvf->node->dr_info.valid_offset;
		dr_fname[0] = '\0';
		DEBUG_FILE("%s: DR not found in global pool. Initialized DR_INFO. FD = %d\n", __func__, dr_fd);
	}
	start_addr = nvf->node->dr_info.start_addr;
	__atomic_fetch_add(&dr_mem_allocated, DR_SIZE,
			   __ATOMIC_SEQ_CST);
	nvf->node->dr_mem_used += DR_SIZE;

	END_TIMING(dr_mem_queue_t, dr_mem_queue_time);
	if (IS_ERR(start_addr) || start_addr == 0)
	{
		MSG("mmap failed for  %s, mmap count %d, addr %lu, errno is %lu\n",
		    strerror(errno), num_mmap, start_addr, errno);
		MSG("Open count %d, close count %d\n",
		    num_open, num_close);
		nvf->posix = 1;
		assert(0);
	}
	/* Get the index of the mmap from the size of mmap and 
	 * from the offset.
	 */ 
	DEBUG_FILE("%s: offset requested = %lu\n", __func__, offset);
	start_offset = (start_offset +
			nvf->node->dr_info.valid_offset);
	*mmap_addr = start_addr + start_offset;
	*offset_within_mmap = start_offset;
	*extent_length = DR_SIZE - start_offset;

	DEBUG_FILE("%s: Will do manage DR memory if it is a write\n",
		   __func__);
	if(iswrite) 
		nvp_manage_dr_memory(nvf, extent_length,
				     len_to_write, start_offset, index);

	if (!wr_lock && !iswrite) {
		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		NVP_UNLOCK_NODE_WR(nvf);

		NVP_LOCK_NODE_RD(nvf, cpuid);
		TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
	}

	return 0;
}
