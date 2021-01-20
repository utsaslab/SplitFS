#include <libsyscall_intercept_hook_point.h>
#include <nv_common.h>

#include "timers.h"
#include "log.h"

RETT_SYSCALL_INTERCEPT _sfs_RENAME(INTF_SYSCALL)
{
	DEBUG_FILE("CALL: %s\n", __func__);

    char *old, *new;
    old = (char *)arg0;
    new = (char *)arg1;

	*result = syscall_no_intercept(SYS_rename, old, new);
	instrumentation_type op_log_entry_time;
	// Write to op log

#if !POSIX_ENABLED
	if(*result == 0) {
		START_TIMING(op_log_entry_t, op_log_entry_time);
		persist_op_entry(LOG_RENAME,
				old,
				new,
				0,
				0);
		END_TIMING(op_log_entry_t, op_log_entry_time);
	}
#endif
	return RETT_NO_PASS_KERN;
}