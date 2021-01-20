// a module which repalces the standart POSIX functions with memory mapped equivalents
#include <libsyscall_intercept_hook_point.h>

#include "file.h"
// #include "perfcount.h"
#include "timers.h"
#include "bg_clear_mmap.h"
#include "stack.h"
#include "add_delay.h"
#include "log.h"
#include "tbl_mmaps.h"
#include "nvp_lock.h"
#include "lru_cache.h"
#include "thread_handle.h"

#include "mmap_cache.h"
#include "handle_mmaps.h"
#include "fsync.h"
#include "stack.h"
#include "fsync.h"

struct NVNode * nvp_allocate_node(int list_idx)
{
	struct NVNode *node = NULL;
	int idx_in_list = -1;
	int i, candidate = -1;

	idx_in_list = pop_from_stack(1, 0, list_idx);	
	if(idx_in_list != -1) {
		node = &_nvp_node_lookup[list_idx][idx_in_list];
		node->index_in_free_list = idx_in_list;
		return node;
	}	
	/*
	 * Get the first unused NVNode from the global array of 1024 NVNodes. 
	 * If the node is not unusued but the reference number is
	 * 0, meaning that there is no thread that has this file open, 
	 * it can be used for holding info of the new file
	 */	
	for (i = 0; i < 1024; i++) {
		if (_nvp_node_lookup[list_idx][i].serialno == 0) {
			DEBUG("Allocate unused node %d\n", i);
			_nvp_free_node_list[list_idx][i].free_bit = 0;
			node = &_nvp_node_lookup[list_idx][i];
			node->index_in_free_list = i;
			_nvp_free_node_list_head[list_idx] = _nvp_free_node_list[list_idx][node->index_in_free_list].next_free_idx;
			break;
		}
		if (candidate == -1 && _nvp_node_lookup[list_idx][i].reference == 0)
			candidate = i;
	}
	if (node) {
		return node;
	}
	if (candidate != -1) {
		node = &_nvp_node_lookup[list_idx][candidate];
		DEBUG("Allocate unreferenced node %d\n", candidate);
		node->index_in_free_list = candidate;		
		_nvp_free_node_list[list_idx][candidate].free_bit = 0;
		_nvp_free_node_list_head[list_idx] = _nvp_free_node_list[list_idx][candidate].next_free_idx;
		return node;
	}
	return NULL;	
}

static struct NVNode * nvp_get_node(const char *path, struct stat *file_st, int result)
{
	int i, index, ret;
	struct NVNode *node = NULL;
	int node_list_idx = pthread_self() % NUM_NODE_LISTS;
	instrumentation_type node_lookup_lock_time, nvnode_lock_time;
	
	pthread_spin_lock(&node_lookup_lock[node_list_idx]);
	/* 
	 *  Checking to see if the file is already open by another thread. In this case, the same NVNode can be used by this thread            
	 *  too. But it will have its own separate NVFile, since the fd is unique per thread 
	 */
	index = file_st->st_ino % 1024;
	if (_nvp_ino_lookup[index]) {
		i = _nvp_ino_lookup[index];
		if ( _nvp_fd_lookup[i].node &&
		     _nvp_fd_lookup[i].node->serialno == file_st->st_ino) {
			DEBUG("File %s is (or was) already open in fd %i "
			      "(this fd hasn't been __open'ed yet)! "
			      "Sharing nodes.\n", path, i);
			
			node = _nvp_fd_lookup[i].node;
			SANITYCHECK(node != NULL);			
			NVP_LOCK_WR(node->lock);
			node->reference++;			
			NVP_LOCK_UNLOCK_WR(node->lock);
			
			pthread_spin_unlock(&node_lookup_lock[node_list_idx]);
			goto out;
		}
	}
	/*
	 * This is the first time the file is getting opened. 
	 * The first unused NVNode is assigned here to hold info of the file.  
	 */
	if(node == NULL) {
		DEBUG("File %s is not already open. "
		      "Allocating new NVNode.\n", path);
		node = nvp_allocate_node(node_list_idx);
		NVP_LOCK_WR(node->lock);
		node->serialno = file_st->st_ino;
		node->reference++;
		NVP_LOCK_UNLOCK_WR(node->lock);
		if(UNLIKELY(!node)) {
			MSG("%s: Node is null\n", __func__);
			assert(0);
		}
	}
	index = file_st->st_ino % 1024;
	if (_nvp_ino_lookup[index] == 0)
		_nvp_ino_lookup[index] = result;
	
	node->free_list_idx = node_list_idx;
	
	pthread_spin_unlock(&node_lookup_lock[node_list_idx]);

	NVP_LOCK_WR(node->lock);

	/* 
	 * Checking if the mapping exists in the global mmap() cache for this inode number. 
	 * If it does, copy all the mapping
	 * from the global mmap() cache on to the NVNode mmap()
         */  
	nvp_add_to_inode_mapping(node, node->backup_serialno);
	nvp_reset_mappings(node);	
	ret = nvp_retrieve_inode_mapping(node);	
	if(ret != 0) {
		/* 
		 * If the height is not 0, that means that there exist levels 
		 * in the file backed mmap() tree. So need to free
		 * the file backed mmap() tree completely. 
		 */
		if(node->height != 0) 
			nvp_cleanup_node(node, 0, 1);		
	}
	node->length = file_st->st_size;
	node->maplength = 0;
	node->true_length = node->length;	
	if (node->true_length >= LARGE_FILE_THRESHOLD)
		node->is_large_file = 1;
	else
		node->is_large_file = 0;
	node->dr_mem_used = 0;
	if (node->true_length == 0) {
		clear_tbl_mmap_entry(&_nvp_tbl_mmaps[file_st->st_ino % APPEND_TBL_MAX], NUM_APP_TBL_MMAP_ENTRIES);
#if DATA_JOURNALING_ENABLED
		
		clear_tbl_mmap_entry(&_nvp_over_tbl_mmaps[file_st->st_ino % OVER_TBL_MAX], NUM_OVER_TBL_MMAP_ENTRIES);

#endif // DATA_JOURNALING_ENABLED

	}

	if(node->dr_info.start_addr != 0 || node->dr_over_info.start_addr != 0) {
		DEBUG_FILE("%s: calling transfer to free pool. Inode = %lu\n", __func__, node->serialno);
		nvp_transfer_to_free_dr_pool(node);
	}
	node->async_file_close = 0;
	node->backup_serialno = node->serialno;
	
	NVP_LOCK_UNLOCK_WR(node->lock);
out:
	return node;
}

/**
 * Do the preprocessing for open call. 
 * 
 * 1. Check if 'path' is a valid pointer
 * 2. Check if the path is on the PM mount, else passthru (to kernel).
 * 3. If the file is not present and is not set to be created, passthru.
 * 4. If file is not a regular or block file, passthru.
 * 
 * Return value: Determines if it should be passed through to the kernel.
 * Return value - int* error: Indicates any error that needs to be passed back to the user via errno. This value is negative of errno
 */
RETT_SYSCALL_INTERCEPT _sfs_OPEN_preprocess(char *path, int oflag, int* error) {
	int access_result;

	*error = 0;

	access_result = syscall_no_intercept(SYS_access, path, F_OK);
	/**
	 * Before we derefernece 'path' pointer, we need to check if it is a valid pointer, but not crash it 
	 * (segfault) if it's invalid.
	 * 
	 * Since it is not possible to validate a pointer in the user-space, 
	 * we are making an access system call which validates  
	 * the pointer.
	 */
	if(access_result == -EFAULT) {
		*error = -EFAULT;
		return RETT_NO_PASS_KERN;
	}

	/**
	 * In case absolute path is specified, check if it belongs to the persistent memory 
	 * mount and only then use SplitFS, else redirect to POSIX
	 */
	if(path[0] == '/') {
		int len = strlen(NVMM_PATH);
		char dest[len + 1];
		dest[len] = '\0';
		strncpy(dest, path, len);

		if(strcmp(dest, NVMM_PATH) != 0) {
			// If not pmem mount then do not intercept
			MSG("Not a pmem file, passing through to kernel\n");
			return RETT_PASS_KERN;
		}
	}

	if(access_result && !FLAGS_INCLUDE(oflag, O_CREAT))
	{
		DEBUG("%s: File does not exist and is not set to be created. Passing to kernel\n", __func__);
		return RETT_PASS_KERN;
		
	} else {
		// file exists
		struct stat file_st;
		
		int stat_ret = syscall_no_intercept(SYS_stat, path, &file_st);
		if(stat_ret < 0) {
			DEBUG("%s: failed to get device stats for \"%s\" (error: %s).  Passing to kernel\n", __func__,
				path, strerror(-stat_ret));
			return RETT_PASS_KERN;
		}
		else if(S_ISREG(file_st.st_mode)) {
			DEBUG("%s: file exists and is a regular file.", __func__);
		}
		else if (S_ISBLK(file_st.st_mode)) {
			DEBUG("%s: file exists and is a block device.", __func__);
		}
		else
		{
			DEBUG("%s: file exists and is not a regular or block file. Passing to kernel\n", __func__);
			return RETT_PASS_KERN;
		}
	}

	return RETT_NO_PASS_KERN;
}

RETT_SYSCALL_INTERCEPT _sfs_OPEN(INTF_SYSCALL) {
	char *path;
	int oflag, mode, access_result, fd, error, pp_ret;

#if BG_CLOSING
	int closed_filedesc = -1, hash_index = -1;
	fd = -1;
#if SEQ_LIST || RAND_LIST
	struct ClosedFiles *clf = NULL;
#else // SEQ_LIST || RAND_LIST
	struct InodeClosedFile *tbl = NULL;
#endif // SEQ_LIST || RAND_LIST
#endif // BG_CLOSING

	// Parse the syscall args
	path = (char *)arg0;
	oflag = (int)arg1;

	
	pp_ret = _sfs_OPEN_preprocess(path, oflag, &error);

	// Passthru
	if(pp_ret == RETT_PASS_KERN) {
		MSG("Passing through OPEN call to kernel\n");
		return RETT_PASS_KERN;
	}
	
	// Error + don't passthru
	if(error != 0) {
		*result = error;
		return RETT_NO_PASS_KERN;
	}

	// Start _nvp_OPEN impl here.
	mode = (int)arg2;

	int sfs_result;
	instrumentation_type open_time, clf_lock_time, nvnode_lock_time;

	START_TIMING(open_t, open_time);
	GLOBAL_LOCK_WR();

	DEBUG_FILE("_nvp_OPEN(%s)\n", path);
	num_open++;

	DEBUG("Attempting to _nvp_OPEN the file \"%s\" with the following "
		"flags (0x%X): ", path, oflag);
	
	/*
	 *  Print all the flags passed to open() 
	 */
	if((oflag&O_RDWR)||((oflag&O_RDONLY)&&(oflag&O_WRONLY))) {
		DEBUG_P("O_RDWR ");
	} else if(FLAGS_INCLUDE(oflag,O_WRONLY)) {
		DEBUG_P("O_WRONLY ");
	} else if(FLAGS_INCLUDE(oflag, O_RDONLY)) {
		DEBUG_P("O_RDONLY ");
	}
	DUMP_FLAGS(oflag,O_APPEND);
	DUMP_FLAGS(oflag,O_CREAT);
	DUMP_FLAGS(oflag,O_TRUNC);
	DUMP_FLAGS(oflag,O_EXCL);
	DUMP_FLAGS(oflag,O_SYNC);
	DUMP_FLAGS(oflag,O_ASYNC);
	DUMP_FLAGS(oflag,O_DSYNC);
	DUMP_FLAGS(oflag,O_FSYNC);
	DUMP_FLAGS(oflag,O_RSYNC);
	DUMP_FLAGS(oflag,O_NOCTTY);
	DUMP_FLAGS(oflag,O_NDELAY);
	DUMP_FLAGS(oflag,O_NONBLOCK);
	DUMP_FLAGS(oflag,O_DIRECTORY);
	DUMP_FLAGS(oflag,O_LARGEFILE);
	DUMP_FLAGS(oflag,O_NOATIME);
	DUMP_FLAGS(oflag,O_DIRECT);
	DUMP_FLAGS(oflag,O_NOFOLLOW);
	DEBUG_P("\n");

	struct stat file_st;
	// Initialize NVNode 
	struct NVNode* node = NULL;

#if BG_CLOSING
	if (async_close_enable) {
		if(num_files_closed >= 800 || (dr_mem_closed_files >= ((5ULL) * 1024 * 1024 * 1024))) {
			ASYNC_CLOSING = 0;
			checkAndActivateBgThread();
		}
	}
#endif

	/*
	 * If creation of the file is involved, 3 parameters must be passed to open(). 
	 * Otherwise, 2 parameters must be passed
	 */
	if (FLAGS_INCLUDE(oflag, O_CREAT))
	{
		instrumentation_type op_log_entry_time;
		// Open system call is done here
		DEBUG_FILE("%s: calling open with path = %s, flag = %d, mode = %d, ino addr = %p, ino size addr = %p\n", __func__, path, oflag, mode, &file_st.st_ino, &file_st.st_size);
		fd = syscall_no_intercept(SYS_open, path, oflag & (~O_APPEND), mode);
#if !POSIX_ENABLED
		if (fd >= 0) {
			START_TIMING(op_log_entry_t, op_log_entry_time);
			persist_op_entry(LOG_FILE_CREATE,
					 path,
					 NULL,
					 mode,
					 oflag);
			END_TIMING(op_log_entry_t, op_log_entry_time);
		}
#endif
	} else { 
		DEBUG_FILE("%s: calling open with path = %s, flag = %d, mode = 0666, ino addr = %p, ino size addr = %p\n", __func__, path, oflag, &file_st.st_ino, &file_st.st_size);
		fd = syscall_no_intercept(SYS_open, path, oflag & (~O_APPEND), 0666);
	}
	if(fd<0)
	{
		DEBUG("%s: Kernel open failed: %s\n", __func__, strerror(errno));
		END_TIMING(open_t, open_time);
		GLOBAL_UNLOCK_WR();
		*result = fd;
		return RETT_NO_PASS_KERN;
	}
    DEBUG_FILE("%s:(%s), fd = %d\n",__func__, path, fd);
	SANITYCHECK(&_nvp_fd_lookup[fd] != NULL);
	struct NVFile* nvf = NULL;
	syscall_no_intercept(SYS_stat, path, &file_st);

#if BG_CLOSING
	if (async_close_enable)
		checkAndActivateBgThread();
	GLOBAL_LOCK_CLOSE_WR();
	hash_index = file_st.st_ino % TOTAL_CLOSED_INODES;
#if SEQ_LIST || RAND_LIST
	clf = &_nvp_closed_files[hash_index];

	LRU_NODE_LOCK_WR(clf);

	fd = remove_from_seq_list_hash(clf, file_st.st_ino);
#else // SEQ_LIST || RAND_LIST
	tbl = &inode_to_closed_file[hash_index];
	NVP_LOCK_HASH_TABLE_WR(tbl);
	fd = remove_from_lru_list_hash(file_st.st_ino, 0);
#endif // SEQ_LIST || RAND_LIST
	if(fd >= 0) {
		if ((oflag & O_RDWR) || FLAGS_INCLUDE(oflag, O_RDONLY)) {
			num_close++;
			closed_filedesc = fd;
			__atomic_fetch_sub(&num_files_closed, 1, __ATOMIC_SEQ_CST);
#if SEQ_LIST || RAND_LIST
			LRU_NODE_UNLOCK_WR(clf);
#else // SEQ_LIST || RAND_LIST
			NVP_UNLOCK_HASH_TABLE_WR(tbl);
#endif // SEQ_LIST || RAND_LIST
			GLOBAL_UNLOCK_CLOSE_WR();

			syscall_no_intercept(SYS_close, *result);
			*result = closed_filedesc;
			nvf = &_nvp_fd_lookup[*result];
			node = nvf->node;
			__atomic_fetch_sub(&dr_mem_closed_files, nvf->node->dr_mem_used, __ATOMIC_SEQ_CST);
			NVP_LOCK_FD_WR(nvf);
			NVP_LOCK_NODE_WR(nvf);
			nvf->valid = 0;
			goto initialize_nvf;
		}
	}

#if SEQ_LIST || RAND_LIST
	LRU_NODE_UNLOCK_WR(clf);
#else // SEQ_LIST || RAND_LIST
	NVP_UNLOCK_HASH_TABLE_WR(tbl);
#endif // SEQ_LIST || RAND_LIST
	GLOBAL_UNLOCK_CLOSE_WR();
#endif	// BG_CLOSING

	// Retrieving the NVFile corresponding to the file descriptor returned by open() system call
	nvf = &_nvp_fd_lookup[fd];
	DEBUG("%s: succeeded for path %s: fd %i returned. "
		"filling in file info\n",__func__, path, *result);

	NVP_LOCK_FD_WR(nvf);

	// Check if the file descriptor is already open. If open, something is wrong and return error
	if(nvf->valid)
	{
		MSG("There is already a file open with that FD (%i)!\n", *result);
		assert(0);
		END_TIMING(open_t, open_time);
		GLOBAL_UNLOCK_WR();
		*result = fd;
		return RETT_NO_PASS_KERN;
	}
	_fd_intercept_lookup[fd] = true;

	/*
	 * NVNode is retrieved here. Keeping this check because in quill it was required. Not necessary in Ledger
	 */
	if(node == NULL)
	{
		// Find or allocate a NVNode
		node = nvp_get_node(path, &file_st, fd);
		NVP_LOCK_WR(node->lock);
	}

#if BG_CLOSING
 initialize_nvf:
#endif // BG_CLOSING
	nvf->fd = fd;
	nvf->node = node;
	nvf->posix = 0;
	nvf->serialno = file_st.st_ino;	
	/* 
	 * Write the entry of this file into the global inode number struct. 
	 * This contains the fd of the thread that first 
	 * opened this file. 
	 */
	// Set FD permissions
	if((oflag & O_RDWR)||((oflag & O_RDONLY) && (oflag & O_WRONLY))) {
		DEBUG("oflag (%i) specifies O_RDWR for fd %i\n", oflag, result);
		nvf->canRead = 1;
		nvf->canWrite = 1;
	} else if(oflag&O_WRONLY) {

#if WORKLOAD_TAR | WORKLOAD_GIT | WORKLOAD_RSYNC
		nvf->posix = 0;
		nvf->canRead = 1;
		nvf->canWrite = 1;
#else // WORKLOAD_TAR

		MSG("File %s is opened O_WRONLY.\n", path);
		MSG("Does not support mmap, use posix instead.\n");
		nvf->posix = 1;
		nvf->canRead = 0;
		nvf->canWrite = 1;
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		END_TIMING(open_t, open_time);
		GLOBAL_UNLOCK_WR();
		*result = nvf->fd;
		return 0;
#endif // WORKLOAD_TAR

	} else if(FLAGS_INCLUDE(oflag, O_RDONLY)) {
		DEBUG("oflag (%i) specifies O_RDONLY for fd %i\n",
			oflag, result);
		nvf->canRead = 1;
		nvf->canWrite = 0;
	} else {
		DEBUG("File permissions don't include read or write!\n");
		nvf->canRead = 0;
		nvf->canWrite = 0;
		assert(0);
	}

	if(FLAGS_INCLUDE(oflag, O_APPEND)) {
		nvf->append = 1;
	} else {
		nvf->append = 0;
	}

	SANITYCHECK(nvf->node != NULL);
	if(FLAGS_INCLUDE(oflag, O_TRUNC) && nvf->node->length)
	{
		DEBUG("We just opened a file with O_TRUNC that was already "
			"open with nonzero length %li.  Updating length.\n",
			nvf->node->length);
		nvf->node->length = 0;
	}
	nvf->posix = 0;
	nvf->debug = 0;

	/* For BDB log file, workaround the fdsync issue */
	if (path[29] == 'l' && path[30] == 'o' && path[31] == 'g') {
		nvf->debug = 1;
	}

	nvf->offset = (size_t*)calloc(1, sizeof(int));
	*nvf->offset = 0;

	if(FLAGS_INCLUDE(oflag, O_DIRECT) && (DO_ALIGNMENT_CHECKS)) {
		nvf->aligned = 1;
	} else {
		nvf->aligned = 0;
	}

	nvf->valid = 1;

	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_WR(nvf);

	END_TIMING(open_t, open_time);

	GLOBAL_UNLOCK_WR();
	*result = nvf->fd;
	return RETT_NO_PASS_KERN;
}

RETT_SYSCALL_INTERCEPT _sfs_REAL_CLOSE(int file, ino_t serialno, int async_file_closing, long* result) {

	instrumentation_type node_lookup_lock_time, nvnode_lock_time, close_syscall_time,
		copy_to_dr_pool_time, copy_to_mmap_cache_time, give_up_node_time;
	int cpuid;
	int node_list_idx;

	*result = 0;

	if (file < 0) {
		*result = -EBADF;
		return RETT_NO_PASS_KERN;
	}

	struct NVFile* nvf = &_nvp_fd_lookup[file];
	num_close++;
	if (nvf->posix) {
		nvf->valid = 0;
		nvf->posix = 0;
		NVP_LOCK_NODE_WR(nvf);
		nvf->node->reference--;
		NVP_UNLOCK_NODE_WR(nvf);
		if (nvf->node->reference == 0) {
			nvf->node->serialno = 0;
			int index = nvf->serialno % 1024;
			_nvp_ino_lookup[index] = 0;
		}
		nvf->serialno = 0;
		DEBUG("Call posix CLOSE for fd %d\n", nvf->fd);
		return RETT_PASS_KERN;
	}

	DEBUG_FILE("%s(%i): Ref count = %d\n", __func__, file, nvf->node->reference);
	DEBUG_FILE("%s: Calling fsync flush on fsync\n", __func__);
	cpuid = GET_CPUID();
#if !SYSCALL_APPENDS
	fsync_flush_on_fsync(nvf, cpuid, 1, 0);	
#endif
	/* 
	 * nvf->node->reference contains how many threads have this file open. 
	 */
	node_list_idx = nvf->node->free_list_idx;

	pthread_spin_lock(&node_lookup_lock[node_list_idx]);

	if(nvf->valid == 0) {
		pthread_spin_unlock(&node_lookup_lock[node_list_idx]);
		*result = -1;
		return RETT_NO_PASS_KERN;
	}
	if(nvf->node->reference <= 0) {
		pthread_spin_unlock(&node_lookup_lock[node_list_idx]);
		*result = -1;
		return RETT_NO_PASS_KERN;
	}
	if(nvf->node->serialno != serialno) {
		pthread_spin_unlock(&node_lookup_lock[node_list_idx]);
		*result = -1;
		return RETT_NO_PASS_KERN;
	}

	NVP_LOCK_NODE_WR(nvf);
	nvf->node->reference--;
	NVP_UNLOCK_NODE_WR(nvf);

	if (nvf->node->reference == 0) {
		nvf->node->serialno = 0;
		push_in_stack(1, 0, nvf->node->index_in_free_list, node_list_idx);
	}
	if (async_file_closing) {
		nvf->node->async_file_close = 1;
	}
	pthread_spin_unlock(&node_lookup_lock[node_list_idx]);

	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_WR(nvf);

	// setting valid to 0 means that this fd is not open. So can be used for a subsequent open of same or different file.
	if(nvf->valid == 0) {
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		*result = -1;
		return RETT_NO_PASS_KERN;
	}
	if(nvf->node->reference < 0) {
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		*result = -1;
		return RETT_NO_PASS_KERN;
	}
	if(nvf->serialno != serialno) {
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_UNLOCK_FD_WR(nvf);
		*result = -1;
		return RETT_NO_PASS_KERN;
	}

	nvf->valid = 0;
	if (nvf->node->reference == 0) {
		nvp_add_to_inode_mapping(nvf->node, nvf->serialno);
		nvf->node->backup_serialno = 0;
		int index = nvf->serialno % 1024;
		_nvp_ino_lookup[index] = 0;
		DEBUG("Close Cleanup node for %d\n", file);
		if(nvf->node->dr_info.start_addr != 0 || nvf->node->dr_over_info.start_addr != 0) {
			nvp_transfer_to_free_dr_pool(nvf->node);
		}
		nvf->node->async_file_close = 0;
		nvp_cleanup_node(nvf->node, 0, 0);
	}
	nvf->serialno = 0;

	NVP_UNLOCK_NODE_WR(nvf);
	NVP_UNLOCK_FD_WR(nvf);

	// close() system call of the file is done here. 
	//START_TIMING(close_syscall_t, close_syscall_time);
	return RETT_NO_PASS_KERN;
}


RETT_SYSCALL_INTERCEPT _sfs_CLOSE(INTF_SYSCALL) {
	int file = (int)arg0;

	DEBUG_FILE("%s: fd = %d\n", __func__, file);

	if( (file<0) || (file >= OPEN_MAX) ) {
		DEBUG("fd %i is larger than the maximum number of open files; ignoring it.\n", file);
		errno = EBADF;
		return -1;
	}

	if(!_fd_intercept_lookup[file]) {
		return RETT_PASS_KERN;
	}
	_fd_intercept_lookup[file] = false;

	RETT_SYSCALL_INTERCEPT rc_res;
	ino_t serialno;
	struct NVFile* nvf = NULL;
	instrumentation_type close_time;

#if BG_CLOSING
	instrumentation_type clf_lock_time;
	int previous_closed_filedesc = -1;
	ino_t previous_closed_serialno = 0, stale_serialno = 0;
	int cpuid, stale_fd = -1;
	int hash_index = -1;
#if SEQ_LIST || RAND_LIST
	struct ClosedFiles *clf = NULL;
#else //SEQ_LIST || RAND_LIST
	struct InodeClosedFile *tbl = NULL;
#endif	//SEQ_LIST || RAND_LIST

	//num_close++;
	// Get the struct NVFile from the file descriptor

        nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		nvf->valid = 0;
		nvf->posix = 0;
		NVP_LOCK_NODE_WR(nvf);
		nvf->node->reference--;
		NVP_UNLOCK_NODE_WR(nvf);
		if (nvf->node->reference == 0) {
			nvf->node->serialno = 0;
			int index = nvf->serialno % 1024;
			_nvp_ino_lookup[index] = 0;
		}
		nvf->serialno = 0;
		DEBUG("Call posix CLOSE for fd %d\n", nvf->fd);
		*result = syscall_no_intercept(SYS_close, file);
		END_TIMING(close_t, close_time);
		GLOBAL_UNLOCK_WR();
		return RETT_NO_PASS_KERN;
	}

	serialno = nvf->node->serialno;	
	GLOBAL_LOCK_CLOSE_WR();

	hash_index = serialno % TOTAL_CLOSED_INODES;

#if SEQ_LIST || RAND_LIST
	clf = &_nvp_closed_files[hash_index];

	//START_TIMING(clf_lock_t, clf_lock_time);
	LRU_NODE_LOCK_WR(clf);
	//END_TIMING(clf_lock_t, clf_lock_time);
#else //SEQ_LIST || RAND_LIST
	tbl = &inode_to_closed_file[hash_index];
	NVP_LOCK_HASH_TABLE_WR(tbl);
#endif	//SEQ_LIST || RAND_LIST
	cpuid = GET_CPUID();
	NVP_LOCK_NODE_RD(nvf, cpuid);

	if(nvf->node->reference == 1) {
		NVP_UNLOCK_NODE_RD(nvf, cpuid);
		__atomic_fetch_add(&dr_mem_closed_files, nvf->node->dr_mem_used, __ATOMIC_SEQ_CST);
#if SEQ_LIST || RAND_LIST
		stale_fd = insert_in_seq_list(clf, &stale_serialno, file, serialno);
#else //SEQ_LIST || RAND_LIST
		stale_fd = insert_in_lru_list(file, serialno, &stale_serialno);
#endif	//SEQ_LIST || RAND_LIST
		if(stale_fd >= 0 && stale_serialno > 0) {
			previous_closed_filedesc = stale_fd;
			previous_closed_serialno = stale_serialno;
		}

		if(previous_closed_filedesc != -1) {
			_sfs_REAL_CLOSE(previous_closed_filedesc, previous_closed_serialno, 1, result);
		}
		else 
			__atomic_fetch_add(&num_files_closed, 1, __ATOMIC_SEQ_CST);

#if SEQ_LIST || RAND_LIST
		LRU_NODE_UNLOCK_WR(clf);
#else //SEQ_LIST || RAND_LIST
		NVP_UNLOCK_HASH_TABLE_WR(tbl);
#endif //SEQ_LIST || RAND_LIST
		GLOBAL_UNLOCK_CLOSE_WR();

		END_TIMING(close_t, close_time);
		GLOBAL_UNLOCK_WR();
		*result = 0;
		return RETT_NO_PASS_KERN;
	}

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
#if SEQ_LIST || RAND_LIST
	LRU_NODE_UNLOCK_WR(clf);
#else //SEQ_LIST || RAND_LIST
	NVP_UNLOCK_HASH_TABLE_WR(tbl);
#endif //SEQ_LIST || RAND_LIST
	GLOBAL_UNLOCK_CLOSE_WR();
#endif //BG_CLOSING

	START_TIMING(close_t, close_time);

	GLOBAL_LOCK_WR();
	DEBUG_FILE("%s: (%i)\n", __func__, file);

	nvf = &_nvp_fd_lookup[file];
	serialno = nvf->node->serialno;

	rc_res = _sfs_REAL_CLOSE(file, serialno, 0, result);

	END_TIMING(close_t, close_time);
	GLOBAL_UNLOCK_WR();
	return rc_res;
}

void nvp_cleanup_node(struct NVNode *node, int free_root, int unmap_btree)
{

	unsigned int height = node->height;
	unsigned long *root = node->root;
	unsigned long *merkle_root = node->merkle_root;
	unsigned long *dirty_cache;
	int total_dirty_mmaps = node->total_dirty_mmaps;
	int root_dirty_num = node->root_dirty_num;
	
	DEBUG("Cleanup: root 0x%x, height %u\n", root, height);

	if(root_dirty_num > 0)
		dirty_cache = node->root_dirty_cache;
	else
		dirty_cache = NULL;
     
	if(unmap_btree && node->root_dirty_num) {
		// munmap() all the file backed mmap()s of this file. 
		nvp_free_btree(root, merkle_root, height, dirty_cache, root_dirty_num, total_dirty_mmaps);
	}
		
	/* 
	 * Deallocate everything related to NVNode. This should be done at the end when Ledger is exiting. 
	 */
	if (free_root && node->root[0]) {
		free(node->root);
		free(node->merkle_root);
		free(node->root_dirty_cache);
		node->root = NULL;
		node->merkle_root = NULL;
		node->root_dirty_cache = NULL;
		return;
	}
	// Copy all the DR mmap()s linked to this node, to the global pool of DR mmap()s
	/*
	 * Resetting the file backed mmap addresses, merkle tree addresses and the dirty file backed mmap cache of this node to 0. 
	 */
	if(!unmap_btree)
		nvp_reset_mappings(node);
}
