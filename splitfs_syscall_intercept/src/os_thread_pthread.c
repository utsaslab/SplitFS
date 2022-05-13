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
 * os_locks_pthread.c -- wrappers around system locking functions
 */

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "os_thread.h"
#include "out.h"

void
os_mutex_init(os_mutex_t *m)
{
	COMPILE_ERROR_ON(sizeof(os_mutex_t) < sizeof(pthread_mutex_t));
	int tmp = pthread_mutex_init((pthread_mutex_t *)m, NULL);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_mutex_init");
	}
}

void
os_mutex_destroy(os_mutex_t *m)
{
	int tmp = pthread_mutex_destroy((pthread_mutex_t *)m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_mutex_destroy");
	}
}

void
os_mutex_lock(os_mutex_t *m)
{
	int tmp = pthread_mutex_lock((pthread_mutex_t *)m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_mutex_lock");
	}
}

void
os_mutex_unlock(os_mutex_t *m)
{
	int tmp = pthread_mutex_unlock((pthread_mutex_t *)m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_mutex_unlock");
	}
}

void
os_rwlock_init(os_rwlock_t *m)
{
	COMPILE_ERROR_ON(sizeof(os_rwlock_t) < sizeof(pthread_rwlock_t));
	int tmp = pthread_rwlock_init((pthread_rwlock_t *)m, NULL);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_rwlock_init");
	}
}

void
os_rwlock_rdlock(os_rwlock_t *m)
{
	int tmp = pthread_rwlock_rdlock((pthread_rwlock_t *)m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_rwlock_rdlock");
	}
}

void
os_rwlock_wrlock(os_rwlock_t *m)
{
	int tmp = pthread_rwlock_wrlock((pthread_rwlock_t *)m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_rwlock_wrlock");
	}
}

void
os_rwlock_unlock(os_rwlock_t *m)
{
	int tmp = pthread_rwlock_unlock((pthread_rwlock_t *)m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_rwlock_unlock");
	}
}

void
os_rwlock_destroy(os_rwlock_t *m)
{
	int tmp = pthread_rwlock_destroy((pthread_rwlock_t *)m);
	if (tmp) {
		errno = tmp;
		FATAL("!pthread_rwlock_destroy");
	}
}

int
os_tls_key_create(os_tls_key_t *key, void (*destr_function)(void *))
{
	COMPILE_ERROR_ON(sizeof(os_tls_key_t) < sizeof(pthread_key_t));

	return pthread_key_create((pthread_key_t *)key, destr_function);
}

void *
os_tls_get(os_tls_key_t key)
{
	return pthread_getspecific((pthread_key_t)key);
}

int
os_tls_set(os_tls_key_t key, const void *ptr)
{
	return pthread_setspecific((pthread_key_t)key, ptr);
}

void
os_once(os_once_t *once, void (*init_routine)(void))
{
	int tmp = pthread_once(once, init_routine);
	if (tmp) {
		errno = tmp;
			FATAL("!pthread_once");
	}
}
