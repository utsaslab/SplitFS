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
 * os_locks.h -- wrappers around system locking functions
 */

#ifndef PMEMFILE_OS_LOCKS_H
#define PMEMFILE_OS_LOCKS_H 1

typedef struct {
	long long data[8];
} os_mutex_t;

typedef struct {
	long long data[8];
} os_rwlock_t;

/*
 * os_mutex_init -- system mutex init wrapper that never fails from
 * caller perspective. If underlying function failed, this function aborts
 * the program.
 */
void os_mutex_init(os_mutex_t *m);

/*
 * os_mutex_destroy -- system mutex destroy wrapper that never fails from
 * caller perspective. If underlying function failed, this function aborts
 * the program.
 */
void os_mutex_destroy(os_mutex_t *m);

/*
 * os_mutex_lock -- system mutex lock wrapper that never fails from
 * caller perspective. If underlying function failed, this function aborts
 * the program.
 */
void os_mutex_lock(os_mutex_t *m);

/*
 * os_mutex_unlock -- system mutex unlock wrapper that never fails from
 * caller perspective. If underlying function failed, this function aborts
 * the program.
 */
void os_mutex_unlock(os_mutex_t *m);

/*
 * os_rwlock_init -- system rwlock init wrapper that never fails from
 * caller perspective. If underlying function failed, this function aborts
 * the program.
 */
void os_rwlock_init(os_rwlock_t *m);

/*
 * os_rwlock_rdlock -- system rwlock rdlock wrapper that never fails from
 * caller perspective. If underlying function failed, this function aborts
 * the program.
 */
void os_rwlock_rdlock(os_rwlock_t *m);

/*
 * os_rwlock_wrlock -- system rwlock wrlock wrapper that never fails from
 * caller perspective. If underlying function failed, this function aborts
 * the program.
 */
void os_rwlock_wrlock(os_rwlock_t *m);

/*
 * os_rwlock_unlock -- system rwlock unlock wrapper that never fails from
 * caller perspective. If underlying function failed, this function aborts
 * the program.
 */
void os_rwlock_unlock(os_rwlock_t *m);

/*
 * os_rwlock_destroy -- system rwlock destroy wrapper that never fails from
 * caller perspective. If underlying function failed, this function aborts
 * the program.
 */
void os_rwlock_destroy(os_rwlock_t *m);

typedef unsigned os_tls_key_t;

int os_tls_key_create(os_tls_key_t *key, void (*destr_function)(void *));

void *os_tls_get(os_tls_key_t key);
int os_tls_set(os_tls_key_t key, const void *ptr);

typedef int os_once_t;

void os_once(os_once_t *once, void (*init_routine)(void));

#endif
