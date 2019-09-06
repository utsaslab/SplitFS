/*
 * FILE NAME include/linux/pmfs_fs.h
 *
 * BRIEF DESCRIPTION
 *
 * Definitions for the PMFS filesystem.
 *
 * Copyright 2012-2013 Intel Corporation
 * Copyright 2009-2011 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright 2003 Sony Corporation
 * Copyright 2003 Matsushita Electric Industrial Co., Ltd.
 * 2003-2004 (c) MontaVista Software, Inc. , Steve Longerbeam
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef _LINUX_PMFS_DEF_H
#define _LINUX_PMFS_DEF_H

#include <linux/types.h>
#include <linux/magic.h>

/* Rohan start */
/*
#include <linux/kobject.h>
#include <linux/cpufreq.h>
#include <asm/fpu/api.h>
*/
//#include "nvmm_perfmodel.c"
/* Rohan end */


#define	PMFS_SUPER_MAGIC	0xEFFC

/*
 * The PMFS filesystem constants/structures
 */

/*
 * Mount flags
 */
#define PMFS_MOUNT_PROTECT 0x000001            /* wprotect CR0.WP */
#define PMFS_MOUNT_XATTR_USER 0x000002         /* Extended user attributes */
#define PMFS_MOUNT_POSIX_ACL 0x000004          /* POSIX Access Control Lists */
#define PMFS_MOUNT_XIP 0x000008                /* Execute in place */
#define PMFS_MOUNT_ERRORS_CONT 0x000010        /* Continue on errors */
#define PMFS_MOUNT_ERRORS_RO 0x000020          /* Remount fs ro on errors */
#define PMFS_MOUNT_ERRORS_PANIC 0x000040       /* Panic on errors */
#define PMFS_MOUNT_HUGEMMAP 0x000080           /* Huge mappings with mmap */
#define PMFS_MOUNT_HUGEIOREMAP 0x000100        /* Huge mappings with ioremap */
#define PMFS_MOUNT_PROTECT_OLD 0x000200        /* wprotect PAGE RW Bit */
#define PMFS_MOUNT_FORMAT      0x000400        /* was FS formatted on mount? */
#define PMFS_MOUNT_MOUNTING    0x000800        /* FS currently being mounted */

/*
 * Maximal count of links to a file
 */
#define PMFS_LINK_MAX          32000

#define PMFS_DEF_BLOCK_SIZE_4K 4096

#define PMFS_INODE_SIZE 128    /* must be power of two */
#define PMFS_INODE_BITS   7

#define PMFS_NAME_LEN 255

/* Rohan start */

/*
#define _MAX_INT    0x7ffffff

#define ENABLE_PERF_MODEL
#define ENABLE_BANDWIDTH_MODEL

#define BANDWIDTH_MONITOR_NS 10000
#define SEC_TO_NS(x) (x * 1000000000UL)
*/

/* Rohan end */

/*
 * Structure of a directory entry in PMFS.
 */
struct pmfs_direntry {
	__le64	ino;                    /* inode no pointed to by this entry */
	__le16	de_len;                 /* length of this directory entry */
	u8	name_len;               /* length of the directory entry name */
	u8	file_type;              /* file type */
	char	name[PMFS_NAME_LEN];   /* File name */
};

#define PMFS_DIR_PAD            4
#define PMFS_DIR_ROUND          (PMFS_DIR_PAD - 1)
#define PMFS_DIR_REC_LEN(name_len)  (((name_len) + 12 + PMFS_DIR_ROUND) & \
				      ~PMFS_DIR_ROUND)

/* PMFS supported data blocks */
#define PMFS_BLOCK_TYPE_4K     0
#define PMFS_BLOCK_TYPE_2M     1
#define PMFS_BLOCK_TYPE_1G     2
#define PMFS_BLOCK_TYPE_MAX    3

#define META_BLK_SHIFT 9

/*
 * Play with this knob to change the default block type.
 * By changing the PMFS_DEFAULT_BLOCK_TYPE to 2M or 1G,
 * we should get pretty good coverage in testing.
 */
#define PMFS_DEFAULT_BLOCK_TYPE PMFS_BLOCK_TYPE_4K

/*
 * Structure of an inode in PMFS. Things to keep in mind when modifying it.
 * 1) Keep the inode size to within 96 bytes if possible. This is because
 *    a 64 byte log-entry can store 48 bytes of data and we would like
 *    to log an inode using only 2 log-entries
 * 2) root must be immediately after the qw containing height because we update
 *    root and height atomically using cmpxchg16b in pmfs_decrease_btree_height 
 * 3) i_size, i_ctime, and i_mtime must be in that order and i_size must be at
 *    16 byte aligned offset from the start of the inode. We use cmpxchg16b to
 *    update these three fields atomically.
 */
struct pmfs_inode {
	/* first 48 bytes */
	__le16	i_rsvd;         /* reserved. used to be checksum */
	u8	    height;         /* height of data b-tree; max 3 for now */
	u8	    i_blk_type;     /* data block size this inode uses */
	__le32	i_flags;            /* Inode flags */
	__le64	root;               /* btree root. must be below qw w/ height */
	__le64	i_size;             /* Size of data in bytes */
	__le32	i_ctime;            /* Inode modification time */
	__le32	i_mtime;            /* Inode b-tree Modification time */
	__le32	i_dtime;            /* Deletion Time */
	__le16	i_mode;             /* File mode */
	__le16	i_links_count;      /* Links count */
	__le64	i_blocks;           /* Blocks count */

	/* second 48 bytes */
	__le64	i_xattr;            /* Extended attribute block */
	__le32	i_uid;              /* Owner Uid */
	__le32	i_gid;              /* Group Id */
	__le32	i_generation;       /* File version (for NFS) */
	__le32	i_atime;            /* Access time */

	struct {
		__le32 rdev;    /* major/minor # */
	} dev;              /* device inode */
	__le32 padding;     /* pad to ensure truncate_item starts 8-byte aligned */
};

/* This is a per-inode structure and follows immediately after the 
 * struct pmfs_inode. It is used to implement the truncate linked list and is 
 * by pmfs_truncate_add(), pmfs_truncate_del(), and pmfs_recover_truncate_list()
 * functions to manage the truncate list */
struct pmfs_inode_truncate_item {
	__le64	i_truncatesize;     /* Size of truncated inode */
	__le64  i_next_truncate;    /* inode num of the next truncated inode */
};

/*
 * #define PMFS_NAME_LEN (PMFS_INODE_SIZE - offsetof(struct pmfs_inode,
 *         i_d.d_name) - 1)
 */

/* #define PMFS_SB_SIZE 128 */ /* must be power of two */
#define PMFS_SB_SIZE 512       /* must be power of two */

typedef struct pmfs_journal {
	__le64     base;
	__le32     size;
	__le32     head;
	/* the next three fields must be in the same order and together.
	 * tail and gen_id must fall in the same 8-byte quadword */
	__le32     tail;
	__le16     gen_id;   /* generation id of the log */
	__le16     pad;
	__le16     redo_logging;
} pmfs_journal_t;


/*
 * Structure of the super block in PMFS
 * The fields are partitioned into static and dynamic fields. The static fields
 * never change after file system creation. This was primarily done because
 * pmfs_get_block() returns NULL if the block offset is 0 (helps in catching
 * bugs). So if we modify any field using journaling (for consistency), we 
 * will have to modify s_sum which is at offset 0. So journaling code fails.
 * This (static+dynamic fields) is a temporary solution and can be avoided
 * once the file system becomes stable and pmfs_get_block() returns correct
 * pointers even for offset 0.
 */
struct pmfs_super_block {
	/* static fields. they never change after file system creation.
	 * checksum only validates up to s_start_dynamic field below */
	__le16		s_sum;              /* checksum of this sb */
	__le16		s_magic;            /* magic signature */
	__le32		s_blocksize;        /* blocksize in bytes */
	__le64		s_size;             /* total size of fs in bytes */
	char		s_volume_name[16];  /* volume name */
	/* points to the location of pmfs_journal_t */
	__le64          s_journal_offset;
	/* points to the location of struct pmfs_inode for the inode table */
	__le64          s_inode_table_offset;

	__le64       s_start_dynamic; 

	/* all the dynamic fields should go here */
	/* s_mtime and s_wtime should be together and their order should not be
	 * changed. we use an 8 byte write to update both of them atomically */
	__le32		s_mtime;            /* mount time */
	__le32		s_wtime;            /* write time */
	/* fields for fast mount support. Always keep them together */
	__le64		s_num_blocknode_allocated;
	__le64		s_num_free_blocks;
	__le32		s_inodes_count;
	__le32		s_free_inodes_count;
	__le32		s_inodes_used_count;
	__le32		s_free_inode_hint;
};

#define PMFS_SB_STATIC_SIZE(ps) ((u64)&ps->s_start_dynamic - (u64)ps)

/* the above fast mount fields take total 32 bytes in the super block */
#define PMFS_FAST_MOUNT_FIELD_SIZE  (36)

/* The root inode follows immediately after the redundant super block */
#define PMFS_ROOT_INO (PMFS_INODE_SIZE)
#define PMFS_BLOCKNODE_IN0 (PMFS_ROOT_INO + PMFS_INODE_SIZE)

/* INODE HINT  START at 3 */ 
#define PMFS_FREE_INODE_HINT_START      (3)

/* ======================= Write ordering ========================= */

#define CACHELINE_SIZE  (64)
#define CACHELINE_MASK  (~(CACHELINE_SIZE - 1))
#define CACHELINE_ALIGN(addr) (((addr)+CACHELINE_SIZE-1) & CACHELINE_MASK)

#define X86_FEATURE_PCOMMIT	( 9*32+22) /* PCOMMIT instruction */
#define X86_FEATURE_CLFLUSHOPT	( 9*32+23) /* CLFLUSHOPT instruction */
#define X86_FEATURE_CLWB	( 9*32+24) /* CLWB instruction */

static inline bool arch_has_pcommit(void)
{
	return static_cpu_has(X86_FEATURE_PCOMMIT);
}

static inline bool arch_has_clwb(void)
{
	return static_cpu_has(X86_FEATURE_CLWB);
}

static inline bool arch_has_clflushopt(void)
{
    return static_cpu_has(X86_FEATURE_CLFLUSHOPT);
}

extern int support_clwb;
extern int support_clflushopt;
extern int support_pcommit;

#define _mm_clflush(addr)\
	asm volatile("clflush %0" : "+m" (*(volatile char *)(addr)))
#define _mm_clflushopt(addr)\
	asm volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)(addr)))
#define _mm_clwb(addr)\
	asm volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)(addr)))
#define _mm_pcommit()\
	asm volatile(".byte 0x66, 0x0f, 0xae, 0xf8")

/* Provides ordering from all previous clflush too */
static inline void PERSISTENT_MARK(void)
{
	/* TODO: Fix me. */
}

static inline void PERSISTENT_BARRIER(void)
{
	asm volatile ("sfence\n" : : );
	if (support_pcommit) {
		/* Do nothing */
	}
}

/* Rohan NVM latency emulation */

void perfmodel_add_delay(int read, size_t size);
//#define NVM_LATENCY 100

/*
static uint64_t monitor_start = 0, monitor_end = 0, now = 0;
atomic64_t bandwidth_consumption = ATOMIC_INIT(0);

extern int nvm_perf_model;

spinlock_t dax_nvm_spinlock;

static uint32_t read_latency_ns = 150;
static uint32_t wbarrier_latency_ns = 0;
static uint32_t nvm_bandwidth = 8000;
static uint32_t dram_bandwidth = 63000;
static uint64_t cpu_freq_mhz = 3600;

#define NS2CYCLE(__ns) (((__ns) * cpu_freq_mhz) / 1000LLU)
#define CYCLE2NS(__cycles) (((__cycles) * 1000) / cpu_freq_mhz)

static inline unsigned long long asm_rdtsc(void)
{
        unsigned hi, lo;
        __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
        return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

static inline unsigned long long asm_rdtscp(void)
{
        unsigned hi, lo;
        __asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi)::"rcx");
        return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}
*/
/* kernel floating point
 * http://yarchive.net/comp/linux/kernel_fp.html
 *  set CFLAGs with -mhard-float
 *
 *  in code.
 *      kernel_fpu_begin();
 *      ...
 *      kernel_fpu_end();
 */
/*
static inline void emulate_latency_ns(long long ns)
{
    uint64_t cycles, start, stop;

    start = asm_rdtscp();
    cycles = NS2CYCLE(ns);

    do {
*/  
      /* RDTSC doesn't necessarily wait for previous instructions to complete
         * so a serializing instruction is usually used to ensure previous
         * instructions have completed. However, in our case this is a desirable
         * property since we want to overlap the latency we emulate with the
         * actual latency of the emulated instruction.
         */
/*
        stop = asm_rdtscp();
    } while (stop - start < cycles);
}

void perfmodel_add_delay(int read, size_t size)
{
#ifdef ENABLE_PERF_MODEL
    uint32_t extra_latency;
    uint32_t do_bandwidth_delay;

    now = asm_rdtscp();

    if (now >= monitor_end) {
                monitor_start = now;
                monitor_end = monitor_start + NS2CYCLE(BANDWIDTH_MONITOR_NS);
                atomic64_set(&bandwidth_consumption, 0);
        }

        atomic64_add(size, &bandwidth_consumption);

        if (atomic64_read(&bandwidth_consumption) >=
                ((nvm_bandwidth << 20) / (SEC_TO_NS(1UL) / BANDWIDTH_MONITOR_NS)))
        do_bandwidth_delay = 1;
    else
        do_bandwidth_delay = 0;
#endif

#ifdef ENABLE_PERF_MODEL
    if (read) {
        extra_latency = read_latency_ns;
    } else
        extra_latency = wbarrier_latency_ns;

    // bandwidth delay for both read and write.
    if (do_bandwidth_delay) {
        // Due to the writeback cache, write does not have latency
        // but it has bandwidth limit.
        // The following is emulated delay when bandwidth is full
        extra_latency += (int)size *
            (1 - (float)(((float) nvm_bandwidth)/1000) /
             (((float)dram_bandwidth)/1000)) / (((float)nvm_bandwidth)/1000);
        // Bandwidth is enough, so no write delay.
                spin_lock(&dax_nvm_spinlock);
                emulate_latency_ns(extra_latency);
                spin_unlock(&dax_nvm_spinlock);
    } else
                emulate_latency_ns(extra_latency);
#endif

    return;
}
*/

/* [sekwonlee] NVM latency emulation ************************/
/*
#define NVM_LATENCY     100
static unsigned long long nvmfs_cpu_freq_mhz = 3600;
typedef uint64_t hrtime_t;

#define NS2CYCLE(__ns) ((__ns) * nvmfs_cpu_freq_mhz / 1000LLU)

static inline unsigned long long asm_rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)lo)|(((unsigned long long)hi)<<32);
}

static inline void emulate_latency_ns(long long ns)
{
    hrtime_t cycles;
    hrtime_t start;
    hrtime_t stop;

    start = asm_rdtsc();
    cycles = NS2CYCLE(ns);

    do {
        stop = asm_rdtsc();
    } while (stop - start < cycles);
}
*/
/* [sekwonlee] *********************************************/

static inline void pmfs_flush_buffer(void *buf, uint32_t len, bool fence)
{
    uint32_t i;
    len = len + ((unsigned long)(buf) & (CACHELINE_SIZE - 1));
    if (support_clwb) {
        for (i = 0; i < len; i += CACHELINE_SIZE) {
            _mm_clwb(buf + i);
            //emulate_latency_ns(NVM_LATENCY);
        }
    } else if (support_clflushopt) {
        for (i = 0; i < len; i += CACHELINE_SIZE) {		
	    _mm_clflushopt(buf + i);
	    /*
	    printk(KERN_INFO "%s: Flushed addr = %p\n",
		   __func__, buf + i);
	    */
            //emulate_latency_ns(NVM_LATENCY);
        }
    } else {
        for (i = 0; i < len; i += CACHELINE_SIZE) {
            _mm_clflush(buf + i);
            //emulate_latency_ns(NVM_LATENCY);
        }
    }

    // Rohan adding write delay
#if CONFIG_LEDGER
    perfmodel_add_delay(0, len);
#endif

    /* Do a fence only if asked. We often don't need to do a fence
     * immediately after clflush because even if we get context switched
     * between clflush and subsequent fence, the context switch operation
     * provides implicit fence. */
    if (fence)
	    PERSISTENT_BARRIER();
}

#endif /* _LINUX_PMFS_DEF_H */
