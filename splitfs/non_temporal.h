#ifndef __LEDGER_NON_TEMPORAL_H_
#define __LEDGER_NON_TEMPORAL_H_

#define _GNU_SOURCE 
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <inttypes.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <emmintrin.h>
#include <stdbool.h>
//#include "nv_common.h"

#define CHUNK_SIZE_GRANULARITY 64
#define CHUNK_SIZE      128 /* 16*8 */
#define CHUNK_SHIFT     7
#define CHUNK_SHIFT_GRANULARITY 6
#define CHUNK_MASK      (CHUNK_SIZE - 1)
#define CHUNK_MASK_GRANULARITY (CHUNK_SIZE_GRANULARITY - 1)

#define DWORD_SIZE      4
#define DWORD_SHIFT     2
#define DWORD_MASK      (DWORD_SIZE - 1)

#define MOVNT_SIZE      16
#define MOVNT_MASK      (MOVNT_SIZE - 1)
#define MOVNT_SHIFT     4
#define FLUSH_ALIGN	((uintptr_t)64)
#define ALIGN_MASK      (FLUSH_ALIGN - 1)

#define MOVNT_THRESHOLD 256
#define MOVNT_THRESHOLD_GRANULARITY 64

#define CLFLUSH_SIZE 64
#define _mm_clflushopt(addr)\
	asm volatile("clflushopt %0" : "+m" (*(volatile char *)(addr)))


char *addr;
struct timeval start,end;
int fd; 
void *memmove_nodrain_movnt_granularity(void *pmemdest, const void *src, size_t len);

#if 0

static size_t Movnt_threshold = MOVNT_THRESHOLD;

static void
predrain_memory_barrier(void)
{
  	_mm_mfence();   /* ensure CLWB or CLFLUSHOPT completes */
}


static void
flush_dcache_invalidate_opt(const void *addr, size_t len)
{
        uintptr_t uptr;

        for (uptr = (uintptr_t)addr & ~(FLUSH_ALIGN - 1);
                uptr < (uintptr_t)addr + len; uptr += FLUSH_ALIGN) {
                _mm_clflushopt((char *)uptr);
        }
}
*/

/*
 * pmem_flush -- flush processor cache for the given range
 */

static void
pmem_flush(const void *addr, size_t len)
{
        flush_dcache_invalidate_opt(addr, len);
}


static void *
memmove_nodrain_movnt_granularity(void *pmemdest, const void *src, size_t len)
{
        __m128i xmm0, xmm1, xmm2, xmm3;
        size_t i;
        __m128i *d;
        __m128i *s;
        void *dest1 = pmemdest;
        size_t cnt;

     	//predrain_memory_barrier();
       
        if (len == 0 || src == pmemdest)
                return pmemdest;

        if (len < Movnt_threshold_granularity) {
                memmove(pmemdest, src, len);
                pmem_flush(pmemdest, len);
                return pmemdest;
        }

        if ((uintptr_t)dest1 - (uintptr_t)src >= len) {
                /*
                 * Copy the range in the forward direction.
                 *
                 * This is the most common, most optimized case, used unless
                 * the overlap specifically prevents it.
                 */
                /* copy up to FLUSH_ALIGN boundary */
                cnt = (uint64_t)dest1 & ALIGN_MASK;
                if (cnt > 0) {
                        cnt = FLUSH_ALIGN - cnt;

                        /* never try to copy more the len bytes */
                        if (cnt > len)
                                cnt = len;

                        uint8_t *d8 = (uint8_t *)dest1;
                        const uint8_t *s8 = (uint8_t *)src;
                        for (i = 0; i < cnt; i++) {
                                *d8 = *s8;
                                d8++;
                                s8++;
                        }
                        pmem_flush(dest1, cnt);
                        dest1 = (char *)dest1 + cnt;
                        src = (char *)src + cnt;
                        len -= cnt;
		}

                d = (__m128i *)dest1;
                s = (__m128i *)src;

                cnt = len >> CHUNK_SHIFT_GRANULARITY;
                for (i = 0; i < cnt; i++) {
                        xmm0 = _mm_loadu_si128(s);
                        xmm1 = _mm_loadu_si128(s + 1);
                        xmm2 = _mm_loadu_si128(s + 2);
                        xmm3 = _mm_loadu_si128(s + 3);
                        s += 4;
                        _mm_stream_si128(d,     xmm0);
                        _mm_stream_si128(d + 1, xmm1);
                        _mm_stream_si128(d + 2, xmm2);
                        _mm_stream_si128(d + 3, xmm3);
                        d += 4;
                }

                /* copy the tail (<128 bytes) in 16 bytes chunks */
                len &= CHUNK_MASK_GRANULARITY;
                if (len != 0) {
                        cnt = len >> MOVNT_SHIFT;
                        for (i = 0; i < cnt; i++) {
                                xmm0 = _mm_loadu_si128(s);
                                _mm_stream_si128(d, xmm0);
                                s++;
                                d++;
                        }
                }
                len &= MOVNT_MASK;
                if (len != 0) {
                        cnt = len >> DWORD_SHIFT;
                        int32_t *d32 = (int32_t *)d;
                        int32_t *s32 = (int32_t *)s;
                        for (i = 0; i < cnt; i++) {
                                _mm_stream_si32(d32, *s32);
                                d32++;
                                s32++;
                        }
                        cnt = len & DWORD_MASK;
                        uint8_t *d8 = (uint8_t *)d32;
                        const uint8_t *s8 = (uint8_t *)s32;

                        for (i = 0; i < cnt; i++) {
                                *d8 = *s8;
                                d8++;
                                s8++;
                        }
                        pmem_flush(d32, cnt);
		
                /* copy the last bytes (<16), first dwords then bytes */

		}
        } else {
                /*
                 * Copy the range in the backward direction.
                 *
                 * This prevents overwriting source data due to an
                 * overlapped destination range.
                 */

                dest1 = (char *)dest1 + len;
                src = (char *)src + len;

                cnt = (uint64_t)dest1 & ALIGN_MASK;
                if (cnt > 0) {
                        /* never try to copy more the len bytes */
                        if (cnt > len)
                                cnt = len;

                        uint8_t *d8 = (uint8_t *)dest1;
                        const uint8_t *s8 = (uint8_t *)src;
                        for (i = 0; i < cnt; i++) {
                                d8--;
                                s8--;
                                *d8 = *s8;
                        }
                        pmem_flush(d8, cnt);
                        dest1 = (char *)dest1 - cnt;
                        src = (char *)src - cnt;
                        len -= cnt;
                }

                d = (__m128i *)dest1;
                s = (__m128i *)src;

                cnt = len >> CHUNK_SHIFT_GRANULARITY;
                for (i = 0; i < cnt; i++) {
                        xmm0 = _mm_loadu_si128(s - 1);
                        xmm1 = _mm_loadu_si128(s - 2);
                        xmm2 = _mm_loadu_si128(s - 3);
                        _mm_stream_si128(d - 4, xmm3);
                        d -= 4;
                }

                /* copy the tail (<128 bytes) in 16 bytes chunks */
                len &= CHUNK_MASK_GRANULARITY;
                if (len != 0) {
                        cnt = len >> MOVNT_SHIFT;
                        for (i = 0; i < cnt; i++) {
                                d--;
                                s--;
                                xmm0 = _mm_loadu_si128(s);
                                _mm_stream_si128(d, xmm0);
                        }
                }

                /* copy the last bytes (<16), first dwords then bytes */
                len &= MOVNT_MASK;
                if (len != 0) {
                        cnt = len >> DWORD_SHIFT;
                        int32_t *d32 = (int32_t *)d;
                        int32_t *s32 = (int32_t *)s;
                        for (i = 0; i < cnt; i++) {
                                d32--;
                                s32--;
                                _mm_stream_si32(d32, *s32);
                        }

                        cnt = len & DWORD_MASK;
                        uint8_t *d8 = (uint8_t *)d32;
                        const uint8_t *s8 = (uint8_t *)s32;

                        for (i = 0; i < cnt; i++) {
                                d8--;
                                s8--;
                                *d8 = *s8;
                        }
                        pmem_flush(d8, cnt);
                }
        }

        /*
         * The call to pmem_*_nodrain() should be followed by pmem_drain()
         * to serialize non-temporal store instructions.  (It could be only
         * one drain after a sequence of pmem_*_nodrain calls).
         * However, on platforms that only support strongly-ordered CLFLUSH
         * for flushing the CPU cache (or that are forced to not use
         * CLWB/CLFLUSHOPT) there is no need to put any memory barrier after
         * the flush, so the pmem_drain() is a no-op function.  In such case,
         * we need to put a memory barrier here.
         */
     	//predrain_memory_barrier();

        return pmemdest;
}

/*
 * memmove_nodrain_movnt -- (internal) memmove to pmem without hw drain, movnt
 */
static void *
memmove_nodrain_movnt(void *pmemdest, const void *src, size_t len)
{
        __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;
        size_t i;
        __m128i *d;
        __m128i *s;
        void *dest1 = pmemdest;
        size_t cnt;

        if (len == 0 || src == pmemdest)
                return pmemdest;

        if (len < Movnt_threshold) {
                memmove(pmemdest, src, len);
                pmem_flush(pmemdest, len);
                return pmemdest;
        }

        if ((uintptr_t)dest1 - (uintptr_t)src >= len) {
                /*
                 * Copy the range in the forward direction.
                 *
                 * This is the most common, most optimized case, used unless
                 * the overlap specifically prevents it.
                 */
                /* copy up to FLUSH_ALIGN boundary */
                cnt = (uint64_t)dest1 & ALIGN_MASK;
                if (cnt > 0) {
                        cnt = FLUSH_ALIGN - cnt;

                        /* never try to copy more the len bytes */
                        if (cnt > len)
                                cnt = len;

                        uint8_t *d8 = (uint8_t *)dest1;
                        const uint8_t *s8 = (uint8_t *)src;
                        for (i = 0; i < cnt; i++) {
                                *d8 = *s8;
                                d8++;
                                s8++;
                        }
                        pmem_flush(dest1, cnt);
                        dest1 = (char *)dest1 + cnt;
                        src = (char *)src + cnt;
                        len -= cnt;
                }

                d = (__m128i *)dest1;
                s = (__m128i *)src;

                cnt = len >> CHUNK_SHIFT;
                for (i = 0; i < cnt; i++) {
                        xmm0 = _mm_loadu_si128(s);
                        xmm1 = _mm_loadu_si128(s + 1);
                        xmm2 = _mm_loadu_si128(s + 2);
                        xmm3 = _mm_loadu_si128(s + 3);
                        xmm4 = _mm_loadu_si128(s + 4);
                        xmm5 = _mm_loadu_si128(s + 5);
                        xmm6 = _mm_loadu_si128(s + 6);
                        xmm7 = _mm_loadu_si128(s + 7);
                        s += 8;
                        _mm_stream_si128(d,     xmm0);
                        _mm_stream_si128(d + 1, xmm1);
                        _mm_stream_si128(d + 2, xmm2);
                        _mm_stream_si128(d + 3, xmm3);
                        _mm_stream_si128(d + 4, xmm4);
                        _mm_stream_si128(d + 5, xmm5);
                        _mm_stream_si128(d + 6, xmm6);
                        _mm_stream_si128(d + 7, xmm7);
                        d += 8;
                }

                /* copy the tail (<128 bytes) in 16 bytes chunks */
                len &= CHUNK_MASK;
                if (len != 0) {
                        cnt = len >> MOVNT_SHIFT;
                        for (i = 0; i < cnt; i++) {
                                xmm0 = _mm_loadu_si128(s);
                                _mm_stream_si128(d, xmm0);
                                s++;
                                d++;
                        }
                }
                len &= MOVNT_MASK;
                if (len != 0) {
                        cnt = len >> DWORD_SHIFT;
                        int32_t *d32 = (int32_t *)d;
                        int32_t *s32 = (int32_t *)s;
                        for (i = 0; i < cnt; i++) {
                                _mm_stream_si32(d32, *s32);
                                d32++;
                                s32++;
                        }
                        cnt = len & DWORD_MASK;
                        uint8_t *d8 = (uint8_t *)d32;
                        const uint8_t *s8 = (uint8_t *)s32;

                        for (i = 0; i < cnt; i++) {
                                *d8 = *s8;
                                d8++;
                                s8++;
                        }
                        pmem_flush(d32, cnt);
		
                /* copy the last bytes (<16), first dwords then bytes */

		}
        } else {
                /*
                 * Copy the range in the backward direction.
                 *
                 * This prevents overwriting source data due to an
                 * overlapped destination range.
                 */

                dest1 = (char *)dest1 + len;
                src = (char *)src + len;

                cnt = (uint64_t)dest1 & ALIGN_MASK;
                if (cnt > 0) {
                        /* never try to copy more the len bytes */
                        if (cnt > len)
                                cnt = len;

                        uint8_t *d8 = (uint8_t *)dest1;
                        const uint8_t *s8 = (uint8_t *)src;
                        for (i = 0; i < cnt; i++) {
                                d8--;
                                s8--;
                                *d8 = *s8;
                        }
                        pmem_flush(d8, cnt);
                        dest1 = (char *)dest1 - cnt;
                        src = (char *)src - cnt;
                        len -= cnt;
                }

                d = (__m128i *)dest1;
                s = (__m128i *)src;

                cnt = len >> CHUNK_SHIFT;
                for (i = 0; i < cnt; i++) {
                        xmm0 = _mm_loadu_si128(s - 1);
                        xmm1 = _mm_loadu_si128(s - 2);
                        xmm2 = _mm_loadu_si128(s - 3);
                        xmm3 = _mm_loadu_si128(s - 4);
                        xmm4 = _mm_loadu_si128(s - 5);
                        xmm5 = _mm_loadu_si128(s - 6);
                        _mm_stream_si128(d - 7, xmm6);
                        _mm_stream_si128(d - 8, xmm7);
                        d -= 8;
                }

                /* copy the tail (<128 bytes) in 16 bytes chunks */
                len &= CHUNK_MASK;
                if (len != 0) {
                        cnt = len >> MOVNT_SHIFT;
                        for (i = 0; i < cnt; i++) {
                                d--;
                                s--;
                                xmm0 = _mm_loadu_si128(s);
                                _mm_stream_si128(d, xmm0);
                        }
                }

                /* copy the last bytes (<16), first dwords then bytes */
                len &= MOVNT_MASK;
                if (len != 0) {
                        cnt = len >> DWORD_SHIFT;
                        int32_t *d32 = (int32_t *)d;
                        int32_t *s32 = (int32_t *)s;
                        for (i = 0; i < cnt; i++) {
                                d32--;
                                s32--;
                                _mm_stream_si32(d32, *s32);
                        }

                        cnt = len & DWORD_MASK;
                        uint8_t *d8 = (uint8_t *)d32;
                        const uint8_t *s8 = (uint8_t *)s32;

                        for (i = 0; i < cnt; i++) {
                                d8--;
                                s8--;
                                *d8 = *s8;
                        }
                        pmem_flush(d8, cnt);
                }
        }

        /*
         * The call to pmem_*_nodrain() should be followed by pmem_drain()
         * to serialize non-temporal store instructions.  (It could be only
         * one drain after a sequence of pmem_*_nodrain calls).
         * However, on platforms that only support strongly-ordered CLFLUSH
         * for flushing the CPU cache (or that are forced to not use
         * CLWB/CLFLUSHOPT) there is no need to put any memory barrier after
         * the flush, so the pmem_drain() is a no-op function.  In such case,
         * we need to put a memory barrier here.
         */
     	predrain_memory_barrier();

        return pmemdest;
}

/*
unsigned long __copy_user_intel_nocache(void *to, void *from, unsigned long size) {
        int d0, d1;

        __asm__ __volatile__(
               "        .align 2,0x90\n"
               "0:      movl 32(%4), %%eax\n"
               "        cmpl $67, %0\n"
               "        jbe 2f\n"
               "1:      movl 64(%4), %%eax\n"
               "        .align 2,0x90\n"
               "2:      movl 0(%4), %%eax\n"
               "21:     movl 4(%4), %%edx\n"
               "        movnti %%eax, 0(%3)\n"
               "        movnti %%edx, 4(%3)\n"
               "3:      movl 8(%4), %%eax\n"
               "31:     movl 12(%4),%%edx\n"
               "        movnti %%eax, 8(%3)\n"
               "        movnti %%edx, 12(%3)\n"
               "4:      movl 16(%4), %%eax\n"
               "41:     movl 20(%4), %%edx\n"
               "        movnti %%eax, 16(%3)\n"
               "        movnti %%edx, 20(%3)\n"
               "10:     movl 24(%4), %%eax\n"
               "51:     movl 28(%4), %%edx\n"
               "        movnti %%eax, 24(%3)\n"
               "        movnti %%edx, 28(%3)\n"
               "11:     movl 32(%4), %%eax\n"
               "61:     movl 36(%4), %%edx\n"
               "        movnti %%eax, 32(%3)\n"
               "        movnti %%edx, 36(%3)\n"
               "12:     movl 40(%4), %%eax\n"
               "71:     movl 44(%4), %%edx\n"
               "        movnti %%eax, 40(%3)\n"
               "        movnti %%edx, 44(%3)\n"
               "13:     movl 48(%4), %%eax\n"
               "81:     movl 52(%4), %%edx\n"
               "        movnti %%eax, 48(%3)\n"
               "        movnti %%edx, 52(%3)\n"
               "14:     movl 56(%4), %%eax\n"
               "91:     movl 60(%4), %%edx\n"
               "        movnti %%eax, 56(%3)\n"
               "        movnti %%edx, 60(%3)\n"
               "        addl $-64, %0\n"
               "        addl $64, %4\n"
               "        addl $64, %3\n"
               "        cmpl $63, %0\n"
               "        ja  0b\n"
               "        sfence \n"
               "5:      movl  %0, %%eax\n"
               "        shrl  $2, %0\n"
               "        andl $3, %%eax\n"
               "        cld\n"
               "6:      rep; movsl\n"
               "        movl %%eax,%0\n"
               "7:      rep; movsb\n"
               "8:\n"
               ".section .fixup,\"ax\"\n"
               "9:      lea 0(%%eax,%0,4),%0\n"
               "16:     jmp 8b\n"
               ".previous\n"
               _ASM_EXTABLE(0b,16b)
               _ASM_EXTABLE(1b,16b)
               _ASM_EXTABLE(2b,16b)
               _ASM_EXTABLE(21b,16b)
               _ASM_EXTABLE(3b,16b)
               _ASM_EXTABLE(31b,16b)
               _ASM_EXTABLE(4b,16b)
               _ASM_EXTABLE(41b,16b)
               _ASM_EXTABLE(10b,16b)
               _ASM_EXTABLE(51b,16b)
               _ASM_EXTABLE(11b,16b)
               _ASM_EXTABLE(61b,16b)
               _ASM_EXTABLE(12b,16b)
               _ASM_EXTABLE(71b,16b)
               _ASM_EXTABLE(13b,16b)
               _ASM_EXTABLE(81b,16b)
               _ASM_EXTABLE(14b,16b)
               _ASM_EXTABLE(91b,16b)
               _ASM_EXTABLE(6b,9b)
               _ASM_EXTABLE(7b,16b)
               : "=&c"(size), "=&D" (d0), "=&S" (d1)
               :  "1"(to), "2"(from), "0"(size)
               : "eax", "edx", "memory");
        return size;
}*/

#endif //if 0
#endif
