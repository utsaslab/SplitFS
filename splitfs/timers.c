#include "timers.h"

void nvp_print_io_stats(void)
{
	MSG("====================== NVP IO stats: ======================\n");
	MSG("open %u, close %u, async close %u\n", num_open, num_close, num_async_close);
	MSG("mmap %u, unlink %u, stat %u\n", num_mmap, num_unlink, num_stat);
	MSG("dr mmap %u, dr mmap critical path %u\n", num_drs, num_drs_critical_path);
	MSG("fsync %u, appendfsync: count %u size %llu average %llu\n",
	    num_fsync, num_appendfsync, appendfsync_size,
	    num_appendfsync ? appendfsync_size / num_appendfsync : 0);
	MSG("READ: count %u, size %llu, average %llu\n", num_read,
		read_size, num_read ? read_size / num_read : 0);
	MSG("WRITE: count %u, size %llu, average %llu\n", num_write,
		write_size, num_write ? write_size / num_write : 0);
	MSG("memcpy READ: count %u, size %llu, average %llu\n",
		num_memcpy_read, memcpy_read_size,
		num_memcpy_read ? memcpy_read_size / num_memcpy_read : 0);
	MSG("anon READ: count %u, size %llu, average %llu\n",
		num_anon_read, anon_read_size,
		num_anon_read ? anon_read_size / num_anon_read : 0);
	MSG("memcpy WRITE: count %u, size %llu, average %llu\n",
		num_memcpy_write, memcpy_write_size,
		num_memcpy_write ? memcpy_write_size / num_memcpy_write : 0);
	MSG("anon WRITE: count %u, size %llu, average %llu\n",
		num_append_write, append_write_size,
		num_append_write ? append_write_size / num_append_write : 0);
	MSG("posix READ: count %u, size %llu, average %llu\n",
		num_posix_read, posix_read_size,
		num_posix_read ? posix_read_size / num_posix_read : 0);
	MSG("posix WRITE: count %u, size %llu, average %llu\n",
		num_posix_write, posix_write_size,
		num_posix_write ? posix_write_size / num_posix_write : 0);
	MSG("write extends %lu, total %lu\n", _nvp_wr_extended,
		_nvp_wr_total);
	MSG("MFENCE: count %u\n",
	       num_mfence);
	MSG("CLFLUSHOPT: count %u\n",
	       num_clflushopt);
	MSG("NON_TEMPORAL_WRITES: count %u, size %llu, average %llu\n",
	       num_write_nontemporal, non_temporal_write_size,
	       num_write_nontemporal ? non_temporal_write_size / num_write_nontemporal : 0);
	MSG("TEMPORAL WRITES: count %u, size %llu, average %llu\n",
	       num_write_temporal, temporal_write_size,
	       num_write_temporal ? temporal_write_size / num_write_temporal : 0);
	MSG("TOTAL SYSCALLS (open + close + read + write + fsync): count %llu\n",
	       num_open + num_close + num_posix_read + num_posix_write);
}

const char *Instruprint[INSTRUMENT_NUM] =
{
	"open",
	"close",
	"pread",
	"pwrite",
	"read",
	"write",
	"seek",
	"fsync",
	"unlink",
	"bg_thread",
	"clf_lock",
	"node_lookup_lock",
	"nvnode_lock",
	"dr_mem_queue",
	"file_mmap",
	"close_syscall",
	"copy_to_dr_pool",
	"copy_to_mmap_cache",
	"appends",
	"clear_dr",
	"swap_extents",
	"give_up_node",
	"get_mmap",
	"get_dr_mmap",
	"copy_overread",
	"copy_overwrite",
	"copy_appendread",
	"copy_appendwrite",
	"read_tbl_mmap",
	"insert_tbl_mmap",
	"clear_mmap_tbl",
	"append_log_entry",
	"op_log_entry",
	"append_log_reinit",
	"remove_overlapping_entry",
	"device",
	"soft_overhead",
};
