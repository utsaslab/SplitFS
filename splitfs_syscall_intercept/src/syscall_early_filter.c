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

/*
 * syscall_early_filter.c -- the table of early filter flags, and
 * function(s) to access the table.
 *
 * For explanations, see syscall_early_filter.h
 */

#include "syscall_early_filter.h"

#include <stdlib.h>
#include "sys_util.h"

static struct syscall_early_filter_entry filter_table[] = {
	[SYS_close] = {
		.must_handle = true,
	},
	[SYS_creat] = {
		.must_handle = true,
	},
	[SYS_fallocate] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_fcntl] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_fdatasync] = {
		.must_handle = true,
		.fd_first_arg = true,
		.returns_zero = true,
	},
	[SYS_fstat] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_fstatfs] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_fsync] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_ftruncate] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_linkat] = {
		.must_handle = true,
	},
	[SYS_link] = {
		.must_handle = true,
	},
	[SYS_lseek] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_lstat] = {
		.must_handle = true,
	},
	[SYS_mkdirat] = {
		.must_handle = false,
	},
	[SYS_mkdir] = {
		.must_handle = false,
	},
	[SYS_mknod] = {
		.must_handle = true,
	},
	[SYS_mknodat] = {
		.must_handle = true,
	},
	[SYS_newfstatat] = {
		.must_handle = true,
	},
	[SYS_openat] = {
		.must_handle = true,
	},
	[SYS_open] = {
		.must_handle = true,
	},
	[SYS_pread64] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_preadv] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_pwrite64] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_pwritev] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_read] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_readv] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_renameat2] = {
		.must_handle = true,
	},
	[SYS_renameat] = {
		.must_handle = true,
	},
	[SYS_rename] = {
		.must_handle = true,
	},
	[SYS_stat] = {
		.must_handle = true,
	},
	[SYS_statfs] = {
		.must_handle = true,
	},
	[SYS_symlinkat] = {
		.must_handle = true,
	},
	[SYS_symlink] = {
		.must_handle = true,
	},
	[SYS_syncfs] = {
		.must_handle = true,
		.fd_first_arg = true,
		.returns_zero = true,
	},
	[SYS_truncate] = {
		.must_handle = true,
	},
	[SYS_unlinkat] = {
		.must_handle = true,
	},
	[SYS_unlink] = {
		.must_handle = true,
	},
	[SYS_write] = {
		.must_handle = true,
		.fd_first_arg = true,
	},
	[SYS_writev] = {
		.must_handle = true,
		.fd_first_arg = true,
	},

	/* Syscalls not handled yet */
	[SYS_dup2] = {
		.must_handle = true,
	},
	[SYS_dup3] = {
		.must_handle = true,
	},
	[SYS_dup] = {
		.must_handle = true,
	},
	[SYS_execveat] = {
		.must_handle = true,
	},
	[SYS_execve] = {
		.must_handle = true,
	},
	[SYS_mmap] = {
		.must_handle = true,
	},
	[SYS_readahead] = {
		.must_handle = true,
		.fd_first_arg = true,
		.returns_ENOTSUP = true,
	},
};

struct syscall_early_filter_entry
get_early_filter_entry(long syscall_number)
{
	if (syscall_number < 0 ||
	    (size_t)syscall_number >= ARRAY_SIZE(filter_table))
		return (struct syscall_early_filter_entry) {false, };

	return filter_table[syscall_number];
}
