#include <libsyscall_intercept_hook_point.h>
#include <nv_common.h>

#include "timers.h"
#include "log.h"

RETT_SYSCALL_INTERCEPT _sfs_LINK(INTF_SYSCALL)
{
	DEBUG_FILE("CALL: %s\n", __func__);
    char *path1, *path2;

    path1 = (char *)arg0;
    path2 = (char *)arg1;

	*result = syscall_no_intercept(SYS_link, path1, path2);
	instrumentation_type op_log_entry_time;
	// Write to op log

#if !POSIX_ENABLED
	if(*result == 0) {
		START_TIMING(op_log_entry_t, op_log_entry_time);
		persist_op_entry(LOG_LINK,
				path1,
				path2,
				0,
				0);
		END_TIMING(op_log_entry_t, op_log_entry_time);
	}
#endif
	return RETT_NO_PASS_KERN;
}

RETT_SYSCALL_INTERCEPT _sfs_SYMLINK(INTF_SYSCALL)
{
	DEBUG_FILE("CALL: %s\n", __func__);

    char *path1, *path2;

    path1 = (char *)arg0;
    path2 = (char *)arg1;

	*result = syscall_no_intercept(SYS_symlink, path1, path2);
	instrumentation_type op_log_entry_time;
	// Write to op log

#if !POSIX_ENABLED
	if(*result == 0) {
		START_TIMING(op_log_entry_t, op_log_entry_time);
		persist_op_entry(LOG_SYMLINK,
				path1,
				path2,
				0,
				0);
		END_TIMING(op_log_entry_t, op_log_entry_time);
	}
#endif
	return RETT_NO_PASS_KERN;
}

RETT_SYSCALL_INTERCEPT _sfs_SYMLINKAT(INTF_SYSCALL)
{
	DEBUG_FILE("CALL: %s\n", __func__);
	instrumentation_type op_log_entry_time;
    char *old_path, *new_path;
    int newdirfd;

    old_path = (char *)arg0;
    newdirfd = (int)arg1;
    new_path = (char *)arg2;

	*result = syscall_no_intercept(SYS_symlinkat, old_path, newdirfd, new_path);
	// Write to op log

#if !POSIX_ENABLED
	char path[256];
	int path_len = 0;
	if (newdirfd == AT_FDCWD) {
		if (new_path[0] != '/') {
			if (getcwd(path, sizeof(path)) == NULL)
				assert(0);

			path_len = strlen(path);
			path[path_len] = '/';
			path[path_len+1] = '\0';

			if (strcat(path, new_path) == NULL)
				assert(0);
		} else {
			if (strcpy(path, new_path) == NULL)
				assert(0);
		}
	} else {
		char fd_str[256];
		if (new_path[0] != '/') {
			sprintf(fd_str, "/proc/self/fd/%d", newdirfd);
			if (readlink(fd_str, path, sizeof(path)) < 0)
				assert(0);

			path_len = strlen(path);
			path[path_len] = '/';
			path[path_len+1] = '\0';
			if (strcat(path, new_path) == NULL)
				assert(0);

		} else {
			if (strcpy(path, new_path) == NULL)
				assert(0);
		}
	}

	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_SYMLINK,
			 old_path,
			 path,
			 0,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return RETT_NO_PASS_KERN;
}