/*
 * Persistent Memory File System
 * Copyright (c) 2012-2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef __PMFS_JOURNAL_H__
#define __PMFS_JOURNAL_H__
#include <linux/slab.h>

/* default pmfs journal size 4MB */
#define PMFS_DEFAULT_JOURNAL_SIZE  (4 << 20)
/* minimum pmfs journal size 64KB */
#define PMFS_MINIMUM_JOURNAL_SIZE  (1 << 16)

#define CACHELINE_SIZE  (64)
#define CLINE_SHIFT		(6)
#define CACHELINE_MASK  (~(CACHELINE_SIZE - 1))
#define CACHELINE_ALIGN(addr) (((addr)+CACHELINE_SIZE-1) & CACHELINE_MASK)

#define LOGENTRY_SIZE  CACHELINE_SIZE
#define LESIZE_SHIFT   CLINE_SHIFT

#define MAX_INODE_LENTRIES (2)
#define MAX_SB_LENTRIES (2)
/* 1 le for dir entry and 1 le for potentially allocating a new dir block */
#define MAX_DIRENTRY_LENTRIES   (2)
/* 2 le for adding or removing the inode from truncate list. used to log
 * potential changes to inode table's i_next_truncate and i_sum */
#define MAX_TRUNCATE_LENTRIES (2)
#define MAX_DATA_PER_LENTRY  48
/* blocksize * max_btree_height */
#define MAX_METABLOCK_LENTRIES \
	((PMFS_DEF_BLOCK_SIZE_4K * 3)/MAX_DATA_PER_LENTRY)

#define MAX_PTRS_PER_LENTRY (MAX_DATA_PER_LENTRY / sizeof(u64))

#define TRANS_RUNNING    1
#define TRANS_COMMITTED  2
#define TRANS_ABORTED    3

#define LE_DATA        0
#define LE_START       1
#define LE_COMMIT      2
#define LE_ABORT       4

#define MAX_GEN_ID  ((uint16_t)-1)

/* persistent data structure to describe a single log-entry */
/* every log entry is max CACHELINE_SIZE bytes in size */
typedef struct {
	__le64   addr_offset;
	__le32   transaction_id;
	__le16   gen_id;
	u8       type;  /* normal, commit, or abort */
	u8       size;
	char     data[48];
} pmfs_logentry_t;

/* volatile data structure to describe a transaction */
typedef struct pmfs_transaction {
	u32              transaction_id;
	u16              num_entries;
	u16              num_used;
	u16              gen_id;
	u16              status;
	pmfs_journal_t  *t_journal;
	pmfs_logentry_t *start_addr;
	struct pmfs_transaction *parent;
} pmfs_transaction_t;

extern pmfs_transaction_t *pmfs_alloc_transaction(void);
extern void pmfs_free_transaction(pmfs_transaction_t *trans);

extern int pmfs_journal_soft_init(struct super_block *sb);
extern int pmfs_journal_hard_init(struct super_block *sb,
		uint64_t base, uint32_t size);
extern int pmfs_journal_uninit(struct super_block *sb);
extern pmfs_transaction_t *pmfs_new_transaction(struct super_block *sb,
		int nclines);
extern pmfs_transaction_t *pmfs_current_transaction(void);
extern int pmfs_add_logentry(struct super_block *sb,
		pmfs_transaction_t *trans, void *addr, uint16_t size, u8 type);
extern int pmfs_commit_transaction(struct super_block *sb,
		pmfs_transaction_t *trans);
extern int pmfs_abort_transaction(struct super_block *sb,
			pmfs_transaction_t *trans);
extern int pmfs_recover_journal(struct super_block *sb);

#endif    /* __PMFS_JOURNAL_H__ */
