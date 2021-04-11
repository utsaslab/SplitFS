#ifndef SPLITFS_EXECV
#define SPLITFS_EXECV

void get_shm_filename(char *filename);

// returns 0 on success, else failed.
int splitfs_restore_fd_if_exec(void);

#endif