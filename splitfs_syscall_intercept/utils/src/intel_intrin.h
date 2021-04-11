/*
 * =====================================================================================
 *
 *       Filename:  intel_intrin.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/07/2019 08:00:43 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#ifndef SPLITFS_INTRIN_H
#define SPLITFS_INTRIN_H

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
#define _mm_clflush(addr)\
	asm volatile("clflush %0" : "+m" (*(volatile char *)(addr)))

void *memmove_nodrain_movnt_granularity(void *pmemdest, const void *src, size_t len);
/* This will point to the right function during startup of 
 splitfs */
void splitfs_init_mm_flush(void);
void _mm_flush(void const* p);

#endif
