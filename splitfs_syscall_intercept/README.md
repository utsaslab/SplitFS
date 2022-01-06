# SplitFS implementation using syscall intercept Library
[syscall_intercept](https://google.com) is a library that can be used to intercept the system calls. This works by rewriting the machine code in text area of the loaded program.  
The prior implementation intercepts the `libc` calls. 
This implementation does not make any changes to the logic of SplitFS itself.

###Currently supported applications
1. PJD Test Suite (Tests run successfully)

###How to use?
All paths (not starting with `/`) are relative to root of repository
1. `cd splitfs_syscall_intercept/src`
2. `make clean && make`
3. A file called `libnvp.so` will be created at `splitfs_syscall_intercept/src` -- this is the library file that needs to be used with `LD_PRELOAD`.
4. Run the application that you want using `LD_PRELOAD=splitfs_syscall_intercept/src/libnvp.so` \<application cmd\>

###How to run the PJD test suite?
All paths (not starting with `/`) are relative to root of repository
1. Set up the pmem file mount at `/mnt/pmem_emul`
2. Make sure the mount point has `write` and `execute` permissions so that all users can delete files. (This is required since some tests use `setuid` to switch to a different user. This user will then not have permission to delete the staging files during exit cleanup)
3. `cd tests`
4. `make all_sysint`
