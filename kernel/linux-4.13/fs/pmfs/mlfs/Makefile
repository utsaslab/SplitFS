obj-m += nvmm_perfmodel.o

CFLAGS_nvmm_perfmodel.o = -mhard-float -Og

all:
	make -C /lib/modules/$(shell uname -r)/build M=`pwd`
clean:
	make -C /lib/modules/$(shell uname -r)/build M=`pwd` clean
