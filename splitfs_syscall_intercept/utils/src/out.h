/*
 * Copyright 2014-2017, Intel Corporation
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
 * out.h -- definitions for "out" module
 */

#ifndef SPLITFS_OUT_H
#define SPLITFS_OUT_H 1

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "compiler_utils.h"
#include "libsyscall_intercept_hook_point.h"

#define LUSR 2  /* user error */
#define LINF 3  /* information */
#define LDBG 4  /* debug info */
#define LTRC 10 /* traces, very verbose */

/*
 * Suppress errors which are after appropriate ASSERT* macro for nondebug
 * builds.
 */

#if !defined(DEBUG) && (defined(__clang_analyzer__) || defined(__COVERITY__))
#define OUT_FATAL_DISCARD_NORETURN void
#else
#define OUT_FATAL_DISCARD_NORETURN
#endif

#ifndef OUT_ENABLED
#ifdef DEBUG
#define OUT_ENABLED 1
#else
#define OUT_ENABLED 0
#endif
#endif

#if OUT_ENABLED == 1

#define DEBUG_ENABLED 1
#define OUT_LOG out_log
#define OUT_NONL out_nonl
#define OUT_FATAL out_fatal
#define OUT_FATAL_ABORT out_fatal

#else

static inline splitfs_printf_like(7, 8) void
out_log_discard(const char *file, int line, const char *func, int level, int pid, pthread_t tid,
		const char *fmt, ...)
{
	(void) file;
	(void) line;
	(void) func;
	(void) level;
    (void) pid;
    (void) tid;
	(void) fmt;
}

static inline splitfs_printf_like(2, 3) void
out_nonl_discard(int level, const char *fmt, ...)
{
	(void) level;
	(void) fmt;
}

static inline splitfs_printf_like(4, 5) OUT_FATAL_DISCARD_NORETURN void
out_fatal_discard(const char *file, int line, const char *func,
		const char *fmt, ...)
{
	(void) file;
	(void) line;
	(void) func;
	(void) fmt;
}

static inline splitfs_printf_like(4, 5) splitfs_noreturn void
out_fatal_abort(const char *file, int line, const char *func,
		const char *fmt, ...)
{
	(void) file;
	(void) line;
	(void) func;
	(void) fmt;

	abort();
}

#define DEBUG_ENABLED 0
#define OUT_LOG out_log_discard
#define OUT_NONL out_nonl_discard
#define OUT_FATAL out_fatal_discard
#define OUT_FATAL_ABORT out_fatal_abort

#endif

/*
 * Trigger compile time error with non-zero builtin constants.
 * Pro: this macro can be used non compile time constants.
 * Con: this macro can only be used in function scope.
 */
#define ASSERT_COMPILE_ERROR_ON(cond) ((void)sizeof(char[(cond) ? -1 : 1]));

/* produce debug/trace output */
#define LOG(level, ...) do { \
	if (!DEBUG_ENABLED) break;\
	OUT_LOG(__FILE__, __LINE__, __func__, level, getpid(), pthread_self(), __VA_ARGS__);\
} while (0)

/* produce debug/trace output without prefix and new line */
#define LOG_NONL(level, ...) do { \
	if (!DEBUG_ENABLED) break; \
	OUT_NONL(level, __VA_ARGS__); \
} while (0)

/* produce output and exit 
#define FATAL(...)\
	OUT_FATAL_ABORT(__FILE__, __LINE__, __func__, __VA_ARGS__)
*/

/* assert a condition is true at runtime */
#define ASSERT_rt(cnd) do { \
	if (!DEBUG_ENABLED || (cnd)) break; \
	OUT_FATAL(__FILE__, __LINE__, __func__, "assertion failure: %s", #cnd);\
} while (0)

/* assertion with extra info printed if assertion fails at runtime */
#define ASSERTinfo_rt(cnd, info) do { \
	if (!DEBUG_ENABLED || (cnd)) break; \
	OUT_FATAL(__FILE__, __LINE__, __func__, \
		"assertion failure: %s (%s = %s)", #cnd, #info, info);\
} while (0)

/* assert two integer values are equal at runtime */
#define ASSERTeq_rt(lhs, rhs) do { \
	if (!DEBUG_ENABLED || ((lhs) == (rhs))) break; \
	OUT_FATAL(__FILE__, __LINE__, __func__,\
	"assertion failure: %s (0x%llx) == %s (0x%llx)", #lhs,\
	(unsigned long long)(lhs), #rhs, (unsigned long long)(rhs)); \
} while (0)

/* assert two integer values are not equal at runtime */
#define ASSERTne_rt(lhs, rhs) do { \
	if (!DEBUG_ENABLED || ((lhs) != (rhs))) break; \
	OUT_FATAL(__FILE__, __LINE__, __func__,\
	"assertion failure: %s (0x%llx) != %s (0x%llx)", #lhs,\
	(unsigned long long)(lhs), #rhs, (unsigned long long)(rhs)); \
} while (0)

/* assert a condition is true */
#define ASSERT(cnd)\
	do {\
		/*\
		 * Detect useless asserts on always true expression. Please use\
		 * COMPILE_ERROR_ON(!cnd) or ASSERT_rt(cnd) in such cases.\
		 */\
		if (__builtin_constant_p(cnd))\
			ASSERT_COMPILE_ERROR_ON(cnd);\
		ASSERT_rt(cnd);\
	} while (0)

/* assertion with extra info printed if assertion fails */
#define ASSERTinfo(cnd, info)\
	do {\
		/* See comment in ASSERT. */\
		if (__builtin_constant_p(cnd))\
			ASSERT_COMPILE_ERROR_ON(cnd);\
		ASSERTinfo_rt(cnd);\
	} while (0)

/* assert two integer values are equal */
#define ASSERTeq(lhs, rhs)\
	do {\
		/* See comment in ASSERT. */\
		if (__builtin_constant_p(lhs) && __builtin_constant_p(rhs))\
			ASSERT_COMPILE_ERROR_ON((lhs) == (rhs));\
		ASSERTeq_rt(lhs, rhs);\
	} while (0)

/* assert two integer values are not equal */
#define ASSERTne(lhs, rhs)\
	do {\
		/* See comment in ASSERT. */\
		if (__builtin_constant_p(lhs) && __builtin_constant_p(rhs))\
			ASSERT_COMPILE_ERROR_ON((lhs) != (rhs));\
		ASSERTne_rt(lhs, rhs);\
	} while (0)

#define ERR(...)\
	out_err(__FILE__, __LINE__, __func__, __VA_ARGS__)

void out_init(const char *log_prefix, const char *log_level_var,
		const char *log_file_var);
void out_fini(void);
void out_log(const char *file, int line, const char *func, int level, int pid, pthread_t tid,
	const char *fmt, ...) splitfs_printf_like(7, 8);
void out_err(const char *file, int line, const char *func,
	const char *fmt, ...) splitfs_printf_like(4, 5);
void out_fatal(const char *file, int line, const char *func,
	const char *fmt, ...) splitfs_printf_like(4, 5) splitfs_noreturn;
void out_set_print_func(void (*print_func)(const char *s));
void out_set_vsnprintf_func(int (*vsnprintf_func)(char *str, size_t size,
	const char *format, va_list ap));
const char *out_get_errormsg(void);

#endif
