# Run tests for splitfs

# The current working directory
CWD=$(shell pwd)
# Root of the repo
ROOT=$(CWD)/..
# SplitFS path
SFS_PATH=/mnt/pmem_emul/
# PJD Dir
PJD_DIR=$(CWD)/pjd-fstest-20080816

all: pjd.posix pjd.sync pjd.strict

# Runs all the tests for SplitFS integrated with syscall_intercept
all_sysint: pjd.posix_sysint pjd.sync_sysint pjd.strict_sysint

# Compile with posix specific flags
pjd.posix_compile: 
	export LEDGER_DATAJ=0 && \
    export LEDGER_POSIX=1 && \
    $(MAKE) -C ../splitfs clean && \
    $(MAKE) -e -C ../splitfs

pjd.posix_compile_sysint:
	export LEDGER_DATAJ=0 && \
    export LEDGER_POSIX=1 && \
    $(MAKE) -C ../splitfs_syscall_intercept/src clean && \
    $(MAKE) -e -C ../splitfs_syscall_intercept/src

# Compile with sync specific flags
pjd.sync_compile:
	export LEDGER_DATAJ=0 && \
    export LEDGER_POSIX=0 && \
    $(MAKE) -C ../splitfs clean && \
    $(MAKE) -e -C ../splitfs

pjd.sync_compile_sysint:
	export LEDGER_DATAJ=0 && \
    export LEDGER_POSIX=0 && \
    $(MAKE) -C ../splitfs_syscall_intercept/src clean && \
    $(MAKE) -e -C ../splitfs_syscall_intercept/src

# Compile with strict specific flags
pjd.strict_compile:
	export LEDGER_DATAJ=1 && \
    export LEDGER_POSIX=0 && \
    $(MAKE) -C ../splitfs clean && \
    $(MAKE) -e -C ../splitfs

pjd.strict_compile_sysint:
	export LEDGER_DATAJ=1 && \
    export LEDGER_POSIX=0 && \
    $(MAKE) -C ../splitfs_syscall_intercept/src clean && \
    $(MAKE) -e -C ../splitfs_syscall_intercept/src

pjd.compile:
	$(MAKE) -C $(PJD_DIR) clean
	$(MAKE) -C $(PJD_DIR)

pjd.run:
	export LD_LIBRARY_PATH=$(ROOT)/splitfs; \
		export NVP_TREE_FILE=$(ROOT)/splitfs/bin/nvp_nvp.tree; \
		export LD_PRELOAD=$(ROOT)/splitfs/libnvp.so; \
		cd $(SFS_PATH) && prove -r $(PJD_DIR)/tests

pjd.run_sysint:
		export LD_PRELOAD=$(ROOT)/splitfs_syscall_intercept/src/libnvp.so; \
		cd $(SFS_PATH) && prove -r $(PJD_DIR)/tests

pjd.posix: pjd.posix_compile pjd.compile pjd.run
pjd.posix_sysint: pjd.posix_compile_sysint pjd.compile pjd.run_sysint

pjd.sync: pjd.sync_compile pjd.compile pjd.run
pjd.sync_sysint: pjd.sync_compile_sysint pjd.compile pjd.run_sysint

pjd.strict: pjd.strict_compile pjd.compile pjd.run
pjd.strict_sysint: pjd.strict_compile_sysint pjd.compile pjd.run_sysint
