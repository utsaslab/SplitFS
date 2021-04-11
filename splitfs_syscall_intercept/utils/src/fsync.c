/*
 * =====================================================================================
 *
 *       Filename:  fsync.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/05/2019 10:11:22 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include "file.h"
#include "sys_util.h"
#include "staging.h"
#include "relink.h"
#include "out.h"
#include <splitfs-posix.h>
#include <syscall.h>


static ssize_t
splitfs_fsync_args_check(struct splitfs_file *file)
{
	if (!(file->flags & PFILE_WRITE)) {
		errno = EBADF;
		return -1;
	}

    return 0;
}

long splitfs_fsync(long fd, struct splitfs_file *file) {

    LOG(0, "In splitfs_fsync(). fd = %ld\n", fd);
    long ret = 0;

    ret = splitfs_fsync_args_check(file);

    pthread_mutex_lock(&file->mutex);
    struct splitfs_vinode *inode = file->vinode;
    pthread_mutex_unlock(&file->mutex);

    os_rwlock_wrlock(&inode->rwlock);

    ASSERT(inode_get_uncommitted_size(inode) >= inode_get_sync_size(inode));

    if (inode_get_uncommitted_size(inode) > inode_get_sync_size(inode)) {

        ASSERT(inode->staging);

        perform_relink(fd, (off_t) (inode_get_sync_size(inode)), inode,
                    inode_get_uncommitted_size(inode) - inode_get_sync_size(inode));

        inode_set_sync_size(inode, inode_get_uncommitted_size(inode));
        inode->staging = NULL;
    }

    os_rwlock_unlock(&inode->rwlock);

    return ret;
}

long splitfs_fdatasync(long fd, struct splitfs_file *file) {
    return splitfs_fsync(fd, file);
}
