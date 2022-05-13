/*
 * =====================================================================================
 *
 *       Filename:  read.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/01/2019 07:39:08 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>
#include <syscall.h>
#include <sys/mman.h>

#include "file.h"
#include "splitfs-posix.h"
#include "utils.h"
#include "handle_mmaps.h"
#include "intel_intrin.h"
#include "add_delay.h"
#include "table_mmaps.h"
#include "out.h"

#define MEMCPY memcpy

static size_t
read_staging_file(struct splitfs_file *file, off_t offset,
        char *buf, size_t count) {

    LOG(0, "in read_staging_file");
    char *mmap_addr = NULL;
    size_t extent_length = 0;

    mmap_addr = (char *)get_staging_mmap_address(file, count,
        (off_t) (offset - (off_t) inode_get_sync_size(file->vinode)), &extent_length, false);

    ASSERT(mmap_addr != NULL);

    ASSERT(extent_length >= count);

    if (extent_length > count)
        extent_length = count;

    MEMCPY((char *)buf, (char *)mmap_addr, extent_length);
    perfmodel_add_delay(1, extent_length);

    return extent_length;
}

/*
 * vinode_read -- reads file
 */
static size_t
vinode_read(long fd, struct splitfs_file *file, off_t offset,
        char *buf, size_t count)
{
	uint64_t size = inode_get_uncommitted_size(file->vinode);
    void *mmap_addr = 0;
    size_t extent_length = 0;
    size_t len_read = 0;
    size_t len_to_read = count;
    // char buf2[count];
    LOG(0, "in vinode_read. fd = %ld, offset = %ld, count = %lu", fd, offset, count);

//goto syscall;
	/*
	 * Start reading at offset, stop reading
	 * when end of file is reached, or count bytes were read.
	 * The following two branches compute how many bytes are
	 * going to be read.
	 */
	if ((size_t)offset >= size)
		return 0; /* EOF already */

	if (size - (size_t)offset < count)
		count = size - (size_t)offset;

    while (len_read < count) {
        size_t len_read_iter = 0;
        if ((size_t)offset >= inode_get_sync_size(file->vinode)) {
            extent_length = read_staging_file(file, offset,
                    (char *) (buf + len_read), len_to_read);
            goto loop_end;
        }

        mmap_addr = (void *)splitfs_get_tbl_entry(file->vinode->tbl_mmap, offset, &extent_length);
        if (mmap_addr == NULL)
            mmap_addr = (void *)get_mmap_address(fd, file, offset, &extent_length);

        if ((size_t)offset + len_to_read >= inode_get_sync_size(file->vinode)) {
            len_read_iter = inode_get_sync_size(file->vinode) - (size_t)offset;
        } else {
            len_read_iter = len_to_read;
        }

        if (extent_length > len_read_iter) {
            extent_length = len_read_iter;
        }

        if (mmap_addr == NULL && extent_length == 0) {
            extent_length = (size_t) syscall_no_intercept(
                    SYS_pread64, fd, (char *) (buf + len_read),
                        len_read_iter, offset);
        } else {
            LOG(0, "reading mmap_addr = %p, length = %lu, inode = %u", (char*)mmap_addr, extent_length, file->vinode->serialno);
            MEMCPY((char *)buf, (char *)mmap_addr, extent_length);
            perfmodel_add_delay(1, extent_length);
            /*
            syscall_no_intercept(SYS_pread64, fd, buf2, extent_length, offset);
            if (strncmp((char*)(buf + len_read), buf2, extent_length) != 0) {
                LOG(0, "%s", buf2);
                LOG(0, "%s", (char*)(buf));
                LOG(0, "Reading wrong data: offset = %ld, length = %lu, addr = %p, fd = %ld, len_read = %lu",
                        offset, extent_length, (char*)mmap_addr, fd, len_read);
                ASSERT(false);
            }
            */
        }

loop_end:
        len_read += extent_length;
        len_to_read -= extent_length;
        offset += (off_t)extent_length;
        buf += extent_length;
    }

    LOG(0, "read returns %lu", count);

//syscall:
    //count = syscall_no_intercept(SYS_pread64, fd, buf, count, offset);
	return count;
}


/*
 * pmemfile_preadv_args_check - checks some read arguments
 * The arguments here can be examined while holding the mutex for the
 * PMEMfile instance, while there is no need to hold the lock for the
 * corresponding vinode instance.
 */
static ssize_t
splitfs_preadv_args_check(struct splitfs_file *file,
		const struct iovec *iov,
		int iovcnt)
{
	LOG(LDBG, "vinode %p iov %p iovcnt %d", file->vinode, iov, iovcnt);

	if (!(file->flags & PFILE_READ)) {
		errno = EBADF;
		return -1;
	}

	if (iovcnt > 0 && iov == NULL) {
		errno = EFAULT;
		return -1;
	}

	for (int i = 0; i < iovcnt; ++i) {
		if (iov[i].iov_base == NULL) {
			errno = EFAULT;
			return -1;
		}
	}

	return 0;
}

static ssize_t
splitfs_preadv_internal(long fd,
		struct splitfs_file *file,
		size_t offset,
		const struct iovec *iov,
		int iovcnt)
{
    LOG(0, "in splitfs_preadv_internal");
	ssize_t ret = 0;

	for (int i = 0; i < iovcnt; ++i) {
		size_t len = iov[i].iov_len;
		if ((ssize_t)((size_t)ret + len) < 0)
			len = (size_t)(SSIZE_MAX - ret);

		ASSERT((ssize_t)((size_t)ret + len) >= 0);

		size_t bytes_read = vinode_read(fd, file, (off_t)offset,
				iov[i].iov_base, len);

		ret += (ssize_t)bytes_read;
		offset += bytes_read;
		if (bytes_read != len)
			break;
	}

	return ret;
}

/*
 * pmemfile_readv_under_filelock - read from a file
 * This function expects the PMEMfile instance to be locked while being called.
 * Since the offset field is used to determine where to read from, and is also
 * updated after a successful read operation, the PMEMfile instance can not be
 * accessed by others while this is happening.
 *
 */
static ssize_t
splitfs_readv_under_filelock(long fd, struct splitfs_file *file,
		const struct iovec *iov, int iovcnt)
{
	ssize_t ret;

	ret = splitfs_preadv_args_check(file, iov, iovcnt);
	if (ret != 0)
		return ret;

	if (iovcnt == 0)
		return 0;

	os_rwlock_rdlock(&file->vinode->rwlock);

	ret = splitfs_preadv_internal(fd,
					file,
					file->offset, iov, iovcnt);


	os_rwlock_unlock(&file->vinode->rwlock);

	if (ret > 0) {
		file->offset += (size_t)ret;
	}

	return ret;
}


ssize_t
splitfs_readv(long fd, struct splitfs_file *file, struct iovec *iov, int iovcnt) {

    LOG(0, "Called splitfs_readv()");
	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	pthread_mutex_lock(&file->mutex);

	ssize_t ret =
		splitfs_readv_under_filelock(fd, file, iov, iovcnt);

	pthread_mutex_unlock(&file->mutex);

    return ret;
}

ssize_t
splitfs_pread(long fd, struct splitfs_file *file, void *buf, size_t count, off_t offset) {

	struct iovec element = { .iov_base = buf, .iov_len = count };
	return splitfs_preadv(fd, file, &element, 1, offset);
}

ssize_t
splitfs_preadv(long fd, struct splitfs_file *file, struct iovec *iov, int iovcnt, off_t offset) {

    LOG(0, "Called splitfs_preadv()");

    if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
    }

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	ssize_t ret;

	pthread_mutex_lock(&file->mutex);

	ret = splitfs_preadv_args_check(file, iov, iovcnt);

	if (ret != 0)
		return ret;

	if (iovcnt == 0)
		return 0;

	os_rwlock_rdlock(&(file->vinode->rwlock));

    ret = splitfs_preadv_internal(fd, file, (size_t)offset, iov, iovcnt);

    os_rwlock_unlock(&(file->vinode->rwlock));
	pthread_mutex_unlock(&file->mutex);

    return ret;
}

ssize_t
splitfs_read(long fd, struct splitfs_file *file, void* buf, size_t count) {

    struct iovec element = { .iov_base = buf, .iov_len = count };
    return splitfs_readv(fd, file, &element, 1);
}
