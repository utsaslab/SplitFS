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
 * sys_util.h -- internal utility wrappers around system functions
 */

#ifndef SPLITFS_SYS_UTIL_H
#define SPLITFS_SYS_UTIL_H 1

#include <errno.h>
#include <string.h>
#include <pthread.h>

#include "libsyscall_intercept_hook_point.h"
#include "compiler_utils.h"

#define SPLITFS_PRELOAD_EXIT_NOT_SUPPORTED	95
#define SPLITFS_PRELOAD_EXIT_TOO_MANY_FDS	96
#define SPLITFS_PRELOAD_EXIT_GETCWD_FAILED	97
#define SPLITFS_PRELOAD_EXIT_CWD_STAT_FAILED	98
#define SPLITFS_PRELOAD_EXIT_POOL_OPEN_FAILED	99
#define SPLITFS_PRELOAD_EXIT_CONFIG_ERROR	100
#define SPLITFS_PRELOAD_EXIT_FATAL_CONDITION	128 + 6

void
exit_with_msg(int ret, const char *msg);

static inline void
FATAL(const char *str)
{
	exit_with_msg(SPLITFS_PRELOAD_EXIT_FATAL_CONDITION, str);
}

/*
 * util_mutex_init -- pthread_mutex_init variant that never fails from
 * caller perspective. If pthread_mutex_init failed, this function aborts
 * the program.
 */
static inline void
util_mutex_init(pthread_mutex_t *m)
{
	int tmp = pthread_mutex_init(m, NULL);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_mutex_init");
	}
}

/*
 * util_mutex_destroy -- pthread_mutex_destroy variant that never fails from
 * caller perspective. If pthread_mutex_destroy failed, this function aborts
 * the program.
 */
static inline void
util_mutex_destroy(pthread_mutex_t *m)
{
	int tmp = pthread_mutex_destroy(m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_mutex_destroy");
	}
}

/*
 * util_mutex_lock -- pthread_mutex_lock variant that never fails from
 * caller perspective. If pthread_mutex_lock failed, this function aborts
 * the program.
 */
static inline void
util_mutex_lock(pthread_mutex_t *m)
{
	int tmp = pthread_mutex_lock(m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_mutex_lock");
	}
}

/*
 * util_mutex_unlock -- pthread_mutex_unlock variant that never fails from
 * caller perspective. If pthread_mutex_unlock failed, this function aborts
 * the program.
 */
static inline void
util_mutex_unlock(pthread_mutex_t *m)
{
	int tmp = pthread_mutex_unlock(m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_mutex_unlock");
	}
}

/*
 * util_rwlock_init -- pthread_rwlock_init variant that never fails from
 * caller perspective. If pthread_rwlock_init failed, this function aborts
 * the program.
 */
static inline void
util_rwlock_init(pthread_rwlock_t *m)
{
	int tmp = pthread_rwlock_init(m, NULL);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_rwlock_init");
	}
}

/*
 * util_rwlock_rdlock -- pthread_rwlock_rdlock variant that never fails from
 * caller perspective. If pthread_rwlock_rdlock failed, this function aborts
 * the program.
 */
static inline void
util_rwlock_rdlock(pthread_rwlock_t *m)
{
	int tmp = pthread_rwlock_rdlock(m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_rwlock_rdlock");
	}
}

/*
 * util_rwlock_wrlock -- pthread_rwlock_wrlock variant that never fails from
 * caller perspective. If pthread_rwlock_wrlock failed, this function aborts
 * the program.
 */
static inline void
util_rwlock_wrlock(pthread_rwlock_t *m)
{
	int tmp = pthread_rwlock_wrlock(m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_rwlock_wrlock");
	}
}

/*
 * util_rwlock_unlock -- pthread_rwlock_unlock variant that never fails from
 * caller perspective. If pthread_rwlock_unlock failed, this function aborts
 * the program.
 */
static inline void
util_rwlock_unlock(pthread_rwlock_t *m)
{
	int tmp = pthread_rwlock_unlock(m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_rwlock_unlock");
	}
}

/*
 * util_rwlock_destroy -- pthread_rwlock_destroy variant that never fails from
 * caller perspective. If pthread_rwlock_destroy failed, this function aborts
 * the program.
 */
static inline void
util_rwlock_destroy(pthread_rwlock_t *m)
{
	int tmp = pthread_rwlock_destroy(m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_rwlock_destroy");
	}
}

#endif
