#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

#define FILE_PATH "/mnt/pmem_emul/test_write"

int main() {
    int fd, ret;
    char writeBuf[] = "Hello World!\n";

    fd = open(FILE_PATH, O_RDWR | O_CREAT, 0644);
    assert(fd>=0);

    ret = write(fd, writeBuf, strlen(writeBuf));
    assert(ret == strlen(writeBuf));

    // Cleanup
    ret = unlink(FILE_PATH);
    assert(ret == 0);

    return 0;
}