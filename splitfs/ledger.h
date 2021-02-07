// Header file for nvmfileops.c

#ifndef __LEDGER_H_
#define __LEDGER_H_

#include <sys/types.h>
#include <emmintrin.h>
#include "nvp_lock.h"
#include "non_temporal.h"
#include "perf_delay/add_delay.h"
//#include "liblfds711/inc/liblfds711.h"
#include "lfq.h"
#include "utils.h"
#include "timers.h"

#define ENV_NV_FOP "NVP_NV_FOP"
#define NVP_IO_EOF_SEEN 0x0010
#define NVP_IO_ERR_SEEN 0x0020

/******************* Data Structures ********************/

//struct lfds711_queue_umm_element *qe, *qe_over, qe_dummy, qe_dummy_over;
//struct lfds711_queue_umm_state qs, qs_over;
struct lfq_ctx staging_mmap_queue_ctx;
struct lfq_ctx staging_over_mmap_queue_ctx;

struct NVFile
{
	NVP_LOCK_DECL;
	volatile bool valid;
	int fd;
	volatile size_t* offset;
	bool canRead;
	bool canWrite;
	bool append;
	bool aligned;
	ino_t serialno; // duplicated so that iterating doesn't require following every node*
	struct NVNode* node;
	bool posix;
	bool debug;
	char padding[200];
	int file_stream_flags;
};

struct free_dr_pool
{
	//struct lfds711_queue_umm_element qe;
	unsigned long start_addr;
	int dr_fd;
	ino_t dr_serialno;
	unsigned long valid_offset;
	unsigned long dr_offset_start;
	unsigned long dr_offset_end;
};

struct table_mmaps
{
	off_t file_start_off;
	off_t file_end_off;
	off_t dr_start_off;
	off_t dr_end_off;
	unsigned long buf_start;
};

struct NVTable_maps
{
	NVP_LOCK_DECL;
	struct table_mmaps *tbl_mmaps;	
	int tbl_mmap_index;	
};

struct NVTable_regions
{
	struct table_mmaps *tbl_mmaps;
	off_t lowest_off;
	off_t highest_off;
	int tbl_mmap_index;
	int region_dirty;
};

struct NVLarge_maps
{
	NVP_LOCK_DECL;
	struct NVTable_regions *regions;
	int num_tbl_mmaps;
	int num_regions;
	int min_dirty_region;
	int max_dirty_region;
};

struct NVNode
{
	ino_t serialno;
	ino_t backup_serialno;
	NVP_LOCK_DECL;

	unsigned long true_length;
	volatile size_t length;
	volatile size_t maplength;
	unsigned long *root;
	unsigned long *merkle_root;
	int free_list_idx;
	int async_file_close;
	unsigned long *root_dirty_cache;
	int root_dirty_num;
	int total_dirty_mmaps;
	unsigned int height;
	volatile int reference;
	int isRootSet;
	int index_in_free_list;
	int is_large_file;

	// DR stuff
	struct free_dr_pool dr_info;
	struct free_dr_pool dr_over_info;
	uint64_t dr_mem_used;
};

struct backupRoots {
	unsigned long *root;
	unsigned long *merkle_root;
	unsigned long *root_dirty_cache;
};

struct InodeToMapping
{
	ino_t serialno;
	unsigned long *root;
	unsigned long *merkle_root;
	unsigned long *root_dirty_cache;
	int root_dirty_num;
	int total_dirty_mmaps;
	unsigned int height;
	char buffer[16];
};

struct full_dr {
	int dr_fd;
	unsigned long start_addr;
	size_t size;
};



/******************* Locking ********************/

#define BG_CLOSING 0
#define ASYNC_CLOSING async_close_enable
#define SEQ_LIST 0
#define RAND_LIST 1
#define DIRTY_TRACKING 0
#define NUM_NODE_LISTS 1

#if WORKLOAD_YCSB
#define INIT_NUM_DR 10
#else
#define INIT_NUM_DR 2
#endif

#define INIT_NUM_DR_OVER 2
#define BG_NUM_DR 2
#define PRINT_CONTENTION_MSGS 0
#define TOTAL_CLOSED_INODES 4096
#define LARGE_TBL_MAX 5
#define APPEND_TBL_MAX 4096
#define OVER_TBL_MAX 4096

#if WORKLOAD_YCSB
#define NUM_OVER_TBL_MMAP_ENTRIES 1024
#define NUM_APP_TBL_MMAP_ENTRIES 1024
#endif

#if WORKLOAD_TPCC | WORKLOAD_REDIS | WORKLOAD_FIO | WORKLOAD_FILEBENCH
#define NUM_OVER_TBL_MMAP_ENTRIES 32768
#define NUM_APP_TBL_MMAP_ENTRIES 10240
#endif

#if WORKLOAD_TAR | WORKLOAD_GIT | WORKLOAD_RSYNC
#define NUM_OVER_TBL_MMAP_ENTRIES 100 // 10240
#define NUM_APP_TBL_MMAP_ENTRIES 100 // 10240
#endif

#define REGION_COVERAGE (40*1024)
#define LARGE_TBL_REGIONS (512*1024*1024 / REGION_COVERAGE)

#if WORKLOAD_TPCC | WORKLOAD_FIO | WORKLOAD_FILEBENCH
#define PER_REGION_TABLES (REGION_COVERAGE / 1024)
#else
#define PER_REGION_TABLES 100 // (REGION_COVERAGE / 1024)
#endif

#define LARGE_FILE_THRESHOLD (3ULL*1024*1024*1024)

volatile int async_close_enable;

#define GLOBAL_LOCKING 0
#if GLOBAL_LOCKING

#define GLOBAL_LOCK_WR()            {pthread_spin_lock(&global_lock);}
#define GLOBAL_UNLOCK_WR()          {pthread_spin_unlock(&global_lock);}

#else

#define GLOBAL_LOCK_WR()            {(void)(global_lock);}
#define GLOBAL_UNLOCK_WR()          {(void)(global_lock);}

#endif
#define GLOBAL_CLOSE_LOCKING 0
#if GLOBAL_CLOSE_LOCKING

#define GLOBAL_LOCK_CLOSE_WR()    {pthread_spin_lock(&global_lock_closed_files);}
#define GLOBAL_UNLOCK_CLOSE_WR()  {pthread_spin_unlock(&global_lock_closed_files);}

#else

#define GLOBAL_LOCK_CLOSE_WR()    {(void)(global_lock_closed_files);}
#define GLOBAL_UNLOCK_CLOSE_WR()  {(void)(global_lock_closed_files);}

#endif
#define LRU_HEAD_LOCKING 0
#if LRU_HEAD_LOCKING

#define LRU_LOCK_HEAD_WR()    {pthread_spin_lock(&global_lock_lru_head);}
#define LRU_UNLOCK_HEAD_WR()  {pthread_spin_unlock(&global_lock_lru_head);}

#else

#define LRU_LOCK_HEAD_WR()    {(void)(global_lock_lru_head);}
#define LRU_UNLOCK_HEAD_WR()  {(void)(global_lock_lru_head);}

#endif
#define LRU_NODE_LOCKING 1
#if LRU_NODE_LOCKING

#define LRU_NODE_LOCK_WR(cnode) NVP_LOCK_WR(cnode->lock)
#define LRU_NODE_UNLOCK_WR(cnode) NVP_LOCK_UNLOCK_WR(cnode->lock)

#else

#define LRU_NODE_LOCK_WR(cnode) {(void)(cnode->lock);}
#define LRU_NODE_UNLOCK_WR(cnode) {(void)(cnode->lock);}

#endif
#define HASH_TABLE_LOCKING 0
#if HASH_TABLE_LOCKING

#define NVP_LOCK_HASH_TABLE_RD(tbl, cpuid)   NVP_LOCK_RD(tbl->lock, cpuid)
#define NVP_UNLOCK_HASH_TABLE_RD(tbl, cpuid) NVP_LOCK_UNLOCK_RD(tbl->lock, cpuid)
#define NVP_LOCK_HASH_TABLE_WR(tbl)          NVP_LOCK_WR(tbl->lock)
#define NVP_UNLOCK_HASH_TABLE_WR(tbl)        NVP_LOCK_UNLOCK_WR(tbl->lock)

#else

#define NVP_LOCK_HASH_TABLE_RD(tbl, cpuid)   {(void)(cpuid);}
#define NVP_UNLOCK_HASH_TABLE_RD(tbl, cpuid) {(void)(cpuid);}
#define NVP_LOCK_HASH_TABLE_WR(tbl)          {(void)(tbl->lock);}
#define NVP_UNLOCK_HASH_TABLE_WR(tbl)        {(void)(tbl->lock);}

#endif
#define STACK_LOCKING 0
#if STACK_LOCKING

#define STACK_LOCK_WR()    {pthread_spin_lock(&stack_lock);}
#define STACK_UNLOCK_WR()  {pthread_spin_unlock(&stack_lock);}

#else

#define STACK_LOCK_WR()    {(void)(stack_lock);}
#define STACK_UNLOCK_WR()  {(void)(stack_lock);}

#endif
#define CLOSE_LOCKING 0
#if CLOSE_LOCKING

#define NVP_LOCK_CLOSE_RD(clf, cpuid)   NVP_LOCK_RD(clf->lock, cpuid)
#define NVP_UNLOCK_CLOSE_RD(clf, cpuid) NVP_LOCK_UNLOCK_RD(clf->lock, cpuid)
#define NVP_LOCK_CLOSE_WR(clf)          NVP_LOCK_WR(clf->lock)
#define NVP_UNLOCK_CLOSE_WR(clf)        NVP_LOCK_UNLOCK_WR(clf->lock)

#else

#define NVP_LOCK_CLOSE_RD(clf, cpuid)   {(void)(cpuid);}
#define NVP_UNLOCK_CLOSE_RD(clf, cpuid) {(void)(cpuid);}
#define NVP_LOCK_CLOSE_WR(clf)          {(void)(clf->lock);}
#define NVP_UNLOCK_CLOSE_WR(clf)        {(void)(clf->lock);}

#endif
#define FD_LOCKING 1
#if FD_LOCKING

#define NVP_LOCK_FD_RD(nvf, cpuid)	NVP_LOCK_RD(nvf->lock, cpuid)
#define NVP_UNLOCK_FD_RD(nvf, cpuid)	NVP_LOCK_UNLOCK_RD(nvf->lock, cpuid)
#define NVP_LOCK_FD_WR(nvf)		NVP_LOCK_WR(	   nvf->lock)
#define NVP_UNLOCK_FD_WR(nvf)		NVP_LOCK_UNLOCK_WR(nvf->lock)

#else

#define NVP_LOCK_FD_RD(nvf, cpuid) {(void)(cpuid);}
#define NVP_UNLOCK_FD_RD(nvf, cpuid) {(void)(cpuid);}
#define NVP_LOCK_FD_WR(nvf) {(void)(nvf->lock);}
#define NVP_UNLOCK_FD_WR(nvf) {(void)(nvf->lock);}

#endif
#define NODE_LOCKING 1
#if NODE_LOCKING

#define NVP_LOCK_NODE_RD(nvf, cpuid)	NVP_LOCK_RD(nvf->node->lock, cpuid)
#define NVP_UNLOCK_NODE_RD(nvf, cpuid)	NVP_LOCK_UNLOCK_RD(nvf->node->lock, cpuid)
#define NVP_LOCK_NODE_WR(nvf)		NVP_LOCK_WR(	   nvf->node->lock)
#define NVP_UNLOCK_NODE_WR(nvf)		NVP_LOCK_UNLOCK_WR(nvf->node->lock)

#else

#define NVP_LOCK_NODE_RD(nvf, cpuid) {(void)(cpuid);}
#define NVP_UNLOCK_NODE_RD(nvf, cpuid) {(void)(cpuid);}
#define NVP_LOCK_NODE_WR(nvf) {(void)(nvf->node->lock);}
#define NVP_UNLOCK_NODE_WR(nvf)	{(void)(nvf->node->lock);}

#endif

#define TBL_MMAP_LOCKING 1
#if TBL_MMAP_LOCKING

#define TBL_ENTRY_LOCK_RD(tbl, cpuid)    {if (tbl) {NVP_LOCK_RD(tbl->lock, cpuid);}}
#define TBL_ENTRY_UNLOCK_RD(tbl, cpuid)  {if (tbl) {NVP_LOCK_UNLOCK_RD(tbl->lock, cpuid);}}
#define TBL_ENTRY_LOCK_WR(tbl)           {if (tbl) {NVP_LOCK_WR(tbl->lock);}}
#define TBL_ENTRY_UNLOCK_WR(tbl)         {if (tbl) {NVP_LOCK_UNLOCK_WR(tbl->lock);}}

#else

#define TBL_ENTRY_LOCK_RD(tbl, cpuid)    {(void)(cpuid);}
#define TBL_ENTRY_UNLOCK_RD(tbl, cpuid)  {(void)(cpuid);}
#define TBL_ENTRY_LOCK_WR(tbl)           {(void)(tbl->lock);}
#define TBL_ENTRY_UNLOCK_WR(tbl)         {(void)(tbl->lock);}

#endif

pthread_spinlock_t staging_mmap_lock;
pthread_spinlock_t staging_over_mmap_lock;

#define QUEUE_LOCKING 0
#if QUEUE_LOCKING

#define QUEUE_LOCK_WR(queue_lock)    {pthread_spin_lock(queue_lock);}
#define QUEUE_UNLOCK_WR(queue_lock)  {pthread_spin_unlock(queue_lock);}

#else

#define QUEUE_LOCK_WR(queue_lock)           {(void)(queue_lock);}
#define QUEUE_UNLOCK_WR(queue_lock)         {(void)(queue_lock);}

#endif

/******************* MMAP ********************/

#define IS_ERR(x) ((unsigned long)(x) >= (unsigned long)-4095)

#define MAP_SIZE 16

#if MAP_SIZE == 512
#define MAX_MMAP_SIZE 536870912
#elif MAP_SIZE == 256
#define MAX_MMAP_SIZE 268435456
#elif MAP_SIZE == 128
#define MAX_MMAP_SIZE 134217728
#elif MAP_SIZE == 64
#define MAX_MMAP_SIZE 67108864
#elif MAP_SIZE == 32
#define MAX_MMAP_SIZE 33554432
#elif MAP_SIZE == 16
#define MAX_MMAP_SIZE 16777216
#elif MAP_SIZE == 8
#define MAX_MMAP_SIZE 8388608
#elif MAP_SIZE == 4
#define MAX_MMAP_SIZE 4194304
#elif MAP_SIZE == 2
#define MAX_MMAP_SIZE 2097152
#else
#define MAX_MMAP_SIZE 536870912
#endif

#define ANON_MAP_SIZE 16

#if ANON_MAP_SIZE == 512
#define ANON_MAX_MMAP_SIZE 536870912
#elif ANON_MAP_SIZE == 256
#define ANON_MAX_MMAP_SIZE 268435456
#elif ANON_MAP_SIZE == 128
#define ANON_MAX_MMAP_SIZE 134217728
#elif ANON_MAP_SIZE == 64
#define ANON_MAX_MMAP_SIZE 67108864
#elif ANON_MAP_SIZE == 32
#define ANON_MAX_MMAP_SIZE 33554432
#elif ANON_MAP_SIZE == 16
#define ANON_MAX_MMAP_SIZE 16777216
#elif ANON_MAP_SIZE == 8
#define ANON_MAX_MMAP_SIZE 8388608
#elif ANON_MAP_SIZE == 4
#define ANON_MAX_MMAP_SIZE 4194304
#elif ANON_MAP_SIZE == 2
#define ANON_MAX_MMAP_SIZE 2097152
#else
#define ANON_MAX_MMAP_SIZE 536870912
#endif

#if WORKLOAD_FILEBENCH
#define DR_SIZE (16*1024*1024)
#define DR_OVER_SIZE (2*1024*1024)
#else
#define DR_SIZE (256*1024*1024)
#define DR_OVER_SIZE (256*1024*1024)
#endif

#define NVMM_PATH "/mnt/pmem_emul/"
#define DR_APPEND_PATH "/mnt/pmem_emul/DR-XXXXXX"
#define DR_OVER_PATH "/mnt/pmem_emul/DR-OVER-XXXXXX"

#define	ALIGN_MMAP_DOWN(addr)	((addr) & ~(MAX_MMAP_SIZE - 1))

#define DO_ALIGNMENT_CHECKS 0

int MMAP_PAGE_SIZE;
int MMAP_HUGEPAGE_SIZE;
void* _nvp_zbuf; // holds all zeroes.  used for aligned file extending. TODO: does sharing this hurt performance?
pthread_spinlock_t	node_lookup_lock[NUM_NODE_LISTS];
struct NVFile* _nvp_fd_lookup;
int execve_fd_passing[1024];
int _nvp_free_list_head;
int _nvp_ino_lookup[1024];
struct full_dr* _nvp_full_drs;
int full_dr_idx;
struct NVTable_maps *_nvp_tbl_mmaps;
struct NVTable_maps *_nvp_over_tbl_mmaps;
struct NVLarge_maps *_nvp_tbl_regions;

struct InodeToMapping* _nvp_ino_mapping;
int OPEN_MAX; // maximum number of simultaneous open files


// modifications to support different FSYNC policies
//#define MEMCPY memcpy
#define MEMCPY intel_memcpy
#define MEMCPY_NON_TEMPORAL memmove_nodrain_movnt_granularity
//#define MEMCPY (void*)copy_from_user_inatomic_nocache
//#define MEMCPY my_memcpy_nocache
//#define MEMCPY mmx2_memcpy
#define MMAP mmap

#define FSYNC_POLICY_NONE 0
#define FSYNC_POLICY_FLUSH_ON_FSYNC 1
#define FSYNC_POLICY_UNCACHEABLE_MAP 2
#define FSYNC_POLICY_NONTEMPORAL_WRITES 3
#define FSYNC_POLICY_FLUSH_ON_WRITE 4

#define FSYNC_POLICY FSYNC_POLICY_FLUSH_ON_FSYNC

#if FSYNC_POLICY == FSYNC_POLICY_NONE
	#define FSYNC_MEMCPY MEMCPY
	#define FSYNC_MMAP MMAP
	#define FSYNC_FSYNC 
#elif FSYNC_POLICY == FSYNC_POLICY_FLUSH_ON_FSYNC
	#define FSYNC_MEMCPY MEMCPY
	#define FSYNC_MMAP MMAP
        #define FSYNC_FSYNC(nvf,cpuid,close,fdsync) fsync_flush_on_fsync(nvf,cpuid,close,fdsync)
#elif FSYNC_POLICY == FSYNC_POLICY_UNCACHEABLE_MAP
	#define FSYNC_MEMCPY MEMCPY
	#define FSYNC_MMAP mmap_fsync_uncacheable_map
	#define FSYNC_FSYNC 
#elif FSYNC_POLICY == FSYNC_POLICY_NONTEMPORAL_WRITES
	#define FSYNC_MEMCPY memcpy_fsync_nontemporal_writes
	#define FSYNC_MMAP MMAP
	#define FSYNC_FSYNC _mm_mfence()
#elif FSYNC_POLICY == FSYNC_POLICY_FLUSH_ON_WRITE
	#define FSYNC_MEMCPY memcpy_fsync_flush_on_write
	#define FSYNC_MMAP MMAP
	#define FSYNC_FSYNC _mm_mfence()
#endif

void nvp_transfer_to_free_dr_pool(struct NVNode *node);
/*
void insert_tbl_mmap_entry(struct NVNode *node,
			   off_t file_off_start,
			   off_t dr_off_start,
			   size_t length,
			   unsigned long buf_start);
void insert_over_tbl_mmap_entry(struct NVNode *node,
			   off_t file_off_start,
			   off_t dr_off_start,
			   size_t length,
			   unsigned long buf_start);
int read_tbl_mmap_entry(struct NVNode *node,
			off_t file_off_start,
			size_t length,
			unsigned long *mmap_addr,
			size_t *extent_length);
*/
static inline void do_cflushopt_len(volatile void* addr, size_t length)
{
	// note: it's necessary to do an mfence before and after calling this function
	size_t i;
	for (i = 0; i < length; i += CLFLUSH_SIZE) {
		_mm_flush((void *)(addr + i));
	}

	perfmodel_add_delay(0, length);
}


#endif
