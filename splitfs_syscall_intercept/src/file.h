/*
 *
 * =====================================================================================
 *
 *       Filename:  file.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:14:13 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#ifndef SPLITFS_FILE_H
#define SPLITFS_FILE_H

#include <nv_common.h>
#include "lfq.h"
#include "nvp_lock.h"
#include "inode.h"

struct NVFile
{
	NVP_LOCK_DECL;
	volatile bool valid;
	int fd;
	volatile size_t* offset;
	bool canRead;
	bool canWrite;
	bool append;
	bool aligned;
	ino_t serialno; // duplicated so that iterating doesn't require following every node*
	struct NVNode* node;
	bool posix;
	bool debug;
	char padding[200];
	int file_stream_flags;
};

struct backupRoots {
	unsigned long *root;
	unsigned long *merkle_root;
	unsigned long *root_dirty_cache;
};

#define TOTAL_CLOSED_INODES 4096

#define FD_LOCKING 1
#if FD_LOCKING

#define NVP_LOCK_FD_RD(nvf, cpuid)	NVP_LOCK_RD(nvf->lock, cpuid)
#define NVP_UNLOCK_FD_RD(nvf, cpuid)	NVP_LOCK_UNLOCK_RD(nvf->lock, cpuid)
#define NVP_LOCK_FD_WR(nvf)		NVP_LOCK_WR(	   nvf->lock)
#define NVP_UNLOCK_FD_WR(nvf)		NVP_LOCK_UNLOCK_WR(nvf->lock)

#else

#define NVP_LOCK_FD_RD(nvf, cpuid) {(void)(cpuid);}
#define NVP_UNLOCK_FD_RD(nvf, cpuid) {(void)(cpuid);}
#define NVP_LOCK_FD_WR(nvf) {(void)(nvf->lock);}
#define NVP_UNLOCK_FD_WR(nvf) {(void)(nvf->lock);}

#endif

extern struct NVFile* _nvp_fd_lookup;

// Index by fd no. If true intercept, else
bool* _fd_intercept_lookup;

void nvp_cleanup_node(struct NVNode *node, int free_root, int unmap_btree);

RETT_SYSCALL_INTERCEPT _sfs_REAL_CLOSE(int file, ino_t serialno, int async_file_closing, long* result);

#endif
