/*
 * =====================================================================================
 *
 *       Filename:  stat.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/01/2019 08:51:54 PM
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
#include <errno.h>

#include "file.h"
#include "inode.h"
#include "splitfs-posix.h"
#include "utils.h"
#include "out.h"

static void
_splitfs_vinode_stat(struct splitfs_vinode *inode, struct stat *buf) {

    LOG(0, "inode->size = %lu, buf size = %lu, serialno = %u\n",
        inode_get_uncommitted_size(inode), buf->st_size, inode->serialno);
    buf->st_size = (off_t) inode_get_uncommitted_size(inode);
}

static int
_splitfs_fstatat(const char *path, struct stat *buf, int flags) {

	LOG(LDBG, "path %s", path);

    int ret = (int) syscall_no_intercept(SYS_stat, path, buf);
    if (ret != 0)
        return ret;

    struct splitfs_vinode *inode = splitfs_map_vinode_check((uint32_t) buf->st_ino);

    if (inode)
        _splitfs_vinode_stat(inode, buf);

    return ret;
}

int
splitfs_fstatat(const char* path, struct stat *buf, int flags) {

    LOG(0, "Called splitfs_fstatat()");

	if (!path) {
		errno = EFAULT;
		return -1;
	}

	if (path[0] != '/') {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	int ret = _splitfs_fstatat(path, buf, flags);

	return ret;
}

int
splitfs_fstat(long fd, struct splitfs_file *file, struct stat *buf) {


	if (!file) {
		errno = EFAULT;
		return -1;
	}

    int ret = (int) syscall_no_intercept(SYS_stat, fd, buf);
    LOG(0, "Called splitfs_fstat(). fd = %ld, sbuf size = %lu", fd, buf->st_size);

    if (file->vinode)
	    _splitfs_vinode_stat(file->vinode, buf);

	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}

int
splitfs_lstat(const char* path, struct stat* buf) {

    LOG(0, "Called splitfs_lstat()");
    return splitfs_fstatat(path, buf, 0);
}

int
splitfs_stat(const char *path, struct stat *buf) {

    LOG(LDBG, "Called splitfs_stat()");
    return splitfs_fstatat(path, buf, 0);
}
