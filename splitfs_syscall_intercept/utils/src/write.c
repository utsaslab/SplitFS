/*
 * =====================================================================================
 *
 *       Filename:  write.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/01/2019 08:44:24 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>
#include <syscall.h>

#include "file.h"
#include "splitfs-posix.h"
#include "utils.h"
#include "handle_mmaps.h"
#include "intel_intrin.h"
#include "relink.h"
#include "staging_thread.h"
#include "add_delay.h"
#include "table_mmaps.h"
#include "out.h"

#define MEMCPY_NON_TEMPORAL memmove_nodrain_movnt_granularity

static size_t
write_staging_file(long fd, struct splitfs_file *file, off_t offset,
        char *buf, size_t count) {
    void *mmap_addr = NULL;
    size_t extent_length = 0;
    LOG(0, "in write_staging_file()");

    /*
    if (count == 7) {
        char print_str[50];
        sprintf(print_str, "count: %d, fd: %ld, offset: %ld\n", count, fd, offset);
        syscall_no_intercept(SYS_write, fileno(stderr), print_str, 24 + sizeof(int) + sizeof(long) + sizeof(long));
        //FATAL("count is 7");
    }
    */


    if (file == NULL)
        FATAL("file is NULL");
    if (buf == NULL)
        FATAL("buf is NULL");
    if (fd < 0)
        FATAL("fd is wrong");
    if (count > (64*1024*1024) || count < 0)
        FATAL("count size is wrong");
    if (!file->vinode)
        FATAL("vinode is NULL");
    if (file->vinode->length < file->vinode->sync_length)
        FATAL("sync_size is wrong");
    if (offset != file->vinode->length)
        FATAL("offset is wrong");

start:
    mmap_addr = get_staging_mmap_address(file, count,
        (off_t) (offset - (off_t)inode_get_sync_size(file->vinode)), &extent_length, true);

    if (mmap_addr == NULL)
        FATAL("mmap addr is null");

    if (extent_length >= count)
        extent_length = count;
    else {
        LOG(0, "changing the staging file");

        size_t relink_size = STAGING_FILE_SIZE -
            (size_t)file->vinode->staging->valid_offset - extent_length;

        if (relink_size > (100*1024*1024UL) || relink_size < 0) {
            char extent_str[50];
            sprintf(extent_str, "extent_length = %lu\n", relink_size);
            syscall_no_intercept(SYS_write, fileno(stderr), extent_str, 50);
            FATAL("relink size is very large");
        }

        if (relink_size > 0)
            perform_relink(fd, (off_t)inode_get_sync_size(file->vinode),
                file->vinode, relink_size);

        file->vinode->staging = NULL;
        inode_set_sync_size(file->vinode, inode_get_uncommitted_size(file->vinode));
        splitfs_call_thread();
        goto start;
    }

    if (offset + (off_t)extent_length > (off_t)inode_get_uncommitted_size(file->vinode))
        inode_set_uncommitted_size(file->vinode, (size_t)offset + extent_length);

    os_rwlock_unlock(&file->vinode->rwlock);
    os_rwlock_rdlock(&file->vinode->rwlock);

    if (extent_length != count)
        FATAL("extent_length is not equal to count");
    if (mmap_addr == NULL)
        FATAL("mmap address is null");
    if (buf == NULL)
        FATAL("buf is null");

    MEMCPY_NON_TEMPORAL((char *)mmap_addr, (char *)buf, extent_length);
    perfmodel_add_delay(0, extent_length);

    //LOG(0, "Addr = %p, data written = %lu, inode size = %lu, sync_size = %lu", mmap_addr, extent_length, file->vinode->length, file->vinode->sync_length);

    off_t offset_in_staging = (unsigned long) mmap_addr - (unsigned long) file->vinode->staging->start_addr;
    long staging_fd = file->vinode->staging->fd;

    /*
    if (count == 7) {
        char print_str[6];
        sprintf(print_str, "%s\n", "Done");
        syscall_no_intercept(SYS_write, fileno(stderr), print_str, 6);
    }
    */

    return extent_length;
}

static size_t
vinode_write(long fd, struct splitfs_file *file,
        off_t offset, const char *buf, size_t count) {

    void *mmap_addr = NULL;
    size_t extent_length = 0;
    size_t len_written = 0;
    size_t len_to_write = count;
    LOG(0, "inode no = %u, size = %lu, write off = %ld, write count = %lu",
            file->vinode->serialno, file->vinode->length, offset, count);

    /* Check for holes */
    if (offset > (off_t)inode_get_uncommitted_size(file->vinode)) {
        os_rwlock_unlock(&file->vinode->rwlock);
        os_rwlock_wrlock(&file->vinode->rwlock);

        inode_set_uncommitted_size(file->vinode, (size_t)offset + count);
        inode_set_sync_size(file->vinode, (size_t)offset + count);

        os_rwlock_unlock(&file->vinode->rwlock);
        os_rwlock_rdlock(&file->vinode->rwlock);

        len_written = (size_t) syscall_no_intercept(
                SYS_pwrite64, fd, (char *)buf,
                count, offset);
        syscall_no_intercept(SYS_fsync, fd);

        return len_written;
    }

    while (len_written < count) {
        size_t len_write_iter = 0;
        if (offset >= (off_t)inode_get_sync_size(file->vinode)) {
            os_rwlock_unlock(&file->vinode->rwlock);
            os_rwlock_wrlock(&file->vinode->rwlock);
            extent_length = write_staging_file(fd, file, offset,
                    (char *) ((unsigned long)buf + len_written), len_to_write);
            goto loop_end;
        }

        mmap_addr = (void *)splitfs_get_tbl_entry(file->vinode->tbl_mmap, offset, &extent_length);
        if (mmap_addr == NULL)
            mmap_addr = get_mmap_address(fd, file, offset, &extent_length);

        if ((size_t)offset + len_to_write >= file->vinode->sync_length) {
            len_write_iter = file->vinode->sync_length - (size_t)offset;
        } else {
            len_write_iter = len_to_write;
        }

        if (extent_length > len_write_iter) {
            extent_length = len_write_iter;
        }

        if (mmap_addr == NULL && extent_length == 0) {
            extent_length = (size_t) syscall_no_intercept(
                    SYS_pwrite64, fd, (char *) (buf + len_written),
                    len_write_iter, offset);
        } else {
            MEMCPY_NON_TEMPORAL((char *)mmap_addr,
                    (char *)((unsigned long)buf + len_written), extent_length);
            perfmodel_add_delay(0, extent_length);
        }

loop_end:
        len_written += extent_length;
        len_to_write -= extent_length;
        offset += (off_t)extent_length;
    }
    return len_written;
}

/*
 * splitfs_pwritev_args_check - checks some write arguments
 * The arguments here can be examined while holding the mutex for the
 * SPLITFSfile instance, while there is no need to hold the lock for the
 * corresponding vinode instance.
 */
static ssize_t
splitfs_pwritev_args_check(struct splitfs_file *file,
		const struct iovec *iov,
		int iovcnt)
{
	LOG(LDBG, "vinode %p iov %p iovcnt %d", file->vinode, iov, iovcnt);

    LOG(0, "file->flags = %ld, PFILE_WRITE = %llu\n", file->flags, PFILE_WRITE);
	if (!(file->flags & PFILE_WRITE)) {
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
splitfs_pwritev_internal(long fd, struct splitfs_file *file,
        off_t offset, struct iovec *iov, int iovcnt) {

    int error = 0;
    size_t sum_len = 0;
    size_t ret = 0;

    LOG(LDBG, "In splitfs_pwritev_internal()");

    for (int i = 0; i < iovcnt; ++i) {
        size_t len = iov[i].iov_len;

        if ((size_t) len < 0)
            len = SSIZE_MAX;

        if ((ssize_t) (sum_len + len) < 0)
            len = SSIZE_MAX - sum_len;

        /*  overflow check */
        if (offset + (off_t)sum_len + (off_t)len < offset)
            len = SSIZE_MAX - (size_t)offset - sum_len;

        sum_len += len;

        if (len != iov[i].iov_len)
            break;
    }

    if (sum_len == 0)
        return 0;

    /* Now write the data.
     */
    for (int i = 0; i < iovcnt; ++i) {
        size_t len = iov[i].iov_len;

        if ((ssize_t)len < 0)
            len = SSIZE_MAX;

        if ((ssize_t) (ret + len) < 0)
            len = SSIZE_MAX - ret;

        /* overflow check */
        if (offset + (off_t)len < offset)
            len = SSIZE_MAX - (size_t)offset;

        if (len > 0)
            vinode_write(fd, file, offset, iov[i].iov_base, len);

        ret += len;
        offset += (off_t)len;

        if (len != iov[i].iov_len)
            break;
    }
    ASSERT(ret > 0);

    if (error) {
        errno = error;
        return -1;
    }

    _mm_sfence();
    return (ssize_t) ret;
}


ssize_t
splitfs_write(long fd, struct splitfs_file *file, const void* buf, size_t count) {

	struct iovec element = {.iov_base = (void *)buf, .iov_len = count};
	return splitfs_writev(fd, file, &element, 1);
}


/*
 * pmemfile_writev_under_filelock - write to a file
 * This function expects the PMEMfile instance to be locked while being called.
 * Since the offset field is used to determine where to read from, and is also
 * updated after a successful read operation, the PMEMfile instance can not be
 * accessed by others while this is happening.
 *
 */
static ssize_t
splitfs_writev_under_filelock(long fd, struct splitfs_file *file,
		struct iovec *iov, int iovcnt)
{
	ssize_t ret;

	ret = splitfs_pwritev_args_check(file, iov, iovcnt);
	if (ret != 0)
		return ret;

	if (iovcnt == 0)
		return 0;

	os_rwlock_rdlock(&file->vinode->rwlock);

	ret = splitfs_pwritev_internal(fd,
					file,
					(off_t)file->offset, iov, iovcnt);


	os_rwlock_unlock(&file->vinode->rwlock);

	if (ret > 0) {
		file->offset += (size_t)ret;
	}

	return ret;
}

ssize_t
splitfs_writev(long fd, struct splitfs_file *file, struct iovec *iov, int iovcnt) {

	if (!file) {
		LOG(LUSR, "NULL file");
		errno = EFAULT;
		return -1;
	}

	pthread_mutex_lock(&file->mutex);

	ssize_t ret =
		splitfs_writev_under_filelock(fd, file, iov, iovcnt);

	pthread_mutex_unlock(&file->mutex);

    return ret;
}

ssize_t
splitfs_pwrite(long fd, struct splitfs_file *file, const void* buf, size_t count, off_t offset) {

    LOG(0, "Called splitfs_pwrite()");
	struct iovec element = {.iov_base = (void *)buf, .iov_len = count};
	return splitfs_pwritev(fd, file, &element, 1, offset);
}

ssize_t
splitfs_pwritev(long fd, struct splitfs_file *file, struct iovec *iov,
        int iovcnt, off_t offset) {

    LOG(0, "Called splitfs_pwritev()");

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

    LOG(0, "checking args");
	ret = splitfs_pwritev_args_check(file, iov, iovcnt);
    LOG(0, "checking args ret = %lu", ret);

	if (ret != 0)
		return ret;

	if (iovcnt == 0)
		return 0;

	os_rwlock_rdlock(&file->vinode->rwlock);

    ret = splitfs_pwritev_internal(fd, file, offset, iov, iovcnt);

    os_rwlock_unlock(&file->vinode->rwlock);
	pthread_mutex_unlock(&file->mutex);

    return ret;
}
