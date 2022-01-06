#include <libsyscall_intercept_hook_point.h>
#include <nv_common.h>

#include "timers.h"
#include "log.h"

// Makes the call synchronous in case of 'strict' and 'sync' mode
RETT_SYSCALL_INTERCEPT _sfs_MKNOD(INTF_SYSCALL) {
	instrumentation_type op_log_entry_time;
    char *path;
    mode_t mode;
    dev_t dev;

    path = (char *)arg0;
    mode = (mode_t)arg1;
    dev = (dev_t)arg2;

	*result = syscall_no_intercept(SYS_mknod, path, mode, dev);

#if !POSIX_ENABLED
	if (S_ISREG(mode)) {
		START_TIMING(op_log_entry_t, op_log_entry_time);
		persist_op_entry(LOG_FILE_CREATE,
				 path,
				 NULL,
				 mode,
				 0);
		END_TIMING(op_log_entry_t, op_log_entry_time);
	}
#endif

	return RETT_NO_PASS_KERN;
}

RETT_SYSCALL_INTERCEPT _sfs_MKNODAT(INTF_SYSCALL) {
    int dirfd;
    char *path;
    mode_t mode;
    dev_t dev;

    dirfd = (int)arg0;
    path = (char *)arg1;
    mode = (mode_t)arg2;
    dev = (dev_t)arg3;

	*result = syscall_no_intercept(SYS_mknodat, dirfd, path, mode, dev);

	char new_path[256];
	int path_len = 0;
	instrumentation_type op_log_entry_time;

	if (S_ISREG(mode)) {
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
	}

#if !POSIX_ENABLED
	START_TIMING(op_log_entry_t, op_log_entry_time);
	persist_op_entry(LOG_FILE_CREATE,
			 new_path,
			 NULL,
			 mode,
			 0);
	END_TIMING(op_log_entry_t, op_log_entry_time);
#endif
	return RETT_NO_PASS_KERN;
}