/*
 * =====================================================================================
 *
 *       Filename:  handle_mmaps.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/05/2019 05:19:25 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */

#ifndef SPLITFS_HANDLE_MMAPS_H
#define SPLITFS_HANDLE_MMAPS_H

#include <inttypes.h>
#include <syscall.h>

#include "file.h"
#include "inode.h"
#include "staging.h"
#include "sys_util.h"
#include "hash_map.h"
#include "mmap_pool.h"
#include "out.h"

#define SIZE_OF_MMAP (2*1024*1024)

void *get_mmap_address(long fd, struct splitfs_file *file, off_t file_offset, size_t *extent_length);
void *get_staging_mmap_address(struct splitfs_file *file, size_t count,
        off_t file_offset, size_t *extent_length, bool write);
long clear_mmaps_from_ino(uint32_t serialno, bool remove_mmap);

#endif
