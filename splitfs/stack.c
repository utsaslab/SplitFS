#include "stack.h"

void push_in_stack(int free_node_list, int free_lru_list, int idx_in_list, int list_idx) {

	if (free_lru_list)
		STACK_LOCK_WR();

	if (free_node_list) {
		_nvp_free_node_list[list_idx][idx_in_list].free_bit = 1;
		_nvp_free_node_list[list_idx][idx_in_list].next_free_idx = _nvp_free_node_list_head[list_idx];
		_nvp_free_node_list_head[list_idx] = idx_in_list;
	} else if (free_lru_list) {
		_nvp_free_lru_list[idx_in_list].free_bit = 1;
		_nvp_free_lru_list[idx_in_list].next_free_idx = _nvp_free_lru_list_head;
		_nvp_free_lru_list_head = idx_in_list;
	} else {
		if (free_lru_list)
			STACK_UNLOCK_WR();	
		return;
	}

	if (free_lru_list)
		STACK_UNLOCK_WR();
}


int pop_from_stack(int free_node_list, int free_lru_list, int list_idx) {

	int idx_in_list = -1;
	int candidate = -1;

	if (free_lru_list)
		STACK_LOCK_WR();
	
	if (free_node_list) {
		while (_nvp_free_node_list[list_idx][_nvp_free_node_list_head[list_idx]].free_bit != 1 && _nvp_free_node_list_head[list_idx] != -1) {

			if(candidate == -1 && _nvp_node_lookup[list_idx][_nvp_free_node_list_head[list_idx]].reference == 0)
				candidate = _nvp_free_node_list_head[list_idx];
			
			_nvp_free_node_list_head[list_idx] = _nvp_free_node_list[list_idx][_nvp_free_node_list_head[list_idx]].next_free_idx;
		}
	       
		if (_nvp_free_node_list_head[list_idx] == -1) 
			goto candidate_lookup;

		if (_nvp_free_node_list[list_idx][_nvp_free_node_list_head[list_idx]].free_bit == 1) {
			idx_in_list = _nvp_free_node_list_head[list_idx];
			_nvp_free_node_list[list_idx][idx_in_list].free_bit = 0;
			_nvp_free_node_list_head[list_idx] = _nvp_free_node_list[list_idx][idx_in_list].next_free_idx;
			goto out;
		}

	} else if (free_lru_list) {
		while (_nvp_free_lru_list[_nvp_free_lru_list_head].free_bit != 1 && _nvp_free_lru_list_head != -1)
			_nvp_free_lru_list_head = _nvp_free_lru_list[_nvp_free_lru_list_head].next_free_idx;		
	       
		if (_nvp_free_lru_list_head == -1)
			return -1;

		idx_in_list = _nvp_free_lru_list_head;
		_nvp_free_lru_list[idx_in_list].free_bit = 0;
		_nvp_free_lru_list_head = _nvp_free_lru_list[idx_in_list].next_free_idx;

		goto out;
	}

 candidate_lookup:
	if (candidate != -1) {
		_nvp_free_node_list[list_idx][candidate].free_bit = 0;
		_nvp_free_node_list_head[list_idx] = _nvp_free_node_list[list_idx][candidate].next_free_idx;
	}

	idx_in_list = candidate;
	
 out:
	if (free_lru_list)
		STACK_UNLOCK_WR();

	return idx_in_list;
}
