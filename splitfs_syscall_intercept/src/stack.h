#ifndef LEDGER_SRC_STACK_H_
#define LEDGER_SRC_STACK_H_

#include <nv_common.h>
#include "inode.h"

#define STACK_LOCK_WR()    {(void)(stack_lock);}
#define STACK_UNLOCK_WR()  {(void)(stack_lock);}

/* 
 * Declare the structure that will hold information of the files that are to be closed 
 */
struct StackNode {
	int free_bit;
	int next_free_idx;
};

/*
 * Global variables to hold the head and tail of LRU list 
 */
struct StackNode *_nvp_free_node_list[NUM_NODE_LISTS];
struct StackNode *_nvp_free_lru_list;
int _nvp_free_node_list_head[NUM_NODE_LISTS];
int _nvp_free_lru_list_head;
struct NVNode *_nvp_node_lookup[NUM_NODE_LISTS];
struct backupRoots *_nvp_backup_roots[NUM_NODE_LISTS];
pthread_spinlock_t stack_lock;

void push_in_stack(int free_node_list, int free_lru_list, int idx_in_list, int list_idx);
int pop_from_stack(int free_node_list, int free_lru_list, int list_idx);

#endif
