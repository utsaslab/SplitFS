#Makefile for nvmfileops

#include common.mk
# this include was moved after all: because it contains a target


# Common to all lib*.so
COMMON_OBJ=nvp_printf.o
NVP_LRU_CACHE=lru_cache.o
#NVP_STAGING_QUEUE=queue_impl.o
NVP_STACK=stack.o
NVP_LOG=log.o
NVP_NON_TEMPORAL=non_temporal.o
NVP_DELAY=perf_delay/add_delay.o
NVP_TIMER=timers.o
NVP_TBL_MMAP=tbl_mmaps.o
NVP_UTILS=utils.o
NVP_LFQUEUE=lfq.c
#NVP_STAGING_QUEUE=queue_impl.c

all: sofiles $(COMMON_OBJ)
#$(NVP_STACK) $(NVP_LOG) $(NVP_LRU_CACHE)

include common.mk

#sofiles: $(COMMON_OBJ) $(NVP_SOFILES) $(MONETA_SOFILES) $(SDSSD_SOFILES) $(BANKSHOT_SOFILES)
sofiles: $(COMMON_OBJ) $(NVP_SOFILES)
#$(NVP_STACK) $(NVP_LOG) $(NVP_LRU_CACHE) 


#     lib*.so files      #

# The only .so which doesn't follow the naming convention for its underlying file.
libnvp.so: fileops_hub.o $(COMMON_OBJ)
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $^ -ldl -funroll-loops -L. -lrt

# Lives in a different directory.
#libmoneta.so:
#	$(MAKE) -C $(BEE3HOME)/Tools/BEE3/library clean build

# Unlike other *.so, requires -pthread flag.
#libfileops_sem.so: fileops_sem.o $(COMMON_OBJ)
#	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $< $(COMMON_OBJ) -ldl -pthread

#libfileops_nvp.so: fileops_nvp.o $(COMMON_OBJ) copy_user_nocache.o
#	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $< $(COMMON_OBJ) copy_user_nocache.o -ldl 

libfileops_nvp.so: fileops_nvp.o $(COMMON_OBJ) $(NVP_LFQUEUE) $(NVP_UTILS) $(NVP_NON_TEMPORAL) $(NVP_DELAY) $(NVP_STACK) $(NVP_TBL_MMAP) $(NVP_LOG) $(NVP_LRU_CACHE) $(NVP_TIMER)
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $< $(COMMON_OBJ) $(NVP_LFQUEUE) $(NVP_UTILS) $(NVP_NON_TEMPORAL) $(NVP_DELAY) $(NVP_STACK) $(NVP_TBL_MMAP) $(NVP_LOG) $(NVP_LRU_CACHE) $(NVP_TIMER) -ldl -funroll-loops -L. -lrt

BANKSHOT2_OBJ=nvp_printf.o fileops_bankshot2_extent.o rbtree.o
libfileops_bankshot2.so: fileops_bankshot2.o $(BANKSHOT2_OBJ) 
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $< $(BANKSHOT2_OBJ) -ldl -funroll-loops

# Don't require special treatment
lib%.so: %.o $(COMMON_OBJ)
	$(CC) $(CFLAGS) -shared -Wl,-soname,$@ -o $@ $< $(COMMON_OBJ) -ldl

memcpy.o: memcpy.c
	$(CC) -c $< -o $@ -O4 -march=core2 -m64 -fPIC


#  Boring stuff  #

clean:
#	$(MAKE) -C test clean
#	- $(MAKE) -C $(MONETA_MOUNT_DIR)/test clean
#	- $(MAKE) -C $(LOCAL_TEST_DIR)/test clean
#	rm -rf /mnt/beecube/*
	rm -f *.o *.i *.ci *.so xdd_result_*.txt $(BDB_TESTS) $(OLTP_TESTS) logs/*.result iogen simplecat

.PRECIOUS: %.i
%.i : %.c
	$(CC) $(CFLAGS) -c -E $< -o $@

.PRECIOUS: %.ci
%.ci : %.i
	indent < $< | grep -v '^#' > $@

%.oi : %.ci
	$(CC) $(CFLAGS) -x c -c $< -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c -Wno-unknown-pragmas $< -o $@

%.o : %.S
	$(CC) $(COPTIMIZATIONS) -fPIC -c $< -o $@



##############################
#                            #
#            Tests           #
#                            #
##############################

# Move to the correct subfolder and perform test

test: clean $(NVP_SOFILES) setup_tmp_tests
	$(MAKE) -C$(LOCAL_TEST_DIR)/test test

.PRECIOUS: %.result
%.result: clean $(NVP_SOFILES) setup_tmp_tests
	$(MAKE) -C$(LOCAL_TEST_DIR)/test $@
	$(MAKE) -C$(LOCAL_TEST_DIR)/test do_process PROCFILE=$(LOG_OUT_DIR)/$@

simpletest: setup_tmp_tests
	export NVP_TREE_FILE="$(LIBNVP_TREE_DIR)nvp_compare_posix_posix.tree"; $(MAKE) test

supertest: clean $(NVP_SOFILES) setup_tmp_tests
	$(MAKE) -C$(LOCAL_TEST_DIR)/test supertest

test_xdd: setup_moneta_tests
	$(MAKE) -C$(MONETA_MOUNT_DIR)/test test_xdd

test_xdd_nvp: setup_nvp_tests
	$(MAKE) -C$(NVP_TEST_DIR)/test test_xdd_nvp

test_bdb: setup_moneta_tests
	$(MAKE) -C$(MONETA_MOUNT_DIR)/test test_bdb

test_bdb_nvp: setup_nvp_tests
	$(MAKE) -C$(NVP_TEST_DIR)/test test_bdb_nvp

test_moneta: setup_moneta_tests
	$(MAKE) -C$(MONETA_MOUNT_DIR)/test test_moneta

simpletest_moneta: setup_moneta_tests
	$(MAKE) -C$(MONETA_MOUNT_DIR)/test simpletest_moneta

supertest_moneta: setup_moneta_tests
	$(MAKE) -C$(MONETA_MOUNT_DIR)/test supertest_moneta

test_oltp: setup_moneta_tests
	$(MAKE) -C$(MONETA_MOUNT_DIR)/test test_oltp

test_oltp_nvp: setup_nvp_tests
	$(MAKE) -C$(NVP_TEST_DIR)/test test_oltp_nvp

setup_oltp: setup_moneta_tests
	$(MAKE) -C$(MONETA_MOUNT_DIR)/test setup_oltp

run_oltp: setup_moneta_tests
	$(MAKE) -C$(MONETA_MOUNT_DIR)/test run_oltp

close_oltp: setup_moneta_tests
	$(MAKE) -C$(MONETA_MOUNT_DIR)/test close_oltp

test_pg: setup_moneta_tests
	$(MAKE) -C $(MONETA_MOUNT_DIR)/test test_pg


setup_tmp_tests: clean $(NVP_SOFILES)
	@echo "Setting up test files in $(LOCAL_TEST_DIR)"
	mkdir -p $(LOCAL_TEST_DIR)/test
	rm -rf $(LOCAL_TEST_DIR)/test/*
	cp nv_common.h debug.h nvp_printf.* $(LOCAL_TEST_DIR)
	cp oltp-config.cnf /tmp/my.cnf
	cp -r test/* $(LOCAL_TEST_DIR)/test


setup_moneta_tests: check_moneta_mounted clean $(NVP_SOFILES) $(MONETA_SOFILES)
	@echo "Setting up test files in $(MONETA_MOUNT_DIR)"
	mkdir -p $(MONETA_MOUNT_DIR)/test
	rm -rf $(MONETA_MOUNT_DIR)/test/*
	cp nv_common.h debug.h nvp_printf.* $(MONETA_MOUNT_DIR)
	cp oltp-config.cnf /tmp/my.cnf
	cp -r test/* $(MONETA_MOUNT_DIR)/test

setup_nvp_tests: clean $(NVP_SOFILES)
	@echo "Setting up test files in $(NVP_TEST_DIR)"
	mkdir -p $(NVP_TEST_DIR)/test
	rm -rf $(NVP_TEST_DIR)/test/*
	cp nv_common.h debug.h nvp_printf.o $(NVP_TEST_DIR)
	cp oltp-config.cnf /tmp/my.cnf
	cp -r test/* $(NVP_TEST_DIR)/test

iogen: clean $(NVP_SOFILES)
	$(CC) nvp_io_generator.c $(CFLAGS) -o iogen
	#/usr/bin/time ./iogen /tmp/memuram0/iotest.txt

iogen_nvp: clean $(NVP_SOFILES)
	$(CC) nvp_io_generator.c $(CFLAGS) -o iogen
	#export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE=$(LIBNVP_TREE_DIR)nvp_nvp.tree; /usr/bin/time ./iogen /tmp/memuram0/iotest.txt

PROFILE_LIBS = libnvp.so libfileops_nvp.so

iogen_profile: iogen
	rm -f sprof.outlog
	$(foreach lib, $(PROFILE_LIBS), make iogen_profile_lib PROF_LIB=$(lib); )

iogen_profile_lib:
	export LD_PROFILE=$(PROF_LIB); rm -f $$LD_PROFILE.profile; export LD_LIBRARY_PATH=$(MY_LD_LIB_PATH); export LD_PRELOAD="libnvp.so"; export NVP_TREE_FILE=$(LIBNVP_TREE_DIR)nvp_nvp.tree; ./iogen /tmp/memuram0/iotest.txt; unset LD_PRELOAD; sprof ./$$LD_PROFILE $$LD_PROFILE.profile -p >> sprof.outlog

fill_ramdisk: iogen
	./iogen /tmp/memuram0/filler.txt
	rm /tmp/memuram0/filler.txt

ramdiskbreaker:
	gcc ramdiskbreaker.c -O2 -fPIC -pthread -o rdb.exe
	./rdb.exe

include make-hs.mk

