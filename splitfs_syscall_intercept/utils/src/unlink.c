/*
 * =====================================================================================
 *
 *       Filename:  unlink.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/01/2019 08:41:17 PM
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

#include "splitfs-posix.h"
#include "utils.h"
#include "handle_mmaps.h"
#include "hash_map.h"
#include "out.h"

static int
_splitfs_unlinkat(const char *pathname) {

    struct stat sbuf;
    int ret = 0;

    ret = (int)syscall_no_intercept(SYS_lstat, pathname, &sbuf);
    if (ret != 0) {
        return ret;
    }

    uint32_t serialno = (uint32_t) sbuf.st_ino;
    bool remove_mmap = true;
    LOG(0, "Called splitfs_unlinkat(). path = %s. inode = %u", pathname, serialno);

    LOG(0, "clearing mmaps");
    ret = (int) clear_mmaps_from_ino(serialno, remove_mmap);

    LOG(0, "clearing tbl mmaps");
    util_mutex_lock(&tbl_mmap_mutex);
    void *tbl_mmaps = hash_map_get(tbl_mmap_cache, serialno);
    if (tbl_mmaps) {
        hash_map_remove(tbl_mmap_cache, serialno, tbl_mmaps);
    }
    util_mutex_unlock(&tbl_mmap_mutex);

    return ret;
}

int splitfs_unlinkat(const char *pathname, long flags) {


    int ret = 0;

	if (!pathname) {
		errno = ENOENT;
		return -1;
	}

	if (pathname[0] != '/') {
		LOG(LUSR, "NULL dir");
		errno = EFAULT;
		return -1;
	}

    if (flags != 0) {
        errno = EINVAL;
        ret = -1;
    } else {
        ret = _splitfs_unlinkat(pathname);
    }

    if (ret == 0) {
        ret = (int) syscall_no_intercept(SYS_unlink, pathname);
    }

    LOG(0, "unlink done. return = %d", ret);
    return ret;
}

int splitfs_unlink(const char* pathname) {
    return splitfs_unlinkat(pathname, 0);
}
