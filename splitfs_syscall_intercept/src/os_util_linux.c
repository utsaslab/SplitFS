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

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "os_util.h"

int
os_getpid(void)
{
	return getpid();
}

void
os_describe_errno(int errnum, char *buf, size_t buflen)
{
	/*
	 * There are 2 versions of strerror_r - returning int and char * -
	 * defined depending on feature macros. We want int variant, so to
	 * catch accidental change in the definition we use temporary int
	 * variable.
	 */
	int r = strerror_r(errnum, buf, buflen);
	if (r)
		snprintf(buf, buflen, "Unknown errno %d", errnum);
}

#ifdef DEBUG
const char *
os_getexecname(void)
{
	static char namepath[PATH_MAX + 1];
	char procpath[PATH_MAX];
	ssize_t cc;

	snprintf(procpath, PATH_MAX, "/proc/%d/exe", os_getpid());

	if ((cc = readlink(procpath, namepath, PATH_MAX)) < 0)
		strcpy(namepath, "unknown");
	else
		namepath[cc] = '\0';

	return namepath;
}
#endif	/* DEBUG */

int
os_usleep(unsigned usec)
{
	return usleep(usec);
}
