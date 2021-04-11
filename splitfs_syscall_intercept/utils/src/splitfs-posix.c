
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
 * pmemfile-posix.c -- library constructor / destructor
 */

#define _GNU_SOURCE

#include <limits.h>
#include <stdio.h>

#include "compiler_utils.h"
#include "file.h"
#include "hash_map.h"
#include "staging.h"
#include "staging_thread.h"
#include "splitfs-posix.h"
#include "sys_util.h"
#include "out.h"
#include "intel_intrin.h"
#include "execv.h"

#define SPLITFS_POSIX_LOG_PREFIX "splitfs-posix"
#define SPLITFS_POSIX_LOG_LEVEL_VAR "SPLITFS_POSIX_LOG_LEVEL"
#define SPLITFS_POSIX_LOG_FILE_VAR "SPLITFS_POSIX_LOG_FILE"

struct hash_map *file_inode_map = NULL;
struct hash_map *global_mmap_cache = NULL;
struct hash_map *tbl_mmap_cache = NULL;
pthread_mutex_t mmap_cache_mutex;
pthread_mutex_t tbl_mmap_mutex;
bool exit_staging_thread = false;

/*
 * splitfs_posix_init -- load-time initialization for splitfs-posix
 *
 * Called automatically by the run-time loader.
 */
splitfs_constructor void
splitfs_posix_init(void)
{

	out_init(SPLITFS_POSIX_LOG_PREFIX, SPLITFS_POSIX_LOG_LEVEL_VAR,
			SPLITFS_POSIX_LOG_FILE_VAR);
	LOG(LDBG, NULL);
    LOG(0, "constructor invoked");

    /*  Write splitfs init stuff here */

    /* Point _mm_flush to the right instruction based on availability of 
       clflushopt cpu instruction */
    splitfs_init_mm_flush();

    /*  Create splitfs_file table */
    splitfs_file_table_init();

    /*  Create splitfs_vinode table */
    splitfs_inode_free_list_init();

    /*  Create file to inode mapping */
    file_inode_map = hash_map_alloc();

    /*  Create global mmap cache */
    global_mmap_cache = hash_map_alloc();
    pthread_mutex_init(&mmap_cache_mutex, NULL);

    /* Create table_mmaps for appends */
    tbl_mmap_cache = hash_map_alloc();
    pthread_mutex_init(&tbl_mmap_mutex, NULL);

    /* Create files for appends */
    splitfs_spool_init();

    /* Start staging thread */
    splitfs_start_thread();

    /* Restore FDs after exec */
    splitfs_restore_fd_if_exec();
}

/*
 * libpmemfile_posix_fini -- libpmemfile-posix cleanup routine
 *
 * Called automatically when the process terminates.
 */
splitfs_destructor void
splitfs_posix_fini(void)
{
	LOG(LDBG, NULL);
    exit_staging_thread = true;
	out_fini();
}

/*
 * pmemfile_errormsg -- return last error message
 */
const char *
splitfs_errormsg(void)
{
	return out_get_errormsg();
}
