#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define FILE_PATH "/mnt/pmem_emul/execv"
#define NEW_PROCESS_PATH "execv_new"

int main() {
    int fd, ret;
    char fd_string[10];
    char *argv[] = {"", NULL};

    fd = open(FILE_PATH, O_RDWR | O_CREAT | O_EXCL, 0666);
    assert(fd>=0);

    ret = write(fd, "xxhello", 7);
    assert(ret==7);

    ret = lseek(fd, 2, SEEK_SET);
    assert(ret==2);

    ret = sprintf(fd_string, "%d", fd);
    fd_string[ret] = '\0';

    argv[0] = fd_string;

    ret = execv(NEW_PROCESS_PATH, argv);
    assert(ret!=-1);
}