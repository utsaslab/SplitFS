/*
 * =====================================================================================
 *
 *       Filename:  read.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:17:00 PM
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
#include <sched.h>

#include <nv_common.h>
#include "timers.h"
#include "handle_mmaps.h"
#include "tbl_mmaps.h"
#include "nvp_lock.h"
#include "file.h"
#include "inode.h"
#include "staging.h"
#include "log.h"
#include "fsync.h"
#include "add_delay.h"

static ssize_t _nvp_read_beyond_true_length(int file, void *buf, size_t count, off_t offset, int wr_lock, int cpuid, 
											struct NVFile *nvf, struct NVTable_maps *tbl_app, struct NVTable_maps *tbl_over)
{
	size_t len_to_read, extent_length, read_count;
	unsigned long mmap_addr;
	off_t read_offset_beyond_true_length, offset_within_mmap;
	instrumentation_type copy_appendread_time, get_dr_mmap_time;

	num_anon_read++;

	//printf("%s: here beyond true length\n", __func__);
	read_count = 0;
	len_to_read = count;

	read_offset_beyond_true_length = offset - nvf->node->true_length;

	while (len_to_read > 0) {

#if DATA_JOURNALING_ENABLED

		read_tbl_mmap_entry(nvf->node,
				    offset,
				    len_to_read,
				    &mmap_addr,
				    &extent_length,
				    1);		
		if (mmap_addr == 0) {
			START_TIMING(get_dr_mmap_t, get_dr_mmap_time);		
			nvp_get_dr_mmap_address(nvf, read_offset_beyond_true_length,
						len_to_read, read_count,
						&mmap_addr, &offset_within_mmap, &extent_length,
						wr_lock, cpuid, 0, tbl_app, tbl_over);
			END_TIMING(get_dr_mmap_t, get_dr_mmap_time);
		}

#else // DATA_JOURNALING_ENABLED

		START_TIMING(get_dr_mmap_t, get_dr_mmap_time);		
		nvp_get_dr_mmap_address(nvf, read_offset_beyond_true_length,
					len_to_read, read_count,
					&mmap_addr, &offset_within_mmap, &extent_length,
					wr_lock, cpuid, 0, tbl_app, tbl_over);
		END_TIMING(get_dr_mmap_t, get_dr_mmap_time);
			
#endif // DATA_JOURNALING_ENABLED
		
		if(extent_length > len_to_read)
			extent_length = len_to_read;
		
		START_TIMING(copy_appendread_t, copy_appendread_time);
		if(FSYNC_MEMCPY(buf, (char *)mmap_addr, extent_length) != buf) {
			MSG("%s: memcpy read failed\n", __func__);
			assert(0);
		}

#if NVM_DELAY
		perfmodel_add_delay(1, extent_length);
#endif		
		END_TIMING(copy_appendread_t, copy_appendread_time);
		num_memcpy_read++;
		memcpy_read_size += extent_length;
		read_offset_beyond_true_length += extent_length;
		read_count  += extent_length;
		buf += extent_length;
		len_to_read -= extent_length;
		anon_read_size += extent_length;
	}

	TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
	TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
	return read_count;
}

ssize_t read_from_file_mmap(int file,
			       off_t read_offset_within_true_length,
			       size_t len_to_read_within_true_length,
			       int wr_lock,
			       int cpuid,
			       void *buf, 
			       struct NVFile *nvf,
			       struct NVTable_maps *tbl_app,
			       struct NVTable_maps *tbl_over)
{
	int ret = 0, ret_get_addr = 0;
	unsigned long mmap_addr = 0, bitmap_root = 0;
	off_t offset_within_mmap = 0;
	size_t extent_length = 0, read_count = 0, posix_read = 0;
	instrumentation_type copy_overread_time, get_mmap_time;
	
	START_TIMING(get_mmap_t, get_mmap_time);
	ret = nvp_get_mmap_address(nvf,
				   read_offset_within_true_length,
				   read_count,
				   &mmap_addr,
				   &bitmap_root,
				   &offset_within_mmap,
				   &extent_length,
				   wr_lock,
				   cpuid,
				   tbl_app,
				   tbl_over);
	END_TIMING(get_mmap_t, get_mmap_time);

	switch (ret) {
	case 0: // Mmaped. Do memcpy.
		break;
	case 1: // Not mmaped. Calling Posix pread.
		posix_read = syscall_no_intercept(SYS_pread64, file, 
										  buf, len_to_read_within_true_length,
										  read_offset_within_true_length);
		num_posix_read++;
		posix_read_size += posix_read;

		return posix_read;
	default:
		break;
	}

	if (extent_length > len_to_read_within_true_length)
		extent_length = len_to_read_within_true_length;		
		
	START_TIMING(copy_overread_t, copy_overread_time);
	DEBUG_FILE("%s: Reading from addr = %p, offset = %lu, size = %lu\n", __func__, (void *) mmap_addr, offset_within_mmap, extent_length);
	if(FSYNC_MEMCPY(buf, (const void * restrict)mmap_addr, extent_length) != buf) {
		printf("%s: memcpy read failed\n", __func__);
		fflush(NULL);
		assert(0);
	}

#if NVM_DELAY
	perfmodel_add_delay(1, extent_length);
#endif

	END_TIMING(copy_overread_t, copy_overread_time);

	num_memcpy_read++;
	memcpy_read_size += extent_length;

	return extent_length;
}

static ssize_t _nvp_do_pread(int file, void *buf, size_t count, off_t offset, int wr_lock, int cpuid, struct NVFile *nvf, struct NVTable_maps *tbl_app, struct NVTable_maps *tbl_over)
{
	SANITYCHECKNVF(nvf);
	long long read_offset_within_true_length = 0;
	size_t read_count, extent_length, read_count_beyond_true_length;
	size_t len_to_read_within_true_length;
	size_t posix_read = 0;
	unsigned long mmap_addr = 0;
	unsigned long bitmap_root = 0;
	off_t offset_within_mmap;
	ssize_t available_length = (nvf->node->length) - offset;
	instrumentation_type copy_overread_time, read_tbl_mmap_time;

	if (UNLIKELY(!nvf->canRead)) {
		DEBUG("FD not open for reading: %i\n", file);
		errno = EBADF;

		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);

		return -1;
	}

	else if (UNLIKELY(offset < 0))
	{
		DEBUG("Requested read at negative offset (%li)\n", offset);
		errno = EINVAL;

		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		return -1;
	}

	if(nvf->aligned)
	{
		DEBUG("This read must be aligned.  Checking alignment.\n");

		if(UNLIKELY(available_length <= 0))
		{
			DEBUG("Actually there weren't any bytes available "
				"to read.  Bye! (length %li, offset %li, "
				"available_length %li)\n", nvf->node->length,
				offset, available_length);

			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
			return 0;
		}

		if(UNLIKELY(count % 512))
		{
			DEBUG("cout is not aligned to 512 (count was %i)\n",
				count);

			errno = EINVAL;
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
			return -1;
		}
		if(UNLIKELY(offset % 512))
		{
			DEBUG("offset was not aligned to 512 (offset was %i)\n",
				offset);

			errno = EINVAL;
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
			return -1;
		}
		if(UNLIKELY(((long long int)buf & (512-1)) != 0))
		{
			DEBUG("buffer was not aligned to 512 (buffer was %p, "
				"mod 512=%i)\n", buf, (long long int)buf % 512);
			errno = EINVAL;

			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
			return -1;
		}
	}

	ssize_t len_to_read = count;
	if (count > available_length)
	{
		len_to_read = available_length;
		DEBUG("Request read length was %li, but only %li bytes "
			"available. (filelen = %li, offset = %li, "
			"requested %li)\n", count, len_to_read,
			nvf->node->length, offset, count);
	}

	if(UNLIKELY( (len_to_read <= 0) || (available_length <= 0) ))
	{
		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		return 0; // reading 0 bytes is easy!
	}

	DEBUG("mmap is length %li, len_to_read is %li\n", nvf->node->maplength,
		len_to_read);

	SANITYCHECK(len_to_read + offset <= nvf->node->length);

	read_count = 0;

	/*
	 * if data to be read <= true_length_for_read, then it can be read from file backed mmap. Otherwise, it can be
	 * read from anonymous mmap 
	 * len_to_read_within_true_length = amount of data that can be read using file backed mmap. 
	 */
	read_offset_within_true_length = (offset > nvf->node->true_length) ? -1 : offset;
		
	if(read_offset_within_true_length == -1)
		len_to_read_within_true_length = 0;
	else {
		len_to_read_within_true_length = (len_to_read + offset > nvf->node->true_length) ? nvf->node->true_length - offset : len_to_read;
	}

	DEBUG_FILE("%s: len of read request = %lu, offset = %lu. True Size of file = %lu. Fake file size = %lu\n", __func__, len_to_read_within_true_length, read_offset_within_true_length, nvf->node->true_length, nvf->node->length);

	while (len_to_read_within_true_length > 0) {
		// Get the file backed mmap address from which the read is to be performed. 
		
		START_TIMING(read_tbl_mmap_t, read_tbl_mmap_time);
		read_tbl_mmap_entry(nvf->node,
				    read_offset_within_true_length,
				    len_to_read_within_true_length,
				    &mmap_addr,
				    &extent_length,
				    1);
		END_TIMING(read_tbl_mmap_t, read_tbl_mmap_time);
		
		DEBUG_FILE("%s: addr to read = %p, size to read = %lu. Inode = %lu\n", __func__, mmap_addr, extent_length, nvf->node->serialno);
		DEBUG("Pread: get_mmap_address returned %d, length %llu\n",
			ret, extent_length);

		if (mmap_addr == 0) {
			extent_length = read_from_file_mmap(file,
							    read_offset_within_true_length,
							    len_to_read_within_true_length,
							    wr_lock,
							    cpuid,
							    buf,
							    nvf,
							    tbl_app,
							    tbl_over);
			goto post_read;

		}
		
		DEBUG_FILE("%s: memcpy args: buf = %p, mmap_addr = %p, length = %lu. File off = %lld. Inode = %lu\n", __func__, buf, (void *) mmap_addr, extent_length, read_offset_within_true_length, nvf->node->serialno);
		START_TIMING(copy_overread_t, copy_overread_time);
		if(FSYNC_MEMCPY(buf,
				(void *)mmap_addr,
				extent_length) != buf) {
			printf("%s: memcpy read failed\n", __func__);
			fflush(NULL);
			assert(0);
		}
#if NVM_DELAY
		perfmodel_add_delay(1, extent_length);
#endif		
		END_TIMING(copy_overread_t, copy_overread_time);
		// Add the NVM read latency

		num_memcpy_read++;
		memcpy_read_size += extent_length;
	post_read:
		len_to_read -= extent_length;
		len_to_read_within_true_length -= extent_length;
		read_offset_within_true_length += extent_length;
		read_count  += extent_length;
		buf += extent_length;
		offset += extent_length;
	}

	if(!len_to_read) {
		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		DEBUG_FILE("%s: Returning from read over. Size = %lu\n", __func__, read_count);
		return read_count;
	}

	// If we need to read from anonymous memory, call _nvp_read_beyond_true_length
	read_count_beyond_true_length = _nvp_read_beyond_true_length(file,
								     buf,
								     len_to_read,
								     offset,
								     wr_lock,
								     cpuid,
								     nvf,
								     tbl_app,
								     tbl_over);
	read_count += read_count_beyond_true_length;
	
	DEBUG_FILE("%s: Returning from read beyond. Size = %lu\n", __func__, read_count);
	return read_count;
}

static ssize_t _nvp_check_read_size_valid(size_t count)
{ 
	if(count == 0)
	{
		DEBUG("Requested a read of 0 length.  No problem\n");
		return 0;
	}
	else if(count < 0)
	{
		DEBUG("Requested read of negative bytes (%li)\n", count);
		errno = EINVAL;
		return -1;
	}

	return count;
}

RETT_SYSCALL_INTERCEPT _sfs_READ(INTF_SYSCALL)
{
	DEBUG_FILE("%s %d\n", __func__, file);

	int file;
	size_t length;
	char *buf;
	instrumentation_type read_time;
	
	file = (int)arg0;
	buf = (char *)arg1;
	length = (size_t)arg2;
	num_read++;

	if(!_fd_intercept_lookup[file]) {
		return RETT_PASS_KERN;
	}

	int res;

	START_TIMING(read_t, read_time);
	GLOBAL_LOCK_WR();

	struct NVFile* nvf = &_nvp_fd_lookup[file];
	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[nvf->node->serialno % APPEND_TBL_MAX];

#if DATA_JOURNALING_ENABLED
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[nvf->node->serialno % OVER_TBL_MAX];
#else
	struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED

	if(nvf->posix) {
		DEBUG("Call posix READ for fd %d\n", nvf->fd);
		*result = syscall_no_intercept(SYS_read, file, buf, length);
		read_size += *result;
		num_posix_read++;
		posix_read_size += *result;
		END_TIMING(read_t, read_time);
		GLOBAL_UNLOCK_WR();
		return RETT_NO_PASS_KERN;
	}

	res = _nvp_check_read_size_valid(length);
	if (res <= 0) {
		END_TIMING(read_t, read_time);
		GLOBAL_UNLOCK_WR();
		*result = -EINVAL;
		return RETT_NO_PASS_KERN;
	}
	#define _GNU_SOURCE
	int cpuid = GET_CPUID();

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_LOCK_NODE_RD(nvf, cpuid);

	TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
	TBL_ENTRY_LOCK_RD(tbl_over, cpuid);

	res = _nvp_do_pread(file, buf, length,
			       __sync_fetch_and_add(nvf->offset, length),
			       0,
			       cpuid,
			       nvf,
			       tbl_app,
			       tbl_over);
	if(res < 0) {
		// errno is set by _nvp_do_pread
		*result = -errno;
	}
	

	NVP_UNLOCK_NODE_RD(nvf, cpuid);

	if(res == length)	{
		DEBUG("PREAD succeeded: extending offset from %li to %li\n",
			*nvf->offset - res, *nvf->offset);
	}
	else if (res <= 0){
		DEBUG("_nvp_READ: PREAD failed; not changing offset. "
			"(returned %i)\n", res);
		//assert(0); // TODO: this is for testing only
		__sync_fetch_and_sub(nvf->offset, length);
	} else {
		DEBUG("_nvp_READ: PREAD failed; Not fully read. "
			"(returned %i)\n", res);
		// assert(0); // TODO: this is for testing only
		__sync_fetch_and_sub(nvf->offset, length - res);
	}

	NVP_UNLOCK_FD_RD(nvf, cpuid);

	read_size += res;

	END_TIMING(read_t, read_time);
	DEBUG_FILE("_nvp_READ %d returns %lu\n", file, result);
	GLOBAL_UNLOCK_WR();
	*result = res;
	return RETT_NO_PASS_KERN;
}
