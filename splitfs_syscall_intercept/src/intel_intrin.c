/*
 * =====================================================================================
 *
 *       Filename:  intel_intrin.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/07/2019 08:07:41 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <cpuid.h>
#include <stdio.h>

#include "intel_intrin.h"

static void (*_mm_flush_fn_ptr)(void const* p);

static void
flush_dcache_invalidate_opt(const void *addr, size_t len)
{
    uintptr_t uptr;

    /*
     * Loop through cache-line-size (typically 64B) aligned chunks
     * covering the given range.
     */
    for (uptr = (uintptr_t)addr & ~(FLUSH_ALIGN - 1);
        uptr < (uintptr_t)addr + len; uptr += FLUSH_ALIGN) {
        _mm_flush((char *)uptr);
    }
}


/*
 * pmem_flush -- flush processor cache for the given range
 */
static void
pmem_flush(const void *addr, size_t len)
{
    flush_dcache_invalidate_opt(addr, len);
}


void *memmove_nodrain_movnt_granularity(void *pmemdest, const void *src, size_t len)
{
    __m128i xmm0, xmm1, xmm2, xmm3;
    size_t i;
    __m128i *d;
    __m128i *s;
    void *dest1 = pmemdest;
    size_t cnt;

    if (len == 0 || src == pmemdest)
        return pmemdest;

    if (len < MOVNT_THRESHOLD_GRANULARITY) {
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

    return pmemdest;
}

void _mm_cache_flush(void const* p) {
  _mm_clflush(p);
}

void _mm_cache_flush_optimised(void const* p) {
  _mm_clflushopt(p);
}

// Figure out if CLFLUSHOPT is supported 
static int is_clflushopt_supported() {
	unsigned int eax, ebx, ecx, edx;
	__cpuid_count(7, 0, eax, ebx, ecx, edx);
	return ebx & bit_CLFLUSHOPT;
}

void _mm_flush(void const* p)
{
    _mm_flush_fn_ptr(p);
}

/*
    Based on availability of CLFLUSHOPT instruction, point _mm_flush to the 
    appropriate function
*/
void splitfs_init_mm_flush(void) {
	if(is_clflushopt_supported()) {
		_mm_flush_fn_ptr = _mm_cache_flush_optimised;
	} else { 
		_mm_flush_fn_ptr = _mm_cache_flush;
	}
}
