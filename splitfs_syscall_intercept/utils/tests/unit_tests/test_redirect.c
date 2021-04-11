/**
 * Context: This was developed to reproduce a bug in pjd tests that echoed something and redirected to a file.
 * A simpler version of the problem is: bash -c "echo text > file"
 * This program further simplifies the above command to debug the crux of the problem.
 * 
 * 
 * In this test we try to replicate the dup calls w.r.t the following command: bash -c "echo text > file"
 * It includes the following e.g dup calls: 
 * (i) fcntl(1, F_DUPFD, 10) 
 * (ii) dup2(3, 1) 
 * (iii) dup2(10, 1)
*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#define ORIG_FILE "/mnt/pmem_emul/redirect"

int main() {
    int fd, backup, ret;

    // Create a file
    fd = open(ORIG_FILE, O_RDWR | O_CREAT | O_EXCL, 0666);
    assert(fd >= 0);

    // Backup stdout
    backup = dup2(1, 10);
    assert(backup == 10);

    // Dup the redirect file into stdout
    ret = dup2(fd, 1);
    assert(ret == 1);

    // Close the original redirect fd
    close(fd);

    // Write something onto stdout (which is now pointing to redirect)
    ret = write(1, "hello\n", 6);
    assert(ret == 6);

    // Restore stdout from backup
    ret = dup2(backup, 1);
    assert(ret == 1);

    ret = close(backup);
    assert(ret == 0);

    return 0;
}
