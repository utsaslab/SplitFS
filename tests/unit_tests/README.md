## How to Run
1. make

Successful run indicates successful test.

## How to check if SplitFS is intercepting the call
We might want to check if SplitFS is successfully intercepting the calls. For this we could examine the symbol table and its corresponding bindings to the shared object.  
This can be done by executing any of the test file with `LD_DEBUG=bindings` environment variable set, along with SplitFS shared object preloaded.

Example:
To test if `pread` is being intercepted.
1. `gcc test_pread.c`
2. `LD_DEBUG=bindings LD_PRELOAD=<lib_nvp_so_path> ./a.out |& grep pread`

On executing above commands you should see something like this, with your corresponding `libnvp.so` path.
>binding file ./a.out [0] to /home/user/wspace/SplitFS/splitfs/libnvp.so [0]: normal symbol \`pread' [GLIBC_2.2.5]  

