#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>

int main()
{
	int fd = 0;
	off_t trunc_size = 20971520;
	int ret = 0;

	fd = open("/mnt/pmem_emul/ftrunc_file", O_WRONLY|O_CREAT, 0666);
	if (fd < 0) {
		printf("open failed. Err = %s\n", strerror(errno));
	}

	ret = ftruncate(fd, trunc_size);
	if (ret != 0) {
		printf("ftruncate failed. Err = %s\n", strerror(errno));
	}

	return ret;
}
