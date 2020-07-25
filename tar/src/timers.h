#ifndef _LEDGER_TIMERS_H_
#define _LEDGER_TIMERS_H_

#include <sys/time.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

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
	anon_mem_queue_t,
	get_map_t,
	copy_data_t,
	close_syscall_t,
	copy_to_anon_pool_t,
	copy_to_mmap_cache_t,
	give_up_node_t,
	experimentation_t,
	INSTRUMENT_NUM,
};

static atomic_uint_least64_t Instrustats[INSTRUMENT_NUM];
const char *Instruprint[INSTRUMENT_NUM] =
{
	"open",
	"close",
	"pread",
	"pwrite",
	"read",
	"write",
	"seek",
	"fsync",
	"unlink",
	"bg_thread",
	"clf_lock",
	"node_lookup_lock",
	"nvnode_lock",
	"anon_mem_queue",
	"get_map",
	"copy_data",
	"close_syscall",
	"copy_to_anon_pool",
	"copy_to_mmap_cache",
	"give_up_node",
	"experimentation",

};

typedef struct timespec instrumentation_type;

#define INSTRUMENT_CALLS 1


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
			if (Instrustats[i] != 0)			\
				printf("%s: timing = %lu nanoseconds\n", \
			       Instruprint[i], Instrustats[i]);		\
		}							\
	}

#else

#define START_TIMING(name, start) {(void)(start);}
#define END_TIMING(name, start) {(void)(start);}
#define PRINT_TIME() {(void)(Instrustats[0]);}
	
#endif

#endif
