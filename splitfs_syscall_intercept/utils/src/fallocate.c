/*
 * =====================================================================================
 *
 *       Filename:  fallocate.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/01/2019 07:33:35 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <syscall.h>

#include "file.h"
#include "splitfs-posix.h"
#include "utils.h"
#include "handle_mmaps.h"
#include "out.h"

static int
_splitfs_inode_fallocate(struct splitfs_vinode *inode, long mode,
        off_t offset, off_t length) {

    bool remove_mmaps = false;
    inode_set_uncommitted_size(inode, (size_t)(offset + length));
    inode_set_sync_size(inode, (size_t)(offset + length));
    inode_set_large_file_status(inode, false);
    clear_mmaps_from_ino(inode->serialno, remove_mmaps);
    return 0;
}


/*
 * fallocate_check_arguments - part of pmemfile_fallocate implementation
 * Perform some checks that are independent of the file being operated on.
 */
static int
fallocate_check_arguments(int mode, off_t offset,
		off_t length)
{
	/*
	 * from man 2 fallocate:
	 *
	 * "EINVAL - offset was less than 0, or len was less
	 * than or equal to 0."
	 */
	if (length <= 0 || offset < 0)
		return EINVAL;

	/*
	 * from man 2 fallocate:
	 *
	 * "EFBIG - offset+len exceeds the maximum file size."
	 */
	if ((size_t)offset + (size_t)length > (size_t)SSIZE_MAX)
		return EFBIG;

	/*
	 * from man 2 fallocate:
	 *
	 * "EOPNOTSUPP -  The  filesystem containing the file referred to by
	 * fd does not support this operation; or the mode is not supported by
	 * the filesystem containing the file referred to by fd."
	 *
	 * As of now, pmemfile_fallocate supports allocating disk space, and
	 * punching holes.
	 */
	if (mode & FALLOC_FL_COLLAPSE_RANGE) {
		ERR("FL_COLLAPSE_RANGE is not supported");
		return EOPNOTSUPP;
	}

	if (mode & FALLOC_FL_ZERO_RANGE) {
		ERR("FL_ZERO_RANGE is not supported");
		return EOPNOTSUPP;
	}

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		/*
		 * from man 2 fallocate:
		 *
		 * "The FALLOC_FL_PUNCH_HOLE flag must be ORed
		 * with FALLOC_FL_KEEP_SIZE in mode; in other words,
		 * even when punching off the end of the file, the file size
		 * (as reported by stat(2)) does not change."
		 */
		if (mode != (FALLOC_FL_PUNCH_HOLE |
				FALLOC_FL_KEEP_SIZE))
			return EOPNOTSUPP;
	} else { /* Allocating disk space */
		/*
		 * Note: According to 'man 2 fallocate' FALLOC_FL_UNSHARE
		 * is another possible flag to accept here. No equivalent of
		 * that flag is supported by pmemfile as of now. Also that man
		 * page is wrong anyways, the header files only refer to
		 * FALLOC_FL_UNSHARE_RANGE, so it is suspected that noone is
		 * using it anyways.
		 */
		if ((mode & ~FALLOC_FL_KEEP_SIZE) != 0)
			return EINVAL;
	}

	return 0;
}

int
splitfs_fallocate(long fd, struct splitfs_file *file, int mode,
        off_t offset, off_t length) {

    LOG(0, "Called splitfs_fallocate()");

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	int error;

	error = fallocate_check_arguments(mode, offset, length);
	if (error)
		goto end;

	ASSERT(offset >= 0);
	ASSERT(length > 0);

	pthread_mutex_lock(&file->mutex);
	uint64_t flags = file->flags;
	struct splitfs_vinode *inode = file->vinode;
	pthread_mutex_unlock(&file->mutex);

	/*
	 * from man 2 fallocate:
	 *
	 * "EBADF  fd is not a valid file descriptor, or is not opened for
	 * writing."
	 */
	if ((flags & PFILE_WRITE) == 0) {
		error = EBADF;
		goto end;
	}

    if (inode) {
        os_rwlock_wrlock(&inode->rwlock);

        error = _splitfs_inode_fallocate(inode, mode, (off_t)offset,
                (off_t)length);

        syscall_no_intercept(SYS_fallocate, fd, mode, offset, length);
        os_rwlock_unlock(&inode->rwlock);
    } else
        syscall_no_intercept(SYS_fallocate, fd, mode, offset, length);

end:
	if (error != 0) {
		errno = error;
		return -1;
	}

    return 0;
}

int splitfs_posix_fallocate(long fd, struct splitfs_file *file,
        int mode, off_t offset, off_t length) {

    return splitfs_fallocate(fd, file, mode, offset, length);
}
