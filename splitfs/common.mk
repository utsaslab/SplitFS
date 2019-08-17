
CC = gcc -g
LD = g++
CXX=g++
MAKE = make

LD = g++
CXX = g++

SYSTEM_TYPE ?= SYSTEM_TYPE_BEE3 # SYSTEM_TYPE_BEE3 or SYSTEM_TYPE_XUPV5

#MONETA_LIB_BUILD=DEBUG
MONETA_LIB_BUILD=RELEASE
SDSSD_LIB_BUILD=RELEASE

#MONETA_LIB_VERSION=-sim
#MONETA_BUILD_TARGET=build-libsim
MONETA_LIB_VERSION=
MONETA_BUILD_TARGET=build-lib

export BEE3HOME=/root

LOG_OUT_DIR=$(BEE3HOME)/test/PosixNVM/logs

LIBNVP_DEBUG=0

# LEDGER VARS
LEDGER_DEBUG=0
MOVNTI=1
SYS_APPENDS=0
SYS_PASS_THROUGH=0
DELAYS=1
LEDGER_DR_BG_CLEAN=1
LEDGER_INSTRU=0

# WORKLOADS
LEDGER_YCSB=1
LEDGER_TPCC=0
LEDGER_REDIS=0
LEDGER_TAR=0
LEDGER_GIT=0
LEDGER_RSYNC=0

LEDGER_TRACE_FP=1

# GUARANTEES
LEDGER_DATAJ=1
LEDGER_POSIX=0

# END LEDGER VARS



LIBNVP_SPIN_ON_ERROR=0

USE_PTHREAD_LOCK=0
USE_SCHED_GETCPU=1

USE_SINGLE_LOCK=0

INTEGRITY_CHECK=0
MEASURE_TIMING=0

USE_BTREE=1
ENABLE_FSYNC_TO_BS=0
ENABLE_FSYNC_TO_CACHE=0
ENABLE_FALLOC=1

UNMAP_ON_CLOSE=0

#MONETA_LIB_DIR=$(BEE3HOME)/Tools/BEE3/library/src$(MONETA_LIB_VERSION)/build/$(MONETA_LIB_BUILD)
#SDSSD_LIB_DIR=${SDSSDHOME}/libs/sdssd/host/build/${SDSSD_LIB_BUILD}:${SDSSDHOME}/libs/io/host/build/${SDSSD_LIB_BUILD}
NVP_LIB_DIR=$(BEE3HOME)/test/PosixNVM

MONETA_MOUNT_DIR = /mnt/beecube
LOCAL_TEST_DIR = /tmp/memuram0/nvp
RAMDISK_TEST_DIR = /tmp/memuram0/

NVP_TEST_DIR = /tmp/memuram0

MY_LD_LIB_PATH=$(PWD):$(NVP_LIB_DIR):$(MONETA_LIB_DIR):$(SDSSD_LIB_DIR):$$LD_LIBRARY_PATH

LIBNVP_TREE_DIR=$(BEE3HOME)/test/PosixNVM/bin/

#LOAD_DIR_X=/x/HotStorage2011-NVP

#COPTIMIZATIONS = -m64
COPTIMIZATIONS = -O3 -m64
#COPTIMIZATIONS = -O3 -march=core2 -m64

#-march=core2 -minline-all-stringops -m64 -fprefetch-loop-arrays
#-mno-align-stringops
#-DTRACE_FP_CALLS=$(LEDGER_TRACE_FP)
CFLAGS = -DPRINT_DEBUG_FILE=$(LEDGER_DEBUG) -DDATA_JOURNALING_ENABLED=$(LEDGER_DATAJ) -DPOSIX_ENABLED=$(LEDGER_POSIX) -DTRACE_FP_CALLS=$(LEDGER_TRACE_FP) -DNVM_DELAY=$(DELAYS) -DNON_TEMPORAL_WRITES=$(MOVNTI) -DSYSCALL_APPENDS=$(SYS_APPENDS) -DPASS_THROUGH_CALLS=$(SYS_PASS_THROUGH) -DBG_CLEANING=$(LEDGER_DR_BG_CLEAN) -DINSTRUMENT_CALLS=$(LEDGER_INSTRU) -DWORKLOAD_YCSB=$(LEDGER_YCSB) -DWORKLOAD_TPCC=$(LEDGER_TPCC) -DWORKLOAD_REDIS=$(LEDGER_REDIS) -DWORKLOAD_TAR=$(LEDGER_TAR) -DWORKLOAD_GIT=$(LEDGER_GIT) -DWORKLOAD_RSYNC=$(LEDGER_RSYNC) -DSHOW_DEBUG=$(LIBNVP_DEBUG) -DSPIN_ON_ERROR=$(LIBNVP_SPIN_ON_ERROR) -Wno-unused-variable -Wall -Wundef -pthread -fPIC $(COPTIMIZATIONS) -D$(SYSTEM_TYPE) -DUSE_PTHREAD_LOCK=$(USE_PTHREAD_LOCK) -DUSE_SCHED_GETCPU=$(USE_SCHED_GETCPU) -DINTEGRITY_CHECK=$(INTEGRITY_CHECK) -DMEASURE_TIMING=$(MEASURE_TIMING) -DUSE_SINGLE_LOCK=$(USE_SINGLE_LOCK) -DENABLE_FSYNC_TO_BS=$(ENABLE_FSYNC_TO_BS) -DENABLE_FSYNC_TO_CACHE=$(ENABLE_FSYNC_TO_CACHE) -DENABLE_FALLOC=$(ENABLE_FALLOC) -DUSE_BTREE=$(USE_BTREE) -DUNMAP_ON_CLOSE=$(UNMAP_ON_CLOSE)

CXXFLAGS=$(CFLAGS)

MARKERRORS = sed -e "s/\(ERROR:\)/$$(tput bold;tput setaf 1)\1$$(tput sgr0)/g" | sed -e "s/\(WARNING:\)/$$(tput bold;tput setaf 3)\1$$(tput sgr0)/g"

HIGHLIGHTERRORS = sed -e "s/\(total errors:\)/$$(tput bold;tput setaf 1)\1$$(tput sgr0)/gI" | sed -e "s/\(error:\)/$$(tput bold;tput setaf 1)\1$$(tput sgr0)/gI"

HIGHLIGHTFAILURE = sed -e "s/\(FAILURE\)/$$(tput bold;tput setaf 1)\1$$(tput sgr0)/g" | sed -e "s/\(SUCCESS\)/$$(tput bold;tput setaf 2)\1$$(tput sgr0)/g" | sed -e "s/\(Assertion\)/$$(tput bold;tput setaf 1)FAILURE$$(tput sgr0): \1/g" 

SWAPSUCCESSFAILURE = sed -e "s/\(FAILURE\)/$$(tput bold;tput setaf 1)SECRETTEMPWORD1823$$(tput sgr0)/g" | sed -e "s/\(SUCCESS\)/$$(tput bold;tput setaf 2)FAILURE$$(tput sgr0)/g" | sed -e "s/\(SECRETTEMPWORD1823\)/$$(tput bold;tput setaf 2)SUCCESS$$(tput sgr0)/g" 

SPECIALCASEFORTESTTESTER = sed -e "s/\(test_tester_fail.testexe: RESULT: FAILURE\)/TEMPORARY/gI" | sed -e "s/\(test_tester_fail.testexe: RESULT: SUCCESS\)/test_tester_fail.testexe: RESULT: FAILURE/gI" | sed -e "s/\(TEMPORARY\)/test_tester_fail.testexe: RESULT: SUCCESS/gI" | sed -e "s/\(test_tester_fail.testexe: RESULT: FAILURE\)/\1 : DON'T TRUST THE REST OF THE TEST CASES!/gI" | sed -e "s/\(test_tester_success.testexe: RESULT: FAILURE\)/\1 : DON'T TRUST THE REST OF THE TEST CASES!/gI" | sed -e "s/\(DON'T TRUST THE REST OF THE TEST CASES!\)/$$(tput bold;tput setaf 1)\1$$(tput sgr0)/g"

MARKINCOMPLETE = sed -e "s/\(result\)/\1: FAILURE: terminated prematurely/gI"

MARKNOLOAD = sed -e "s/\(If you're reading this, the library is being loaded!\)/\1: FAILURE: did not load libnvp.so/gI"

#TESTS = test_09.result
TESTS = test_tester_fail.result test_tester_success.result helloworld.result nvmfileops_test.result test_open.result test_multiplefiles.result test_simultaneousfd.result test_largefilecopy.result test_resizefiles.result test_zipper.result test_rand_zipper.result test_process_zipper.result test_thread.result test_invalid_seek.result test_rollingfd.result test_holes.result test_holes_trunc.result test_open_simultaneous.result test_open_perms.result test_holes_simultaneous.result nvmfileops_test_links.result test_odirect.result test_read_extended.result test_mkstemp.result test_stdout.result test_01.result test_02.result test_03.result test_04.result test_05.result test_07.result test_08.result test_09.result randomTest.result

#SOFILES = fileops_hub.so nvmfileops.so fileops_compareharness.so wrapops.so fileops_filter.so moneta.so fileops_sem.so fileops_death.so
#NVP_SOFILES = libnvp.so fileops_nvm.so fileops_compareharness.so fileops_wrap.so fileops_filter.so fileops_sem.so fileops_death.so
NVP_SOFILES = libnvp.so libfileops_nvp.so # libfileops_wrap.so libfileops_filter.so libfileops_sem.so libfileops_count.so libfileops_harness.so libfileops_perfcount.so libfileops_hackmmap.so libfileops_death.so #libfileops_bankshot2.so
#MONETA_SOFILES = libfileops_moneta.so libmoneta.so
#SDSSD_SOFILES = libfileops_sdssd.so
#BANKSHOT_SOFILES = libfileops_sdssdbs.so

MONETA_DEV_PATH=/dev/bbd0

BDB_EXEC_DIR=$$BEE3HOME/Workloads/BDB/Multi


check_moneta_mounted:
	if [ "`stat $(MONETA_MOUNT_DIR) | grep 'Device: fb00h' | wc -l`" == "0" ]; then echo "FAILURE: $(MONETA_MOUNT_DIR) is NOT a Moneta device!" | $(HIGHLIGHTFAILURE) ; exit 1; else echo "SUCCESS: $(MONETA_MOUNT_DIR) is on a Moneta device." | $(HIGHLIGHTFAILURE); fi

check_moneta:
	if [ "`stat . | grep 'Device: fb00h' | wc -l`" == "0" ]; then echo "FAILURE: Current directory is NOT on a Moneta device!" | $(HIGHLIGHTFAILURE) ; exit 1; else echo "SUCCESS: Current directory is on a Moneta device." | $(HIGHLIGHTFAILURE); fi

