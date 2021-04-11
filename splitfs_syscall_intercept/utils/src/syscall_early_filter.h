/*
 * Copyright 2017, Intel Corporation
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

#ifndef SPLITFS_SRC_SYSCALL_EARLY_FILTER_H
#define SPLITFS_SRC_SYSCALL_EARLY_FILTER_H

#include <stdbool.h>
#include <sys/syscall.h>

/*
 * Some flags controlling syscall handling at a high level.
 * These flags are contained in a table, provide information that
 * depend on the syscall number of syscall to be handled, and can be used before
 * parsing any arguments of a syscall (thus called "early").
 * The most important aspect to remember is: "before parsing any arguments of
 * a syscall". Which really just means "syscall specific parsing...", i.e.
 * parsing that is specific to a single syscall.
 * In practice, some of these flags are applicable to any syscall, and some
 * are only applicable to some of them.
 * These flags should not be used for anything that is specific to a single
 * syscall.
 *
 * As of this writing, this is only used in the preload.c source file.
 * Please see the function called hook in preload.c
 */
struct syscall_early_filter_entry {
	/* Might this be pmemfile related, or can this syscall be ignored? */
	bool must_handle;

	/*
	 * The fd_first_arg flag marks syscalls, which accept a file descriptor
	 * as their first argument. This allows libpmemfile to easily isolate
	 * the process of checking the first argument, and making a decision
	 * based on that fd being associated with pmemfile-posix or not.
	 * This is used to fetch pmemfile pointer and pass it to syscall
	 * instead of fd. One exception is close which must get fd so flag
	 * is not set for that syscall.
	 * Some obvious examples: read, write, fstat, etc...
	 *
	 * But:
	 * Only those which operate on the file specified by this fd. The *_at
	 * syscalls usually also accept an fd as a first argument, but those
	 * directory references must be processed in a different way.
	 * For example the first argument of openat is an fd, but it does not
	 * refer to a file that is potentially handled using libpmemfile-posix.
	 * Instead, the decision (to forward to libpmemfile-posix or to the
	 * kernel) is made after fully resolving the path.
	 */
	bool fd_first_arg;

	/*
	 * The returns_zero flag marks syscalls which operate on a file
	 * descriptor, and just return zero whenever that fd is associated
	 * with a pmemfile-posix handled file. This allows an easy
	 * implementation of some effectively NOP syscalls, e.g.:
	 * The syncfs syscall can always be considered successful when
	 * called with an fd referring to a pmemfile resident file -- thus
	 * libpmemfile can return zero (indicating success), without parsing
	 * any of the syscall arguments.
	 *
	 * Important: this only has meaning once it is known, that the syscall
	 * attempts to operate on a pmemfile handled file (kernel handled file
	 * descriptors of course should be forwarded to the kernel). Thus,
	 * it is not strictly true, that it allows libpmemfile to make a
	 * decision before parsing any of the arguments.
	 * Still, this logic can be shared between multiple syscalls, thus
	 * it can still be considered "early" filtering -- filtering before
	 * parsing the other arguments specific to the syscall.
	 *
	 * This implies, that returns_zero can only be set when the
	 * fd_first_arg flag is also set (this covers common cases, where
	 * the first argument is an fd, and zero can be returned).
	 */
	bool returns_zero;

	/*
	 * The idea behind the returns_ENOTSUP flag is the exactly the same
	 * as in the case of the returns_zero flag (except for the return
	 * value). E.g. as long as writev is not implemented for pmemfile
	 * handled files, the libpmemfile library can return ENOTSUP for a
	 * writev syscalls.
	 */
	bool returns_ENOTSUP;
};

/*
 * get_early_filter_entry -- returns a filter entry with flags corresponding
 * to the syscall number. When called with an invalid syscall number (e.g. -1),
 * all returned flags are set to false.
 */
struct syscall_early_filter_entry get_early_filter_entry(long syscall_number);

#endif
