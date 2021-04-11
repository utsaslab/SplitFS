#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define FILE_PATH "/mnt/pmem_emul/test_open"

int main() {
    int fd, ret;
    char fd_string[10];
    char *argv[] = {"", NULL};

    fd = open(FILE_PATH, O_CREAT | O_EXCL, 0666);
    assert(fd>=0);

    return 0;
}