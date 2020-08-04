#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define FILE_PATH "/mnt/pmem_emul/close_on_exec"
#define NEW_PROCESS_PATH "cloexec_new"

int main() {
    int fd, ret;
    char fd_string[10];
    char *argv[] = {"", NULL};

    fd = open(FILE_PATH, O_CREAT | O_EXCL, 0666);
    assert(fd>=0);

    ret = fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
    assert(ret==0);

    ret = sprintf(fd_string, "%d", fd);
    fd_string[ret] = '\0';

    argv[0] = fd_string;

    ret = execv(NEW_PROCESS_PATH, argv);
    assert(ret!=-1);
}
