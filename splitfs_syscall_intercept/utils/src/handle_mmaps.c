/*
 * =====================================================================================
 *
 *       Filename:  handle_mmaps.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/05/2019 04:33:49 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <sys/mman.h>
#include "handle_mmaps.h"

static void create_file_mmap(long fd, struct splitfs_file *file, unsigned mmap_index, off_t file_offset) {

    ASSERT(file->vinode->file_mmaps[mmap_index] == 0);
    off_t mmap_start_off = file_offset & ~(SIZE_OF_MMAP - 1);
    long mmap_flags = 0;
    if (file->flags & PFILE_READ)
        mmap_flags |= PROT_READ;
    if (file->flags & PFILE_WRITE)
        mmap_flags |= PROT_WRITE;

    char *addr = (char *)syscall_no_intercept(SYS_mmap, NULL,
        SIZE_OF_MMAP,
        mmap_flags,
        MAP_SHARED | MAP_POPULATE,
        (int) fd,
        mmap_start_off);

    if (addr == MAP_FAILED) {
        FATAL("mmap creation failed\n");
    }

    file->vinode->file_mmaps[mmap_index] = (unsigned long)addr;
}

void *get_mmap_address(long fd, struct splitfs_file *file, off_t file_offset, size_t *extent_length) {

    unsigned mmap_index = (unsigned) (file_offset / SIZE_OF_MMAP);
    unsigned largest_valid_mmap = (unsigned) (file->vinode->sync_length / SIZE_OF_MMAP) - 1;
    if (mmap_index > largest_valid_mmap) {
        *extent_length = 0;
        return NULL;
    }

    if (file->vinode->file_mmaps[mmap_index] == 0) {
        os_rwlock_unlock(&file->vinode->rwlock);
        os_rwlock_wrlock(&file->vinode->rwlock);
        create_file_mmap(fd, file, mmap_index, file_offset);
        os_rwlock_unlock(&file->vinode->rwlock);
        os_rwlock_rdlock(&file->vinode->rwlock);
    }

    ASSERT(file->vinode->file_mmaps[mmap_index] != 0);
    uint64_t start_addr = file->vinode->file_mmaps[mmap_index];
    off_t offset_in_mmap = file_offset % SIZE_OF_MMAP;

    *extent_length = (size_t) (SIZE_OF_MMAP - offset_in_mmap);
    return (void *) (start_addr + (unsigned long) offset_in_mmap);
}

void *get_staging_mmap_address(struct splitfs_file *file, size_t count,
        off_t file_offset, size_t *extent_length, bool write) {

    struct sfile_description *staging = NULL;

    if (!file)
        FATAL("file is NULL");
    if (!file->vinode)
        FATAL("vinode is NULL");
    if (file_offset < 0 || file->offset > file->vinode->length)
        FATAL("file_offset is wrong");
    if (!extent_length)
        FATAL("extent_length is NULL");
    if (count < 0 || count > (64*1024*1024))
        FATAL("count is wrong");

    if (write && file->vinode->staging == NULL) {
        get_staging_file(file);
    }

    if (!file->vinode->staging)
        FATAL("staging is NULL");

    staging = file->vinode->staging;
    void *start_addr = (void *)(staging->start_addr);
    off_t offset_in_mmap = file_offset + staging->valid_offset;

    if (offset_in_mmap >= (off_t)STAGING_FILE_SIZE) {
        *extent_length = 0;
        if (staging->valid_offset > STAGING_FILE_SIZE)
            staging->valid_offset = (off_t)STAGING_FILE_SIZE;
    }
    else
        *extent_length = (size_t)STAGING_FILE_SIZE - (size_t)offset_in_mmap;

    //if ((void *)(start_addr) == NULL)
    //    FATAL("start addr is NULL");

    return (void *) ((uint64_t)start_addr + (uint64_t) offset_in_mmap);
}

long clear_mmaps_from_ino(uint32_t serialno, bool remove_mmap) {

    long ret = 0;

    util_mutex_lock(&mmap_cache_mutex);

    char **file_mmaps = (char **)hash_map_get(global_mmap_cache, serialno);
    hash_map_remove(global_mmap_cache, serialno, file_mmaps);

    util_mutex_unlock(&mmap_cache_mutex);

    if (file_mmaps) {
        for (unsigned i = 0; i < PER_NODE_MMAPS; i++) {
            if (file_mmaps[i] != 0) {
                ret = munmap((char *)file_mmaps[i], SIZE_OF_MMAP);
                if (ret != 0) {
                    errno = EFAULT;
                    return ret;
                }
            }
        }
    }

    if (remove_mmap)
        splitfs_mmap_add(file_mmaps);

    return ret;
}
