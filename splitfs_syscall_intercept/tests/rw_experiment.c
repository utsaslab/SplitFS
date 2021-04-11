#define _GNU_SOURCE 
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <asm/unistd.h>
#include <inttypes.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <assert.h>
#include <sys/syscall.h>

#define FSIZE (2ULL*1024*1024*1024)
#define IOSIZE (512*1024*1024)
#define BLKSIZE (4*1024)
#define CLSIZE (64)

char *addr;
struct timeval start,end;
int precomputed_rand[FSIZE/CLSIZE];


void precompute_rand(unsigned long datasize) {

	int i = 0;
	int num_blocks = IOSIZE / datasize;

	for (i = 0; i < num_blocks; i++)
		precomputed_rand[i] = rand() % (IOSIZE - datasize);
}


void performExperiment(int operation, uint32_t datasize, int seq, char *buf, int fd) {

	uint32_t num_blks;
	unsigned long long offset = 0;
	unsigned int r = 0;
    int write_size = 0;
	int i = 0, j = 0;
	int num_clines = 0;

	num_blks = IOSIZE / datasize;

    printf("%s: data size = %u, num_blocks = %d\n", __func__, datasize, num_blks);


	gettimeofday(&start, NULL);

	r = 0;
	offset = 0;
	i = 0;

    if(operation == 0) {
        for(i = 0; i < num_blks; i++)
        {
            if(pread64(fd, buf, datasize, offset) != datasize) {
                perror("read failed\n");
                printf("%s: offset = %llu\n", __func__, offset);
                exit(-1);
            }
            offset += datasize;
            r = precomputed_rand[i];

            if(seq == 0)
                offset = r;
        }
    } else if(operation == 1) {
        for(i = 0; i < num_blks; i++)
        {
            if(write_size = (pwrite64(fd, buf, datasize, offset)) != datasize) {
                perror("write failed\n");
                printf("%s: Returned value = %d. Offset = %llu\n", __func__, write_size, offset);
                exit(-1);
            }

            offset += datasize;
            r = precomputed_rand[i];

            if(seq == 0)
                offset = r;
        }

        fsync(fd);
    }

	gettimeofday(&end, NULL);
}

void parseArgs(char *argv[], int *operation, int *seq, uint32_t *datasize) {

  char datasizestr[100];
  char size_granularity;

  if ((strcmp(argv[1], "write") == 0)) {
	*operation = 1;
  }

  else if ((strcmp(argv[1], "read") == 0)) {
	*operation = 0;
  }

  if ((strcmp(argv[2], "seq") == 0)) {
    *seq = 1;
  }

  else if ((strcmp(argv[2], "rand") == 0)) {
    *seq = 0;
  }

  strcpy(datasizestr, argv[3]);

  *datasize = atoi(datasizestr);

  printf("The arguments passed are: operation = %d, seq = %d, rw_size = %u\n", *operation, *seq, *datasize);

}

/* Example usage: ./a.out read seq 4096 */

int main(int argc, char *argv[]) {

	int fd;
    time_t curtime;
	unsigned int i,t;
	char *buf;
	int seq;
	uint32_t datasize;
	int operation = 2;

	if(argc < 4) {
	  	perror("Usage: ./a.out <read/write> <seq/rand> <write_size>");
		exit(-1);
	}

	parseArgs(argv, &operation, &seq, &datasize);

	fd = open("/mnt/pmem_emul/test.txt", O_RDWR | O_CREAT, 0666);
	if(fd < 0) {
		perror("file not opened!\n");
		exit(-1);
	}

	buf = (char *)malloc(sizeof(char)*datasize);

	printf("################ STARTING HERE ##################\n");

	srand(5);
	precompute_rand(datasize);

	for(i = 0; i < sizeof(buf); i++)
		buf[i] = 'R';

	performExperiment(operation, datasize, seq, buf, fd);

	//pause();

	t =(end.tv_sec-start.tv_sec)*1000000 + (end.tv_usec-start.tv_usec);


    printf("\nTime for %dMB ops  = %u\n", IOSIZE/(1024*1024), t);

	close(fd);

	return 0;
}
