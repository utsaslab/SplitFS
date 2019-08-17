#include "lru_cache.h"


int insert_in_seq_list(struct ClosedFiles *node, ino_t *stale_serialno, int fd, ino_t serialno) {

	int stale_fd = -1;

	if (node->fd != -1) {
		stale_fd = node->fd;
		*stale_serialno = node->serialno;
	}

	node->fd = fd;
	node->serialno = serialno;

	return stale_fd;
}


/*
 * Insert a new node in the LRU cache at the head position 
 */
int insert_in_lru_list(int fd, ino_t serialno, ino_t *stale_serialno) {

	struct ClosedFiles *node = NULL;
	struct ClosedFiles *node_to_be_removed = NULL;
	int stale_fd = -1;
	int hash_index = -1;
	int idx_in_free_list = -1;

	LRU_LOCK_HEAD_WR();	

	idx_in_free_list = pop_from_stack(0, 1, -1);			
	if (idx_in_free_list != -1) {

		node = (struct ClosedFiles *)&_nvp_closed_files[idx_in_free_list];	
		node->fd = fd;
		node->serialno = serialno;
		node->index_in_free_list = idx_in_free_list;
		node->next_closed_file = -1;
		node->prev_closed_file = -1;
		
		if (lru_head == -1) {
			lru_head = node->index_in_free_list;
			lru_tail = node->index_in_free_list;
			lru_tail_serialno = node->serialno;
		} else if (lru_tail == -1) {
			lru_tail = lru_head;
			lru_tail_serialno = _nvp_closed_files[lru_tail].serialno;
		} else {
			node->next_closed_file = lru_head;		
			_nvp_closed_files[lru_head].prev_closed_file = node->index_in_free_list;
			lru_head = node->index_in_free_list;
		}

		hash_index = serialno % 1024;

		if (inode_to_closed_file[hash_index].index != -1) {
			node_to_be_removed = (struct ClosedFiles *)&_nvp_closed_files[inode_to_closed_file[hash_index].index];
			stale_fd = remove_from_lru_list_hash(node_to_be_removed->serialno, 1);
			*stale_serialno = node_to_be_removed->serialno;			
		}
		
		inode_to_closed_file[hash_index].index = node->index_in_free_list;
	}

	LRU_UNLOCK_HEAD_WR();

	return stale_fd;
}

/*
 * Remove a node from the LRU cache, searching based on inode number
 */
int remove_from_lru_list_hash(ino_t serialno, int lock_held) {

	int hash_index = -1, fd = -1, prev_node_idx = -1, next_node_idx = -1;
	struct ClosedFiles *node = NULL, *prev_node = NULL, *next_node = NULL;
	int lock_set = 0;
	
	hash_index = serialno % 1024;

	if (!lock_held) {
		LRU_LOCK_HEAD_WR();
		lock_set = 1;
	}
		
	if (inode_to_closed_file[hash_index].index != -1) {

		node = (struct ClosedFiles *)&_nvp_closed_files[inode_to_closed_file[hash_index].index];
		
		if (node->serialno == serialno) {

			prev_node_idx = node->prev_closed_file;
			next_node_idx = node->next_closed_file;
			if (prev_node_idx >= 0) {
				prev_node = (struct ClosedFiles *)&_nvp_closed_files[prev_node_idx];
			}

			if (next_node_idx >= 0) {
				next_node = (struct ClosedFiles *)&_nvp_closed_files[next_node_idx];
			}

			fd = node->fd;

			if (prev_node_idx != -1) 				
			        prev_node->next_closed_file = next_node_idx;
			if (next_node_idx != -1)
			        next_node->prev_closed_file = prev_node_idx;
			if (node->index_in_free_list == lru_head) {
				lru_head = node->next_closed_file;
				_nvp_closed_files[lru_head].prev_closed_file = -1;
			}
			if (node->index_in_free_list == lru_tail) {
				lru_tail = node->prev_closed_file;
				lru_tail_serialno = _nvp_closed_files[lru_tail].serialno;
				_nvp_closed_files[lru_tail].next_closed_file = -1;
			}

			node->prev_closed_file = -1;
			node->next_closed_file = -1;
			node->fd = -1;
			node->serialno = 0;

			if(!lock_held) {
				LRU_UNLOCK_HEAD_WR();
				lock_set = 0;
			}

			inode_to_closed_file[hash_index].index = -1;
			push_in_stack(0, 1, node->index_in_free_list, -1);
		}
	}

	if(!lock_held && lock_set) {
		LRU_UNLOCK_HEAD_WR();
		lock_set = 0;
	}

	return fd;
}

/* 
 * Remove a node from LRU cache based on LRU policy for background thread 
 */
int remove_from_lru_list_policy(ino_t *serialno) {

	int hash_index = -1;
	ino_t local_serialno = 0;
	int fd = -1;
	struct ClosedFiles *node = NULL;
	
	LRU_LOCK_HEAD_WR();

	node = (struct ClosedFiles *)&_nvp_closed_files[lru_tail];

	local_serialno = node->serialno;
	hash_index = local_serialno % 1024;
		
	lru_tail = node->prev_closed_file;
	if (lru_tail != -1) {
		lru_tail_serialno = _nvp_closed_files[lru_tail].serialno;
		_nvp_closed_files[lru_tail].next_closed_file = -1;
	} else
		lru_tail_serialno = 0;			

	fd = node->fd;
	*serialno = local_serialno;

	node->next_closed_file = -1;
	node->prev_closed_file = -1;
	node->fd = -1;
	node->serialno = 0;
	
	inode_to_closed_file[hash_index].index = -1;
	push_in_stack(0, 1, node->index_in_free_list, -1);

	LRU_UNLOCK_HEAD_WR();	

	return fd;
}

/* 
 * Remove a node from LRU cache based on LRU policy for background thread 
 */
int remove_from_seq_list(struct ClosedFiles *node, ino_t *serialno) {

	int fd = -1;
	
	fd = node->fd;
	*serialno = node->serialno;
	
	node->next_closed_file = -1;
	node->prev_closed_file = -1;
	node->fd = -1;
	node->serialno = 0;

	return fd;
}


int remove_from_seq_list_hash(struct ClosedFiles *node, ino_t serialno) {

	int fd = -1;
	
	if(node->serialno == serialno) {
		fd = node->fd;	
		node->next_closed_file = -1;
		node->prev_closed_file = -1;
		node->fd = -1;
		node->serialno = 0;
	}
	
	return fd;
}

