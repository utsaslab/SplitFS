/*
 * =====================================================================================
 *
 *       Filename:  _nvp_posix.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:43:16 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include <libsyscall_intercept_hook_point.h>
#include <stdlib.h>
// a module which repalces the standart POSIX functions with memory mapped equivalents

#include <cpuid.h>
#include <nv_common.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <emmintrin.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <linux/kernel.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <stdatomic.h>

#include "lru_cache.h"
#include "thread_handle.h"

#include "bg_clear_mmap.h"
#include "add_delay.h"

#include "staging.h"
#include "inode.h"
#include "timers.h"
#include "non_temporal.h"
#include "nvp_lock.h"
#include "stack.h"
#include "log.h"

#include "file.h"
#include "tbl_mmaps.h"
#include "mmap_cache.h"
#include "handle_mmaps.h"
#include "fsync.h"
#include "execve.h"

void _initialize_splitfs(void);
void* _nvp_zbuf; // holds all zeroes.  used for aligned file extending. TODO: does sharing this hurt performance?

// Define a system call intercepting function
typedef RETT_SYSCALL_INTERCEPT (*syscall_hook)(INTF_SYSCALL);

// Map storing the system call function pointers. Index corresponds to system call number
syscall_hook syscall_hook_arr[512];


void *(*import_memcpy)(void * __restrict__ b, const void * __restrict__ a, size_t n);

extern void * __memcpy(void * __restrict__ to, const void * __restrict__ from, size_t len);


int _nvp_ino_lookup[1024];
pthread_spinlock_t node_lookup_lock[NUM_NODE_LISTS];
struct full_dr* _nvp_full_drs;
int full_dr_idx;
pthread_spinlock_t stack_lock;

struct InodeToMapping* _nvp_ino_mapping;
struct NVFile* _nvp_fd_lookup;
struct NVNode *_nvp_node_lookup[NUM_NODE_LISTS];

struct StackNode *_nvp_free_node_list[NUM_NODE_LISTS];
int _nvp_free_node_list_head[NUM_NODE_LISTS];
struct NVNode *_nvp_node_lookup[NUM_NODE_LISTS];

int run_background_cleaning_thread;
int started_bg_cleaning_thread;
int exit_bg_cleaning_thread;
int calledBgCleaningThread;
int waiting_for_cleaning_signal;

atomic_uint_fast64_t dr_mem_allocated;

struct NVTable_maps *_nvp_tbl_mmaps;
struct NVTable_maps *_nvp_over_tbl_mmaps;
struct NVLarge_maps *_nvp_tbl_regions;

pthread_spinlock_t global_lock;

int OPEN_MAX;

static void nvp_cleanup(void)
{
	int i, j;

#if BG_CLOSING
	while(!waiting_for_signal)
		sleep(1);

	//cancel thread
	cancelBgThread();
	exit_bgthread = 1;
	cleanup = 1;
	bgCloseFiles(1);
#endif

	nvp_free_dr_mmaps();
	free(_nvp_fd_lookup);

	for (i = 0; i < NUM_NODE_LISTS; i++) {
		pthread_spin_lock(&node_lookup_lock[i]);

		for (j = 0; j< OPEN_MAX; j++) {
			nvp_cleanup_node(&_nvp_node_lookup[i][j], 1, 1); 
		}

		pthread_spin_unlock(&node_lookup_lock[i]);

		free(_nvp_node_lookup[i]);
	}

	for (i = 0; i < OPEN_MAX; i++) {
		nvp_free_btree(_nvp_ino_mapping[i].root,
			       _nvp_ino_mapping[i].merkle_root,
			       _nvp_ino_mapping[i].height,
			       _nvp_ino_mapping[i].root_dirty_cache,
			       _nvp_ino_mapping[i].root_dirty_num,
			       _nvp_ino_mapping[i].total_dirty_mmaps);
	}
	free(_nvp_ino_mapping);

	// DEBUG_FILE("%s: CLEANUP FINISHED\n", __func__);
	// MSG("%s: Done Cleaning up\n", __func__);
}

static void nvp_exit_handler(void)
{
	MSG("Exit: print stats\n");
	nvp_print_io_stats();
	PRINT_TIME();

	MSG("calling cleanup\n");
	DEBUG_FILE("%s: CLEANUP STARTED\n", __func__);
	nvp_cleanup();
}

static void _nvp_SIGUSR1_handler(int sig)
{
	MSG("SIGUSR1: print stats\n");
	//nvp_print_time_stats();
	nvp_print_io_stats();
	PRINT_TIME();
}

static void _nvp_SIGBUS_handler(int sig)
{
	ERROR("We got a SIGBUS (sig %i)! "
		"This almost certainly means someone tried to access an area "
		"inside an mmaped region but past the length of the mmapped "
		"file.\n", sig);
	MSG("%s: sigbus got\n", __func__);
	fflush(NULL);

	assert(0);
}

void _mm_cache_flush(void const* p) {
  asm volatile("clflush %0" : "+m" (*(volatile char *)(p)));
}

void _mm_cache_flush_optimised(void const* p) {
  asm volatile("clflushopt %0" : "+m" (*(volatile char *)(p)));
}

// Figure out if CLFLUSHOPT is supported 
int is_clflushopt_supported() {
	unsigned int eax, ebx, ecx, edx;
	__cpuid_count(7, 0, eax, ebx, ecx, edx);
	return ebx & bit_CLFLUSHOPT;
}

void _initialize_splitfs(void)
{
	OPEN_MAX = 1024;
	int i, j;
	struct InodeToMapping *tempMapping;

	assert(!posix_memalign(((void**)&_nvp_zbuf), 4096, 4096));

	_nvp_print_fd = fdopen(syscall_no_intercept(SYS_dup, 2), "a");
	MSG("Now printing on fd %p\n", _nvp_print_fd);
	assert(_nvp_print_fd >= 0);

	/*
	 Based on availability of CLFLUSHOPT instruction, point _mm_flush to the 
	 appropriate function
	*/
	if(is_clflushopt_supported()) {
		MSG("CLFLUSHOPT is supported!\n");
		_mm_flush = _mm_cache_flush_optimised;
	} else { 
		MSG("CLFLUSHOPT is not supported! Using CLFLUSH \n");
		_mm_flush = _mm_cache_flush;
	}

#if WORKLOAD_TAR | WORKLOAD_GIT | WORKLOAD_RSYNC
	ASYNC_CLOSING = 0;
#else
	ASYNC_CLOSING = 1;
#endif // WORKLOAD_TAR

	/* 
	 * Allocating and Initializing NVFiles. Total number of NVFiles = 1024. 
	 * _nvp_fd_lookup is an array of struct NVFile 
	*/
	_nvp_fd_lookup = (struct NVFile*)calloc(OPEN_MAX,
						sizeof(struct NVFile));
	// Allocating intercept lookup table. If true then SplitFS handles the file, else passed on to ext4.
	_fd_intercept_lookup = (bool *) calloc(OPEN_MAX, sizeof(bool));
	if (!_nvp_fd_lookup || !_fd_intercept_lookup)
		assert(0);
	// Initializing the valid bits and locks of each NVFile
	for(i = 0; i < OPEN_MAX; i++) {
		_nvp_fd_lookup[i].valid = 0;
		NVP_LOCK_INIT(_nvp_fd_lookup[i].lock);
	}
	/* Initializing the closed file descriptor array */
	_nvp_closed_files = (struct ClosedFiles*)calloc(TOTAL_CLOSED_INODES, sizeof(struct ClosedFiles));
	for(i = 0; i < TOTAL_CLOSED_INODES; i++) {
		_nvp_closed_files[i].fd = -1;
		_nvp_closed_files[i].serialno = 0;
		_nvp_closed_files[i].index_in_free_list = -1;
		_nvp_closed_files[i].next_closed_file = -1;
		_nvp_closed_files[i].prev_closed_file = -1;
		NVP_LOCK_INIT(_nvp_closed_files[i].lock);
	}
	if(!_nvp_closed_files)
		assert(0);

	/* Initialize and allocate hash table for closed file descriptor array */
	inode_to_closed_file = (struct InodeClosedFile *)calloc(OPEN_MAX, sizeof(struct InodeClosedFile));
	for(i = 0; i < OPEN_MAX; i++) {
		inode_to_closed_file[i].index = -1;
		NVP_LOCK_INIT(inode_to_closed_file[i].lock);
	}
	if(!inode_to_closed_file)
		assert(0);

	lru_head = -1;
	lru_tail = -1;
	lru_tail_serialno = 0;	

	/* 
	   Allocate and initialize the free list for nodes
	*/
	for (i = 0; i < NUM_NODE_LISTS; i++) {
		_nvp_free_node_list[i] = (struct StackNode*)calloc(OPEN_MAX,
								   sizeof(struct StackNode));
		for(j = 0; j < OPEN_MAX; j++) {
			_nvp_free_node_list[i][j].free_bit = 1;
			_nvp_free_node_list[i][j].next_free_idx = j+1;
		}
		_nvp_free_node_list[i][OPEN_MAX - 1].next_free_idx = -1;
	}

	_nvp_free_lru_list = (struct StackNode*)calloc(OPEN_MAX,
						       sizeof(struct StackNode));
	for(i = 0; i < OPEN_MAX; i++) {
		_nvp_free_lru_list[i].free_bit = 1;
		_nvp_free_lru_list[i].next_free_idx = i+1;
	}
       	_nvp_free_lru_list[OPEN_MAX - 1].next_free_idx = -1;
	for (i = 0; i < NUM_NODE_LISTS; i++) {
		if (!_nvp_free_node_list[i])
			assert(0);
	}
	if(!_nvp_free_lru_list)
		assert(0);
	for (i = 0; i < NUM_NODE_LISTS; i++) {
		_nvp_free_node_list_head[i] = 0;
	}
	_nvp_free_lru_list_head = 0;
	/* 
	   Allocating and Initializing mmap cache. Can hold mmaps, merkle trees and dirty mmap caches belonging to 1024 files. _nvp_ino_mapping is an array of struct InodeToMapping 
	*/
	_nvp_ino_mapping = (struct InodeToMapping*)calloc(OPEN_MAX, sizeof(struct InodeToMapping));
	memset((void *)_nvp_ino_mapping, 0, OPEN_MAX * sizeof(struct InodeToMapping));
	if (!_nvp_ino_mapping)
		assert(0);
	for(i=0; i<OPEN_MAX; i++) {
		tempMapping = &_nvp_ino_mapping[i];
		// Allocating region to store mmap() addresses
		tempMapping->root = malloc(1024 * sizeof(unsigned long));
		memset((void *)tempMapping->root, 0, 1024 * sizeof(unsigned long));

		tempMapping->merkle_root = malloc(1024 * sizeof(unsigned long));
		memset((void *)tempMapping->merkle_root, 0, 1024 * sizeof(unsigned long));

		// Allocating region to store dirty mmap caches
		tempMapping->root_dirty_cache = malloc(20 * sizeof(unsigned long));
		memset((void *)tempMapping->root_dirty_cache, 0, 20 * sizeof(unsigned long));

		tempMapping->root_dirty_num = 0;
		tempMapping->total_dirty_mmaps = 0;

		// Initializing the inode numbers = keys to 0
		_nvp_ino_mapping[i].serialno = 0;
	}
	/*
	 * Allocating and Initializing NVNode. Number of NVNodes = 1024. 
	 * _nvp_node_lookup is an array of struct NVNode 
	*/
	for (i = 0; i < NUM_NODE_LISTS; i++) {
		_nvp_node_lookup[i] = (struct NVNode*)calloc(OPEN_MAX,
							  sizeof(struct NVNode));
		if (!_nvp_node_lookup[i])
			assert(0);

		_nvp_backup_roots[i] = (struct backupRoots*)calloc(OPEN_MAX,
								   sizeof(struct backupRoots));
		if (!_nvp_backup_roots[i])
			assert(0);


		memset((void *)_nvp_node_lookup[i], 0, OPEN_MAX * sizeof(struct NVNode));
		// Allocating and initializing all the dynamic structs inside struct NVNode 
		for(j = 0; j < OPEN_MAX; j++) {
			// Initializing lock associated with NVNode
			NVP_LOCK_INIT(_nvp_node_lookup[i][j].lock);

			// Allocating and Initializing mmap() roots associated with NVNode 
			_nvp_node_lookup[i][j].root = malloc(1024 * sizeof(unsigned long));
			memset((void *)_nvp_node_lookup[i][j].root, 0, 1024 * sizeof(unsigned long));

			// Allocating and Initializing merkle tree roots associated with NVNode
			_nvp_node_lookup[i][j].merkle_root = malloc(1024 * sizeof(unsigned long));
			memset((void *)_nvp_node_lookup[i][j].merkle_root, 0, 1024 * sizeof(unsigned long));

			// Allocating and Initializing the dirty mmap cache associated with NVNode
			_nvp_node_lookup[i][j].root_dirty_cache = malloc(20 * sizeof(unsigned long));
			memset((void *)_nvp_node_lookup[i][j].root_dirty_cache, 0, 20 * sizeof(unsigned long));

			_nvp_node_lookup[i][j].root_dirty_num = 0;
			_nvp_node_lookup[i][j].total_dirty_mmaps = 0;

			// Allocating and Initializing DR root of the node
			memset((void *)&_nvp_node_lookup[i][j].dr_info, 0, sizeof(struct free_dr_pool));

			_nvp_backup_roots[i][j].root = _nvp_node_lookup[i][j].root;
			_nvp_backup_roots[i][j].merkle_root = _nvp_node_lookup[i][j].merkle_root;
			_nvp_backup_roots[i][j].root_dirty_cache = _nvp_node_lookup[i][j].root_dirty_cache;

		}
	}

	/*
	  Allocating and Initializing the free pool of DR mmap()s. Total number of mmap()s allowed = 1024.
	*/
	//lfds711_queue_umm_init_valid_on_current_logical_core( &qs, &qe_dummy, NULL );
	lfq_init(&staging_mmap_queue_ctx, NVP_NUM_LOCKS/2);

#if DATA_JOURNALING_ENABLED

	//lfds711_queue_umm_init_valid_on_current_logical_core( &qs_over, &qe_dummy_over, NULL );
	lfq_init(&staging_over_mmap_queue_ctx, NVP_NUM_LOCKS/2);

#endif

	MMAP_PAGE_SIZE = getpagesize();
	MMAP_HUGEPAGE_SIZE = 2097152;

	init_append_log();
#if !POSIX_ENABLED
	init_op_log();
#endif

	struct free_dr_pool *free_pool_mmaps;
	char prefault_buf[MMAP_PAGE_SIZE];
	char dr_fname[256];
	int dr_fd, ret;
	struct stat stat_buf;
	int max_perms = PROT_READ | PROT_WRITE;
	int num_dr_blocks = DR_SIZE / MMAP_PAGE_SIZE;
	free_pool_mmaps = (struct free_dr_pool *) malloc(sizeof(struct free_dr_pool)*INIT_NUM_DR);
	for (i = 0; i < MMAP_PAGE_SIZE; i++)
		prefault_buf[i] = '0';

	for (i = 0; i < INIT_NUM_DR; i++) {
		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-XXXXXX");
		dr_fd = syscall_no_intercept(SYS_open, mktemp(dr_fname), O_RDWR | O_CREAT, 0666);
		if (dr_fd < 0) {
			MSG("%s: mkstemp of DR file failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		ret = posix_fallocate(dr_fd, 0, DR_SIZE);
		if (ret < 0) {
			MSG("%s: posix_fallocate failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		num_mmap++;
		num_drs++;
		free_pool_mmaps[i].start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_SIZE,
			 max_perms, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 dr_fd, //fd_with_max_perms,
			 0
			 );
		fstat(dr_fd, &stat_buf);
		free_pool_mmaps[i].dr_serialno = stat_buf.st_ino;
		free_pool_mmaps[i].dr_fd = dr_fd;
	        free_pool_mmaps[i].valid_offset = 0;
	        free_pool_mmaps[i].dr_offset_start = DR_SIZE;
		free_pool_mmaps[i].dr_offset_end = free_pool_mmaps[i].valid_offset;

		for (j = 0; j < num_dr_blocks; j++) {
#if NON_TEMPORAL_WRITES

			if(MEMCPY_NON_TEMPORAL((char *)free_pool_mmaps[i].start_addr + j*MMAP_PAGE_SIZE, prefault_buf, MMAP_PAGE_SIZE) == NULL) {
				MSG("%s: non-temporal memcpy failed\n", __func__);
				assert(0);
			}

#else

			if(FSYNC_MEMCPY((char *)free_pool_mmaps[i].start_addr + j*MMAP_PAGE_SIZE, prefault_buf, MMAP_PAGE_SIZE) == NULL) {
				MSG("%s: non-temporal memcpy failed\n", __func__);
				assert(0);
			}

#endif // NON_TEMPORAL_WRITES

#if NVM_DELAY

			perfmodel_add_delay(0, MMAP_PAGE_SIZE);

#endif //NVM_DELAY

		}

		//LFDS711_QUEUE_UMM_SET_VALUE_IN_ELEMENT(free_pool_mmaps[i].qe,
		//				       &free_pool_mmaps[i] );

		//lfds711_queue_umm_enqueue( &qs, &free_pool_mmaps[i].qe );
		if (lfq_enqueue(&staging_mmap_queue_ctx, &(free_pool_mmaps[i])) != 0)
			assert(0);

		MSG("%s: dr fd = %d, start addr = %p\n", __func__, dr_fd,
			   free_pool_mmaps[i].start_addr);
		dr_fname[0] = '\0';
		num_drs_left++;
	}

#if DATA_JOURNALING_ENABLED

	int num_dr_over_blocks = DR_OVER_SIZE / MMAP_PAGE_SIZE;
	free_pool_mmaps = NULL;
	free_pool_mmaps = (struct free_dr_pool *) malloc(sizeof(struct free_dr_pool)*INIT_NUM_DR_OVER);
	for (i = 0; i < MMAP_PAGE_SIZE; i++)
		prefault_buf[i] = '0';

	for (i = 0; i < INIT_NUM_DR_OVER; i++) {
		sprintf(dr_fname, "%s%s", NVMM_PATH, "DR-OVER-XXXXXX");
		dr_fd = syscall_no_intercept(SYS_open, mktemp(dr_fname), O_RDWR | O_CREAT, 0666);
		if (dr_fd < 0) {
			MSG("%s: mkstemp of DR file failed. Err = %s\n",
			    __func__, strerror(dr_fd));
			assert(0);
		}
		ret = posix_fallocate(dr_fd, 0, DR_OVER_SIZE);
		if (ret < 0) {
			MSG("%s: posix_fallocate failed. Err = %s\n",
			    __func__, strerror(errno));
			assert(0);
		}
		num_mmap++;
		num_drs++;
		free_pool_mmaps[i].start_addr = (unsigned long) FSYNC_MMAP
			(
			 NULL,
			 DR_OVER_SIZE,
			 max_perms, //max_perms,
			 MAP_SHARED | MAP_POPULATE,
			 dr_fd, //fd_with_max_perms,
			 0
			 );
		syscall_no_intercept(SYS_fstat, dr_fd, &stat_buf);
		free_pool_mmaps[i].dr_serialno = stat_buf.st_ino;
		free_pool_mmaps[i].dr_fd = dr_fd;
	        free_pool_mmaps[i].valid_offset = 0;
	        free_pool_mmaps[i].dr_offset_start = free_pool_mmaps[i].valid_offset;
		free_pool_mmaps[i].dr_offset_end = DR_OVER_SIZE;

		for (j = 0; j < num_dr_over_blocks; j++) {

#if NON_TEMPORAL_WRITES

			if(MEMCPY_NON_TEMPORAL((char *)free_pool_mmaps[i].start_addr + j*MMAP_PAGE_SIZE, prefault_buf, MMAP_PAGE_SIZE) == NULL) {
				MSG("%s: non-temporal memcpy failed\n", __func__);
				assert(0);
			}

#else // NON_TEMPORAL_WRITES

			if(FSYNC_MEMCPY((char *)free_pool_mmaps[i].start_addr + j*MMAP_PAGE_SIZE, prefault_buf, MMAP_PAGE_SIZE) == NULL) {
				MSG("%s: non-temporal memcpy failed\n", __func__);
				assert(0);
			}

#endif // NON_TEMPORAL_WRITES

#if NVM_DELAY

			perfmodel_add_delay(0, MMAP_PAGE_SIZE);

#endif //NVM_DELAY

		}

		//LFDS711_QUEUE_UMM_SET_VALUE_IN_ELEMENT(free_pool_mmaps[i].qe,
		//				       &free_pool_mmaps[i] );

		if (lfq_enqueue(&staging_over_mmap_queue_ctx, &(free_pool_mmaps[i])) != 0)
			assert(0);
		//lfds711_queue_umm_enqueue( &qs_over, &free_pool_mmaps[i].qe );

		MSG("%s: dr fd = %d, start addr = %p\n", __func__, dr_fd,
		    free_pool_mmaps[i].start_addr);
		dr_fname[0] = '\0';
		num_drs_left++;
	}

	// Creating array of full DRs to dispose at process end time.
	_nvp_full_drs = (struct full_dr *) malloc(1024*sizeof(struct full_dr));
	memset((void *) _nvp_full_drs, 0, 1024*sizeof(struct full_dr));
	full_dr_idx = 0;

	_nvp_tbl_regions = (struct NVLarge_maps *) malloc(LARGE_TBL_MAX*sizeof(struct NVLarge_maps));
	memset((void *) _nvp_tbl_regions, 0, LARGE_TBL_MAX*sizeof(struct NVLarge_maps));
	for (i = 0; i < LARGE_TBL_MAX; i++) {
       		_nvp_tbl_regions[i].regions = (struct NVTable_regions *) malloc(LARGE_TBL_REGIONS*sizeof(struct NVTable_regions));
		memset((void *) _nvp_tbl_regions[i].regions, 0, LARGE_TBL_REGIONS*sizeof(struct NVTable_regions));
		for (j = 0; j < LARGE_TBL_REGIONS; j++) {
			_nvp_tbl_regions[i].regions[j].tbl_mmaps = (struct table_mmaps *) malloc(PER_REGION_TABLES*sizeof(struct table_mmaps));
			_nvp_tbl_regions[i].regions[j].lowest_off = (REGION_COVERAGE)*(j + 1);
			_nvp_tbl_regions[i].regions[j].highest_off = 0;
			memset((void *) _nvp_tbl_regions[i].regions[j].tbl_mmaps, 0, PER_REGION_TABLES*sizeof(struct table_mmaps));
		}
		_nvp_tbl_regions[i].min_dirty_region = LARGE_TBL_REGIONS;
		_nvp_tbl_regions[i].max_dirty_region = 0;
	}

	MSG("%s: Large regions set\n", __func__);

	_nvp_over_tbl_mmaps = (struct NVTable_maps *) malloc(OVER_TBL_MAX*sizeof(struct NVTable_maps));
	for (i = 0; i < OVER_TBL_MAX; i++) {
		_nvp_over_tbl_mmaps[i].tbl_mmaps = (struct table_mmaps *) malloc(NUM_OVER_TBL_MMAP_ENTRIES*sizeof(struct table_mmaps));
		memset((void *)_nvp_over_tbl_mmaps[i].tbl_mmaps, 0, NUM_OVER_TBL_MMAP_ENTRIES*sizeof(struct table_mmaps));
		_nvp_over_tbl_mmaps[i].tbl_mmap_index = 0;
		NVP_LOCK_INIT(_nvp_over_tbl_mmaps[i].lock);
	}

	MSG("%s: Tbl over mmaps set\n", __func__);

#endif	// DATA_JOURNALING_ENABLED

	_nvp_tbl_mmaps = (struct NVTable_maps *) malloc(APPEND_TBL_MAX*sizeof(struct NVTable_maps));
	for (i = 0; i < APPEND_TBL_MAX; i++) {
		_nvp_tbl_mmaps[i].tbl_mmaps = (struct table_mmaps *) malloc(NUM_APP_TBL_MMAP_ENTRIES*sizeof(struct table_mmaps));
		memset((void *)_nvp_tbl_mmaps[i].tbl_mmaps, 0, NUM_APP_TBL_MMAP_ENTRIES*sizeof(struct table_mmaps));
		_nvp_tbl_mmaps[i].tbl_mmap_index = 0;
		NVP_LOCK_INIT(_nvp_tbl_mmaps[i].lock);
	}

	MSG("%s: Tbl mmaps set\n", __func__);

	// Initializing global lock for accessing NVNode
	for (i = 0; i < NUM_NODE_LISTS; i++) {
		pthread_spin_init(&node_lookup_lock[i], PTHREAD_PROCESS_SHARED);
	}
	pthread_spin_init(&global_lock, PTHREAD_PROCESS_SHARED);
	pthread_spin_init(&global_lock_closed_files, PTHREAD_PROCESS_SHARED);
	pthread_spin_init(&global_lock_lru_head, PTHREAD_PROCESS_SHARED);
	pthread_spin_init(&stack_lock, PTHREAD_PROCESS_SHARED);	

	MSG("%s: Global locks created\n", __func__);

	SANITYCHECK(MMAP_PAGE_SIZE > 100);
	INITIALIZE_TIMERS();
	/*
	  Setting up variables and initialization for background thread
	*/
	cleanup = 0;

	waiting_for_signal = 0;
	started_bgthread = 0;
	exit_bgthread = 0;
	waiting_for_cleaning_signal = 0;
	started_bg_cleaning_thread = 0;
	exit_bg_cleaning_thread = 0;

	lim_num_files = 100;
	lim_dr_mem = (5ULL) * 1024 * 1024 * 1024;
	lim_dr_mem_closed = 500 * 1024 * 1024;
	run_background_thread = 0;
	initEnvForBg();
	initEnvForBgClean();
	MSG("%s: initialized environment, OPEN_MAX = %d\n", __func__, OPEN_MAX);
        dr_mem_allocated = 0;
	dr_mem_closed_files = 0;
#if BG_CLOSING
	calledBgThread = 0;
	startBgThread();
#endif
#if BG_CLEANING
	calledBgCleaningThread = 0;
	startBgCleaningThread();
#endif
	/*
	 * Setting up signal handlers: SIGBUS and SIGUSR 
	 */
	DEBUG("Installing signal handler.\n");
	signal(SIGBUS, _nvp_SIGBUS_handler);
	/* For filebench */
	signal(SIGUSR1, _nvp_SIGUSR1_handler);
	/*
	  Setting up the exit handler to print stats 
	*/
	atexit(nvp_exit_handler);

	int pid = getpid();
	char exec_nvp_filename[BUF_SIZE];

	sprintf(exec_nvp_filename, "/dev/shm/exec-ledger-%d", pid);
	if (access(exec_nvp_filename, F_OK ) != -1) {
		_sfs_SHM_COPY();
	}
}

void _init_hook_arr() {
	syscall_hook_arr[SYS_open] = &_sfs_OPEN;
	syscall_hook_arr[SYS_close] = &_sfs_CLOSE;
	syscall_hook_arr[SYS_read] = &_sfs_READ;
	syscall_hook_arr[SYS_write] = &_sfs_WRITE;
	syscall_hook_arr[SYS_lseek] = &_sfs_SEEK;
	syscall_hook_arr[SYS_execve] = &_sfs_EXECVE;
	syscall_hook_arr[SYS_fsync] = &_sfs_FSYNC;
	syscall_hook_arr[SYS_dup] = &_sfs_DUP;
	syscall_hook_arr[SYS_dup2] = &_sfs_DUP2;
#if !POSIX_ENABLED
	syscall_hook_arr[SYS_mknod] = &_sfs_MKNOD;
	syscall_hook_arr[SYS_mknodat] = &_sfs_MKNODAT;
	syscall_hook_arr[SYS_mkdir] = &_sfs_MKDIR;
	syscall_hook_arr[SYS_mkdirat] = &_sfs_MKDIRAT;
	syscall_hook_arr[SYS_rename] = &_sfs_RENAME;
	syscall_hook_arr[SYS_rmdir] = &_sfs_RMDIR;
	syscall_hook_arr[SYS_link] = &_sfs_LINK;
	syscall_hook_arr[SYS_symlink] = &_sfs_SYMLINK;
	syscall_hook_arr[SYS_symlinkat] = &_sfs_SYMLINKAT;
	syscall_hook_arr[SYS_unlink] = &_sfs_UNLINK;
	syscall_hook_arr[SYS_unlinkat] = &_sfs_UNLINKAT;
#endif
}

#if DEBUG_INTERCEPTIONS
bool visited[512];
int sfd;
#endif

static RETT_SYSCALL_INTERCEPT
hook(long syscall_number, INTF_SYSCALL)
{
	// If not defined then pass to kernel
	if(syscall_hook_arr[syscall_number] == NULL) {

#if DEBUG_INTERCEPTIONS
	// Write to the file all the system calls that were not intercepted by SplitFS
	int num = (int)syscall_number;
	if(sfd == 0) {
		sfd = syscall_no_intercept(SYS_open, "/tmp/sfs_unintercepted_syscalls.log", O_CREAT | O_RDWR | O_APPEND, 0644);
		if(sfd <= 0) {
			perror("error!");
		}
		assert(sfd > 0);
	}
	if(!visited[num]) {
		char buf[512];
		int len; 
		len = sprintf(buf, "%d\n", num);
		syscall_no_intercept(SYS_write, sfd, buf, len);
		visited[num] = true;
	}
#endif
		return RETT_PASS_KERN;
	}
	return syscall_hook_arr[syscall_number](arg0, arg1, arg2, arg3, arg4, arg5, result);
}

static __attribute__((constructor)) void
init(void)
{
	_initialize_splitfs();
	_init_hook_arr();

	// Set up the callback function
	intercept_hook_point = hook;
}