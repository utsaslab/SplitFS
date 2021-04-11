# Run tests for splitfs

# The current working directory
CWD=$(shell pwd)
# Root of the repo
ROOT=$(CWD)/..
# SplitFS mount path
SFS_MNT_PATH=/mnt/pmem_emul/
# PJD Dir
PJD_DIR=$(CWD)/pjd-fstest-20080816
# SplitFS library file path. Either set via env variable or defaults to assigned path
SPLITFS_LIB_PATH ?= ${ROOT}/build/src/libsplitfs.so

pjd:
	export LD_PRELOAD=${SPLITFS_LIB_PATH}; \
	cd ${SFS_MNT_PATH} && prove -r ${PJD_DIR}/tests

compile_fstest:
	make -C ${PJD_DIR} all