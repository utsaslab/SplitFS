# The current working directory
CWD=$(shell pwd)
# Root of the repo
ROOT=$(CWD)/../..

all: test_close_on_exec_open test_close_on_exec_fcntl test_fread_unlocked test_pread test_file_sync_range test_fallocate test_posix_fallocate

test_close_on_exec_open: test_close_on_exec_new_process.c test_open_cloexec_old_process.c
	@gcc -o cloexec_new test_close_on_exec_new_process.c
	@gcc test_open_cloexec_old_process.c
	@$(MAKE) run_default
	@rm cloexec_new

test_close_on_exec_fcntl: test_close_on_exec_new_process.c test_fcntl_cloexec_old_process.c
	@gcc -o cloexec_new test_close_on_exec_new_process.c
	@gcc test_fcntl_cloexec_old_process.c
	@$(MAKE) run_default
	@rm cloexec_new

test_fread_unlocked: test_fread_unlocked.c
	@gcc test_fread_unlocked.c
	@$(MAKE) run_default

test_pread: test_pread.c
	@gcc test_pread.c -Wno-implicit-function-declaration
	@$(MAKE) run_default

test_file_sync_range: test_file_sync_range.c
	@gcc test_file_sync_range.c
	@$(MAKE) run_default

test_fallocate: test_fallocate.c
	@gcc test_fallocate.c
	@$(MAKE) run_default

test_posix_fallocate: test_posix_fallocate.c
	@gcc test_posix_fallocate.c -Wno-implicit-function-declaration
	@$(MAKE) run_default

run_default:
	@export LD_LIBRARY_PATH=$(ROOT)/splitfs; \
		export NVP_TREE_FILE=$(ROOT)/splitfs/bin/nvp_nvp.tree; \
		export LD_PRELOAD=$(ROOT)/splitfs/libnvp.so; \
		./a.out
	@rm ./a.out
