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
 * Gosh, this path resolving was pain to write.
 * XXX: clean up this whole file, and add some more explanations.
 */

#include <assert.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdlib.h>
#include <inttypes.h>
#include <syscall.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "libsyscall_intercept_hook_point.h"
#include "splitfs-posix.h"

#include "preload.h"
#include "out.h"

static void
_resolve_path(long fd_at,
	      const char *path,
	      struct resolved_path *result)
{
	if (path == NULL) {
		result->error_code = -EFAULT;
		return;
	}

	size_t size; /* The length of the whole path to be resolved. */
    size_t result_path_size = 0;
	result->error_code = 0;

    if (path[0] != '/') {
        if (fd_at == AT_FDCWD) {
            char cwd_path[PATH_MAX];
            if (getcwd(cwd_path, sizeof(cwd_path)) == NULL) {
                result->error_code = -ENOTDIR;
                return;
            }
            for (size = 0; cwd_path[size] != '\0'; size++) {
                result->path[size] = cwd_path[size];
            }
            result_path_size = size;
        } else {
            /* Get the stat buf of the directory fd_at */
            char at_path[PATH_MAX];
            char proc_fd_path[PATH_MAX];
            long readlink_ret;

            sprintf(proc_fd_path, "/proc/self/fd/%ld", fd_at);
            readlink_ret = syscall_no_intercept(SYS_readlink, proc_fd_path, at_path, sizeof(at_path));
            if (readlink_ret == -1) {
                result->error_code = -ENOTDIR;
                return;
            }
            at_path[readlink_ret] = '\0';

            for (size = 0; at_path[size] != '\0'; ++size) {
                result->path[result_path_size] = at_path[size];
                result_path_size++;
            }
        }
    }

    // Suppose we open 'file.txt' in cwd '/root/wspace' then the path should be 
    // '/root/wspace/file.txt' not '/root/wspacefile.txt' hence adding a '/'
    if (path[0] != '\0' && result_path_size > 0 && result->path[result_path_size-1] != '/') {
        result->path[result_path_size] = '/';
        result_path_size++;
    }

    /* Copy input path to result */
    for (size = 0; path[size] != '\0'; ++size) {
    	result->path[result_path_size] = path[size];
        result_path_size++;
        if (result_path_size == (sizeof(result->path) - 1)) {
            result->error_code = -ENAMETOOLONG;
            return;
        }
	}
    result->path[result_path_size] = '\0';

    result->path_len = result_path_size;

	if (size == 0) { /* empty string */
		result->error_code = -ENOTDIR;
		return;
	}
}

/*
 * resolve_path - the main logic for resolving paths containing arbitrary
 * combinations of path components in the kernel's vfs and pmemfile pools.
 *
 * The at argument describes the starting directory of the path resolution,
 * It can refer to either a directory in pmemfile pool, or a directory accessed
 * via the kernel.
 */
void
resolve_path(long fd_at,
	     const char *path,
	     struct resolved_path *result)
{
	_resolve_path(fd_at, path, result);
}
