/*
 * =====================================================================================
 *
 *       Filename:  fsync.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/25/2019 03:22:19 PM
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
#include <stdlib.h>

#include "fsync.h"
#include "nvp_lock.h"
#include "inode.h"
#include "relink.h"
#include "staging.h"
#include "tbl_mmaps.h"
#include "timers.h"
#include "handle_mmaps.h"

static inline void copy_appends_to_file(struct NVFile* nvf, int close, int fdsync)
{
	if (close && nvf->node->reference > 1)
		return;

	swap_extents(nvf, close);
	nvp_transfer_to_free_dr_pool(nvf->node);
}

/* FIXME: untested */
void fsync_flush_on_fsync(struct NVFile* nvf, int cpuid, int close, int fdsync)
{
	struct NVTable_maps *tbl_app = &_nvp_tbl_mmaps[nvf->node->serialno % APPEND_TBL_MAX];

#if DATA_JOURNALING_ENABLED
	struct NVTable_maps *tbl_over = &_nvp_over_tbl_mmaps[nvf->node->serialno % OVER_TBL_MAX];
#else
	struct NVTable_maps *tbl_over = NULL;
#endif // DATA_JOURNALING_ENABLED

	NVP_LOCK_NODE_WR(nvf);
	TBL_ENTRY_LOCK_WR(tbl_app);
	TBL_ENTRY_LOCK_WR(tbl_over);

	copy_appends_to_file(nvf, close, fdsync);

	TBL_ENTRY_UNLOCK_WR(tbl_over);
	TBL_ENTRY_UNLOCK_WR(tbl_app);
	NVP_UNLOCK_NODE_WR(nvf);
}

RETT_SYSCALL_INTERCEPT _sfs_FSYNC(INTF_SYSCALL)
{
	instrumentation_type fsync_time;
	int cpuid = -1;
	int file;

	file = (int)arg0;

	if(!_fd_intercept_lookup[file]) {
		return RETT_PASS_KERN;
	}

	START_TIMING(fsync_t, fsync_time);
	GLOBAL_LOCK_WR();

	// Retrieve the NVFile from the global array of NVFiles
	cpuid = GET_CPUID();
	struct NVFile* nvf = &_nvp_fd_lookup[file];
	// This goes to fsync_flush_on_fsync()
	fsync_flush_on_fsync(nvf, cpuid, 0, 0);
	num_fsync++;
	END_TIMING(fsync_t, fsync_time);
	GLOBAL_UNLOCK_WR();
	*result = 0;
	return RETT_NO_PASS_KERN;
}