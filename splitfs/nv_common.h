// Header file shared by nvmfileops.c, fileops_compareharness.c

#ifndef __NV_COMMON_H_
#define __NV_COMMON_H_

#ifndef __cplusplus
//typedef long long off64_t;
#endif

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/uio.h>
#include <dlfcn.h>
#include <stdint.h>
#include <sched.h>
#include <ctype.h>
#include "debug.h"

#include "boost/preprocessor/seq/for_each.hpp"
//#include "boost/preprocessor/cat.hpp"

#define BUF_SIZE 40

#define MIN(X,Y) (((X)<(Y))?(X):(Y))
#define MAX(X,Y) (((X)>(Y))?(X):(Y))

// tell the compiler a branch is/is not likely to be followed
#define LIKELY(x)       __builtin_expect((x),1)
#define UNLIKELY(x)     __builtin_expect((x),0)

#define assert(x)							\
	if(UNLIKELY(!(x))) {						\
		PRINTTRACE();						\
		fflush(NULL);						\
		ERROR("NVP_ASSERT("#x") failed!\n");			\
		exit(100);						\
	}

// places quotation marks around arg (eg, MK_STR(stuff) becomes "stuff")
#define MK_STR(arg) #arg
#define MK_STR2(x) MK_STR(x)
#define MK_STR3(x) MK_STR2(x)

#define MACRO_WRAP(a) a
#define MACRO_CAT(a, b) MACRO_WRAP(a##b)


//#define MONETA_DEVICE_PATH "/dev/bbda" 
#define MONETA_CHAR_DEVICE_PATH "/dev/monetaCtrla"
#define SDSSD_CHAR_DEVICE_PATH "/dev/monetaCtrla"
#define MONETA_BLOCK_DEVICE_PATH "/dev/monetaa"
#define SDSSD_BLOCK_DEVICE_PATH "/dev/monetaa"

#define ST_MONETA_DEVICE_ID 252
#define ST_SDSSD_DEVICE_ID 252
#define ST_SDSSD_BANKSHOT_DEVICE_ID 251

#ifndef __cplusplus
typedef int bool;
#define false 0
#define true 1
#endif

int execv_done;

// maximum number of file operations to support simultaneously
#define MAX_FILEOPS 32


// functions to use when invoking system calls (since the normal ones may be aliased)
//#define ALLOPS_FINITEPARAMS READ WRITE CLOSE SEEK TRUNC DUP DUP2 FORK MMAP READV WRITEV PIPE MUNMAP MSYNC
//#define ALLOPS OPEN IOCTL ALLOPS_FINITEPARAMS

// BOOST_PP only support parenthesis-delimited lists...
// I would have implemented this with BOOST_PP, but <see previous line>
#define OPS_FINITEPARAMS_64 (FTRUNC64) (SEEK64) (PREAD64) (PWRITE64)
#define OPS_64 OPS_FINITEPARAMS (OPEN64)

#ifdef TRACE_FP_CALLS
#define ALLOPS_FINITEPARAMS_WPAREN (POSIX_FALLOCATE) (POSIX_FALLOCATE64) (FALLOCATE) (READ) (FREAD) (CLEARERR) (FEOF) (FERROR)  (WRITE) (FWRITE) (FSEEK) (FTELL) (FTELLO) (CLOSE) (FCLOSE) (SEEK) (FTRUNC) (DUP) (DUP2) (FORK) (VFORK) (READV) (WRITEV) (PIPE) (SOCKETPAIR) OPS_FINITEPARAMS_64 (PREAD) (PWRITE) (FSYNC) (FDSYNC) (SOCKET) (ACCEPT) (UNLINK) (UNLINKAT) (SYNC_FILE_RANGE) (STAT) (STAT64) (FSTAT) (FSTAT64) (LSTAT) (LSTAT64)

#define ALLOPS_WPAREN (OPEN) (OPENAT) (CREAT) (EXECVE) (EXECVP) (EXECV) (FOPEN) (FOPEN64) (FREAD_UNLOCKED) (IOCTL) (TRUNC) (MKNOD) (MKNODAT) (FCNTL) ALLOPS_FINITEPARAMS_WPAREN
#define SHM_WPAREN (SHM_COPY)
// NOTE: clone is missing on purpose.(MMAP) (MUNMAP) (MSYNC) (CLONE) (MMAP64)
#define METAOPS (MKDIR) (RENAME) (LINK) (SYMLINK) (RMDIR) (SYMLINKAT) (MKDIRAT)

#define FILEOPS_WITH_FP (FREAD) (CLEARERR) (FEOF) (FERROR) (FWRITE) (FSEEK) (FTELL) (FTELLO) 

#else

#define ALLOPS_FINITEPARAMS_WPAREN (READ) (WRITE) (CLOSE) (SEEK) (FTRUNC) (DUP) (DUP2) (FORK) (VFORK) (READV) (WRITEV) (PIPE) (SOCKETPAIR) OPS_FINITEPARAMS_64 (PREAD) (PWRITE) (FSYNC) (FDSYNC) (SOCKET) (ACCEPT) (UNLINK) (UNLINKAT) (STAT) (STAT64) (FSTAT) (FSTAT64) (LSTAT) (LSTAT64)
#define ALLOPS_WPAREN (OPEN) (OPENAT) (CREAT) (EXECVE) (EXECVP) (EXECV) (IOCTL) (TRUNC) (MKNOD) (MKNODAT) ALLOPS_FINITEPARAMS_WPAREN
#define SHM_WPAREN (SHM_COPY)
#define METAOPS (MKDIR) (RENAME) (LINK) (SYMLINK) (RMDIR) (SYMLINKAT) (MKDIRAT)

#endif

#define FILEOPS_WITH_FD (READ) (WRITE) (SEEK) (READV) (WRITEV) (FTRUNC) (FTRUNC64) (SEEK64) (PREAD) (PREAD64) (PWRITE) (PWRITE64) (FSYNC) (FDSYNC) (POSIX_FALLOCATE) (POSIX_FALLOCATE64) (FALLOCATE) (SYNC_FILE_RANGE) (FSTAT) (FSTAT64)

//(ACCEPT)
#define FILEOPS_WITHOUT_FD (FORK) (VFORK)
#define FILEOPS_PIPE (PIPE)
#define FILEOPS_SOCKET (SOCKETPAIR)
//(SOCKET)


// Every time a function is used, determine whether the module's functions have been resolved.
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
// These aren't resolved at runtime before main() because it's impossible to determine
// RELIABLY which constructor will be called first.
#define CHECK_RESOLVE_FILEOPS_OLDVERSION(NAME) do{			\
		if(UNLIKELY(NAME##fileops==NULL)) {			\
			if(NAME##resolve_fileops()!=0) {		\
				ERROR("Couldn't resolve " #NAME "!\n"); \
			}						\
		} } while(0)

#define CHECK_RESOLVE_FILEOPS(NAME) do{					\
		if(UNLIKELY(NAME##fileops==NULL)) {			\
			MSG("Tried to use "#NAME" fileops, but they weren't initialized!  BLARG\n" FAIL); \
			assert(0);					\
		} } while(0)

// Macros to create a Fileops_p structure with pointers for the module,
// then register that Fileops_p structure with the hub.
// Thgis occurs at execution time, before main() is called.
// Fourth argument supported as function call at end of constructor.
#define ADD_FUNCTP(FUNCT, prefix) fo->FUNCT = prefix##FUNCT; 
#define ADD_FUNCTP_IWRAP(r, data, elem) ADD_FUNCTP(elem, data) 

#define INIT_FILEOPS_P(NAME, PREFIX)					\
	struct Fileops_p* fo = (struct Fileops_p*) calloc(1, sizeof(struct Fileops_p)); \
	fo->name = NAME;						\
	fo->resolve = PREFIX##resolve_fileops;				\
	BOOST_PP_SEQ_FOR_EACH(ADD_FUNCTP_IWRAP, PREFIX, ALLOPS_WPAREN); \
	BOOST_PP_SEQ_FOR_EACH(ADD_FUNCTP_IWRAP, PREFIX, METAOPS);	\
	BOOST_PP_SEQ_FOR_EACH(ADD_FUNCTP_IWRAP, PREFIX, SHM_WPAREN);	\
	_hub_add_fileop(fo); 

//extern struct Fileops_p* _hub_fileops_lookup[];
#define MODULE_REGISTRATION_F(NAME, PREFIX, ...)			\
	extern int OPEN_MAX;						\
	struct Fileops_p* PREFIX##fileops;				\
	int PREFIX##resolve_fileops(char*);				\
	void PREFIX##init(void) __attribute__((constructor));		\
	void PREFIX##init(void) {					\
		MSG("Initializing " NAME "_init\n");			\
		PREFIX##fileops = NULL;					\
		INIT_FILEOPS_P(NAME, PREFIX);				\
		if(OPEN_MAX<1) {					\
			OPEN_MAX = sysconf(_SC_OPEN_MAX);		\
			OPEN_MAX = 1024;				\
			DEBUG("Maximum simultaneous open files: %i\n", OPEN_MAX); \
		}							\
		__VA_ARGS__						\
			}						\
	int PREFIX##resolve_fileops(char* tree) {			\
		PREFIX##fileops = default_resolve_fileops(tree, NAME);	\
		if(PREFIX##fileops) { return 0; }			\
		else {							\
			ERROR("Failed to resolve "NAME" fileops\n");	\
			return -1;					\
		}							\
	}


struct Fileops_p* default_resolve_fileops(char* tree, char* name);

// Used to determine contents of flags passed to OPEN
#define FLAGS_INCLUDE(flags, x) ((flags&x)||(x==0))
#define DUMP_FLAGS(flags, x) do{ if(FLAGS_INCLUDE(flags, x)) { DEBUG_P("%s(0x%X) ",#x,x); } }while(0)

#define WEAK_ALIAS(a) __attribute__ ((weak, alias(a)))


// Information about the functions which are wrapped by EVERY module
// Alias: the standard function which most users will call
#define ALIAS_OPEN   open
#define ALIAS_CREAT  creat
#define ALIAS_EXECVE execve
#define ALIAS_EXECVP execvp
#define ALIAS_EXECV execv
#define ALIAS_MKNOD __xmknod
#define ALIAS_MKNODAT __xmknodat

#ifdef TRACE_FP_CALLS
#define ALIAS_FOPEN  fopen
#define ALIAS_FOPEN64  fopen64
#define ALIAS_FREAD  fread
#define ALIAS_FEOF 	 feof
#define ALIAS_FERROR ferror
#define ALIAS_FREAD_UNLOCKED  fread_unlocked
#define ALIAS_CLEARERR clearerr
#define ALIAS_FWRITE fwrite
#define ALIAS_FSEEK  fseek
#define ALIAS_FTELL  ftell
#define ALIAS_FTELLO ftello
#define ALIAS_FCLOSE fclose
#endif

#define ALIAS_READ   read
#define ALIAS_WRITE  write
#define ALIAS_SEEK   lseek
#define ALIAS_CLOSE  close
#define ALIAS_FTRUNC ftruncate
#define ALIAS_TRUNC  truncate
#define ALIAS_DUP    dup
#define ALIAS_DUP2   dup2
#define ALIAS_FCNTL fcntl
#define ALIAS_FORK   fork
#define ALIAS_VFORK  vfork
#define ALIAS_MMAP   mmap
#define ALIAS_READV  readv
#define ALIAS_WRITEV writev
#define ALIAS_PIPE   pipe
#define ALIAS_SOCKETPAIR   socketpair
#define ALIAS_IOCTL  ioctl
#define ALIAS_MUNMAP munmap
#define ALIAS_MSYNC  msync
#define ALIAS_CLONE  __clone
#define ALIAS_PREAD  pread
#define ALIAS_PREAD64 pread64
#define ALIAS_PWRITE pwrite
#define ALIAS_PWRITE64 pwrite64
//#define ALIAS_PWRITESYNC pwrite64_sync
#define ALIAS_FSYNC  fsync
#define ALIAS_SYNC_FILE_RANGE sync_file_range
#define ALIAS_FDSYNC fdatasync
#define ALIAS_FTRUNC64 ftruncate64
#define ALIAS_OPEN64  open64
#define ALIAS_SEEK64  lseek64
#define ALIAS_MMAP64  mmap64
#define ALIAS_MKSTEMP mkstemp
#define ALIAS_MKSTEMP64 mkstemp64
#define ALIAS_ACCEPT  accept
#define ALIAS_SOCKET  socket
#define ALIAS_UNLINK  unlink
#define ALIAS_POSIX_FALLOCATE posix_fallocate
#define ALIAS_POSIX_FALLOCATE64 posix_fallocate64
#define ALIAS_FALLOCATE fallocate
#define ALIAS_STAT __xstat
#define ALIAS_STAT64 __xstat64
#define ALIAS_FSTAT __fxstat
#define ALIAS_FSTAT64 __fxstat64
#define ALIAS_LSTAT __lxstat
#define ALIAS_LSTAT64 __lxstat64
/* Now all the metadata operations */
#define ALIAS_MKDIR mkdir
#define ALIAS_RENAME rename
#define ALIAS_LINK link
#define ALIAS_SYMLINK symlink
#define ALIAS_RMDIR rmdir
/* All the *at operations */
#define ALIAS_OPENAT openat
#define ALIAS_SYMLINKAT symlinkat
#define ALIAS_MKDIRAT mkdirat
#define ALIAS_UNLINKAT  unlinkat


// The function return type
#define RETT_OPEN   int
#define RETT_CREAT  int
#define RETT_EXECVE int
#define RETT_EXECVP int
#define RETT_EXECV int
#define RETT_SHM_COPY void
#define RETT_MKNOD int
#define RETT_MKNODAT int

#ifdef TRACE_FP_CALLS
#define RETT_FOPEN  FILE*
#define RETT_FOPEN64  FILE*
#define RETT_FREAD  size_t
#define RETT_FEOF   int
#define RETT_FERROR int
#define RETT_FREAD_UNLOCKED size_t
#define RETT_CLEARERR void
#define RETT_FWRITE size_t
#define RETT_FSEEK  int
#define RETT_FTELL  long int
#define RETT_FTELLO off_t
#define RETT_FCLOSE int
#endif

#define RETT_READ   ssize_t
#define RETT_WRITE  ssize_t
#define RETT_SEEK   off_t
#define RETT_CLOSE  int
#define RETT_FTRUNC int
#define RETT_TRUNC  int
#define RETT_DUP    int
#define RETT_DUP2   int
#define RETT_FCNTL  int
#define RETT_FORK   pid_t
#define RETT_VFORK  pid_t
#define RETT_MMAP   void*
#define RETT_READV  ssize_t
#define RETT_WRITEV ssize_t
#define RETT_PIPE   int
#define RETT_SOCKETPAIR   int
#define RETT_IOCTL  int
#define RETT_MUNMAP int
#define RETT_MSYNC  int
#define RETT_CLONE  int
#define RETT_PREAD  ssize_t
#define RETT_PREAD64 ssize_t
#define RETT_PWRITE ssize_t
#define RETT_PWRITE64 ssize_t
//#define RETT_PWRITESYNC ssize_t
#define RETT_FSYNC  int
#define RETT_SYNC_FILE_RANGE int
#define RETT_FDSYNC int
#define RETT_FTRUNC64 int
#define RETT_OPEN64  int
#define RETT_SEEK64  off64_t
#define RETT_MMAP64  void*
#define RETT_MKSTEMP int
#define RETT_MKSTEMP64 int
#define RETT_ACCEPT  int
#define RETT_SOCKET  int
#define RETT_UNLINK  int
#define RETT_POSIX_FALLOCATE int
#define RETT_POSIX_FALLOCATE64 int
#define RETT_FALLOCATE int
#define RETT_STAT int
#define RETT_STAT64 int
#define RETT_FSTAT int
#define RETT_FSTAT64 int
#define RETT_LSTAT int
#define RETT_LSTAT64 int
/* Now all the metadata operations */
#define RETT_MKDIR int
#define RETT_RENAME int
#define RETT_LINK int
#define RETT_SYMLINK int
#define RETT_RMDIR int
/* All the *at operations */
#define RETT_OPENAT int
#define RETT_SYMLINKAT int
#define RETT_MKDIRAT int
#define RETT_UNLINKAT int


// The function interface
#define INTF_OPEN const char *path, int oflag, ...
#define INTF_CREAT const char *path, mode_t mode
#define INTF_EXECVE const char *filename, char *const argv[], char *const envp[]
#define INTF_EXECVP const char *file, char *const argv[]
#define INTF_EXECV const char *path, char *const argv[]
#define INTF_SHM_COPY void
#define INTF_MKNOD int ver, const char* path, mode_t mode, dev_t* dev
#define INTF_MKNODAT int ver, int dirfd, const char* path, mode_t mode, dev_t* dev

#ifdef TRACE_FP_CALLS
#define INTF_FOPEN  const char* __restrict path, const char* __restrict mode
#define INTF_FOPEN64  const char* __restrict path, const char* __restrict mode
#define INTF_FREAD  void* __restrict buf, size_t length, size_t nmemb, FILE* __restrict fp
#define INTF_CLEARERR FILE* fp
#define INTF_FEOF   FILE* fp
#define INTF_FERROR FILE* fp
#define INTF_FREAD_UNLOCKED  void* __restrict buf, size_t length, size_t nmemb, FILE* __restrict fp
#define INTF_FWRITE const void* __restrict buf, size_t length, size_t nmemb, FILE* __restrict fp
#define INTF_FSEEK  FILE* fp, long int offset, int whence
#define INTF_FTELL  FILE* fp
#define INTF_FTELLO FILE* fp
#define INTF_FCLOSE FILE* fp
#endif

#define INTF_READ   int file, void* buf, size_t length
#define INTF_WRITE  int file, const void* buf, size_t length
#define INTF_SEEK   int file, off_t offset, int whence
#define INTF_CLOSE  int file
#define INTF_FTRUNC int file, off_t length
#define INTF_TRUNC  const char* path, off_t length
#define INTF_FCNTL  int file, int cmd, ...
#define INTF_DUP    int file
#define INTF_DUP2   int file, int fd2
#define INTF_FORK   void
#define INTF_VFORK  void
#define INTF_MMAP   void *addr, size_t len, int prot, int flags, int file, off_t off
#define INTF_READV  int file, const struct iovec *iov, int iovcnt
#define INTF_WRITEV int file, const struct iovec *iov, int iovcnt
#define INTF_PIPE   int file[2]
#define INTF_SOCKETPAIR   int domain, int type, int protocol, int sv[2]
#define INTF_IOCTL  int file, unsigned long int request, ...
#define INTF_MUNMAP void *addr, size_t len
#define INTF_MSYNC  void *addr, size_t len, int flags
#define INTF_CLONE  int (*fn)(void *a), void *child_stack, int flags, void *arg
#define INTF_PREAD  int file,       void *buf, size_t count, off_t offset
#define INTF_PREAD64  int file,       void *buf, size_t count, off_t offset
#define INTF_PWRITE int file, const void *buf, size_t count, off_t offset
#define INTF_PWRITE64 int file, const void *buf, size_t count, off_t offset
//#define INTF_PWRITESYNC int file, const void *buf, size_t count, off_t offset
#define INTF_FSYNC  int file
#define INTF_SYNC_FILE_RANGE int file, off_t offset, off_t nbytes, unsigned int flags
#define INTF_FDSYNC int file
#define INTF_FTRUNC64 int file, off64_t length
#define INTF_OPEN64  const char* path, int oflag, ...
#define INTF_SEEK64  int file, off64_t offset, int whence
#define INTF_MMAP64  void *addr, size_t len, int prot, int flags, int file, off64_t off
#define INTF_MKSTEMP char* file
#define INTF_MKSTEMP64 char* file
#define INTF_ACCEPT  int file, struct sockaddr *addr, socklen_t *addrlen
#define INTF_SOCKET  int domain, int type, int protocol
#define INTF_UNLINK  const char* path
#define INTF_POSIX_FALLOCATE int file, off_t offset, off_t len
#define INTF_POSIX_FALLOCATE64 int file, off_t offset, off_t len
#define INTF_FALLOCATE int file, int mode, off_t offset, off_t len
#define INTF_STAT int ver, const char *path, struct stat *buf
#define INTF_STAT64 int ver, const char *path, struct stat64 *buf
#define INTF_FSTAT int ver, int file, struct stat *buf
#define INTF_FSTAT64 int ver, int file, struct stat64 *buf
#define INTF_LSTAT int ver, const char *path, struct stat *buf
#define INTF_LSTAT64 int ver, const char *path, struct stat64 *buf
/* Now all the metadata operations */
#define INTF_MKDIR const char *path, uint32_t mode
#define INTF_RENAME const char *old, const char *new
#define INTF_LINK const char *path1, const char *path2
#define INTF_SYMLINK const char *path1, const char *path2
#define INTF_RMDIR const char *path
/* All the *at operations */
#define INTF_OPENAT int dirfd, const char* path, int oflag, ...
#define INTF_UNLINKAT  int dirfd, const char* path, int flags
#define INTF_SYMLINKAT const char* old_path, int newdirfd, const char* new_path
#define INTF_MKDIRAT int dirfd, const char* path, mode_t mode

// The interface, without types.  Used when calling from inside macros.
// CALL_ names must match INTF_ names.
#define CALL_OPEN   path, oflag
#define CALL_CREAT  path, mode
#define CALL_EXECVE filename, argv, envp
#define CALL_EXECVP file, argv
#define CALL_EXECV path, argv
#define CALL_MKNOD ver, path, mode, dev
#define CALL_MKNODAT ver, dirfd, path, mode, dev

#ifdef TRACE_FP_CALLS
#define CALL_FOPEN  path, mode
#define CALL_FOPEN64  path, mode
#define CALL_FREAD  buf, length, nmemb, fp
#define CALL_FEOF   fp
#define CALL_FERROR fp
#define CALL_FREAD_UNLOCKED  buf, length, nmemb, fp
#define CALL_CLEARERR fp
#define CALL_FWRITE buf, length, nmemb, fp
#define CALL_FSEEK  fp, offset, whence
#define CALL_FTELL  fp
#define CALL_FTELLO fp
#define CALL_FCLOSE fp
#endif

#define CALL_IOCTL  file, request
#define CALL_READ   file, buf, length
#define CALL_WRITE  file, buf, length
#define CALL_SEEK   file, offset, whence
#define CALL_CLOSE  file
#define CALL_FTRUNC file, length
#define CALL_TRUNC  path, length
#define CALL_DUP    file
#define CALL_DUP2   file, fd2
#define CALL_FCNTL file, cmd
#define CALL_FORK   
#define CALL_VFORK
#define CALL_MMAP   addr, len, prot, flags, file, off
#define CALL_READV  file, iov, iovcnt
#define CALL_WRITEV file, iov, iovcnt
#define CALL_PIPE   file
#define CALL_SOCKETPAIR   domain, type, protocol, sv
#define CALL_MUNMAP addr, len
#define CALL_MSYNC  addr, len, flags
#define CALL_CLONE  fn, child_stack, flags, arg
#define CALL_PREAD  file, buf, count, offset
#define CALL_PREAD64  file, buf, count, offset
#define CALL_PWRITE file, buf, count, offset
#define CALL_PWRITE64 file, buf, count, offset
//#define CALL_PWRITESYNC file, buf, count, offset
#define CALL_FSYNC  file
#define CALL_SYNC_FILE_RANGE file, offset, nbytes, flags
#define CALL_FDSYNC file
#define CALL_FTRUNC64 CALL_FTRUNC
#define CALL_OPEN64  CALL_OPEN
#define CALL_SEEK64  CALL_SEEK
#define CALL_MMAP64  CALL_MMAP
#define CALL_MKSTEMP file
#define CALL_MKSTEMP64 file
#define CALL_ACCEPT  file, addr, addrlen
#define CALL_SOCKET  domain, type, protocol
#define CALL_UNLINK  path
#define CALL_POSIX_FALLOCATE file, offset, len
#define CALL_POSIX_FALLOCATE64 file, offset, len
#define CALL_FALLOCATE file, mode, offset, len
#define CALL_STAT ver, path, buf
#define CALL_STAT64 ver, path, buf
#define CALL_FSTAT ver, file, buf
#define CALL_FSTAT64 ver, file, buf
#define CALL_LSTAT ver, path, buf
#define CALL_LSTAT64 ver, path, buf
/* Now all the metadata operations */
#define CALL_MKDIR path, mode
#define CALL_RENAME old, new
#define CALL_LINK path1, path2
#define CALL_SYMLINK path1, path2
#define CALL_RMDIR path
/* All the *at operations */
#define CALL_OPENAT dirfd, path, oflag
#define CALL_UNLINKAT  dirfd, path, flags
#define CALL_SYMLINKAT old_path, newdirfd, new_path
#define CALL_MKDIRAT dirfd, path, mode

// A format string for printf on the parameters
#define PFFS_OPEN   "%s, %i"
#define PFFS_CREAT  "%s, %i"
#define PFFS_EXECVE "%s, %s, %s"
#define PFFS_EXECVP "%s, %s"
#define PFFS_EXECV "%s, %s"
#define PFFS_SHM_COPY ""
#define PFFS_MKNOD "%i, %s, %i, %p"
#define PFFS_MKNODAT "%i, %i, %s, %i, %p"

#ifdef TRACE_FP_CALLS
#define PFFS_FOPEN  "%s, %s"
#define PFFS_FOPEN64  "%s, %s"
#define PFFS_FREAD  "%p, %i, %i, %p"
#define PFFS_FOEF   "%p"
#define PFFS_FREAD_UNLOCKED  "%p, %i, %i, %p"
#define PFFS_FWRITE "%p, %i, %i, %p"
#define PFFS_FSEEK  "%p, %i, %i"
#define PFFS_FTELL  "%p"
#define PFFS_FTELLO "%p"
#define PFFS_FCLOSE "%p"
#endif

#define PFFS_READ   "%i, %p, %i"
#define PFFS_WRITE  "%i, %p, %i"
#define PFFS_SEEK   "%i, %i, %i"
#define PFFS_CLOSE  "%i"
#define PFFS_FTRUNC "%i, %i"
#define PFFS_TRUNC  "%s, %i"
#define PFFS_DUP    "%i"
#define PFFS_DUP2   "%i, %i"
#define PFFS_FCNTL  "%d, %d"
#define PFFS_FORK   ""
#define PFFS_FORK   ""
#define PFFS_MMAP   "%p, %i, %i, %i, %i"
#define PFFS_READV  "%i, %p, %i"
#define PFFS_WRITEV "%i, %p, %i"
#define PFFS_PIPE   "%p"
#define PFFS_SOCKETPAIR  "%i, %i, %i, %p"
#define PFFS_IOCTL  "%i, %i"
#define PFFS_MUNMAP "%p, %i"
#define PFFS_MSYNC  "%p, %i, %i"
#define PFFS_CLONE  "%p, %p, %i, %p"
#define PFFS_PREAD  "%i, %p, %i, %i"
#define PFFS_PREAD64  "%i, %p, %i, %i"
#define PFFS_PWRITE "%i, %p, %i, %i"
#define PFFS_PWRITE64 "%i, %p, %i, %i"
//#define PFFS_PWRITESYNC "%i, %p, %i, %i"
#define PFFS_FSYNC  "%i"
#define PFFS_FDSYNC "%i"
#define PFFS_FTRUNC64 "%i, %i"
#define PFFS_OPEN64  "%s, %i"
#define PFFS_SEEK64  "%i, %i, %i"
#define PFFS_MMAP64  "%p, %i, %i, %i, %i"
#define PFFS_MKSTEMP "%s"
#define PFFS_MKSTEMP64 "%s"
#define PFFS_ACCEPT  "%d, %p, %p"
#define PFFS_SOCKET  "%d, %d, %d"
#define PFFS_UNLINK  "%s"
#define PFFS_POSIX_FALLOCATE "%i, %lu, %lu"
#define PFFS_POSIX_FALLOCATE64 "%i, %lu, %lu"
#define PFFS_FALLOCATE "%i, %i, %lu, %lu"
#define PFFS_STAT "%i, %s, %p"
#define PFFS_STAT64 "%i, %s, %p"
#define PFFS_FSTAT "%i, %i, %p"
#define PFFS_FSTAT64 "%i, %i, %p"
#define PFFS_LSTAT "%i, %s, %p"
#define PFFS_LSTAT64 "%i, %s, %p"
/* Now all the metadata operations */
#define PFFS_MKDIR "%s, %lu"
#define PFFS_RENAME "%s, %s"
#define PFFS_LINK "%s, %s"
#define PFFS_SYMLINK "%s, %s"
#define PFFS_RMDIR "%s"
/* All the *at operations */
#define PFFS_OPENAT "%i, %s, %i"
#define PFFS_UNLINKAT  "%d, %s, %d"
#define PFFS_SYMLINKAT "%s, %i, %s"
#define PFFS_MKDIRAT "%i, %s, %i"


// STD: the lowest (non-weak alias) version used by gcc
#define STD_OPEN   __open
#define STD_CREAT  __creat
#define STD_EXECVE __execve
#define STD_EXECVP __execvp
#define STD_EXECV __execv
#define STD_MKNOD __xmknod
#define STD_MKNODAT __xmknodat

#ifdef TRACE_FP_CALLS
#define STD_FOPEN  __fopen
#define STD_FOPEN64  __fopen64
#define STD_FREAD  __fread
#define STD_FREAD_UNLOCKED  __fread_unlocked
#define STD_FEOF   _IO_feof
#define STD_FERROR _IO_ferror
#define STD_CLEARERR _IO_CLEARERR
#define STD_FWRITE __fwrite
#define STD_FSEEK  __fseek
#define STD_FTELL  __ftell
#define STD_FTELLO __ftello
#define STD_FCLOSE __fclose
#endif

#define STD_OPEN64 __open64
#define STD_READ   __read
#define STD_WRITE  __write
#define STD_SEEK   __lseek
#define STD_SEEK64 __lseek64
#define STD_CLOSE  __close
#define STD_FTRUNC __ftruncate
#define STD_TRUNC  __truncate
#define STD_FTRUNC64 __ftruncate64
#define STD_DUP    __dup
#define STD_DUP2   __dup2
#define STD_FCNTL  __libc_fcntl
#define STD_FORK   __fork
#define STD_VFORK  __vfork
#define STD_MMAP   __mmap
#define STD_MMAP64 __mmap64
#define STD_READV  __readv
#define STD_WRITEV __writev
#define STD_PIPE   __pipe
#define STD_SOCKETPAIR   __socketpair
#define STD_IOCTL  __ioctl
#define STD_MUNMAP __munmap
#define STD_MSYNC  __libc_msync
#define STD_CLONE  __clone
#define STD_PREAD  __pread
#define STD_PREAD64 __pread64
#define STD_PWRITE __pwrite64
#define STD_PWRITE64 __pwrite64
//#define STD_PWRITESYNC __pwrite64_sync
#define STD_FSYNC  __fsync
#define STD_FDSYNC __fdsync
#define STD_MKSTEMP __mkstemp
#define STD_MKSTEMP64 __mkstemp64
#define STD_UNLINK __unlink
#define STD_POSIX_FALLOCATE __posix_fallocate
#define STD_POSIX_FALLOCATE64 __posix_fallocate64
#define STD_FALLOCATE __fallocate
#define STD_SYNC_FILE_RANGE sync_file_range
#define STD_STAT __xstat
#define STD_STAT64 __xstat64
#define STD_FSTAT __fxstat
#define STD_FSTAT64 __fxstat64
#define STD_LSTAT __lxstat
#define STD_LSTAT64 __lxstat64
/* Now all the metadata operations */
#define STD_MKDIR __mkdir
#define STD_RENAME __rename
#define STD_LINK __link
#define STD_SYMLINK __symlink
#define STD_RMDIR __rmdir
/* All the *at operations */
#define STD_OPENAT __openat
#define STD_UNLINKAT __unlinkat
#define STD_SYMLINKAT __symlinkat
#define STD_MKDIRAT __mkdirat

// declare as extern all the standard functions listed in ALLOPS
#define STD_DECL(FUNCT) extern RETT_##FUNCT STD_##FUNCT ( INTF_##FUNCT ) ;
#define STD_DECL_IWRAP(r, data, elem) STD_DECL(elem)

//BOOST_PP_SEQ_FOR_EACH(STD_DECL_IWRAP, placeholder, ALLOPS_WPAREN);

struct Fileops_p {
	char* name;
	int (*resolve) (char*);
	// add a pointer for each supported operation type
	#define ADD_FILEOP(op) RETT_##op (* op ) ( INTF_##op ) ;
	#define ADD_FILEOP_IWRAP(r, data, elem) ADD_FILEOP(elem)
	BOOST_PP_SEQ_FOR_EACH(ADD_FILEOP_IWRAP, placeholder, ALLOPS_WPAREN);
	BOOST_PP_SEQ_FOR_EACH(ADD_FILEOP_IWRAP, placeholder, SHM_WPAREN);	
	BOOST_PP_SEQ_FOR_EACH(ADD_FILEOP_IWRAP, placeholder, METAOPS);
};



// These functions are used to manage the standard directory of available functions.
// This directory lives in _hub_.
void _hub_add_fileop(struct Fileops_p* fo);
struct Fileops_p* _hub_find_fileop(const char* name);

void _hub_resolve_all_fileops(char* tree);

// method used by custom module resolvers which will extract module names of
// direct decendents.
struct Fileops_p** resolve_n_fileops(char* tree, char* name, int count);


// Used to declare and set up aliasing for the functions in a module.
// Can be called with ALLOPS_WPAREN or ALLOPS_FINITEPARAMS_WPAREN, for example.
#define DECLARE_AND_ALIAS_FUNCS(FUNCT, prefix) \
	RETT_##FUNCT prefix##FUNCT(INTF_##FUNCT); \
	RETT_##FUNCT ALIAS_##FUNCT(INTF_##FUNCT) WEAK_ALIAS(MK_STR(prefix##FUNCT));
#define DECLARE_AND_ALIAS_FUNCTS_IWRAP(r, data, elem) DECLARE_AND_ALIAS_FUNCS(elem, data)


// Same as above, but used to declare without aliasing for all the functions in a module.
// _hub_ is the only module which actually does external aliasing.
#define DECLARE_WITHOUT_ALIAS_FUNCS(FUNCT, prefix) \
	RETT_##FUNCT prefix##FUNCT(INTF_##FUNCT);
#define DECLARE_WITHOUT_ALIAS_FUNCTS_IWRAP(r, data, elem) DECLARE_WITHOUT_ALIAS_FUNCS(elem, data)


// Used to fill in the blanks for functions which aren't implemented in a module.
// The module's .c file should have a list of functions which aren't implemented
// which gets passed in.
#define WRAP_NOT_IMPLEMENTED(op, prefix) \
	RETT_##op prefix##op ( INTF_##op ) { \
		DEBUG("CALL: " MK_STR(prefix##op) " not implemented!\n"); \
		assert(0); \
	}
#define WRAP_NOT_IMPLEMENTED_IWRAP(r, data, elem) WRAP_NOT_IMPLEMENTED(elem, data) 

#define RESOLVE_TWO_FILEOPS(MODULENAME, OP1, OP2) \
	DEBUG("Resolving module "MODULENAME": wants two fileops.\n"); \
	struct Fileops_p** result = resolve_n_fileops(tree, MODULENAME, 2); \
	OP1 = result[0]; \
	OP2 = result[1]; \
	if(OP1 == NULL) { \
		ERROR("Failed to resolve "#OP1"\n"); \
		assert(0); \
	} else { \
		DEBUG(MODULENAME"("#OP1") resolved to %s\n", OP1->name); \
	} \
	if(OP2 == NULL) { \
		ERROR("Failed to resolve "#OP2"\n"); \
		assert(0); \
	} else { \
		DEBUG(MODULENAME"("#OP2") resolved to %s\n", OP2->name); \
	}

#endif

// breaking the build
