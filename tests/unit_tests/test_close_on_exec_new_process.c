#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#define FILE_PATH "/mnt/pmem_emul/close_on_exec"

void cleanup() {
    int ret;

    ret = unlink(FILE_PATH);
    assert(ret==0);
}

int main(int argc, char **argv) {
    int ret;
    char fd_path[256];
    char file_name[256];
    assert(argc>=1);

    ret = sprintf(fd_path, "/proc/%d/%s", getpid(), argv[0]);
    assert(ret>0);

    fd_path[ret] = '\0';

    printf("The fd path to readlink: %s\n", fd_path);
    
    ret = readlink(fd_path, file_name, 256);

    // It might be possible that there is someother file corresponding to the file descriptor in the old process.
    // In that case we check if the filename is the same as that in the parent i.e FILE_PATH macro.
    // If that is not the case then we should see ENOENT
    if(ret == -1) {
        assert(errno==ENOENT);
        printf("SUCCESS: No file corresponding to fd %s found!\n", argv[0]);
        cleanup();
        return 0;
    }
    file_name[ret] = '\0';

    printf("The filename of fd %s is %s\n", argv[0], file_name);

    assert(strcmp(file_name, FILE_PATH) != 0);
    cleanup();
}
