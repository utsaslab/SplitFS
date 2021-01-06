/*
Test this by compiling this and running it with LD_DEBUG=bindings environment variable and look for the 
bindings of the executable to 'pread' and 'pread64'. Make sure they are binding to libnvp.so
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>

#define FILE_PATH "/mnt/pmem_emul/test_pread"

int main() {
    int ret;
    int fd;
    char buf[100];

    fd = open(FILE_PATH, O_CREAT | O_RDWR | O_EXCL);
    assert(fd>=0);

    ret = write(fd, "ABCDEFG", 7);
    assert(ret==7);

    // do the pread
    ret = pread(fd, buf, 5, 0);
    assert(ret==5);
    assert(!strcmp("ABCDE", buf));
    buf[5] = '\0';
    printf("Result from pread is %s\n", buf);

    // do pread64
    ret = pread64(fd, buf, 5, 1);
    assert(ret==5);
    assert(!strcmp("BCDEF", buf));
    printf("Result from pread64 is %s\n", buf);

    ret = close(fd);
    assert(ret != -1);

    ret = unlink(FILE_PATH);
    assert(ret==0);
}