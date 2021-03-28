# Common to all lib*.so
COMMON_OBJ=nvp_printf.o

all: sofiles $(COMMON_OBJ)

include common.mk

sofiles: $(COMMON_OBJ) $(NVP_SOFILES)


#     lib*.so files      #

# Uncomment the above in the end.
libnvp.so: $(COMMON_OBJ) add_delay.o bg_clear_mmap.o dup.o execve.o file.o fsync.o handle_mmaps.o link.o log.o lru_cache.o mkdir.o mknod.o mmap_cache.o non_temporal.o read.o relink.o rename.o rmdir.o seek.o splitfs_posix.o stack.o staging.o tbl_mmaps.o thread_handle.o timers.o unlink.o utils.o write.o lfq.o
	$(CC) $(CFLAGS) -fpic -shared -Wl,-soname,$@ -o $@ $^ -ldl -funroll-loops -L. -lsyscall_intercept -lrt

memcpy.o: memcpy.c
	$(CC) -c $< -o $@ -O4 -march=core2 -m64 -fPIC


#  Boring stuff  #

clean:
	rm -f *.o *.i *.ci *.so xdd_result_*.txt logs/*.result iogen simplecat

.PRECIOUS: %.i
%.i : %.c
	$(CC) $(CFLAGS) -c -E $< -o $@

.PRECIOUS: %.ci
%.ci : %.i
	indent < $< | grep -v '^#' > $@

%.oi : %.ci
	$(CC) $(CFLAGS) -x c -c $< -o $@

%.o : %.c
	$(CC) $(CFLAGS) -I../include -c -Wno-unknown-pragmas $< -o $@

%.o : %.S
	$(CC) $(COPTIMIZATIONS) -fPIC -c $< -o $@

include make-hs.mk

