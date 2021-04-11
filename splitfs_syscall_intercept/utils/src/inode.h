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

#ifndef SPLITFS_INODE_H
#define SPLITFS_INODE_H

#include <stdint.h>
#include <time.h>
#include <stdbool.h>

#include "splitfs-posix.h"
#include "os_thread.h"
#include "staging.h"

#define PER_NODE_MMAPS 1024
#define DIRTY_MMAP_CACHE_SIZE 20

/*
#define SPLITFS_S_LONGSYMLINK 0x10000
COMPILE_ERROR_ON((SPLITFS_S_IFMT | SPLITFS_ALLPERMS) &
		SPLITFS_S_LONGSYMLINK);
*/

/* volatile inode */
struct splitfs_vinode {
	/* reference counter */
	uint32_t ref;

	/* read-write lock, also protects inode read/writes */
	os_rwlock_t rwlock;

	/* list of mmaps of the file */
	uint64_t *file_mmaps;

	/* inode number of the file */
	uint32_t serialno;

    /* length matching with file system */
	size_t sync_length;

    /* uncommitted length due to appends */
	volatile size_t length;

	//int free_list_idx;

    /* cache of dirty file mmaps for use during close */
    //uint32_t *dirty_file_mmap_cache;

	//int root_dirty_num;

    /* total dirty mmaps */
	//int total_dirty_file_mmaps;

    /* large files are handled differently */
	bool is_large_file;

	// Staging file
    struct sfile_description *staging;

    // Table mmap
    struct table_mmap *tbl_mmap;
};

static inline size_t inode_get_sync_size(struct splitfs_vinode *i) {
    return (i->sync_length);
}

static inline size_t inode_get_uncommitted_size(struct splitfs_vinode *i) {
    return (i->length);
}

static inline uint32_t inode_get_ino(struct splitfs_vinode *i) {
    return (i->serialno);
}

static inline uint32_t inode_get_ref(struct splitfs_vinode *i) {
    return (i->ref);
}

/*
static inline int inode_get_dirty_mmaps(struct splitfs_vinode *i) {
    return (i->total_dirty_file_mmaps);
}
*/

static inline bool inode_get_large_file_status(struct splitfs_vinode *i) {
    return (i->is_large_file);
}

static inline void inode_set_sync_size(struct splitfs_vinode *i, uint64_t size) {
    i->sync_length = size;
}

static inline void inode_set_uncommitted_size(struct splitfs_vinode *i, uint64_t size) {
    i->length = size;
}

static inline void inode_set_ino(struct splitfs_vinode *i, uint32_t ino) {
    i->serialno = ino;
}

static inline void inode_set_ref(struct splitfs_vinode *i, uint32_t ref_val) {
    i->ref = ref_val;
}

/*
static inline void inode_set_dirty_mmaps(struct splitfs_vinode *i, int dirty_mmaps) {
    i->total_dirty_file_mmaps = dirty_mmaps;
}
*/

static inline void inode_set_large_file_status(struct splitfs_vinode *i, bool status) {
    i->is_large_file = status;
}

static inline void inode_inc_ref_val(struct splitfs_vinode *i, uint32_t inc_val) {
    i->ref += inc_val;
}

static inline void inode_inc_sync_size(struct splitfs_vinode *i, uint64_t inc_size) {
    i->sync_length += inc_size;
}

static inline void inode_inc_uncommitted_size(struct splitfs_vinode *i, uint64_t inc_size) {
    i->length += inc_size;
}

/*
static inline void inode_inc_dirty_mmaps(struct splitfs_vinode *i, int inc_dirty) {
    i->total_dirty_file_mmaps += inc_dirty;
}
*/

void splitfs_inode_free_list_init(void);
struct splitfs_vinode *splitfs_vinode_assign(uint32_t serialno);
void splitfs_vinode_add(struct splitfs_vinode *entry);
long splitfs_vinode_unref(long fd, struct splitfs_vinode *inode);
struct splitfs_vinode *splitfs_map_vinode_check(uint32_t serialno);

#endif
