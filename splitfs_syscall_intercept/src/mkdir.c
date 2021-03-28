#include <libsyscall_intercept_hook_point.h>
#include <nv_common.h>

#include "timers.h"
#include "log.h"

RETT_SYSCALL_INTERCEPT _sfs_MKDIR(INTF_SYSCALL)
{
	DEBUG_FILE("CALL: %s\n", __func__);
	instrumentation_type op_log_entry_time;

    char *path;
    mode_t mode;

	path = (char *)arg0;
	mode = (mode_t)arg1;

	// Write to op log
	*result = syscall_no_intercept(SYS_mkdir, path, mode);
	DEBUG_FILE("%s: System call returned %d. Logging\n", __func__, result);

#if !POSIX_ENABLED
	if(*result == 0) {
		START_TIMING(op_log_entry_t, op_log_entry_time);
		persist_op_entry(LOG_DIR_CREATE,
				path,
				NULL,
				mode,
				0);
		END_TIMING(op_log_entry_t, op_log_entry_time);
	}
#endif
	return RETT_NO_PASS_KERN;
}

RETT_SYSCALL_INTERCEPT _sfs_MKDIRAT(INTF_SYSCALL) {
	DEBUG_FILE("CALL: %s\n", __func__);
	instrumentation_type op_log_entry_time;
	int dirfd, mode;
	char *path;

	dirfd = (int)arg0;
	path = (char *)arg1;
	mode = (int)arg2;

	*result = syscall_no_intercept(SYS_mkdirat, dirfd, path, mode);

	// Write to op log

#if !POSIX_ENABLED
	char new_path[256];
	int path_len = 0;
	if (dirfd == AT_FDCWD) {
		if (path[0] != '/') {
			if (getcwd(new_path, sizeof(new_path)) == NULL)
				assert(0);
			path_len = strlen(new_path);
			new_path[path_len] = '/';
			new_path[path_len+1] = '\0';

			if (strcat(new_path, path) != new_path)
				assert(0);
		} else {
			if (strcpy(new_path, path) == NULL)
				assert(0);
		}
	} else {
		char fd_str[256];
		if (path[0] != '/') {
			sprintf(fd_str, "/proc/self/fd/%d", dirfd);
			if (readlink(fd_str, new_path, sizeof(new_path)) == -1)
				assert(0);
			path_len = strlen(new_path);
			new_path[path_len] = '/';
			new_path[path_len+1] = '\0';
			if (strcat(new_path, path) != new_path)
				assert(0);
		} else {
			if (strcpy(new_path, path) == NULL)
				assert(0);
		}
	}

	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_DIR_CREATE,
			 new_path,
			 NULL,
			 mode,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return RETT_NO_PASS_KERN;
}
