#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <syscall.h>
#include <unistd.h>

#include "splitfs-posix.h"
#include "constants.h"
#include "vfd_table.h"
#include "file.h"
#include "inode.h"
#include "os_util.h"
#include "sys_util.h"
#include "hash_map.h"
#include "execv.h"
#include "mmap_pool.h"
#include "table_mmaps.h"

struct vfile_description_flat {
	int valid;
	struct splitfs_file sfs_file;
	struct splitfs_vinode vinode;
};

int restore_fds(void);


void get_shm_filename(char *filename) {
	int ret;

    ret = sprintf(filename, "exec-ledger-%d", os_getpid());
	filename[ret] = '\0';
}

/**
 * This will do the following:
 * 1. FSYNC all the files
 * 2. [TODO] Cleanup the staging files.
 * 3. [TODO] Close the files that have close_on_exec flag set.
 * 4. Create vfile_description_flat to store all the splitfs file data structures with index corresponding to fd
 * 5. Loop through the vfd table to see which fds are open and populate open files' data structures
 * 6. Open shared memory file, mmap and write the data
 */
int splitfs_execv(void) {
	struct vfile_description_flat vfs[OPEN_MAX];
	struct splitfs_file *fp;
	char exec_splitfs_filename[BUF_SIZE], buf[512];
	int exec_ledger_fd = -1;

	// Fsync all the files.
	for(int i=0; i<OPEN_MAX; i++) {
		fp = splitfs_execv_vfd_get(i);
		if(fp == NULL) {
			vfs[i].valid = 0;
			continue;
		}
		splitfs_fsync(i, fp);
	}
	
	// Copy over information from valid files.
	for(int i=0; i<OPEN_MAX; i++) {
		fp = splitfs_execv_vfd_get(i);
		if(fp == NULL) {
			vfs[i].valid = 0;
			continue;
		}
		splitfs_fsync(i, fp);
		vfs[i].valid = 1;
		vfs[i].sfs_file = *fp;
		vfs[i].vinode = *(fp->vinode);
	}

	get_shm_filename(exec_splitfs_filename);
	exec_ledger_fd = shm_open(exec_splitfs_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (exec_ledger_fd == -1) {
		buf[sprintf(buf, "Failed to open shared memory: %s", strerror(errno))] = '\0';
		FATAL(buf);
	}

	long res = syscall_no_intercept(SYS_ftruncate, exec_ledger_fd, (10*1024*1024));
	if (res == -1) {
		buf[sprintf(buf, "%s: ftruncate failed. Err = %s\n", __func__, strerror(errno))] = '\0';
		FATAL(buf);
	}

	char *shm_area = mmap(NULL, 10*1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, exec_ledger_fd, 0);
	if (shm_area == NULL) {
		buf[sprintf(buf, "%s: mmap failed. Err = %s\n", __func__, strerror(errno))] = '\0';
		FATAL(buf);
	}

	if (memcpy(shm_area, vfs, OPEN_MAX * sizeof(struct vfile_description_flat)) == NULL) {
		buf[sprintf(buf, "%s: memcpy of vfile_description_flat failed. Err = %s\n", __func__, strerror(errno))] = '\0';
		FATAL(buf);
	}

	return 0;
}


/**
 * This will do the following:
 * 1. Load the data structure vfile_description_flat from the shared memory file
 * 2. For all valid == 1 indices, do the following
 * 2a. Assign splitfs file using 'splitfs_file_assign'
 * 2b. Set the struct with corresponding values (flags, offset, serial no)
 * 2c. Get the vinode for the serial number using 'splitfs_vinode_assign'
 * 2d. Initialise the inode values like - serialno, file_mmaps, sync_length, length is_large_file etc - using functions wherever applicable.
 * 2e. Associate the splitfs_file structure with the vinode using spltifs_vfd_assign
 * 2f. Assign the splitfs_file structure to the appropriate index in vfd table using 'splitfs_vfd_assign'
 */
int restore_fds(void) {
	struct splitfs_file *fp;
	struct splitfs_vinode *vi;
	struct vfile_description_flat vfs[OPEN_MAX];
	char exec_splitfs_filename[BUF_SIZE], buf[512];
	char *shm_area;
	int exec_ledger_fd = -1;
	long ret;

	get_shm_filename(exec_splitfs_filename);
	exec_ledger_fd = shm_open(exec_splitfs_filename, O_RDONLY, 0666);
	if (exec_ledger_fd == -1) {
		buf[sprintf(buf, "Failed to open shared memory: %s", strerror(errno))] = '\0';
		FATAL(buf);
	}

	shm_area = mmap(NULL, 10*1024*1024, PROT_READ, MAP_SHARED, exec_ledger_fd, 0);
	if (shm_area == NULL) {
		buf[sprintf(buf, "%s: mmap failed. Err = %s\n", __func__, strerror(errno))] = '\0';
		FATAL(buf);
	}

	if (memcpy(vfs, shm_area, OPEN_MAX * sizeof(struct vfile_description_flat)) == NULL) {
		buf[sprintf(buf, "%s: memcpy of fd lookup failed. Err = %s\n", __func__, strerror(errno))] = '\0';
		FATAL(buf);
	}

	for(int i=0; i<OPEN_MAX; i++) {
		if(vfs[i].valid == 0) {
			continue;
		}

		// Initialise the splitfs_file structure
		fp = splitfs_file_assign();
		if (fp == NULL) {
			FATAL("Ran out of files\n");
		}
		fp->flags = vfs[i].sfs_file.flags;
		fp->offset = vfs[i].sfs_file.offset;
		fp->serialno = vfs[i].sfs_file.serialno;
		

		// Initialise the corresponding vinode
		vi = splitfs_vinode_assign(fp->serialno);
		if (vi == NULL) {
			FATAL("Ran out of inodes\n");
		}
		inode_set_ino(vi, inode_get_ino(&vfs[i].vinode));
		inode_set_uncommitted_size(vi, inode_get_uncommitted_size(&vfs[i].vinode));
		inode_set_sync_size(vi, inode_get_sync_size(&vfs[i].vinode));
		inode_set_large_file_status(vi, inode_get_large_file_status(&vfs[i].vinode));

		void *mmaps = splitfs_mmap_assign();
		if (mmaps == NULL) {
			vi->file_mmaps = calloc(1, PER_NODE_MMAPS * sizeof(uint64_t));
		}
		util_mutex_lock(&tbl_mmap_mutex);
        void *tbl_mmaps = hash_map_get(tbl_mmap_cache, vi->serialno);
        if (tbl_mmaps) {
            hash_map_remove(tbl_mmap_cache, vi->serialno, tbl_mmaps);
        }
        util_mutex_unlock(&tbl_mmap_mutex);

        vi->tbl_mmap = (struct table_mmap *)tbl_mmaps;

        if (vi->tbl_mmap == NULL) {
            vi->tbl_mmap = splitfs_alloc_tbl();
        }

		// Associate the inode with the file
		ASSERT(vi->tbl_mmap);
    	fp->vinode = vi;

		// Assign the splitfs_file structure to the appropriate index in vfd table
		ret = splitfs_vfd_assign(i, fp);
		if(ret < 0) {
			buf[sprintf(buf, "%s: Failed to assign vfd %d to the vfd_table\n", __func__, i)] = '\0';
			FATAL(buf);
		}
	}

	ret = munmap(shm_area, 10*1024*1024);
	ASSERT(ret==0);
	ret = shm_unlink(exec_splitfs_filename);
	ASSERT(ret==0);

	return 0;
}

int splitfs_restore_fd_if_exec(void) {
	int ret = 0;
	char execv_full_path[BUF_SIZE];
	char filename[BUF_SIZE];

	get_shm_filename(filename);

	execv_full_path[sprintf(execv_full_path, "/dev/shm/%s", filename)] = '\0';

	if (access(execv_full_path, F_OK ) != -1) {
		ret = restore_fds();
	}
	return ret;
}
