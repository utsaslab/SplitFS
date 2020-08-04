#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#define FILE_PATH "/mnt/pmem_emul/sync_file_range"
// 4MB
#define FILE_SIZE 1024 * 1024 * 4

int main() {
    int fd;
    int res;
    struct stat sbuf;

    fd = open(FILE_PATH, O_CREAT | O_RDWR | O_EXCL, 0666);
    assert(fd>=0);

    res = posix_fallocate(fd, 0, FILE_SIZE);
    assert(res==0);

    res = fstat(fd, &sbuf);
    assert(res==0);
    assert(sbuf.st_size==FILE_SIZE);

    // Good idea to log in SplitFS to check if it is being intercepted at all.
    // Or we could also use LD_DEBUG=bindings
    res = sync_file_range(fd, 0, 5, SYNC_FILE_RANGE_WRITE);
    assert(res==0);

    res = unlink(FILE_PATH);
    assert(res==0);
}