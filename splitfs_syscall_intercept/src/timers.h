#ifndef _LEDGER_TIMERS_H_
#define _LEDGER_TIMERS_H_

#include <sys/time.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <debug.h>

extern unsigned int num_open;
extern unsigned int num_close;
extern unsigned int num_async_close;
extern unsigned int num_read;
extern unsigned int num_write;
extern unsigned int num_stat;
extern unsigned int num_unlink;
extern unsigned int num_appendfsync;
extern unsigned int num_memcpy_read;
extern unsigned int num_anon_read;
extern unsigned int num_memcpy_write;
extern unsigned int num_append_write;
extern unsigned int num_posix_read;
extern unsigned int num_posix_write;
extern unsigned int num_fsync;
extern unsigned int num_mfence;
extern unsigned int num_write_nontemporal;
extern unsigned int num_write_temporal;
extern unsigned int num_clflushopt;
extern unsigned int num_mmap;
extern unsigned int num_drs;
extern unsigned int num_drs_critical_path;
extern unsigned long long appendfsync_size;
extern unsigned long long non_temporal_write_size;
extern unsigned long long temporal_write_size;
extern unsigned long long read_size;
extern unsigned long long write_size;
extern unsigned long long memcpy_read_size;
extern unsigned long long anon_read_size;
extern unsigned long long memcpy_write_size;
extern unsigned long long append_write_size;
extern unsigned long long posix_read_size;
extern unsigned long long posix_write_size;
extern unsigned long long total_syscalls;
extern unsigned long long deleted_size;
extern volatile size_t _nvp_wr_extended;
extern volatile size_t _nvp_wr_total;
extern atomic_uint_fast64_t num_drs_left;

void nvp_init_io_stats(void);
void nvp_print_io_stats(void);

enum instrumentation_vars {
	open_t,      
	close_t,
	pread_t,
	pwrite_t,
	read_t,
	write_t,
	seek_t,
	fsync_t,
	unlink_t,
	bg_thread_t,
	clf_lock_t,
	node_lookup_lock_t,
	nvnode_lock_t,
	dr_mem_queue_t,
	file_mmap_t,
	close_syscall_t,
	copy_to_dr_pool_t,
	copy_to_mmap_cache_t,
	appends_t,
	clear_dr_t,
	swap_extents_t,
	give_up_node_t,
	get_mmap_t,
	get_dr_mmap_t,
	copy_overread_t,
	copy_overwrite_t,
	copy_appendread_t,
	copy_appendwrite_t,
	read_tbl_mmap_t,
	insert_tbl_mmap_t,
	clear_mmap_tbl_t,
	append_log_entry_t,
	op_log_entry_t,
	append_log_reinit_t,
	remove_overlapping_entry_t,
	device_t,
	soft_overhead_t,
	INSTRUMENT_NUM,
};

static atomic_uint_least64_t Instrustats[INSTRUMENT_NUM];
extern const char *Instruprint[INSTRUMENT_NUM];
typedef struct timespec instrumentation_type;

#define INITIALIZE_TIMERS()					\
	{							\
		int i;						\
		for (i = 0; i < INSTRUMENT_NUM; i++)		\
			Instrustats[i] = 0;			\
	}							\

#if INSTRUMENT_CALLS 

#define START_TIMING(name, start)				\
	{							\
		clock_gettime(CLOCK_MONOTONIC, &start);		\
        }

#define END_TIMING(name, start)						\
	{                                                               \
		instrumentation_type end;				\
		clock_gettime(CLOCK_MONOTONIC, &end);			\
		__atomic_fetch_add(&Instrustats[name], (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec), __ATOMIC_SEQ_CST); \
        }

#define PRINT_TIME()							\
	{								\
		int i;							\
		for(i=0; i<INSTRUMENT_NUM; i++)	{			\
			if (Instrustats[i] != 0) 			\
				MSG("%s: timing = %lu ms\n",		\
				    Instruprint[i], Instrustats[i] / 1000000); \
		}							\
	}


#else

#define START_TIMING(name, start) {(void)(start);}
#define END_TIMING(name, start) {(void)(start);}
#define PRINT_TIME() {(void)(Instrustats[0]);}
	
#endif

#endif
