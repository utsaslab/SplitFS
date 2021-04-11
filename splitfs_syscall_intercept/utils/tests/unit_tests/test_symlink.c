#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>

#define TARGET_FILE_PATH "/mnt/pmem_emul/target"
#define SYM_FILE_PATH "/mnt/pmem_emul/sym"

int main() {
    int fd, ret;
    char buf[256];

    // Create the target file
    fd = open(TARGET_FILE_PATH, O_CREAT | O_RDWR, 0666);
    assert(fd >= 0);

    // Write something into the file
    ret = write(fd, "hello\n", 6);
    assert(ret == 6);

    // Close the file
    ret = close(fd);
    assert(ret == 0);

    // Create a symlink
    ret = symlink(TARGET_FILE_PATH, SYM_FILE_PATH);
    assert(ret == 0);

    // Delete the actual file first
    ret = unlink(TARGET_FILE_PATH);
    assert(ret == 0);

    // Delete the symlink next
    ret = unlink(SYM_FILE_PATH);
    assert(ret == 0);

    return 0;
}