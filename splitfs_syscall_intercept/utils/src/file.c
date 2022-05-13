/*
 * =====================================================================================
 *
 *       Filename:  file.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  07/31/2019 04:38:40 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <inttypes.h>
#include <syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "constants.h"
#include "file.h"
#include "splitfs-posix.h"
#include "mmap_pool.h"
#include "truncate.h"
#include "sys_util.h"
#include "utils.h"
#include "hash_map.h"
#include "table_mmaps.h"
#include "out.h"

static struct splitfs_file *free_file_slots[OPEN_MAX];
static unsigned free_slot_count;
static pthread_mutex_t free_file_slot_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct splitfs_file *fetch_free_file_slot(void) {

    struct splitfs_file *entry;

    util_mutex_lock(&free_file_slot_mutex);

    if (free_slot_count == 0)
        entry = NULL;
    else
        entry = free_file_slots[--free_slot_count];

    util_mutex_unlock(&free_file_slot_mutex);

    return entry;
}

struct splitfs_file *
splitfs_file_assign() {

    struct splitfs_file *entry = fetch_free_file_slot();
    return entry;
}

static void splitfs_file_add(struct splitfs_file *entry) {

    util_mutex_lock(&free_file_slot_mutex);

    free_file_slots[free_slot_count++] = entry;

    util_mutex_unlock(&free_file_slot_mutex);
}

void splitfs_file_table_init() {

    static struct splitfs_file store[ARRAY_SIZE(free_file_slots) - 1];
    for (unsigned i = 0; i < ARRAY_SIZE(store); ++i) {
        pthread_mutex_init(&store[i].mutex, NULL);
        splitfs_file_add(store + i);
    }
}


/*
 * check_flags -- open(2) flags tester
 */

static int
check_flags(long flags)
{
	if (flags & SPLITFS_O_APPEND) {
		flags &= ~SPLITFS_O_APPEND;
	}

	if (flags & SPLITFS_O_ASYNC) {
		ERR("O_ASYNC is not supported");
		errno = EINVAL;
		return -1;
	}

	if (flags & SPLITFS_O_CREAT) {
		LOG(LTRC, "O_CREAT");
		flags &= ~SPLITFS_O_CREAT;
	}

	/* XXX: move to interposing layer */
	if (flags & SPLITFS_O_CLOEXEC) {
		LOG(LINF, "O_CLOEXEC is always enabled");
		flags &= ~SPLITFS_O_CLOEXEC;
	}

	if (flags & SPLITFS_O_DIRECT) {
		LOG(LINF, "O_DIRECT is always enabled");
		flags &= ~SPLITFS_O_DIRECT;
	}

	/* O_TMPFILE contains O_DIRECTORY */
	if ((flags & SPLITFS_O_TMPFILE) == SPLITFS_O_TMPFILE) {
		ERR("O_TMPFILE is not supported");
        errno = EINVAL;
        return -1;
	}

	if (flags & SPLITFS_O_DIRECTORY) {
		LOG(0, "O_DIRECTORY is not supported");
        //errno = EINVAL;
        return -1;
	}

	if (flags & SPLITFS_O_DSYNC) {
		LOG(LINF, "O_DSYNC is always enabled");
		flags &= ~SPLITFS_O_DSYNC;
	}

	if (flags & SPLITFS_O_EXCL) {
		LOG(LTRC, "O_EXCL");
		flags &= ~SPLITFS_O_EXCL;
	}

	if (flags & SPLITFS_O_NOCTTY) {
		LOG(LINF, "O_NOCTTY is always enabled");
		flags &= ~SPLITFS_O_NOCTTY;
	}

    flags &= ~SPLITFS_O_NOATIME;

	if (flags & SPLITFS_O_NOFOLLOW) {
		LOG(LTRC, "O_NOFOLLOW");
		flags &= ~SPLITFS_O_NOFOLLOW;
	}

	if (flags & SPLITFS_O_NONBLOCK) {
		LOG(LINF, "O_NONBLOCK is ignored");
		flags &= ~SPLITFS_O_NONBLOCK;
	}

	if (flags & SPLITFS_O_PATH) {
		LOG(LTRC, "O_PATH");
		flags &= ~SPLITFS_O_PATH;
	}

	if (flags & SPLITFS_O_SYNC) {
		LOG(LINF, "O_SYNC is always enabled");
		flags &= ~SPLITFS_O_SYNC;
	}

	if (flags & SPLITFS_O_TRUNC) {
		LOG(LTRC, "O_TRUNC");
		flags &= ~SPLITFS_O_TRUNC;
	}

	if ((flags & SPLITFS_O_ACCMODE) == SPLITFS_O_RDONLY) {
		LOG(LTRC, "O_RDONLY");
		flags -= SPLITFS_O_RDONLY;
	}

	if ((flags & SPLITFS_O_ACCMODE) == SPLITFS_O_WRONLY) {
		LOG(LTRC, "O_WRONLY");
		flags -= SPLITFS_O_WRONLY;
	}

	if ((flags & SPLITFS_O_ACCMODE) == SPLITFS_O_RDWR) {
		LOG(LTRC, "O_RDWR");
		flags -= SPLITFS_O_RDWR;
	}

	if (flags) {
		LOG(0, "unknown flag 0x%lx\n", flags);
		errno = EINVAL;
		return -1;
	}

	return 0;
}

static SPLITFSfile *
_splitfs_openat(const char *path, long flags, ...) {

    LOG(0, "path = %s", path);
	if (check_flags(flags))
		return NULL;

	va_list ap;
	va_start(ap, flags);
	long mode = 0;

	if (flags & SPLITFS_O_CREAT) {
		mode = va_arg(ap, long);
		LOG(LDBG, "mode %lo", mode);
		mode &= SPLITFS_ALLPERMS;
	}
	va_end(ap);

    struct splitfs_file *file = NULL;
    struct splitfs_vinode *inode = NULL;
    struct stat sbuf;

    if (syscall_no_intercept(SYS_stat, path, &sbuf) != 0) {
        FATAL("stat failed\n");
    }

    LOG(0, "sbuf st_mode = %u, IFREG = %d. sbuf.st_mode & SPLITFS_S_IFREG = %d",
            sbuf.st_mode, SPLITFS_S_IFREG, sbuf.st_mode & SPLITFS_S_IFREG);
    if ((sbuf.st_mode & SPLITFS_S_IFREG) == 0)
        return NULL;

	int accmode = flags & SPLITFS_O_ACCMODE;

    file = splitfs_file_assign();
    if (file == NULL) {
        FATAL("Ran out of files\n");
    }

    pthread_mutex_lock(&file->mutex);

    file->offset = 0;
    file->serialno = (uint32_t) sbuf.st_ino;

    LOG(0, "flags passed = %ld", flags);
	if (flags & SPLITFS_O_PATH)
		file->flags = PFILE_PATH;
	else if (accmode == SPLITFS_O_RDONLY)
		file->flags = PFILE_READ;
	else if (accmode == SPLITFS_O_WRONLY)
		file->flags = PFILE_WRITE;
	else if (accmode == SPLITFS_O_RDWR)
		file->flags = PFILE_READ | PFILE_WRITE;
    LOG(0, "flags set = %ld\n", file->flags);

    inode = splitfs_vinode_assign(file->serialno);
    if (inode == NULL) {
        FATAL("Ran out of inodes\n");
    }

    os_rwlock_wrlock(&inode->rwlock);

    inode_set_ino(inode, (uint32_t) sbuf.st_ino);
    LOG(0, "path = %s, inode no = %u", path, inode_get_ino(inode));

    if (inode_get_ref(inode) == 1) {

        if (flags & SPLITFS_O_TRUNC) {
            splitfs_truncate_vinode(inode, 0);
        } else {
            inode_set_uncommitted_size(inode, (size_t) sbuf.st_size);
            inode_set_sync_size(inode, (size_t) sbuf.st_size);
            inode_set_large_file_status(inode, false);
        }

        util_mutex_lock(&mmap_cache_mutex);
        void *mmaps = hash_map_get(global_mmap_cache, inode->serialno);

        if (mmaps != NULL) {
            if (inode->file_mmaps != NULL)
                splitfs_mmap_add(inode->file_mmaps);
            inode->file_mmaps = mmaps;
            hash_map_remove(global_mmap_cache, inode->serialno, mmaps);
        } else {
            if (inode->file_mmaps == NULL) {
                mmaps = splitfs_mmap_assign();
                if (mmaps == NULL) {
                    inode->file_mmaps = calloc(1, PER_NODE_MMAPS * sizeof(uint64_t));
                }
            }
        }
        util_mutex_unlock(&mmap_cache_mutex);

        util_mutex_lock(&tbl_mmap_mutex);
        void *tbl_mmaps = hash_map_get(tbl_mmap_cache, inode->serialno);
        if (tbl_mmaps) {
            hash_map_remove(tbl_mmap_cache, inode->serialno, tbl_mmaps);
        }
        util_mutex_unlock(&tbl_mmap_mutex);

        inode->tbl_mmap = (struct table_mmap *)tbl_mmaps;

        if (inode->tbl_mmap == NULL) {
            inode->tbl_mmap = splitfs_alloc_tbl();
        }
    }

    ASSERT(inode->tbl_mmap);
    file->vinode = inode;

    os_rwlock_unlock(&inode->rwlock);
    pthread_mutex_unlock(&file->mutex);

    return file;
}

long splitfs_close(long fd, SPLITFSfile *file) {

    LOG(0, "In splitfs_close. fd = %ld", fd);

    long ret = 0;
    struct splitfs_vinode *inode = file->vinode;
    file->vinode = NULL;

    ret = splitfs_vinode_unref(fd, inode);
    if (ret == 0)
        splitfs_file_add(file);

    LOG(0, "file closed");
    return 0;
}

SPLITFSfile *
splitfs_openat(const char *path, long flags, ...) {

	if (!path) {
		LOG(LUSR, "NULL pathname");
		errno = ENOENT;
		return NULL;
	}

	va_list ap;
	va_start(ap, flags);
	long mode = 0;
	if (flags & SPLITFS_O_CREAT)
		mode = va_arg(ap, long);
	va_end(ap);

    SPLITFSfile *ret = _splitfs_openat(path, flags, mode);

    return ret;
}
