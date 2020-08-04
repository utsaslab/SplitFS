#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#define FILE_PATH "/mnt/pmem_emul/fread_unlocked"

int main() {
    int ret;
    char read_buf[256];
    FILE *fp;
    fp = fopen(FILE_PATH, "w+");
    assert(fp!=NULL);

    ret = fwrite("Hello", 1, 5, fp);
    assert(ret==5);

    ret = fseek(fp, SEEK_SET, SEEK_SET);
    assert(ret==0);

    ret = fread_unlocked(read_buf, 1, 5, fp);
    assert(ret==5);
    read_buf[ret] = '\0';

    printf("Read the following from the file %s\n", read_buf);

    fclose(fp);
    ret = unlink(FILE_PATH);
    assert(ret==0);
}