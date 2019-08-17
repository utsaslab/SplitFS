#ifndef LEDGER_SRC_LRU_H_
#define LEDGER_SRC_LRU_H_

#include "fileops_nvp.h"
#include "stack.h"
#include <stdatomic.h>

/* 
 * Declare the structure that will hold information of the files that are to be closed 
 */
struct ClosedFiles {	
	NVP_LOCK_DECL;
	int fd;
	ino_t serialno;  
	int index_in_free_list;
	int next_closed_file;
	int prev_closed_file;
};

struct InodeClosedFile {
	NVP_LOCK_DECL;
	int index;
};

/*
 * Declare the hash table that will map inode number to the node in the LRU list 
 */
struct InodeClosedFile *inode_to_closed_file;
atomic_uint_fast64_t dr_mem_closed_files;
atomic_uint_fast64_t dr_mem_allocated;
atomic_uint_fast64_t num_files_closed;
pthread_spinlock_t global_lock_lru_head;

/*
 * Global variables to hold the head and tail of LRU list 
 */
struct ClosedFiles *_nvp_closed_files;
int lru_head;
int lru_tail;
int lru_tail_serialno;

int insert_in_seq_list(struct ClosedFiles *node, ino_t *stale_serialno, int fd, ino_t serialno);
int insert_in_lru_list(int fd, ino_t serialno, ino_t *stale_serialno);
int remove_from_lru_list_hash(ino_t serialno, int lock_held);
int remove_from_lru_list_policy(ino_t *serialno);
int remove_from_seq_list(struct ClosedFiles *node, ino_t *serialno);
int remove_from_seq_list_hash(struct ClosedFiles *node, ino_t serialno);

#endif
