#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#define FILE_PATH "/mnt/pmem_emul/execv"

void cleanup() {
    int ret;

    ret = unlink(FILE_PATH);
    assert(ret==0);
}

int main(int argc, char **argv) {
    int ret, fd;
    char buf[256];
    assert(argc>=1);

    fd = atoi(argv[0]);

    ret = read(fd, buf, 5);
    assert(ret == 5);
    buf[ret] = '\0';

    ret = strncmp(buf, "hello", 5);
    assert(ret==0);

    printf("The string read in the new process is: %s\n", buf);

    cleanup();

    return 0;
}