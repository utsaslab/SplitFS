/*
 * =====================================================================================
 *
 *       Filename:  out.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  08/02/2019 06:45:54 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), Rohan Kadekodi
 *   Organization:  University of Texas at Austin
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <syscall.h>

#include "compiler_utils.h"
#include "os_thread.h"
#include "os_util.h"
#include "out.h"

#define UTIL_MAX_ERR_MSG 128

static const char *Log_prefix;
static int Log_level;
static FILE *Out_fp;
static unsigned Log_alignment;

#define MAXPRINT 8192	/* maximum expected log line */

/*
 * out_init -- initialize the log
 *
 * This is called from the library initialization code.
 */
void
out_init(const char *log_prefix, const char *log_level_var,
		const char *log_file_var)
{
	static int once;

	/* only need to initialize the out module once */
	if (once)
		return;
	once++;

	Log_prefix = log_prefix;

#ifdef DEBUG
	char *log_level;
	char *log_file;

	if ((log_level = getenv(log_level_var)) != NULL) {
		Log_level = atoi(log_level);
		// if (Log_level < 0) {
		// 	Log_level = 0;
		// }
	}

	if ((log_file = getenv(log_file_var)) != NULL && log_file[0] != '\0') {
		size_t cc = strlen(log_file);

		/* reserve more than enough space for a PID + '\0' */
		char *log_file_pid = alloca(cc + 30);
        fprintf(stderr, "Here in debug\n");
		if (cc > 0 && log_file[cc - 1] == '-') {
			snprintf(log_file_pid, cc + 30, "%s%d",
				log_file, os_getpid());
			log_file = log_file_pid;
		}
		if ((Out_fp = fopen(log_file, "w")) == NULL) {
			char buff[UTIL_MAX_ERR_MSG];
			os_describe_errno(errno, buff, UTIL_MAX_ERR_MSG);
			fprintf(stderr, "Error (%s): %s=%s: %s\n",
					log_prefix, log_file_var,
					log_file, buff);
			abort();
		}
	}
#endif	/* DEBUG */

	if (Out_fp == NULL)
		Out_fp = stderr;
	else
		setvbuf(Out_fp, NULL, _IOLBF, 0);

#ifdef DEBUG
	LOG(LDBG, "pid %d: program: %s", os_getpid(), os_getexecname());
#endif

}

/*
 * out_fini -- close the log file
 *
 * This is called to close log file before process stop.
 */
void
out_fini(void)
{
	if (Out_fp != NULL && Out_fp != stderr) {
		fclose(Out_fp);
		Out_fp = stderr;
	}
}

/*
 * out_print_func -- default print_func, goes to stderr or Out_fp
 */
static void
out_print_func(const char *s)
{
    syscall_no_intercept(SYS_write, fileno(Out_fp), s, strlen(s));
	//fputs(s, Out_fp);
}

/*
 * calling Print(s) calls the current print_func...
 */
typedef void (*Print_func)(const char *s);
typedef int (*Vsnprintf_func)(char *str, size_t size, const char *format,
		va_list ap);
static Print_func Print = out_print_func;
static Vsnprintf_func Vsnprintf = vsnprintf;

/*
 * out_set_print_func -- allow override of print_func used by out module
 */
void
out_set_print_func(void (*print_func)(const char *s))
{
	LOG(3, "print %p", print_func);

	Print = (print_func == NULL) ? out_print_func : print_func;
}

/*
 * out_set_vsnprintf_func -- allow override of vsnprintf_func used by out module
 */
void
out_set_vsnprintf_func(int (*vsnprintf_func)(char *str, size_t size,
				const char *format, va_list ap))
{
	LOG(3, "vsnprintf %p", vsnprintf_func);

	Vsnprintf = (vsnprintf_func == NULL) ? vsnprintf : vsnprintf_func;
}

/*
 * out_snprintf -- (internal) custom snprintf implementation
 */
static splitfs_printf_like(3, 4) int
out_snprintf(char *str, size_t size, const char *format, ...)
{
	int ret;
	va_list ap;

	va_start(ap, format);
	ret = Vsnprintf(str, size, format, ap);
	va_end(ap);

	return (ret);
}

/*
 * out_common -- common output code, all output goes through here
 */
static void
out_common(const char *file, int line, const char *func, int level, int pid, pthread_t tid,
		const char *suffix, const char *fmt, va_list ap)
{
	int oerrno = errno;
	char buf[MAXPRINT];
	unsigned cc = 0;
	int ret;
	const char *sep = "";
	char errstr[UTIL_MAX_ERR_MSG] = "";

	if (file) {
		char *f = strrchr(file, DIR_SEPARATOR);
		if (f)
			file = f + 1;
		ret = out_snprintf(&buf[cc], MAXPRINT - cc,
				"<%s>: <%d> <%d: %lu> [%s:%d %s] ",
				Log_prefix, level, pid, tid, file, line, func);
		if (ret < 0) {
			Print("out_snprintf failed");
			goto end;
		}
		cc += (unsigned)ret;
		if (cc < Log_alignment) {
			memset(buf + cc, ' ', Log_alignment - cc);
			cc = Log_alignment;
		}
	}

	if (fmt) {
		if (*fmt == '!') {
			fmt++;
			sep = ": ";
			os_describe_errno(errno, errstr, UTIL_MAX_ERR_MSG);
		}
		ret = Vsnprintf(&buf[cc], MAXPRINT - cc, fmt, ap);
		if (ret < 0) {
			Print("Vsnprintf failed");
			goto end;
		}
		cc += (unsigned)ret;
	}

	out_snprintf(&buf[cc], MAXPRINT - cc, "%s%s%s", sep, errstr, suffix);

	Print(buf);

end:
	errno = oerrno;
}

/*
 * out_error -- common error output code, all error messages go through here
 */
static void
out_error(const char *file, int line, const char *func,
		const char *suffix, const char *fmt, va_list ap)
{
	int oerrno = errno;
	unsigned cc = 0;
	int ret;
	const char *sep = "";
	char errstr[UTIL_MAX_ERR_MSG] = "";

	char *errormsg = (char *)out_get_errormsg();

	if (fmt) {
		if (*fmt == '!') {
			fmt++;
			sep = ": ";
			os_describe_errno(errno, errstr, UTIL_MAX_ERR_MSG);
		}
		ret = Vsnprintf(&errormsg[cc], MAXPRINT, fmt, ap);
		if (ret < 0) {
			strcpy(errormsg, "Vsnprintf failed");
			goto end;
		}
		cc += (unsigned)ret;
		out_snprintf(&errormsg[cc], MAXPRINT - cc, "%s%s",
				sep, errstr);
	}

#ifdef DEBUG
	if (Log_level >= 1) {
		char buf[MAXPRINT];
		cc = 0;

		if (file) {
			char *f = strrchr(file, DIR_SEPARATOR);
			if (f)
				file = f + 1;
			ret = out_snprintf(&buf[cc], MAXPRINT,
					"<%s>: <1> [%s:%d %s] ",
					Log_prefix, file, line, func);
			if (ret < 0) {
				Print("out_snprintf failed");
				goto end;
			}
			cc += (unsigned)ret;
			if (cc < Log_alignment) {
				memset(buf + cc, ' ', Log_alignment - cc);
				cc = Log_alignment;
			}
		}

		out_snprintf(&buf[cc], MAXPRINT - cc, "%s%s", errormsg,
				suffix);

		Print(buf);
	}
#endif

end:
	errno = oerrno;
}

/*
 * out_log -- output a log line if Log_level >= level
 */
void
out_log(const char *file, int line, const char *func, int level, int pid, pthread_t tid,
		const char *fmt, ...)
{
	va_list ap;

	if (Log_level < level)
		return;

	va_start(ap, fmt);
	out_common(file, line, func, level, pid, tid, "\n", fmt, ap);

	va_end(ap);
}

/*
 * out_fatal -- output a fatal error & die (i.e. assertion failure)
 */
void
out_fatal(const char *file, int line, const char *func,
		const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	out_common(file, line, func, 1, 1, 1, "\n", fmt, ap);

	va_end(ap);

	abort();
}

/*
 * out_err -- output an error message
 */
void
out_err(const char *file, int line, const char *func,
		const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	out_error(file, line, func, "\n", fmt, ap);

	va_end(ap);
}

/*
 * out_get_errormsg -- get the last error message
 */
const char *
out_get_errormsg(void)
{
    return "Last message";
}
