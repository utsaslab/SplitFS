#ifndef LEVELDB_INCLUDE_TIMERS_H_
#define LEVELDB_INCLUDE_TIMERS_H_

#include <sys/time.h>
#include <string.h>
#include <time.h>
#include <atomic>

enum instrumentation_vars {
	memtable_t,
	imm_memtable_t,
	db_read_t,
	db_open_read_t,
	db_close_read_t,
	get_files_t,
	after_get_files_t,
	find_table_t,
	block_reader_t, 
	read_block_t,
	insert_block_cache_t,
	new_iterator_t,
	INSTRUMENT_NUM,
};

extern std::atomic<uint64_t> Instrustats[INSTRUMENT_NUM];
extern const char *Instruprint[INSTRUMENT_NUM];

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
	        Instrustats[name].fetch_add((end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec)); \
        }

#define PRINT_TIME()							\
	{								\
		int i;							\
		for(i=0; i<INSTRUMENT_NUM; i++)				\
			std::cout << Instruprint[i] << ": timing = " << Instrustats[i] << " nanoseconds\n"; \
	}


#else

#define START_TIMING(start) {(void)(start);}
#define END_TIMING(start) {(void)(start);}
#define PRINT_TIME(name) {(void)(name);}
	
#endif

#endif

