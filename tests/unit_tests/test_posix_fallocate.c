#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#define FILE_PATH "/mnt/pmem_emul/posix_fallocate"
// 4MB
#define FILE_SIZE 1024 * 1024 * 4

void call_fn(int type) {
    int fd;
    int res;
    struct stat sbuf;

    fd = open(FILE_PATH, O_CREAT | O_RDWR | O_EXCL, 0666);
    assert(fd>=0);

    if(type == 0) {
        res = posix_fallocate(fd, 0, FILE_SIZE);
    } else {
        res = posix_fallocate64(fd, 0, FILE_SIZE);
    }
    assert(res==0);

    res = fstat(fd, &sbuf);
    assert(res==0);
    assert(sbuf.st_size==FILE_SIZE);

    res = unlink(FILE_PATH);
    assert(res==0);
}

int main() {
    call_fn(0);
    call_fn(1);
}