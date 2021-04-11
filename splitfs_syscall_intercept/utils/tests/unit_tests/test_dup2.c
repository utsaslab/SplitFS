#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#define ORIG_FILE "/mnt/pmem_emul/dup_orig"
#define REPLACE_FILE "/mnt/pmem_emul/dup_replace"

int main() {
    int fd, fd2, ret;

    // Create a file
    fd = open(ORIG_FILE, O_CREAT | O_EXCL, 0644);
    assert(fd >= 0);

    // Create another file
    fd2 = open(REPLACE_FILE, O_CREAT | O_EXCL, 0644);
    assert(fd2 >= 0);

    // Dup the first file into the file descriptor of the second file.
    ret = dup2(fd, fd2);
    assert(ret == fd2);

    // Cleanup
    ret = close(fd);
    assert(ret == 0);

    ret = close(fd2);
    assert(ret == 0);

    ret = unlink(ORIG_FILE);
    assert(ret == 0);

    ret = unlink(REPLACE_FILE);
    assert(ret == 0);

    return 0;
}
