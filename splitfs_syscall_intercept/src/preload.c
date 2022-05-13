
#define _GNU_SOURCE

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <syscall.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <stdio.h>
#include <limits.h>
#include <linux/fs.h>
#include <utime.h>
#include <sys/fsuid.h>
#include <sys/capability.h>
#include <dlfcn.h>
#include <limits.h>

#include <asm-generic/errno.h>

#include "compiler_utils.h"
#include "libsyscall_intercept_hook_point.h"
#include "sys_util.h"
#include "preload.h"
#include "syscall_early_filter.h"
#include "out.h"

static pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;

static int exit_on_ENOTSUP;
static long check_errno(long e, long syscall_no)
{
	if (e == -ENOTSUP && exit_on_ENOTSUP) {
		char buf[100];
		sprintf(buf, "syscall %ld not supported by pmemfile, exiting",
				syscall_no);

		exit_with_msg(SPLITFS_PRELOAD_EXIT_NOT_SUPPORTED, buf);
	}

	return e;
}

void
exit_with_msg(int ret, const char *msg)
{
	if (msg && msg[0] == '!') {
		char buf[100];
		char *errstr = strerror_r(errno, buf, sizeof(buf));
		fprintf(stderr, "%s: %d %s\n", msg + 1, errno,
				errstr ? errstr : "unknown");

		log_write("%s: %d %s\n", msg + 1, errno,
				errstr ? errstr : "unknown");
	} else if (msg) {
		fprintf(stderr, "%s\n", msg);

		log_write("%s\n", msg);
	}

	exit(ret);
	__builtin_unreachable();
}

static size_t page_size;

static inline bool
is_accessible(const void *ptr, size_t len)
{
	if (len == 0)
		return true;
	if (!ptr)
		return false;
	return true;
}

static inline bool
is_str_accessible(const char *str)
{
	if (!str)
		return false;
	return true;
}

static inline bool
is_valid_ptr(const char *str)
{
	long result;
	result = syscall_no_intercept(SYS_access, str, F_OK);
	/**
	 * We need to check if 'str' is a valid pointer, but not crash it 
	 * (segfault) if it's invalid.
	 * 
	 * Since it is not possible to validate a pointer in the user-space, 
	 * we are making an access system call which validates  
	 * the pointer.
	 */
	if(result == -EFAULT) {
		return false;
	}
	return true;
}

static inline int
verify_iovec(long lvec, long cnt)
{
	return 0;
}

static long
hook_linkat(int fd0, const char *arg0, int fd1, const char * arg1, long flags)
{
    //exit_with_msg(-1, "hook_linkat() not supported. Exiting");
    return syscall_no_intercept(SYS_linkat, fd0, arg0, fd1, arg1, flags);
}

static long
hook_unlinkat(int fd, const char *path, long flags)
{
    if (!is_str_accessible(path) || !is_valid_ptr(path))
        return -EFAULT;
	
	if (flags & AT_REMOVEDIR) {
		return syscall_no_intercept(SYS_unlinkat, fd, path, flags);
	}

    long ret = 0;
    struct resolved_path where;

    resolve_path(fd, path, &where);

    if (where.error_code != 0) {
        ret = where.error_code;
    } else if (strstr(where.path, PMEM_ROOT_PATH) != (const char *)where.path) {
        ret = syscall_no_intercept(SYS_unlinkat, fd, path, flags);
    } else {
        ret = splitfs_unlinkat(where.path, flags);
    }

    return ret;
}

static long
hook_newfstatat(int fd, const char *path, struct stat *st, long arg2)
{
    //exit_with_msg(-1, "hook_newfstatat() not supported. Exiting");
    return syscall_no_intercept(SYS_newfstatat, fd, path, st, arg2);
}

static long
openat_helper(struct resolved_path *where, long flags, long mode)
{
    /*
    long temp_flags;

    if (flags & O_DIRECTORY || flags & O_ASYNC || flags & O_TMPFILE) {
        temp_flags = flags;
    }
    else {
        temp_flags = flags;
        temp_flags = temp_flags & ~O_RDONLY;
        temp_flags = temp_flags & ~O_WRONLY;
        temp_flags = temp_flags | O_RDWR;
    }
    */

	long fd = syscall_no_intercept(SYS_open, where->path, flags, mode);
	if (fd < 0)
		return fd;

    SPLITFSfile *file = splitfs_openat(where->path,
            ((int)flags) & ~O_NONBLOCK,
            (mode_t)mode);

    long r = fd;
    if (file != NULL) {
        r = splitfs_vfd_assign(fd, file);
        if (r < 0) {
            splitfs_close(fd, file);
            syscall_no_intercept(SYS_close, fd);
        }
    }

	return r;
}

static long
hook_openat(int fd_at, const char *path, long flags, long mode)
{
    if (!is_str_accessible(path) || !is_valid_ptr(path))
        return -EFAULT;

	long ret = 0;
	struct resolved_path where;

    resolve_path(fd_at, path, &where);

	if (where.error_code != 0) {
		/* path resolution failed */
		ret = where.error_code;
	} else if (strstr(where.path, PMEM_ROOT_PATH) != (char*) (where.path)) {
		/* Not pmemfile resident path */
		ret = syscall_no_intercept(SYS_openat,
                   fd_at, where.path, flags, mode);
	} else {
		ret = openat_helper(&where, flags, mode);
	}

	return ret;
}

static long
hook_fcntl(long fd, int cmd, long arg)
{
    //exit_with_msg(-1, "hook_fcntl() not supported. Exiting");
    return syscall_no_intercept(SYS_fcntl, fd, cmd, arg);
}

static long
hook_renameat2(int fd_old, const char *path_old, int fd_new,
		const char *path_new, unsigned flags)
{
    long ret = 0;
    struct resolved_path where_old, where_new;

	if (!is_valid_ptr(path_old) || !is_valid_ptr(path_new)) {
		return -EFAULT;
	}

    resolve_path(fd_old, path_old, &where_old);
    resolve_path(fd_new, path_new, &where_new);

    ret = syscall_no_intercept(SYS_renameat2, fd_old, path_old, fd_new, path_new, flags);
    return ret;
}

static long
hook_truncate(const char *path, off_t length)
{
	if (!is_str_accessible(path) || !is_valid_ptr(path))
		return -EFAULT;

	long result;

    if (strstr(path, PMEM_ROOT_PATH) != path) {
        result = syscall_no_intercept(SYS_truncate, path, length);
    } else {
        result = splitfs_truncate(path, length);
    }

	return result;
}

static long
hook_symlinkat(const char *target, int fd, const char *linkpath)
{
    //exit_with_msg(-1, "hook_symlinkat() not supported. Exiting");
    return syscall_no_intercept(SYS_symlinkat, target, fd, linkpath);
}

static long
hook_execveat(int fd, const char *path, char *const argv[],
		char *const envp[], int flags)
{
    //exit_with_msg(-1, "hook_execveat() not supported. Exiting");
	long result = splitfs_execv();
	if (result != 0) {
		FATAL("Failed to execv");
	}
    return syscall_no_intercept(SYS_execveat, fd, path, argv, envp, flags);
}

static long
hook_mmap(long arg0, long arg1, long arg2,
		long arg3, int fd, long arg5)
{
	return syscall_no_intercept(SYS_mmap,
				    arg0, arg1, arg2, arg3, fd, arg5);
}

static long
hook_mknodat(int fd, const char *path, mode_t mode, dev_t dev)
{
    //exit_with_msg(-1, "hook_mknodat() not supported. Exiting");
    return syscall_no_intercept(SYS_mknodat, fd, path, mode, dev);
}

static long
hook_statfs(const char *path, struct statfs *buf)
{
    //exit_with_msg(-1, "hook_statfs() not supported. Exiting");
    return syscall_no_intercept(SYS_statfs, path, buf);
}

static long
dispatch_syscall(long syscall_number,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5)
{

	switch (syscall_number) {

	/* Use pmemfile_openat to implement open, create, openat */
	case SYS_open:
		return hook_openat(AT_FDCWD, (const char *)arg0, arg1, arg2);

	case SYS_creat:
		return hook_openat(AT_FDCWD, (const char *)arg0,
			O_WRONLY | O_CREAT | O_TRUNC, arg1);

	case SYS_openat:
		return hook_openat((int)arg0, (const char *)arg1, arg2, arg3);

	case SYS_rename:
		return hook_renameat2(AT_FDCWD, (const char *)arg0,
			AT_FDCWD, (const char *)arg1, 0);

	case SYS_renameat:
		return hook_renameat2((int)arg0, (const char *)arg1, (int)arg2,
			(const char *)arg3, 0);

	case SYS_renameat2:
		return hook_renameat2((int)arg0, (const char *)arg1, (int)arg2,
			(const char *)arg3, (unsigned)arg4);

	case SYS_link:
		/* Use pmemfile_linkat to implement link */
		return hook_linkat(AT_FDCWD, (const char *)arg0,
				AT_FDCWD, (const char *)arg1, 0);

	case SYS_linkat:
		return hook_linkat((int)arg0, (const char *)arg1, (int)arg2,
				(const char *)arg3, arg4);

	case SYS_unlink:
		/* Use pmemfile_unlinkat to implement unlink */
		return hook_unlinkat(AT_FDCWD, (const char *)arg0, 0);

	case SYS_unlinkat:
		return hook_unlinkat((int)arg0, (const char *)arg1, arg2);

	case SYS_rmdir:
		/* Use pmemfile_unlinkat to implement rmdir */
		return hook_unlinkat(AT_FDCWD, (const char *)arg0,
				AT_REMOVEDIR);

	/*
	 * The newfstatat syscall implements both stat and lstat.
	 * Linux calls it: newfstatat ( I guess there was an old one )
	 * POSIX / libc interfaces call it: fstatat
	 * pmemfile calls it: pmemfile_fstatat
	 *
	 * fstat is unique.
	 */
	case SYS_stat:
		return hook_newfstatat(AT_FDCWD, (const char *)arg0,
				(struct stat*)arg1, 0);

	case SYS_lstat:
		return hook_newfstatat(AT_FDCWD, (const char *)arg0,
				(struct stat*)arg1, AT_SYMLINK_NOFOLLOW);

	case SYS_newfstatat:
		return hook_newfstatat((int)arg0, (const char *)arg1,
				(struct stat*)arg2, arg3);

	case SYS_close:
		return splitfs_vfd_close((int)arg0);

	case SYS_mmap:
		return hook_mmap(arg0, arg1, arg2, arg3, (int)arg4, arg5);

	case SYS_truncate:
		return hook_truncate((const char *)arg0, arg1);

	case SYS_symlink:
		return hook_symlinkat((const char *)arg0,
			AT_FDCWD, (const char *)arg1);

	case SYS_symlinkat:
		return hook_symlinkat((const char *)arg0, (int)arg1,
			(const char *)arg2);

	case SYS_mknod:
		return hook_mknodat(AT_FDCWD, (const char *)arg0,
				(mode_t)arg1, (dev_t)arg2);

	case SYS_mknodat:
		return hook_mknodat((int)arg0, (const char *)arg1,
				(mode_t)arg2, (dev_t)arg3);

	case SYS_execve:
		return hook_execveat(AT_FDCWD, (const char *)arg0,
		    (char *const *)arg1, (char *const *)arg2, 0);

	case SYS_execveat:
		return hook_execveat((int)arg0, (const char *)arg1,
			(char *const *)arg2, (char *const *)arg3, (int)arg4);

	case SYS_dup:
		return splitfs_vfd_dup((int)arg0);

	case SYS_dup2:
		return splitfs_vfd_dup2((int)arg0, (int)arg1);

	case SYS_dup3:
		return splitfs_vfd_dup3((int)arg0, (int)arg1, (int)arg2);

	case SYS_statfs:
		return hook_statfs((const char *)arg0, (struct statfs *)arg1);

	default:
		/* Did we miss something? */
		// assert(false);
		return syscall_no_intercept(syscall_number,
		    arg0, arg1, arg2, arg3, arg4, arg5);
	}
}

static long
dispatch_syscall_fd_first(long syscall_number,
            struct splitfs_file *file,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5)
{
	switch (syscall_number) {

	case SYS_write: {
		if (!is_accessible((void *)arg1, (size_t)arg2))
			return -EFAULT;

		return splitfs_write(arg0, file, (void*)arg1, (size_t)arg2);
	}

	case SYS_writev: {
		int ret;
		if ((ret = verify_iovec(arg1, arg2)))
			return ret;

		return splitfs_writev(arg0, file, (struct iovec*)arg1, (int)arg2);
	}

	case SYS_read: {
		if (!is_accessible((void *)arg1, (size_t)arg2))
			return -EFAULT;

		return splitfs_read(arg0, file, (void*)arg1, (size_t)arg2);
	}

	case SYS_readv: {
		int ret;
		if ((ret = verify_iovec(arg1, arg2)))
			return ret;

		return splitfs_readv(arg0, file, (struct iovec*)arg1, (int)arg2);
	}

	case SYS_lseek:
		return splitfs_lseek(arg0, file, arg1, (int)arg2);

	case SYS_pread64: {
		if (!is_accessible((void *)arg1, (size_t)arg2))
			return -EFAULT;

		return splitfs_pread(arg0, file, (void*)arg1, (size_t)arg2, arg3);
	}

	case SYS_pwrite64: {
		if (!is_accessible((void *)arg1, (size_t)arg2))
			return -EFAULT;

		return splitfs_pwrite(arg0, file, (void*)arg1, (size_t)arg2, arg3);
	}

	case SYS_preadv: {
		int ret;
		if ((ret = verify_iovec(arg1, arg2)))
			return ret;

		return splitfs_preadv(arg0, file, (struct iovec*)arg1, (int)arg2, arg3);
	}

	case SYS_pwritev: {
		int ret;
		if ((ret = verify_iovec(arg1, arg2)))
			return ret;

		return splitfs_pwritev(arg0, file, (struct iovec*)arg1, (int)arg2, arg3);
	}

	case SYS_fcntl:
		return hook_fcntl(arg0, (int)arg1, arg2);

	case SYS_ftruncate:
		return splitfs_ftruncate(arg0, file, arg1);

	case SYS_fallocate:
		return splitfs_fallocate(arg0, file, (int)arg1, arg2, arg3);

	case SYS_fstat: {
		if (!is_accessible((void *)arg1, sizeof(struct stat)))
			return -EFAULT;

		return splitfs_fstat(arg0, file, (void*)arg1);
	}

    case SYS_fsync: {
        return splitfs_fsync(arg0, file);
    }

	default:
		/* Did we miss something? */
		assert(false);
		return syscall_no_intercept(syscall_number,
		    arg0, arg1, arg2, arg3, arg4, arg5);

	}
}


/*
 * Return values expected by libsyscall_intercept:
 * A non-zero return value if it should execute the syscall,
 * zero return value if it should not execute the syscall, and
 * use *result value as the syscall's result.
 */
#define NOT_HOOKED 1
#define HOOKED 0

static int
hook(const struct syscall_early_filter_entry *filter_entry, long syscall_number,
			long arg0, long arg1,
			long arg2, long arg3,
			long arg4, long arg5,
			long *syscall_return_value)
{
	if (syscall_number != SYS_execve) {
		// We do not lock for execve since we do shm_open
		util_mutex_lock(&global_mutex); // TODO: Why is this required?
	}

    //char data[1000];
    //for (int i = 0; i < 1000; i++)
    //data[i] = '\0';
    //sprintf(data, "<%s, %d>: syscall number = %ld\n", __func__, __LINE__, syscall_number);
    //syscall_no_intercept(SYS_write, fileno(stderr), data, strlen(data));
	if (syscall_number == SYS_fcntl &&
	    ((int)arg1 == F_DUPFD || (int)arg1 == F_DUPFD_CLOEXEC)) {
		/*
		 * Other fcntl commands on pmemfile resident files are handled
		 * via dispatch_syscall_fd_first.
		 *
		 * XXX: close-on-exec flag is not handled correctly yet.
		 */
		*syscall_return_value =
		    splitfs_vfd_fcntl_dup((int)arg0, (int)arg2);

		//syscall_no_intercept(SYS_write, fileno(stderr), "END\n", 5);
		if (syscall_number != SYS_execve) {
			util_mutex_unlock(&global_mutex);
		}
		return HOOKED;
	}

	int is_hooked;

	is_hooked = HOOKED;

	if (filter_entry->fd_first_arg) {

		struct splitfs_file *file = splitfs_vfd_ref((int)arg0);
        if (!file) {
            *syscall_return_value = syscall_no_intercept(syscall_number,
                    arg0, arg1, arg2, arg3, arg4, arg5);
            goto end;
        }

        *syscall_return_value =
		dispatch_syscall_fd_first(syscall_number,
					  file, arg0, arg1, arg2, arg3, arg4, arg5);

		*syscall_return_value =
		    check_errno(*syscall_return_value, syscall_number);

		splitfs_vfd_unref((int)arg0);

	} else {
		*syscall_return_value = dispatch_syscall(syscall_number,
							 arg0, arg1, arg2, arg3, arg4, arg5);
	}

    /*
    *syscall_return_value = syscall_no_intercept(syscall_number,
            arg0, arg1, arg2, arg3, arg4, arg5);
    */
end:
    //syscall_no_intercept(SYS_write, fileno(stderr), "END\n", 5);
	if (syscall_number != SYS_execve) {
		util_mutex_unlock(&global_mutex);
	}
    return is_hooked;
}

static __thread bool guard_flag;
/*
 * hook_reentrance_guard_wrapper -- a wrapper which can notice reentrance.
 *
 * The guard_flag flag allows pmemfile to prevent the hooking of its own
 * syscalls. E.g. while handling an open syscall, libpmemfile might
 * call pmemfile_pool_open, which in turn uses an open syscall internally.
 * This internally used open syscall is once again forwarded to libpmemfile,
 * but using this flag libpmemfile can notice this case of reentering itself.
 *
 * XXX This approach still contains a very significant bug, as libpmemfile being
 * called inside a signal handler might easily forward a mock fd to the kernel.
 */
static int
hook_reentrance_guard_wrapper(long syscall_number,
				long arg0, long arg1,
				long arg2, long arg3,
				long arg4, long arg5,
				long *syscall_return_value)
{
	struct syscall_early_filter_entry filter_entry;
	filter_entry = get_early_filter_entry(syscall_number);

	if (!filter_entry.must_handle)
		return NOT_HOOKED;

	int is_hooked;

	guard_flag = true;
	int oerrno = errno;
	is_hooked = hook(&filter_entry, syscall_number, arg0, arg1, arg2, arg3,
			arg4, arg5, syscall_return_value);
	errno = oerrno;
	guard_flag = false;

	return is_hooked;
}

static void
init_hooking(void)
{
	/*
	 * Install the callback to be called by the syscall intercepting library
	 */
	intercept_hook_point = &hook_reentrance_guard_wrapper;
}

static int (*libc__xpg_strerror_r)(int __errnum, char *__buf, size_t __buflen);
static char *(*libc_strerror_r)(int __errnum, char *__buf, size_t __buflen);
static char *(*libc_strerror)(int __errnum);

int __xpg_strerror_r(int __errnum, char *__buf, size_t __buflen);

/*
 * XSI-compliant version of strerror_r. We have to override it to handle
 * possible deadlock/infinite recursion when pmemfile is called from inside of
 * strerror_r implementation and we call back into libc because of some failure
 * (notably: pool opening failed when process switching is enabled).
 */
int
__xpg_strerror_r(int __errnum, char *__buf, size_t __buflen)
{
	if (!guard_flag && libc__xpg_strerror_r)
		return libc__xpg_strerror_r(__errnum, __buf, __buflen);

	if (__errnum == EAGAIN) {
		const char *str =
			"Resource temporary unavailable (pmemfile wrapper)";
		if (__buflen < strlen(str) + 1)
			return ERANGE;
		strcpy(__buf, str);
		return 0;
	}

	const char *str = "Error code %d (pmemfile wrapper)";
	if (__buflen < strlen(str) + 10)
		return ERANGE;
	sprintf(__buf, str, __errnum);

	return 0;
}

/*
 * GNU-compliant version of strerror_r. See __xpg_strerror_r description.
 */
char *
strerror_r(int __errnum, char *__buf, size_t __buflen)
{
	if (!guard_flag && libc_strerror_r)
		return libc_strerror_r(__errnum, __buf, __buflen);

	const char *str = "Error code %d (pmemfile wrapper)";
	if (__buflen < strlen(str) + 10)
		return NULL;

	sprintf(__buf, str, __errnum);
	return __buf;
}

/*
 * See __xpg_strerror_r description.
 */
char *
strerror(int __errnum)
{
	static char buf[100];
	if (!guard_flag && libc_strerror)
		return libc_strerror(__errnum);

	sprintf(buf, "Error code %d (pmemfile wrapper)", __errnum);
	return buf;
}

static volatile int pause_at_start;

splitfs_constructor void
splitfs_preload_constructor(void)
{
	long ps_sys = sysconf(_SC_PAGE_SIZE);
	if (ps_sys < 0) {
	    FATAL("!sysconf PAGE_SIZE");
	}
	page_size = (size_t)ps_sys;

	splitfs_vfd_table_init();

    /*
	const char *env_str = getenv("PMEMFILE_EXIT_ON_NOT_SUPPORTED");
	if (env_str)
		exit_on_ENOTSUP = env_str[0] == '1';

	log_init(getenv("PMEMFILE_PRELOAD_LOG"),
			getenv("PMEMFILE_PRELOAD_LOG_TRUNC"));

	env_str = getenv("PMEMFILE_PRELOAD_PROCESS_SWITCHING");
	if (env_str)
		process_switching = env_str[0] == '1';

	initialize_validate_pointers();

	env_str = getenv("PMEMFILE_PRELOAD_PAUSE_AT_START");
	if (env_str && env_str[0] == '1') {
		pause_at_start = 1;
		while (pause_at_start)
			;
	}

	assert(pool_count == 0);
	struct stat kernel_cwd_stat;
	stat_cwd(&kernel_cwd_stat);

	detect_mount_points(&kernel_cwd_stat);
	establish_mount_points(getenv("PMEMFILE_POOLS"), &kernel_cwd_stat);
    */

	libc__xpg_strerror_r = dlsym(RTLD_NEXT, "__xpg_strerror_r");
	if (!libc__xpg_strerror_r)
		FATAL("!can't find __xpg_strerror_r");

	libc_strerror_r = dlsym(RTLD_NEXT, "strerror_r");
	if (!libc_strerror_r)
		FATAL("!can't find strerror_r");

	libc_strerror = dlsym(RTLD_NEXT, "strerror");
	if (!libc_strerror)
		FATAL("!can't find strerror");

	/*
	 * Must be the last step, the callback can be called anytime
	 * after the call to init_hooking()
	 */
	init_hooking();

    /*
	char *cd = getenv("PMEMFILE_CD");
	if (cd && chdir(cd)) {
		perror("chdir");
		exit(1);
	}
    */
}

splitfs_destructor void
splitfs_preload_destructor(void)
{
	/*
	 * Flush all streams, before library state is destructed.
	 * Fixes an issue when application forgets to flush or close a file
	 * it written to and libc destructor calls fflush when pmemfile
	 * and pmemobj state doesn't exist anymore.
	 */
	fflush(NULL);
}
