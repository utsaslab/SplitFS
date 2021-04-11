/*
 * =====================================================================================
 *
 *       Filename:  truncate.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/01/2019 07:20:46 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <inttypes.h>
#include <syscall.h>
#include <limits.h>

#include "truncate.h"

long
splitfs_truncate_vinode(struct splitfs_vinode *inode, off_t length) {

    bool remove_mmap = false;
    inode_set_uncommitted_size(inode, (size_t)length);
    inode_set_sync_size(inode, (size_t)length);
    inode_set_large_file_status(inode, false);
    clear_mmaps_from_ino(inode->serialno, remove_mmap);

    return 0;
}

static long
_splitfs_truncate(const char *path, off_t length) {

    struct stat sbuf;
    long ret = 0;

    ret = stat(path, &sbuf);
    if (ret != 0) {
        errno = EINVAL;
        return -1;
    }

    uint32_t serialno = (uint32_t) sbuf.st_ino;

    struct splitfs_vinode *inode = splitfs_map_vinode_check(serialno);

    if (inode == NULL) {
        bool remove_mmap = false;
        clear_mmaps_from_ino(serialno, remove_mmap);
        ret = syscall_no_intercept(SYS_truncate, path, length);
        return ret;
    }

    os_rwlock_wrlock(&inode->rwlock);
    splitfs_truncate_vinode(inode, length);
    ret = syscall_no_intercept(SYS_truncate, path, length);
    os_rwlock_unlock(&inode->rwlock);

    return ret;
}

long
splitfs_truncate(const char* path, off_t length) {

    LOG(0, "Called splitfs_truncate()");
    long error = 0;

	if (!path) {
		LOG(LUSR, "NULL path");
		errno = EFAULT;
		return -1;
	}

	if (length < 0) {
		errno = EINVAL;
		return -1;
	}

    if (error != 0)
        return error;

    error = _splitfs_truncate(path, length);

    return error;
}

long
splitfs_ftruncate(long fd, struct splitfs_file *file, off_t length) {

    LOG(0, "Called splitfs_ftruncate()");

    long ret = 0;

    util_mutex_lock(&file->mutex);
    struct splitfs_vinode *inode = file->vinode;
    util_mutex_unlock(&file->mutex);

    if (!inode) {
        ret = syscall_no_intercept(SYS_ftruncate, fd, length);
        return ret;
    }


    os_rwlock_wrlock(&inode->rwlock);
    ret = splitfs_truncate_vinode(inode, length);
    ret = syscall_no_intercept(SYS_ftruncate, fd, length);
    os_rwlock_unlock(&inode->rwlock);

    return ret;
}
