/*
 * Copyright 2016-2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * lseek.c -- pmemfile_lseek implementation
 */

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include "file.h"
#include "splitfs-posix.h"
#include "sys_util.h"
#include "utils.h"
#include "out.h"

static inline off_t
add_off(size_t cur, off_t off)
{
	COMPILE_ERROR_ON(sizeof(cur) != sizeof(int64_t));
	ASSERT(cur <= INT64_MAX);

	cur += (size_t)off;

	if (cur > INT64_MAX)
		return -1;

	return (off_t)cur;
}

static off_t
splitfs_lseek_locked(SPLITFSfile *file, off_t offset,
		int whence)
{
	LOG(LDBG, "file %p offset %ld whence %d", file, offset, whence);

	if (file->flags & PFILE_PATH) {
		errno = EBADF;
		return -1;
	}

    struct splitfs_vinode *inode = file->vinode;
    ASSERT(inode);

	off_t ret = 0;
	int new_errno = EINVAL;

	switch (whence) {
		case SPLITFS_SEEK_SET:
			ret = offset;
			if (ret < 0) {
				/*
				 * From POSIX: EINVAL if
				 * "...the resulting file offset would be
				 * negative for a regular file..."
				 *
				 * POSIX manpage also mentions EOVERFLOW
				 * "The resulting file offset would be a value
				 * which cannot be represented correctly in an
				 * object of type off_t."
				 * However in existing implementations it looks
				 * like it is only used to represent user-type
				 * overflow - user calls lseek, when off_t is
				 * 32-bit, but internal kernel type is 64-bit,
				 * and returned value cannot be represented
				 * EOVERFLOW is returned.
				 * With 64-bit off_t type EINVAL is returned in
				 * case of overflow.
				 */
				new_errno = EINVAL;
			}
			break;
		case SPLITFS_SEEK_CUR:
			ret = add_off(file->offset, offset);
			if (ret < 0) {
				/* Error as in SEEK_SET */
				new_errno = EINVAL;
			}
			break;
		case SPLITFS_SEEK_END:
			os_rwlock_rdlock(&inode->rwlock);
				ret = add_off(inode_get_uncommitted_size(inode), offset);
			os_rwlock_unlock(&inode->rwlock);

			if (ret < 0) {
				/* Error as in SEEK_SET */
				new_errno = EINVAL;
			}
			break;
		case SPLITFS_SEEK_DATA:
		case SPLITFS_SEEK_HOLE:
            new_errno = EOPNOTSUPP;
			break;
		default:
			ret = -1;
			break;
	}

	if (ret < 0) {
		ret = -1;
		errno = new_errno;
	} else {
		if (file->offset != (size_t)ret)
			LOG(LDBG, "off diff: old %lu != new %lu", file->offset,
					(size_t)ret);
		file->offset = (size_t)ret;
	}

	return ret;
}

/*
 * splitfs_lseek -- changes file current offset
 */
off_t
splitfs_lseek(long fd, struct splitfs_file *file, off_t offset, int whence)
{
    LOG(0, "Called splitfs_lseek()");

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	COMPILE_ERROR_ON(sizeof(offset) != 8);
	off_t ret;

	pthread_mutex_lock(&file->mutex);
	ret = splitfs_lseek_locked(file, offset, whence);
	pthread_mutex_unlock(&file->mutex);

	return ret;
}
