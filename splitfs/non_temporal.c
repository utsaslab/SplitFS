#include "non_temporal.h"

static size_t Movnt_threshold_granularity = MOVNT_THRESHOLD_GRANULARITY;

#if 0
static void
predrain_memory_barrier(void)
{
  	_mm_mfence();   /* ensure CLWB or CLFLUSHOPT completes */
}
#endif

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
                _mm_clflushopt((char *)uptr);
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
