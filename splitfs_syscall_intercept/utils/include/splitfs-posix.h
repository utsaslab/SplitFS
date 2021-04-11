/*
 * =====================================================================================
 *
 *       Filename:  splitfs-posix.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/01/2019 08:58:57 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */

#ifndef SPLITFS_POSIX_H
#define SPLITFS_POSIX_H

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <utime.h>

typedef struct splitfs_file SPLITFSfile;

#define SPLITFS_O_RDONLY	       00
#define SPLITFS_O_WRONLY	       01
#define SPLITFS_O_RDWR		       02
#define SPLITFS_O_ACCMODE	     0003

#define SPLITFS_O_CREAT	     0100
#define SPLITFS_O_EXCL		     0200
#define SPLITFS_O_NOCTTY	     0400
#define SPLITFS_O_TRUNC	    01000
#define SPLITFS_O_APPEND	    02000
#define SPLITFS_O_NONBLOCK	    04000
#define SPLITFS_O_NDELAY	SPLITFS_O_NONBLOCK
#define SPLITFS_O_SYNC		 04010000
#define SPLITFS_O_ASYNC	   020000

#define SPLITFS_O_LARGEFILE	        0
#define SPLITFS_O_DIRECTORY	  0200000
#define SPLITFS_O_NOFOLLOW	  0400000
#define SPLITFS_O_CLOEXEC	 02000000
#define SPLITFS_O_DIRECT	   040000
#define SPLITFS_O_NOATIME	 01000000
#define SPLITFS_O_PATH		010000000
#define SPLITFS_O_DSYNC	   010000
#define SPLITFS_O_TMPFILE	(020000000 | SPLITFS_O_DIRECTORY)

#define SPLITFS_S_IFREG	0100000

#define SPLITFS_ALLPERMS		07777

#define SPLITFS_SEEK_SET  0
#define SPLITFS_SEEK_CUR  1
#define SPLITFS_SEEK_END  2
#define SPLITFS_SEEK_DATA 3
#define SPLITFS_SEEK_HOLE 4

#define PAGE_SIZE 4096

SPLITFSfile *splitfs_openat(const char *path, long flags, ...);
long splitfs_close(long fd, SPLITFSfile *file);
int splitfs_fallocate(long fd, SPLITFSfile *file, int mode, off_t offset, off_t length);
int splitfs_posix_fallocate(long fd, SPLITFSfile *file, int mode, off_t offset, off_t length);
int splitfs_fstatat(const char* path, struct stat *buf, int flags);
int splitfs_fstat(long fd, SPLITFSfile *file, struct stat *buf);
int splitfs_lstat(const char* path, struct stat* buf);
int splitfs_stat(const char* path, struct stat* buf);
ssize_t splitfs_readv(long fd, SPLITFSfile *file, struct iovec *iov, int iovcnt);
ssize_t splitfs_pread(long fd, SPLITFSfile *file, void *buf, size_t count, off_t offset);
ssize_t splitfs_preadv(long fd, SPLITFSfile *file, struct iovec *iov, int iovcnt, off_t offset);
ssize_t splitfs_read(long fd, SPLITFSfile *file, void* buf, size_t count);
ssize_t splitfs_write(long fd, SPLITFSfile *file, const void* buf, size_t count);
ssize_t splitfs_writev(long fd, SPLITFSfile *file, struct iovec *iov, int iovcnt);
ssize_t splitfs_pwrite(long fd, SPLITFSfile *file, const void* buf, size_t count, off_t offset);
ssize_t splitfs_pwritev(long fd, SPLITFSfile *file, struct iovec *iov, int iovcnt, off_t offset);
int splitfs_unlinkat(const char* pathname, long flags);
int splitfs_unlink(const char* pathname);
long splitfs_truncate(const char* path, off_t length);
off_t splitfs_lseek(long fd, SPLITFSfile *file, off_t offset, int whence);
long splitfs_ftruncate(long fd, SPLITFSfile *file, off_t length);
long splitfs_fsync(long fd, SPLITFSfile *file);
long splitfs_fdatasync(long fd, SPLITFSfile *file);
int splitfs_execv(void);

const char *splitfs_errormsg(void);

#endif

