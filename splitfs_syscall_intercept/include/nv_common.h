// Header file shared by nvmfileops.c, fileops_compareharness.c
#define _GNU_SOURCE

#ifndef __NV_COMMON_H_
#define __NV_COMMON_H_

#ifndef __cplusplus
#endif

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/kernel.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sched.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/uio.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <sched.h>
#include <ctype.h>
#include <signal.h>
#include <pthread.h>
#include "debug.h"
#include "boost/preprocessor/seq/for_each.hpp"

#define MIN(X,Y) (((X)<(Y))?(X):(Y))
#define MAX(X,Y) (((X)>(Y))?(X):(Y))

// tell the compiler a branch is/is not likely to be followed
#define LIKELY(x)       __builtin_expect((x),1)
#define UNLIKELY(x)     __builtin_expect((x),0)

#define assert(x) if(UNLIKELY(!(x))) { printf("ASSERT FAILED ROHAN\n"); fflush(NULL); ERROR("NVP_ASSERT("#x") failed!\n"); exit(100); }

// ----------------- Syscall Intercept Stuff ----------------
#define INTF_SYSCALL long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long *result
#define RETT_SYSCALL_INTERCEPT int

// Pass thru call to kernel
#define RETT_PASS_KERN 1

// Took over call. Don't pass to kernel.
#define RETT_NO_PASS_KERN 0
// ----------------------------------------------------------

#define DO_ALIGNMENT_CHECKS 0

// places quotation marks around arg (eg, MK_STR(stuff) becomes "stuff")
#define MK_STR(arg) #arg
#define MK_STR2(x) MK_STR(x)
#define MK_STR3(x) MK_STR2(x)

#define MACRO_WRAP(a) a
#define MACRO_CAT(a, b) MACRO_WRAP(a##b)

#ifndef __cplusplus
typedef int bool;
#define false 0
#define true 1
#endif


#define BG_CLOSING 0
#define SEQ_LIST 0
#define RAND_LIST 1

// maximum number of file operations to support simultaneously
#define MAX_FILEOPS 32
#define BUF_SIZE 40

// Every time a function is used, determine whether the module's functions have been resolved.
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>

extern int OPEN_MAX;

#define NOSANITYCHECK 1
#if NOSANITYCHECK
	#define SANITYCHECK(x)
#else
	#define SANITYCHECK(x) if(UNLIKELY(!(x))) { ERROR("NVP_SANITY("#x") failed!\n"); exit(101); }
#endif

#define ASYNC_CLOSING async_close_enable
volatile int async_close_enable;

// Used to determine contents of flags passed to OPEN
#define FLAGS_INCLUDE(flags, x) ((flags&x)||(x==0))
#define DUMP_FLAGS(flags, x) do{ if(FLAGS_INCLUDE(flags, x)) { DEBUG_P("%s(0x%X) ",#x,x); } }while(0)


#define NVP_CHECK_NVF_VALID(nvf) do{					\
    if(UNLIKELY(!nvf->valid)) {				\
        DEBUG("Invalid file descriptor: %i\n", file);	\
        errno = 0;					\
        return -1;					\
    }							\
    else							\
    {						\
        DEBUG("this function is operating on node %p\n", nvf->node); \
    }						\
} while(0)

#define NVP_CHECK_NVF_VALID_WR(nvf) do{					\
    if(UNLIKELY(!nvf->valid)) {				\
        DEBUG("Invalid file descriptor: %i\n", file);	\
        errno = 0;					\
        return -1;					\
    }							\
    else {							\
        DEBUG("this function is operating on node %p\n", nvf->node); \
    }						\
} while(0)

#define IS_ERR(x) ((unsigned long)(x) >= (unsigned long)-4095)

// modifications to support different FSYNC policies
#define NVMM_PATH "/mnt/pmem_emul/"

#define SANITYCHECKNVF(nvf)						\
	SANITYCHECK(nvf->valid);					\
	SANITYCHECK(nvf->node != NULL);					\
	SANITYCHECK(nvf->fd >= 0);					\
	SANITYCHECK(nvf->fd < OPEN_MAX);				\
	SANITYCHECK(nvf->offset != NULL);				\
	SANITYCHECK(*nvf->offset >= 0);					\
	SANITYCHECK(nvf->node->length >=0);				\
	SANITYCHECK(nvf->node->maplength >= nvf->node->length);		\
	SANITYCHECK(nvf->node->data != NULL)


#define SFS_OPS (CLOSE) (DUP) (DUP2) (EXECVE) (FSYNC) (LINK) (MKDIR) (MKDIRAT) (MKNOD) (MKNODAT) (OPEN) (READ) \
        (RENAME) (RMDIR) (SEEK) (SYMLINK) (SYMLINKAT) (UNLINK) (UNLINKAT) (WRITE)
#define DECLARE_SFS_FUNCS(FUNCT, prefix) \
    RETT_SYSCALL_INTERCEPT prefix##FUNCT(INTF_SYSCALL);
#define DECLARE_SFS_FUNCS_IWRAP(r, data, elem) DECLARE_SFS_FUNCS(elem, data)

BOOST_PP_SEQ_FOR_EACH(DECLARE_SFS_FUNCS_IWRAP, _sfs_, SFS_OPS)

#endif
