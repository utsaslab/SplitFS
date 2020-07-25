# Run tests for splitfs

# The current working directory
CWD=$(shell pwd)
# Root of the repo
ROOT=$(CWD)/../..
# SplitFS path
SFS_PATH=/mnt/pmem_emul/

all: splitfs.posix splitfs.sync splitfs.strict

# Compile with posix specific flags
splitfs.posix_compile:
	export LEDGER_DATAJ=0 && \
    export LEDGER_POSIX=1 && \
	export LEDGER_GIT=1 && \
    $(MAKE) -C ${ROOT}/splitfs clean && \
    $(MAKE) -e -C ${ROOT}/splitfs

# Compile with sync specific flags
splitfs.sync_compile:
	export LEDGER_DATAJ=0 && \
    export LEDGER_POSIX=0 && \
	export LEDGER_GIT=1 && \
    $(MAKE) -C ${ROOT}/splitfs clean && \
    $(MAKE) -e -C ${ROOT}/splitfs

# Compile with strict specific flags
splitfs.strict_compile:
	export LEDGER_DATAJ=1 && \
    export LEDGER_POSIX=0 && \
	export LEDGER_GIT=1 && \
    $(MAKE) -C ${ROOT}/splitfs clean && \
    $(MAKE) -e -C ${ROOT}/splitfs

splitfs.posix: splitfs.posix_compile

splitfs.sync: splitfs.sync_compile

splitfs.strict: splitfs.strict_compile
