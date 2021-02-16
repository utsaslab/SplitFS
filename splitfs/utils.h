#ifndef _SPLITFS_UTILS_H
#define _SPLITFS_UTILS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <linux/kernel.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << 12)
#define PAGE_MASK = ~(PAGE_SIZE - 1)

size_t align_next_page(size_t address);
size_t align_cur_page(size_t address);
off_t align_page_offset(off_t cur_offset, off_t target_offset);

#endif
