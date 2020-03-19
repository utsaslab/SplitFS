#ifndef LEDGER_SRC_MMAN_H_
#define LEDGER_SRC_MMAN_H_

// Some functions for tinkering with the cache

#include "ledger.h"

/*
void _mm_clflush(void const*p) - Cache line containing p is flushed and invalidated from all caches in the coherency domain.

void _mm_lfence(void) - Guarantees that every load instruction that precedes, in program order, the load fence instruction is globally visible before any load instruction that follows the fence in program order.

_mm_sfence(void) - Guarantees that every preceding store is globally visible before any subsequent store.

void _mm_mfence(void) - Guarantees that every memory access that precedes, in program order, the memory fence instruction is globally visible before any memory instruction that follows the fence in program order.
*/

// TODO: what should this size be?

static inline void do_cflush_len(volatile void* addr, size_t length)
{
	// note: it's necessary to do an mfence before and after calling this function

	size_t i;
	for (i = 0; i < length; i += 64) {
		_mm_flush((void const*)(addr + i));
	}
}

#endif
