#include <sys/types.h>
#include <dirent.h>
#include <assert.h>
#include <stdio.h>

int main() {
    DIR *dir;

    dir = opendir("/home/om/wspace/splitfs-new/tests/pjd-fstest-20080816/tests/chflags");
    assert(dir != NULL);
}
