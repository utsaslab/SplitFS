#include "pmfs.h"

const char *Timingstring[TIMING_NUM] = 
{
	"create",
	"unlink",
	"readdir",
	"xip_read",
	"xip_write",
	"xip_write_fast",
	"internal_write",
	"memcpy_read",
	"memcpy_write",
	"alloc_blocks",
	"new_trans",
	"add_logentry",
	"commit_trans",
	"mmap_fault",
	"fsync",
	"free_tree",
	"evict_inode",
	"recovery",
};

unsigned long long Timingstats[TIMING_NUM];
u64 Countstats[TIMING_NUM];

atomic64_t fsync_pages = ATOMIC_INIT(0);

void pmfs_print_IO_stats(void)
{
	printk("=========== PMFS I/O stats ===========\n");
	printk("Fsync %ld pages\n", atomic64_read(&fsync_pages));
}

void pmfs_print_timing_stats(void)
{
	int i;

	printk("======== PMFS kernel timing stats ========\n");
	for (i = 0; i < TIMING_NUM; i++) {
		if (measure_timing || Timingstats[i]) {
			printk("%s: count %llu, timing %llu, average %llu\n",
				Timingstring[i],
				Countstats[i],
				Timingstats[i],
				Countstats[i] ?
				Timingstats[i] / Countstats[i] : 0);
		} else {
			printk("%s: count %llu\n",
				Timingstring[i],
				Countstats[i]);
		}
	}

	pmfs_print_IO_stats();
}

void pmfs_clear_stats(void)
{
	int i;

	printk("======== Clear PMFS kernel timing stats ========\n");
	for (i = 0; i < TIMING_NUM; i++) {
		Countstats[i] = 0;
		Timingstats[i] = 0;
	}
}
