#include <sys/time.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include "timers.h"

std::atomic<uint64_t> Instrustats[INSTRUMENT_NUM];

const char *Instruprint[INSTRUMENT_NUM] =
 {
	 "memtable_read",
	 "imm_memtable_read",
	 "file_read",
	 "file_open_during_read",
	 "file_close_during_read",
	 "get_files",
	 "after_get_files",
	 "find_table",
	 "block_reader",
	 "read_block",
	 "insert_block_cache",
	 "new_iterator",
 };

