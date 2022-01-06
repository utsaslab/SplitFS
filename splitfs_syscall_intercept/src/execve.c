#include <libsyscall_intercept_hook_point.h>
#include <nv_common.h>
#include <syscall.h>

#include "file.h"
#include "execve.h"
#include "stack.h"
#include "handle_mmaps.h"

RETT_SYSCALL_INTERCEPT _sfs_EXECVE(INTF_SYSCALL) {

	int exec_ledger_fd = -1, i = 0;
	unsigned long offset_in_map = 0;
	int pid = getpid();
	char exec_nvp_filename[BUF_SIZE];

	for (i = 0; i < 1024; i++) {
		if (_nvp_fd_lookup[i].offset != NULL)
			execve_fd_passing[i] = *(_nvp_fd_lookup[i].offset);
		else
			execve_fd_passing[i] = 0;
	}

	sprintf(exec_nvp_filename, "exec-ledger-%d", pid);
	exec_ledger_fd = shm_open(exec_nvp_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (exec_ledger_fd == -1) {
		printf("%s: %s\n", __func__, strerror(errno));
		assert(0);
	}

	int res = syscall_no_intercept(SYS_ftruncate, exec_ledger_fd, (10*1024*1024));
	if (res <= -1) {
		printf("%s: ftruncate failed. Err = %s\n", __func__, strerror(-res));
		assert(0);
	}

	char *shm_area = mmap(NULL, 10*1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, exec_ledger_fd, 0);
	if (shm_area == NULL) {
		printf("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	if (memcpy(shm_area + offset_in_map, _nvp_fd_lookup, 1024 * sizeof(struct NVFile)) == NULL) {
		printf("%s: memcpy of fd lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(struct NVFile));

	if (memcpy(shm_area + offset_in_map, execve_fd_passing, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of execve offset failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));


	if (memcpy(shm_area + offset_in_map, _nvp_node_lookup[0], 1024*sizeof(struct NVNode)) == NULL) {
		printf("%s: memcpy of node lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024*sizeof(struct NVNode));

	if (memcpy(shm_area + offset_in_map, _nvp_ino_lookup, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of ino lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));

	if (memcpy(shm_area + offset_in_map, _nvp_free_node_list[0], 1024*sizeof(struct StackNode)) == NULL) {
		printf("%s: memcpy of free node list failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	nvp_free_dr_mmaps();
	offset_in_map += (1024 * sizeof(struct StackNode));

	return RETT_PASS_KERN;
}

void _sfs_SHM_COPY() {

	int exec_ledger_fd = -1;
	int i,j;
	unsigned long offset_in_map = 0;
	int pid = getpid();
	char exec_nvp_filename[BUF_SIZE];

	sprintf(exec_nvp_filename, "exec-ledger-%d", pid);
	exec_ledger_fd = shm_open(exec_nvp_filename, O_RDONLY, 0666);

	if (exec_ledger_fd == -1) {
		printf("%s: shm_open failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	char *shm_area = mmap(NULL, 10*1024*1024, PROT_READ, MAP_SHARED, exec_ledger_fd, 0);
	if (shm_area == NULL) {
		printf("%s: mmap failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	if (memcpy(_nvp_fd_lookup, shm_area + offset_in_map, 1024 * sizeof(struct NVFile)) == NULL) {
		printf("%s: memcpy of fd lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(struct NVFile));

	if (memcpy(execve_fd_passing, shm_area + offset_in_map, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of offset passing failed. Err = %s\n", __func__, strerror(errno));
	}

	offset_in_map += (1024 * sizeof(int));

	for (i = 0; i < 1024; i++) {
		_nvp_fd_lookup[i].offset = (size_t*)calloc(1, sizeof(int));
		*(_nvp_fd_lookup[i].offset) = execve_fd_passing[i];
	}

	if (memcpy(_nvp_node_lookup[0], shm_area + offset_in_map, 1024*sizeof(struct NVNode)) == NULL) {
		printf("%s: memcpy of node lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	for (i = 0; i < 1024; i++) {
		_nvp_fd_lookup[i].node = NULL;
		_nvp_node_lookup[0][i].root_dirty_num = 0;
		_nvp_node_lookup[0][i].total_dirty_mmaps = 0;
		_nvp_node_lookup[0][i].isRootSet = 0;
		_nvp_node_lookup[0][i].height = 0;
		_nvp_node_lookup[0][i].root_dirty_num = 0;

		_nvp_node_lookup[0][i].root = _nvp_backup_roots[0][i].root;
		_nvp_node_lookup[0][i].merkle_root = _nvp_backup_roots[0][i].merkle_root;
	}

	offset_in_map += (1024*sizeof(struct NVNode));

	for (i = 0; i < 1024; i++) {
		if (_nvp_fd_lookup[i].fd != -1) {
			for (j = 0; j < 1024; j++) {
				if (_nvp_fd_lookup[i].serialno == _nvp_node_lookup[0][j].serialno) {
					_nvp_fd_lookup[i].node = &_nvp_node_lookup[0][j];
					break;
				}
			}
		}
	}

	if (memcpy(_nvp_ino_lookup, shm_area + offset_in_map, 1024 * sizeof(int)) == NULL) {
		printf("%s: memcpy of ino lookup failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	offset_in_map += (1024 * sizeof(int));

	if (memcpy(_nvp_free_node_list[0], shm_area + offset_in_map, 1024*sizeof(struct StackNode)) == NULL) {
		printf("%s: memcpy of free node list failed. Err = %s\n", __func__, strerror(errno));
		assert(0);
	}

	munmap(shm_area, 10*1024*1024);
	shm_unlink(exec_nvp_filename);
}