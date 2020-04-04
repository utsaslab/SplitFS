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

# Compile with posix specific flags
pjd.posix_compile: 
	export LEDGER_DATAJ=0 && \
    export LEDGER_POSIX=1 && \
    $(MAKE) -C ../splitfs clean && \
    $(MAKE) -e -C ../splitfs

# Compile with sync specific flags
pjd.sync_compile:
	export LEDGER_DATAJ=0 && \
    export LEDGER_POSIX=0 && \
    $(MAKE) -C ../splitfs clean && \
    $(MAKE) -e -C ../splitfs

# Compile with strict specific flags
pjd.strict_compile:
	export LEDGER_DATAJ=1 && \
    export LEDGER_POSIX=0 && \
    $(MAKE) -C ../splitfs clean && \
    $(MAKE) -e -C ../splitfs

pjd.compile:
	$(MAKE) -C $(PJD_DIR) clean
	$(MAKE) -C $(PJD_DIR)

pjd.run:
	export LD_LIBRARY_PATH=$(ROOT)/splitfs; \
		export NVP_TREE_FILE=$(ROOT)/splitfs/bin/nvp_nvp.tree; \
		export LD_PRELOAD=$(ROOT)/splitfs/libnvp.so; \
		cd $(SFS_PATH) && prove -r $(PJD_DIR)/tests

pjd.posix: pjd.posix_compile pjd.compile pjd.run

pjd.sync: pjd.sync_compile pjd.compile pjd.run

pjd.strict: pjd.strict_compile pjd.compile pjd.run
