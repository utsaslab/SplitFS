#include <libsyscall_intercept_hook_point.h>
#include <nv_common.h>

#include "timers.h"
#include "log.h"

RETT_SYSCALL_INTERCEPT _sfs_RMDIR(INTF_SYSCALL)
{
	DEBUG_FILE("CALL: %s\n", __func__);

    char *path;
    path = (char *)arg0;

	*result = syscall_no_intercept(SYS_rmdir, path);
	instrumentation_type op_log_entry_time;
	// Write to op log

#if !POSIX_ENABLED
	if(*result == 0) {
		START_TIMING(op_log_entry_t, op_log_entry_time);
		persist_op_entry(LOG_DIR_DELETE,
				path,
				NULL,
				0,
				0);
		END_TIMING(op_log_entry_t, op_log_entry_time);
	}
#endif
	return RETT_NO_PASS_KERN;
}