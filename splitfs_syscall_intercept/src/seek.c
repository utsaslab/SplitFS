/*
 * =====================================================================================
 *
 *       Filename:  seek.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:53:31 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
// required for sched_getcpu (GET_CPUID)
#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif
#include <libsyscall_intercept_hook_point.h>
#include <stdlib.h>

#include <nv_common.h>
#include "nvp_lock.h"
#include "file.h"
#include "inode.h"
#include "timers.h"

off64_t _nvp_do_seek64(int file, off64_t offset, int whence, struct NVFile *nvf)
{
	DEBUG("_nvp_do_seek64\n");

	//struct NVFile* nvf = &_nvp_fd_lookup[file];

	DEBUG("_nvp_do_seek64: file len %li, map len %li, current offset %li, "
		"requested offset %li with whence %li\n", 
		nvf->node->length, nvf->node->maplength, *nvf->offset,
		offset, whence);

	switch(whence)
	{
		case SEEK_SET:
			if(offset < 0)
			{
				DEBUG("offset out of range (would result in "
					"negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(nvf->offset) = offset ;
			//if(offset == 0)
				//INITIALIZE_TIMER();
			return *(nvf->offset);

		case SEEK_CUR:
			if((*(nvf->offset) + offset) < 0)
			{
				DEBUG("offset out of range (would result in "
					"negative offset).\n");
				errno = EINVAL;
				return -1;
			}
			*(nvf->offset) += offset ;
			return *(nvf->offset);

		case SEEK_END:
			if( nvf->node->length + offset < 0 )
			{
				DEBUG("offset out of range (would result in "
					"negative offset).\n");
				errno = EINVAL;
				return -1;
			}

			*(nvf->offset) = nvf->node->length + offset;
			return *(nvf->offset);

		default:
			DEBUG("Invalid whence parameter.\n");
			errno = EINVAL;
			return -1;
	}

	assert(0); // unreachable
	return -1;
}

RETT_SYSCALL_INTERCEPT _sfs_SEEK(INTF_SYSCALL)
{
	DEBUG("%s\n", __func__);
	int file = (int)arg0;

	if(!_fd_intercept_lookup[file]) {
		return RETT_PASS_KERN;
	}

	GLOBAL_LOCK_WR();

	instrumentation_type seek_time;
	int whence, ret;
	off_t offset;

	offset = (off_t)arg1;
	whence = (off_t)arg2;

	DEBUG("%s: %d\n", __func__, file);
	START_TIMING(seek_t, seek_time);

	struct NVFile* nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix SEEK64 for fd %d\n", nvf->fd);
		END_TIMING(seek_t, seek_time);
		DEBUG_FILE("%s: END\n", __func__);
		return syscall_no_intercept(SYS_lseek, file, offset, whence);
	}

	int cpuid = GET_CPUID();

	NVP_LOCK_FD_WR(nvf);
	NVP_CHECK_NVF_VALID_WR(nvf);
	NVP_LOCK_NODE_RD(nvf, cpuid);

	ret =  _nvp_do_seek64(file, offset, whence, nvf);

	NVP_UNLOCK_NODE_RD(nvf, cpuid);
	NVP_UNLOCK_FD_WR(nvf);

	END_TIMING(seek_t, seek_time);

	GLOBAL_UNLOCK_WR();
	if(ret == -1) {
		*result = -errno;
	}
	*result = ret;
	return RETT_NO_PASS_KERN;
}
