
#ifndef __LEDGER_NVP_LOCK_H_
#define __LEDGER_NVP_LOCK_H_

#include <stdint.h>

// This file describes a custom type of RW locks which are very fast in the read case
// but very slow in the write case.
// One lock exists per logical core.  On read, a processor simply gets a rdlock on
// the lock with its number.  On write, a processor must sequentially acquire a
// wrlock on every processor.

#define cpuid(func,ax,bx,cx,dx)\
	__asm__ __volatile__ ("cpuid":\
	"=a" (ax), "=b" (bx), "=c" (cx), "=d" (dx) : "a" (func));

static inline int _nvp_get_cpuid(void)
{
	uint32_t eax=0; // output: eax
	uint32_t ebx=0; // output: ebx
	uint32_t ecx=1; // output: ecx
	uint32_t edx=0; // output: edx

	cpuid(0x0B, eax,ebx,ecx,edx);

	int id = (((edx&1)<<3) + ((edx>>4)&1) + (edx&0xe));

	return id;
}

static inline int return_zero(void)
{
	return 0;
}

#ifndef USE_PTHREAD_LOCK
	#define	USE_PTHREAD_LOCK 1
#endif

#ifndef USE_SCHED_GETCPU
	#define	USE_SCHED_GETCPU 1
#endif

#if	USE_SINGLE_LOCK
	#define	GET_CPUID	return_zero
#elif	USE_SCHED_GETCPU
	#define	GET_CPUID	sched_getcpu
#else
	#define	GET_CPUID	_nvp_get_cpuid
#endif

#if	USE_SINGLE_LOCK
	#define NVP_NUM_LOCKS	2
#else
	#define NVP_NUM_LOCKS	32
#endif

// double the number of logical cores: each lock takes up half a cache line, so to
// reduce contention we space them out across cache lines.
#if	USE_PTHREAD_LOCK

#define NVP_LOCK_DECL pthread_rwlock_t lock[NVP_NUM_LOCKS]

#define NVP_LOCK_INIT(lock) { int iter; for(iter=0; iter<NVP_NUM_LOCKS; iter++) { pthread_rwlock_init(&lock[iter], NULL); } }

#define NVP_EXTENT_LOCK_DECL pthread_rwlock_t extent_lock

#define NVP_LOCK_INIT_EXTENT_LOCK(lock) { pthread_rwlock_init(&lock, NULL); }

#define NVP_LOCK_DO_CHECKING 1
#if NVP_LOCK_DO_CHECKING
	#define NVP_LOCK_CHECK(statement) { \
		int result = statement; \
		if(result) { \
			ERROR("Failed to perform lock operation (see following error message)\n"); \
			PRINT_ERROR_NAME(result); \
			assert(0); \
		} \
		}
#else
	#define NVP_LOCK_CHECK(statement) statement
#endif

#define SANITY assert


#define NVP_LOCK_RD(lock, cpuid) \
	SANITY(cpuid<(NVP_NUM_LOCKS/2)); \
	DEBUG("NVP_RDLOCK requested on CPU %i, lock %p\n", cpuid, &lock); \
	NVP_LOCK_CHECK(pthread_rwlock_rdlock(&lock[cpuid*2])); \
	DEBUG("NVP_RDLOCK acquired on CPU %i, lock %p\n", cpuid, &lock)

#define NVP_LOCK_UNLOCK_RD(lock, cpuid) \
	DEBUG("Releasing NVP_RDLOCK on CPU %i, lock %p\n", cpuid, &lock); \
	SANITY(cpuid<(NVP_NUM_LOCKS/2)); \
	NVP_LOCK_CHECK(pthread_rwlock_unlock(&lock[cpuid*2])); \
	DEBUG("NVP_RDLOCK released on CPU %i, lock %p\n", cpuid, &lock)


#define NVP_LOCK_WR(lock) {						\
		int iter;						\
		DEBUG("NVP_WRLOCK requested on cpu %i, lock %p\n", GET_CPUID(), &lock); \
		for(iter=0; iter<NVP_NUM_LOCKS; iter+=2) {		\
			NVP_LOCK_CHECK(pthread_rwlock_wrlock(&lock[iter])); \
		}							\
		DEBUG("NVP_WRLOCK acquired on cpu %i, lock %p\n", GET_CPUID(), &lock); \
	}

#define NVP_LOCK_UNLOCK_WR(lock) {					\
		int iter;						\
		DEBUG("NVP_WRLOCK releasing on cpu %i, lock %p\n", GET_CPUID(), &lock); \
		for(iter=0; iter<NVP_NUM_LOCKS; iter+=2) {		\
			NVP_LOCK_CHECK(pthread_rwlock_unlock(&lock[iter])); \
		}							\
		DEBUG("NVP_WRLOCK released on cpu %i, lock %p\n", GET_CPUID(), &lock); \
	}

#define NVP_LOCK_EXTENT_TREE(lock) \
	NVP_LOCK_CHECK(pthread_rwlock_wrlock(&lock));

#define NVP_UNLOCK_EXTENT_TREE(lock) \
	NVP_LOCK_CHECK(pthread_rwlock_unlock(&lock));


	//	DEBUG("NVP_WR Locking %i\n", iter);
	//	DEBUG("NVP_WR Unlocking %i\n", iter);

#else

#define WR_HELD	(1 << 30)

#define NVP_LOCK_DECL uint32_t lock[NVP_NUM_LOCKS * 16]

#define NVP_LOCK_INIT(lock) { int iter; for(iter=0; iter<NVP_NUM_LOCKS * 16; iter++) { lock[iter] = 0; } }

#define NVP_EXTENT_LOCK_DECL uint32_t extent_lock

#define NVP_LOCK_INIT_EXTENT_LOCK(lock) { lock = 0; }

#define NVP_LOCK_CHECK(statement) statement

#define SANITY assert

#define NVP_LOCK_RD(lock, cpuid) \
	SANITY(cpuid<(NVP_NUM_LOCKS/2)); \
	DEBUG("NVP_RDLOCK requested on CPU %i, lock %p\n", cpuid, &lock); \
	while(__sync_fetch_and_add(&lock[cpuid * 2 * 16], 1) >= WR_HELD) \
		__sync_fetch_and_sub(&lock[cpuid * 2 * 16], 1); \
	DEBUG("NVP_RDLOCK acquired on CPU %i, lock %p\n", cpuid, &lock)

#define NVP_LOCK_UNLOCK_RD(lock, cpuid) \
	DEBUG("NVP_RDLOCK releasing on CPU %i, lock %p\n", cpuid, &lock); \
	SANITY(cpuid<(NVP_NUM_LOCKS/2)); \
	__sync_fetch_and_sub(&lock[cpuid * 2 * 16], 1); \
	DEBUG("NVP_RDLOCK released on CPU %i, lock %p\n", cpuid, &lock)


#define NVP_LOCK_WR(lock) { int iter; \
	DEBUG("NVP_WRLOCK requested on cpu %i, lock %p\n", GET_CPUID(), &lock); \
	for(iter=0; iter<NVP_NUM_LOCKS; iter+=2) {			\
		while(!__sync_bool_compare_and_swap(&lock[iter * 16], 0, WR_HELD)) \
			;						\
	}								\
	DEBUG("NVP_WRLOCK acquired on cpu %i, lock %p\n", GET_CPUID(), &lock); \
	}								

#define NVP_LOCK_UNLOCK_WR(lock) { int iter; \
	DEBUG("NVP_WRLOCK releasing on cpu %i, lock %p\n", GET_CPUID(), &lock); \
	for(iter=0; iter<NVP_NUM_LOCKS; iter+=2) {			\
		__sync_fetch_and_sub(&lock[iter * 16], WR_HELD);	\
	}								\
	DEBUG("NVP_WRLOCK released on cpu %i, lock %p\n", GET_CPUID(), &lock); \
	}

#define NVP_LOCK_EXTENT_TREE(lock) \
		while(!__sync_bool_compare_and_swap(&lock, 0, WR_HELD)) \
			;

#define NVP_UNLOCK_EXTENT_TREE(lock) \
		__sync_fetch_and_sub(&lock, WR_HELD);

	//	DEBUG("NVP_WR Locking %i\n", iter);
	//	DEBUG("NVP_WR Unlocking %i\n", iter);

#endif

#endif
