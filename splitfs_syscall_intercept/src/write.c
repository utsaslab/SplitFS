/*
 * =====================================================================================
 *
 *       Filename:  write.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:19:46 PM
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
#include "timers.h"
#include "add_delay.h"
#include "handle_mmaps.h"
#include "tbl_mmaps.h"
#include "nvp_lock.h"
#include "file.h"
#include "inode.h"
#include "staging.h"
#include "log.h"
#include "relink.h"

#if !DATA_JOURNALING_ENABLED
static ssize_t write_to_file_mmap(int file,
			      off_t write_offset_within_true_length,
			      size_t len_to_write_within_true_length,
			      int wr_lock,
			      int cpuid,
			      const void *buf, 
			      struct NVFile *nvf)
{
    int ret = 0;
    unsigned long mmap_addr = 0, bitmap_root = 0;
    off_t offset_within_mmap = 0;
    size_t extent_length = 0, write_count = 0, posix_write = 0, data_written = 0;
    instrumentation_type copy_overwrite_time, get_mmap_time;

    while(len_to_write_within_true_length > 0) {
        START_TIMING(get_mmap_t, get_mmap_time);
        ret = nvp_get_mmap_address(nvf,
                write_offset_within_true_length,
                write_count,
                &mmap_addr,
                &bitmap_root,
                &offset_within_mmap,
                &extent_length,
                wr_lock,
                cpuid,
                NULL,
                NULL);
        END_TIMING(get_mmap_t, get_mmap_time);

        switch (ret) {
            case 0: // Mmaped. Do memcpy.
                break;
            case 1: // Not mmaped. Calling Posix pread.
                posix_write = syscall_no_intercept(SYS_pwrite64, file,
                        buf,
                        len_to_write_within_true_length,
                        write_offset_within_true_length);
                num_posix_write++;
                posix_write_size += posix_write;
                return posix_write;
            default:
                break;
        }

        if (extent_length > len_to_write_within_true_length)
            extent_length = len_to_write_within_true_length;

        START_TIMING(copy_overwrite_t, copy_overwrite_time);

#if NON_TEMPORAL_WRITES
        DEBUG_FILE("%s: args: mmap_addr = %p, offset in mmap = %lu, length to write = %lu\n", __func__, (char *)mmap_addr, offset_within_mmap, extent_length);
        if(MEMCPY_NON_TEMPORAL((char *)mmap_addr, buf, extent_length) == NULL) {
            printf("%s: non-temporal memcpy failed\n", __func__);
            fflush(NULL);
            assert(0);
        }
        num_write_nontemporal++;
        non_temporal_write_size += extent_length;
#else //NON_TEMPORAL_WRITES
        if(FSYNC_MEMCPY((char *)mmap_addr, buf, extent_length) == NULL) {
            printf("%s: non-temporal memcpy failed\n", __func__);
            fflush(NULL);
            assert(0);
        }
#endif //NON TEMPORAL WRITES
#if NVM_DELAY
        perfmodel_add_delay(0, extent_length);
#endif
        num_memcpy_write++;
        memcpy_write_size += extent_length;
        len_to_write_within_true_length -= extent_length;
        write_offset_within_true_length += extent_length;
        buf += extent_length;
        data_written += extent_length;
        num_mfence++;
        _mm_sfence();

        END_TIMING(copy_overwrite_t, copy_overwrite_time);
    }
    return data_written;
}
#endif 

/* 
 * _nvp_extend_write gets called whenever there is an append to a file. The write first goes to the
 * anonymous memory region through memcpy. During fsync() time, the data is copied non-temporally from
 * anonymous DRAM to the file. 
 */
static ssize_t _nvp_extend_write(int file, const void *buf, size_t count, off_t offset,
			       int wr_lock,
			       int cpuid,
			       struct NVFile *nvf,
			       struct NVTable_maps *tbl_app,
			       struct NVTable_maps *tbl_over)
{

	size_t len_to_write, write_count;
	off_t write_offset;
	instrumentation_type get_dr_mmap_time, copy_appendwrite_time, clear_dr_time, swap_extents_time;
	instrumentation_type device_time;
	
	// Increment counter for append
	_nvp_wr_extended++;
	num_memcpy_write++;
	num_append_write++;	
	DEBUG("Request write length %li will extend file. "
	      "(filelen=%li, offset=%li, count=%li, extension=%li)\n",
	      count, nvf->node->length, offset, count, extension);		
	len_to_write = count;
	write_count = 0;
	write_offset = offset;
	DEBUG_FILE("%s: requesting write of size %lu, offset = %lu. FD = %d\n", __func__, count, offset, nvf->fd);
	unsigned long mmap_addr;
	off_t offset_within_mmap, write_offset_wrt_true_length;
	size_t extent_length, extension_with_node_length;
	instrumentation_type append_log_entry_time;
	extension_with_node_length = 0;

 get_addr:	
	/* This is used mostly to check if the write is not an append,
	 * but is way beyond the length of the file. 
	 */
	write_offset_wrt_true_length = write_offset - (off_t) nvf->node->true_length;
	DEBUG_FILE("%s: write offset = %lu, true length = %lu\n", __func__, write_offset, nvf->node->true_length);
	// The address to perform the memcpy to is got from this function. 
	START_TIMING(get_dr_mmap_t, get_dr_mmap_time);
	
	nvp_get_dr_mmap_address(nvf, write_offset_wrt_true_length, len_to_write,
				write_count, &mmap_addr, &offset_within_mmap,
				&extent_length, wr_lock, cpuid, 1, tbl_app, tbl_over);
	DEBUG_FILE("%s: extent_length = %lu, len_to_write = %lu\n",
		   __func__, extent_length, len_to_write);
	
	END_TIMING(get_dr_mmap_t, get_dr_mmap_time);
	
	DEBUG_FILE("%s: ### EXTENT_LENGTH >= LEN_TO_WRITE extent_length = %lu, len_to_write = %lu\n", __func__,
		   extent_length, len_to_write);
	if (extent_length < len_to_write) {
		nvf->node->dr_info.dr_offset_end -= extent_length;
		swap_extents(nvf, nvf->node->true_length);			
		off_t offset_in_page = 0;
		nvf->node->true_length = nvf->node->length;
		if (nvf->node->true_length >= LARGE_FILE_THRESHOLD)
			nvf->node->is_large_file = 1;
		START_TIMING(clear_dr_t, clear_dr_time);
		DEBUG_FILE("%s: EXTENT_LENGTH < LEN_TO_WRITE, EXTENT FD = %d, extent_length = %lu, len_to_write = %lu\n",
			   __func__,
			   nvf->node->dr_info.dr_fd,
			   extent_length,
			   len_to_write);
#if BG_CLEANING
		change_dr_mmap(nvf->node, 0);
#else
		create_dr_mmap(nvf->node, 0);
#endif
		END_TIMING(clear_dr_t, clear_dr_time);

		offset_in_page = (off_t) (nvf->node->true_length) % MMAP_PAGE_SIZE;
		if (offset_in_page != 0 && nvf->node->dr_info.valid_offset < DR_SIZE) {
			nvf->node->dr_info.valid_offset += (unsigned long) offset_in_page;
			nvf->node->dr_info.dr_offset_start = DR_SIZE;
			nvf->node->dr_info.dr_offset_end = nvf->node->dr_info.valid_offset;
		}
		if (nvf->node->dr_info.valid_offset == DR_SIZE) {
			nvf->node->dr_info.valid_offset = DR_SIZE;
			nvf->node->dr_info.dr_offset_start = DR_SIZE;
			nvf->node->dr_info.dr_offset_end = DR_SIZE;
		}
		DEBUG_FILE("%s: RECEIVED OTHER EXTENT. dr fd = %d, dr addr = %p, dr v.o = %lu, dr start off = %lu, dr end off = %lu\n",
			   __func__, nvf->node->dr_info.dr_fd, nvf->node->dr_info.start_addr, nvf->node->dr_info.valid_offset,
			   nvf->node->dr_info.dr_offset_start, nvf->node->dr_info.dr_offset_end);

		if (tbl_over != NULL)	{
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		if (tbl_app != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		}
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_LOCK_NODE_RD(nvf, cpuid);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		}
		if (tbl_over != NULL)	{
			TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		}
		DEBUG_FILE("%s: Cleared mmap\n", __func__);
		goto get_addr;
	}
	if (extent_length > len_to_write)
		extent_length = len_to_write;
	if((extent_length + (size_t) write_offset) > nvf->node->length)
		extension_with_node_length = extent_length + (size_t)write_offset - nvf->node->length;
	
	if ((mmap_addr % MMAP_PAGE_SIZE) != (nvf->node->length % MMAP_PAGE_SIZE))
		assert(0);
	
	nvf->node->length += extension_with_node_length;
	
	memcpy_write_size += extent_length;
	append_write_size += extent_length;
	
	if (!wr_lock) {
		if (tbl_over != NULL) {
			TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		}
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);	
		NVP_UNLOCK_NODE_WR(nvf);
		NVP_LOCK_NODE_RD(nvf, cpuid);
		if (tbl_app != NULL) {
			TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		}
		if (tbl_over != NULL)	{
			TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		}
	}
	
#if SYSCALL_APPENDS

	offset_within_mmap = write_offset - nvf->node->true_length;
	syscall_no_intercept(SYS_pwrite, nvf->node->dr_info.dr_fd, buf, extent_length, offset_within_mmap);
	syscall_no_intercept(SYS_fsync, nvf->fd);

#else // SYSCALL APPENDS

	// Write to anonymous DRAM. No dirty tracking to be performed here. 
	START_TIMING(copy_appendwrite_t, copy_appendwrite_time);	
	START_TIMING(device_t, device_time);

	DEBUG_FILE("%s: memcpy args: buf = %p, mmap_addr = %p, length = %lu. File off = %lld. Inode = %lu\n", __func__, buf, (void *) mmap_addr, extent_length, write_offset, nvf->node->serialno);
	if(MEMCPY_NON_TEMPORAL((char *)mmap_addr, buf, extent_length) == NULL) {
		printf("%s: non-temporal memcpy failed\n", __func__);
		fflush(NULL);
		assert(0);
	}
	//_mm_sfence();
	//num_mfence++;
	num_write_nontemporal++;
	non_temporal_write_size += extent_length;

#if NVM_DELAY
	perfmodel_add_delay(0, extent_length);
#endif // NVM_DELAY
	
	END_TIMING(device_t, device_time);
	END_TIMING(copy_appendwrite_t, copy_appendwrite_time);
	
	if (tbl_over != NULL)	{
		TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
	}
	if (tbl_app != NULL) {
		TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
	}
	if (!wr_lock) {
		NVP_UNLOCK_NODE_RD(nvf, cpuid);	
	} else {
		NVP_UNLOCK_NODE_WR(nvf);
	}
	NVP_UNLOCK_FD_RD(nvf, cpuid);
	// Log the append

#if !POSIX_ENABLED
	
	START_TIMING(append_log_entry_t, append_log_entry_time);
	persist_append_entry(nvf->node->serialno,
			     nvf->node->dr_info.dr_serialno,
			     offset,
			     offset_within_mmap,
			     extent_length);
	END_TIMING(append_log_entry_t, append_log_entry_time);
#endif
	
#endif // SYSCALL APPENDS
	
	len_to_write -= extent_length;
	write_offset += extent_length;
	write_count  += extent_length;
	buf += extent_length;			

	DEBUG_FILE("%s: Returning write count = %lu. FD = %d\n", __func__, write_count, nvf->fd);
	return (ssize_t) write_count;
}

ssize_t _nvp_do_pwrite(int file, const void *buf, size_t count, off_t offset,
			   int wr_lock,
			   int cpuid,
			   struct NVFile *nvf,
			   struct NVTable_maps *tbl_app,
			   struct NVTable_maps *tbl_over)
{
	off_t write_offset, offset_within_mmap;
	size_t write_count, extent_length;
	size_t posix_write;
	unsigned long mmap_addr = 0;
	unsigned long bitmap_root = 0;
	uint64_t extendFileReturn;
	instrumentation_type appends_time, read_tbl_mmap_time, copy_overwrite_time, get_dr_mmap_time,
		append_log_entry_time, clear_dr_time, insert_tbl_mmap_time;
	DEBUG_FILE("%s: fd = %d, offset = %lu, count = %lu\n", __func__, file, offset, count);
	_nvp_wr_total++;

	 SANITYCHECKNVF(nvf);
	 if(UNLIKELY(!nvf->canWrite)) {
		 DEBUG("FD not open for writing: %i\n", file);
		 errno = EBADF;

		 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		 NVP_UNLOCK_NODE_RD(nvf, cpuid);
		 NVP_UNLOCK_FD_RD(nvf, cpuid);
		 return -1;
	 }
	 if(nvf->aligned)
		 {
			 DEBUG("This write must be aligned.  Checking alignment.\n");
			 if(UNLIKELY(count % 512))
				 {
					 DEBUG("count is not aligned to 512 (count was %li)\n",
					       count);
					 errno = EINVAL;

					 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
					 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
					 NVP_UNLOCK_NODE_RD(nvf, cpuid);
					 NVP_UNLOCK_FD_RD(nvf, cpuid);
					 return -1;
				 }
			 if(UNLIKELY(offset % 512))
				 {
					 DEBUG("offset was not aligned to 512 "
					       "(offset was %li)\n", offset);
					 errno = EINVAL;

					 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
					 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
					 NVP_UNLOCK_NODE_RD(nvf, cpuid);
					 NVP_UNLOCK_FD_RD(nvf, cpuid);
					 return -1;
				 }

			 if(UNLIKELY(((long long int)buf & (512-1)) != 0))
				 {
					 DEBUG("buffer was not aligned to 512 (buffer was %p, "
					       "mod 512 = %li)\n", buf,
					       (long long int)buf % 512);
					 errno = EINVAL;

					 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
					 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
					 NVP_UNLOCK_NODE_RD(nvf, cpuid);
					 NVP_UNLOCK_FD_RD(nvf, cpuid);
					 return -1;
				 }
		 }
	 if(nvf->append)
		 {
			 DEBUG("this fd (%i) is O_APPEND; setting offset from the "
			       "passed value (%li) to the end of the file (%li) "
			       "prior to writing anything\n", nvf->fd, offset,
			       nvf->node->length);
			 offset = nvf->node->length;
		 }

	 ssize_t len_to_write;
	 ssize_t extension_with_read_length;
	 DEBUG("time for a Pwrite. file length %li, offset %li, extension %li, count %li\n", nvf->node->length, offset, extension, count);

	 len_to_write = count;
		
	 SANITYCHECK(nvf->valid);
	 SANITYCHECK(nvf->node != NULL);
	 SANITYCHECK(buf > 0);
	 SANITYCHECK(count >= 0);

	 write_count = 0;
	 write_offset = offset;		

	 if (write_offset >= nvf->node->length + 1) {
		 DEBUG_FILE("%s: Hole getting created. Doing Write system call\n", __func__);
		 posix_write = syscall_no_intercept(SYS_pwrite64, file, buf, count, write_offset);
		 syscall_no_intercept(SYS_fsync, file);
		 num_posix_write++;
		 posix_write_size += posix_write;
		 if (!wr_lock) {
			 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
			 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
			 NVP_UNLOCK_NODE_RD(nvf, cpuid);

			 NVP_LOCK_NODE_WR(nvf);
			 TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
			 TBL_ENTRY_LOCK_RD(tbl_over, cpuid);
		 }
		 if (write_offset + count <= nvf->node->length) {
			 DEBUG_FILE("%s: offset fault. Offset of write = %lu, count = %lu, node length = %lu\n", __func__, write_offset, count, nvf->node->length);
			 assert(0);
		 }

		 nvf->node->length = write_offset + count;
		 nvf->node->true_length = nvf->node->length;
		 if (nvf->node->true_length >= LARGE_FILE_THRESHOLD)
			 nvf->node->is_large_file = 1;

		 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
		 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);
		 NVP_UNLOCK_NODE_WR(nvf);
		 NVP_UNLOCK_FD_RD(nvf, cpuid);
		 return posix_write;
	 }

	 if (write_offset == nvf->node->length)
		 goto appends;

	 if (write_offset >= nvf->node->true_length) {
		 MSG("%s: write_offset = %lu, true_length = %lu\n", __func__, write_offset, nvf->node->true_length);
		 assert(0);
	 }

#if DATA_JOURNALING_ENABLED

	 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
	 TBL_ENTRY_LOCK_WR(tbl_over);

	 // Get the file backed mmap address to which the write is to be performed. 
 get_addr:
	 START_TIMING(get_dr_mmap_t, get_dr_mmap_time);

	 nvp_get_over_dr_address(nvf, write_offset, len_to_write,
				 &mmap_addr, &offset_within_mmap,
				 &extent_length, wr_lock, cpuid,
				 tbl_app, tbl_over);
	 DEBUG_FILE("%s: extent_length = %lu, len_to_write = %lu\n",
		    __func__, extent_length, len_to_write);		
	 END_TIMING(get_dr_mmap_t, get_dr_mmap_time);

	 if (extent_length < len_to_write) {
		 //size_t len_swapped = swap_extents(nvf, nvf->node->true_length);		
		 off_t offset_in_page = 0;
		 START_TIMING(clear_dr_t, clear_dr_time);
		 DEBUG_FILE("%s: EXTENT_LENGTH < LEN_TO_WRITE, EXTENT FD = %d, extent_length = %lu, len_to_write = %lu\n",
			    __func__,
			    nvf->node->dr_info.dr_fd,
			    extent_length,
			    len_to_write);
#if BG_CLEANING
		 change_dr_mmap(nvf->node, 1);
#else
		 create_dr_mmap(nvf->node, 1);
#endif
		 END_TIMING(clear_dr_t, clear_dr_time);

		 DEBUG_FILE("%s: RECEIVED OTHER EXTENT. dr over fd = %d, dr over addr = %p, dr over start off = %lu, dr over end off = %lu\n", __func__, nvf->node->dr_over_info.dr_fd, nvf->node->dr_over_info.start_addr, nvf->node->dr_over_info.dr_offset_start, nvf->node->dr_over_info.dr_offset_end);

		 TBL_ENTRY_UNLOCK_WR(tbl_over);
		 TBL_ENTRY_UNLOCK_WR(tbl_app);
		 NVP_UNLOCK_NODE_WR(nvf);
		 NVP_LOCK_NODE_RD(nvf, cpuid);

		 TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
		 TBL_ENTRY_LOCK_WR(tbl_over);
		 goto get_addr;
	 }

	 TBL_ENTRY_UNLOCK_WR(tbl_over);
	 TBL_ENTRY_UNLOCK_WR(tbl_app);
	 NVP_UNLOCK_NODE_WR(nvf);
	 NVP_LOCK_NODE_RD(nvf, cpuid);

	 TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
	 TBL_ENTRY_LOCK_RD(tbl_over, cpuid);

#else // DATA_JOURNALING_ENABLED

	 START_TIMING(read_tbl_mmap_t, read_tbl_mmap_time);
	 read_tbl_mmap_entry(nvf->node, write_offset,
			     len_to_write, &mmap_addr,
			     &extent_length, 1);
	 END_TIMING(read_tbl_mmap_t, read_tbl_mmap_time);
 
	 if (mmap_addr == 0) {
		 extent_length = write_to_file_mmap(file, write_offset,
						    len_to_write, wr_lock,
						    cpuid, buf, 
						    nvf);

		 goto post_write;
	 }

#endif // DATA_JOURNALING_ENABLED

	 if (extent_length > len_to_write)
		 extent_length = len_to_write;

	 // The write is performed to file backed mmap
	 START_TIMING(copy_overwrite_t, copy_overwrite_time);

#if NON_TEMPORAL_WRITES

	 DEBUG_FILE("%s: memcpy args: buf = %p, mmap_addr = %p, length = %lu. File off = %lld. Inode = %lu\n", __func__, buf, (void *) mmap_addr, extent_length, write_offset, nvf->node->serialno);

	 if(MEMCPY_NON_TEMPORAL((char *)mmap_addr, buf, extent_length) == NULL) {
		 printf("%s: non-temporal memcpy failed\n", __func__);
		 fflush(NULL);
		 assert(0);
	 }
	 _mm_sfence();
	 num_mfence++;
	 num_write_nontemporal++;
	 non_temporal_write_size += extent_length;

#else //NON_TEMPORAL_WRITES

	 if(FSYNC_MEMCPY((char *)mmap_addr, buf, extent_length) != (char *)mmap_addr) {
		 printf("%s: memcpy failed\n", __func__);
		 fflush(NULL);
		 assert(0);
	 }

#if DIRTY_TRACKING

	 modifyBmap((struct merkleBtreeNode *)bitmap_root, offset_within_mmap, extent_length);

#endif //DIRTY_TRACKING

	 num_memcpy_write++;

#endif //NON_TEMPORAL_WRITES

#if NVM_DELAY
	 perfmodel_add_delay(0, extent_length);
#endif

	 END_TIMING(copy_overwrite_t, copy_overwrite_time);

#if DATA_JOURNALING_ENABLED
 
	 START_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);
	 insert_over_tbl_mmap_entry(nvf->node,
				    write_offset,
				    offset_within_mmap,
				    extent_length,
				    mmap_addr);
	 END_TIMING(insert_tbl_mmap_t, insert_tbl_mmap_time);

#endif // DATA_JOURNALING_ENABLED

#if !DATA_JOURNALING_ENABLED
 post_write:
#endif
	 memcpy_write_size += extent_length;
	 len_to_write -= extent_length;
	 write_offset += extent_length;
	 write_count  += extent_length;
	 buf += extent_length;

	 TBL_ENTRY_UNLOCK_RD(tbl_over, cpuid);
	 TBL_ENTRY_UNLOCK_RD(tbl_app, cpuid);

	 NVP_UNLOCK_NODE_RD(nvf, cpuid);
	 NVP_UNLOCK_FD_RD(nvf, cpuid);

#if DATA_JOURNALING_ENABLED
 
	 START_TIMING(append_log_entry_t, append_log_entry_time);
	 persist_append_entry(nvf->node->serialno,
			      nvf->node->dr_over_info.dr_serialno,
			      write_offset,
			      offset_within_mmap,
			      extent_length);
	 END_TIMING(append_log_entry_t, append_log_entry_time);

#endif // DATA_JOURNALING_ENABLED

	 return write_count;

	 // If we need to append data, we should call _nvp_extend_write to write to anonymous mmap. 
 appends:
	 START_TIMING(appends_t, appends_time);
	 extendFileReturn = _nvp_extend_write(file, buf,
					      len_to_write,
					      write_offset,
					      wr_lock, cpuid,
					      nvf,
					      tbl_app,
					      tbl_over);
	 END_TIMING(appends_t, appends_time);
	 len_to_write -= extendFileReturn;
	 write_count += extendFileReturn;
	 write_offset += extendFileReturn;
	 buf += extendFileReturn;

	 DEBUG("About to return from _nvp_PWRITE with ret val %li.  file len: "
		 "%li, file off: %li, map len: %li, node %p\n",
		 count, nvf->node->length, nvf->offset,
		 nvf->node->maplength, nvf->node);
	 return write_count;
 }

static ssize_t _nvp_check_write_size_valid(size_t count)
{
	if(count == 0)
	{
		DEBUG("Requested a write of 0 bytes.  No problem\n");
		return 0;
	}

	if(((signed long long int)count) < 0)
	{
		DEBUG("Requested a write of %li < 0 bytes.\n",
			(signed long long int)count);
		errno = EINVAL;
		return -1;
	}

	return count;
}

RETT_SYSCALL_INTERCEPT _sfs_WRITE(INTF_SYSCALL)
{
	DEBUG("%s: %d\n",__func__, file);
	num_write++;
	int file, res;
	instrumentation_type write_time;

	file = (int)arg0;

	if(!_fd_intercept_lookup[file]) {
		return RETT_PASS_KERN;
	}

	char *buf;
	int length;

	buf = (char *)arg1;
	length = (int)arg2;

	START_TIMING(write_t, write_time);

	GLOBAL_LOCK_WR();

	struct NVFile* nvf = &_nvp_fd_lookup[file];

	if (nvf->posix) {
		DEBUG("Call posix WRITE for fd %d\n", nvf->fd);
		res = syscall_no_intercept(SYS_write, file, buf, length);
		write_size += res;
		num_posix_write++;
		posix_write_size += res;
		END_TIMING(write_t, write_time);
		GLOBAL_UNLOCK_WR();
		*result = res;
		return RETT_NO_PASS_KERN;
	}

	if (nvf->node == NULL) {
		res =  syscall_no_intercept(SYS_write, file, buf, length);
		write_size += res;
		num_posix_write++;
		posix_write_size += res;
		END_TIMING(write_t, write_time);
		GLOBAL_UNLOCK_WR();
		*result = res;
		return RETT_NO_PASS_KERN;
	}

	int cpuid = GET_CPUID();
	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[nvf->node->serialno % APPEND_TBL_MAX];

#if DATA_JOURNALING_ENABLED
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[nvf->node->serialno % OVER_TBL_MAX];
#else
	struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED

	res = _nvp_check_write_size_valid(length);
	if (res <= 0) {
		END_TIMING(write_t, write_time);
		GLOBAL_UNLOCK_WR();
		*result = res;
		return RETT_NO_PASS_KERN;
	}

	NVP_LOCK_FD_RD(nvf, cpuid); // TODO
	NVP_LOCK_NODE_RD(nvf, cpuid); //TODO

	TBL_ENTRY_LOCK_RD(tbl_app, cpuid);
	TBL_ENTRY_LOCK_RD(tbl_over, cpuid);

	res = _nvp_do_pwrite(file, buf, length,
				__sync_fetch_and_add(nvf->offset, length),
				0,
				cpuid,
				nvf,
				tbl_app,
				tbl_over);

	if(res >= 0)
	{
		if(nvf->append)
		{
			DEBUG("PWRITE succeeded and append == true. "
				"Setting offset to end...\n"); 
			//fflush(NULL);
			//assert(_nvp_do_seek64(nvf->fd, 0, SEEK_END, nvf)
			//	!= (RETT_SEEK64)-1);
		}
		else
		{
			DEBUG("PWRITE succeeded: extending offset "
				"from %li to %li\n",
				*nvf->offset - res, *nvf->offset);
		}
	}

	DEBUG("About to return from _nvp_WRITE with ret val %i (errno %i). "
		"file len: %li, file off: %li, map len: %li\n",
		res, errno, nvf->node->length, nvf->offset,
		nvf->node->maplength);

	write_size += res;

	END_TIMING(write_t, write_time);
	GLOBAL_UNLOCK_WR();

	DEBUG_FILE("%s: Returning %d\n", __func__, res);
	if(res == -1) {
		*result = -errno;
	}
	*result = res;
	return RETT_NO_PASS_KERN;
}
