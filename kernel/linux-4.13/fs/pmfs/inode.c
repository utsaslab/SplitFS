/*
 * BRIEF DESCRIPTION
 *
 * Inode methods (allocate/free/read/write).
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

#include <linux/fs.h>
#include <linux/aio.h>
#include <linux/sched.h>
#include <linux/highuid.h>
#include <linux/module.h>
#include <linux/mpage.h>
#include <linux/backing-dev.h>
#include <linux/types.h>
#include <linux/ratelimit.h>
#include "pmfs.h"
#include "xip.h"

unsigned int blk_type_to_shift[PMFS_BLOCK_TYPE_MAX] = {12, 21, 30};
uint32_t blk_type_to_size[PMFS_BLOCK_TYPE_MAX] = {0x1000, 0x200000, 0x40000000};

/*
 * allocate a data block for inode and return it's absolute blocknr.
 * Zeroes out the block if zero set. Increments inode->i_blocks.
 */
static int pmfs_new_data_block(struct super_block *sb, struct pmfs_inode *pi,
		unsigned long *blocknr, int zero)
{
	unsigned int data_bits = blk_type_to_shift[pi->i_blk_type];

	int errval = pmfs_new_block(sb, blocknr, pi->i_blk_type, zero);

	if (!errval) {
		pmfs_memunlock_inode(sb, pi);
		le64_add_cpu(&pi->i_blocks,
			(1 << (data_bits - sb->s_blocksize_bits)));
		pmfs_memlock_inode(sb, pi);
	}

	return errval;
}

/*
 * find the offset to the block represented by the given inode's file
 * relative block number.
 */
u64 pmfs_find_data_block(struct inode *inode, unsigned long file_blocknr)
{
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);
	u32 blk_shift;
	unsigned long blk_offset, blocknr = file_blocknr;
	unsigned int data_bits = blk_type_to_shift[pi->i_blk_type];
	unsigned int meta_bits = META_BLK_SHIFT;
	u64 bp;

	/* convert the 4K blocks into the actual blocks the inode is using */
	blk_shift = data_bits - sb->s_blocksize_bits;
	blk_offset = file_blocknr & ((1 << blk_shift) - 1);
	blocknr = file_blocknr >> blk_shift;

	if (blocknr >= (1UL << (pi->height * meta_bits)))
		return 0;

	bp = __pmfs_find_data_block(sb, pi, blocknr);
	pmfs_dbg1("find_data_block %lx, %x %llx blk_p %p blk_shift %x"
		" blk_offset %lx\n", file_blocknr, pi->height, bp,
		pmfs_get_block(sb, bp), blk_shift, blk_offset);

	if (bp == 0)
		return 0;
	return bp + (blk_offset << sb->s_blocksize_bits);
}

/* recursive_find_region: recursively search the btree to find hole or data
 * in the specified range
 * Input:
 * block: points to the root of the b-tree
 * height: height of the btree
 * first_blocknr: first block in the specified range
 * last_blocknr: last_blocknr in the specified range
 * @data_found: indicates whether data blocks were found
 * @hole_found: indicates whether a hole was found
 * hole: whether we are looking for a hole or data
 */
static int recursive_find_region(struct super_block *sb, __le64 block,
	u32 height, unsigned long first_blocknr, unsigned long last_blocknr,
	int *data_found, int *hole_found, int hole)
{
	unsigned int meta_bits = META_BLK_SHIFT;
	__le64 *node;
	unsigned long first_blk, last_blk, node_bits, blocks = 0;
	unsigned int first_index, last_index, i;

	node_bits = (height - 1) * meta_bits;

	first_index = first_blocknr >> node_bits;
	last_index = last_blocknr >> node_bits;

	node = pmfs_get_block(sb, le64_to_cpu(block));

	for (i = first_index; i <= last_index; i++) {
		if (height == 1 || node[i] == 0) {
			if (node[i]) {
				*data_found = 1;
				if (!hole)
					goto done;
			} else {
				*hole_found = 1;
			}

			if (!*hole_found || !hole)
				blocks += (1UL << node_bits);
		} else {
			first_blk = (i == first_index) ?  (first_blocknr &
				((1 << node_bits) - 1)) : 0;

			last_blk = (i == last_index) ? (last_blocknr &
				((1 << node_bits) - 1)) : (1 << node_bits) - 1;

			blocks += recursive_find_region(sb, node[i], height - 1,
				first_blk, last_blk, data_found, hole_found,
				hole);
			if (!hole && *data_found)
				goto done;
			/* cond_resched(); */
		}
	}
done:
	return blocks;
}

/*
 * find the file offset for SEEK_DATA/SEEK_HOLE
 */
unsigned long pmfs_find_region(struct inode *inode, loff_t *offset, int hole)
{
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);
	unsigned int data_bits = blk_type_to_shift[pi->i_blk_type];
	unsigned long first_blocknr, last_blocknr;
	unsigned long blocks = 0, offset_in_block;
	int data_found = 0, hole_found = 0;

	if (*offset >= inode->i_size)
		return -ENXIO;

	if (!inode->i_blocks || !pi->root) {
		if (hole)
			return inode->i_size;
		else
			return -ENXIO;
	}

	offset_in_block = *offset & ((1UL << data_bits) - 1);

	if (pi->height == 0) {
		data_found = 1;
		goto out;
	}

	first_blocknr = *offset >> data_bits;
	last_blocknr = inode->i_size >> data_bits;

	pmfs_dbg_verbose("find_region offset %llx, first_blocknr %lx,"
		" last_blocknr %lx hole %d\n",
		  *offset, first_blocknr, last_blocknr, hole);

	blocks = recursive_find_region(inode->i_sb, pi->root, pi->height,
		first_blocknr, last_blocknr, &data_found, &hole_found, hole);

out:
	/* Searching data but only hole found till the end */
	if (!hole && !data_found && hole_found)
		return -ENXIO;

	if (data_found && !hole_found) {
		/* Searching data but we are already into them */
		if (hole)
			/* Searching hole but only data found, go to the end */
			*offset = inode->i_size;
		return 0;
	}

	/* Searching for hole, hole found and starting inside an hole */
	if (hole && hole_found && !blocks) {
		/* we found data after it */
		if (!data_found)
			/* last hole */
			*offset = inode->i_size;
		return 0;
	}

	if (offset_in_block) {
		blocks--;
		*offset += (blocks << data_bits) +
			   ((1 << data_bits) - offset_in_block);
	} else {
		*offset += blocks << data_bits;
	}

	return 0;
}

/* examine the meta-data block node up to the end_idx for any non-null
 * pointers. if found return false, else return true.
 * required to determine if a meta-data block contains no pointers and hence
 * can be freed.
 */
static inline bool is_empty_meta_block(__le64 *node, unsigned int start_idx,
	unsigned int end_idx)
{
	int i, last_idx = (1 << META_BLK_SHIFT) - 1;
	for (i = 0; i < start_idx; i++)
		if (unlikely(node[i]))
			return false;
	for (i = end_idx + 1; i <= last_idx; i++)
		if (unlikely(node[i]))
			return false;
	return true;
}

/* recursive_truncate_blocks: recursively deallocate a range of blocks from
 * first_blocknr to last_blocknr in the inode's btree.
 * Input:
 * block: points to the root of the b-tree where the blocks need to be allocated
 * height: height of the btree
 * first_blocknr: first block in the specified range
 * last_blocknr: last_blocknr in the specified range
 * end: last byte offset of the range
 */
static int recursive_truncate_blocks(struct super_block *sb, __le64 block,
	u32 height, u32 btype, unsigned long first_blocknr,
	unsigned long last_blocknr, bool *meta_empty)
{
	unsigned long blocknr, first_blk, last_blk;
	unsigned int node_bits, first_index, last_index, i;
	__le64 *node;
	unsigned int freed = 0, bzero;
	int start, end;
	bool mpty, all_range_freed = true;
	struct pmfs_sb_info *sbi = PMFS_SB(sb);

	node = pmfs_get_block(sb, le64_to_cpu(block));

	node_bits = (height - 1) * META_BLK_SHIFT;

	start = first_index = first_blocknr >> node_bits;
	end = last_index = last_blocknr >> node_bits;

	if (height == 1) {
		struct pmfs_blocknode *start_hint = NULL;
		mutex_lock(&sbi->s_lock);
		for (i = first_index; i <= last_index; i++) {
			if (unlikely(!node[i]))
				continue;
			/* Freeing the data block */
			blocknr = pmfs_get_blocknr(sb, le64_to_cpu(node[i]),
				    btype);
			__pmfs_free_block(sb, blocknr, btype, &start_hint);
			freed++;
		}
		mutex_unlock(&sbi->s_lock);
	} else {
		for (i = first_index; i <= last_index; i++) {
			if (unlikely(!node[i]))
				continue;
			first_blk = (i == first_index) ? (first_blocknr &
				((1 << node_bits) - 1)) : 0;

			last_blk = (i == last_index) ? (last_blocknr &
				((1 << node_bits) - 1)) : (1 << node_bits) - 1;

			freed += recursive_truncate_blocks(sb, node[i],
				height - 1, btype, first_blk, last_blk, &mpty);
			/* cond_resched(); */
			if (mpty) {
				/* Freeing the meta-data block */
				blocknr = pmfs_get_blocknr(sb, le64_to_cpu(
					    node[i]), PMFS_BLOCK_TYPE_4K);
				pmfs_free_block(sb, blocknr,PMFS_BLOCK_TYPE_4K);
			} else {
				if (i == first_index)
				    start++;
				else if (i == last_index)
				    end--;
				all_range_freed = false;
			}
		}
	}
	if (all_range_freed &&
		is_empty_meta_block(node, first_index, last_index)) {
		*meta_empty = true;
	} else {
		/* Zero-out the freed range if the meta-block in not empty */
		if (start <= end) {
			bzero = (end - start + 1) * sizeof(u64);
			pmfs_memunlock_block(sb, node);
			memset(&node[start], 0, bzero);
			pmfs_memlock_block(sb, node);
			pmfs_flush_buffer(&node[start], bzero, false);
		}
		*meta_empty = false;
	}
	return freed;
}

unsigned int pmfs_free_inode_subtree(struct super_block *sb,
		__le64 root, u32 height, u32 btype, unsigned long last_blocknr)
{
	unsigned long first_blocknr;
	unsigned int freed;
	bool mpty;
	timing_t free_time;

	if (!root)
		return 0;

	PMFS_START_TIMING(free_tree_t, free_time);
	if (height == 0) {
		first_blocknr = pmfs_get_blocknr(sb, le64_to_cpu(root),
			btype);
		pmfs_free_block(sb, first_blocknr, btype);
		freed = 1;
	} else {
		first_blocknr = 0;

		freed = recursive_truncate_blocks(sb, root, height, btype,
			first_blocknr, last_blocknr, &mpty);
		BUG_ON(!mpty);
		first_blocknr = pmfs_get_blocknr(sb, le64_to_cpu(root),
			PMFS_BLOCK_TYPE_4K);
		pmfs_free_block(sb, first_blocknr,PMFS_BLOCK_TYPE_4K);
	}
	PMFS_END_TIMING(free_tree_t, free_time);
	return freed;
}

static void pmfs_decrease_btree_height(struct super_block *sb,
	struct pmfs_inode *pi, unsigned long newsize, __le64 newroot)
{
	unsigned int height = pi->height, new_height = 0;
	unsigned long blocknr, last_blocknr;
	__le64 *root;
	char b[8];

	if (pi->i_blocks == 0 || newsize == 0) {
		/* root must be NULL */
		BUG_ON(newroot != 0);
		goto update_root_and_height;
	}

	last_blocknr = ((newsize + pmfs_inode_blk_size(pi) - 1) >>
		pmfs_inode_blk_shift(pi)) - 1;
	while (last_blocknr > 0) {
		last_blocknr = last_blocknr >> META_BLK_SHIFT;
		new_height++;
	}
	if (height == new_height)
		return;
	pmfs_dbg_verbose("reducing tree height %x->%x\n", height, new_height);
	while (height > new_height) {
		/* freeing the meta block */
		root = pmfs_get_block(sb, le64_to_cpu(newroot));
		blocknr = pmfs_get_blocknr(sb, le64_to_cpu(newroot),
			PMFS_BLOCK_TYPE_4K);
		newroot = root[0];
		pmfs_free_block(sb, blocknr, PMFS_BLOCK_TYPE_4K);
		height--;
	}
update_root_and_height:
	/* pi->height and pi->root need to be atomically updated. use
	 * cmpxchg16 here. The following is dependent on a specific layout of
	 * inode fields */
	*(u64 *)b = *(u64 *)pi;
	/* pi->height is at offset 2 from pi */
	b[2] = (u8)new_height;
	/* TODO: the following function assumes cmpxchg16b instruction writes
	 * 16 bytes atomically. Confirm if it is really true. */
	cmpxchg_double_local((u64 *)pi, &pi->root, *(u64 *)pi, pi->root,
		*(u64 *)b, newroot);
}

static unsigned long pmfs_inode_count_iblocks_recursive(struct super_block *sb,
		__le64 block, u32 height)
{
	__le64 *node;
	unsigned int i;
	unsigned long i_blocks = 0;

	if (height == 0)
		return 1;
	node = pmfs_get_block(sb, le64_to_cpu(block));
	for (i = 0; i < (1 << META_BLK_SHIFT); i++) {
		if (node[i] == 0)
			continue;
		i_blocks += pmfs_inode_count_iblocks_recursive(sb, node[i],
								height - 1);
	}
	return i_blocks;
}

static inline unsigned long pmfs_inode_count_iblocks (struct super_block *sb,
	struct pmfs_inode *pi, __le64 root)
{
	unsigned long iblocks;
	if (root == 0)
		return 0;
	iblocks = pmfs_inode_count_iblocks_recursive(sb, root, pi->height);
	return (iblocks << (pmfs_inode_blk_shift(pi) - sb->s_blocksize_bits));
}

/* Support for sparse files: even though pi->i_size may indicate a certain
 * last_blocknr, it may not be true for sparse files. Specifically, last_blocknr
 * can not be more than the maximum allowed by the inode's tree height.
 */
static inline unsigned long pmfs_sparse_last_blocknr(unsigned int height,
		unsigned long last_blocknr)
{
	if (last_blocknr >= (1UL << (height * META_BLK_SHIFT)))
		last_blocknr = (1UL << (height * META_BLK_SHIFT)) - 1;
	return last_blocknr;
}

/*
 * Free data blocks from inode in the range start <=> end
 */
static void __pmfs_truncate_blocks(struct inode *inode, loff_t start,
				    loff_t end)
{
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);
	unsigned long first_blocknr, last_blocknr;
	__le64 root;
	unsigned int freed = 0;
	unsigned int data_bits = blk_type_to_shift[pi->i_blk_type];
	unsigned int meta_bits = META_BLK_SHIFT;
	bool mpty;

	inode->i_mtime = inode->i_ctime = current_time(inode);

	if (!pi->root)
		goto end_truncate_blocks;

	pmfs_dbg_verbose("truncate: pi %p iblocks %llx %llx %llx %x %llx\n", pi,
			 pi->i_blocks, start, end, pi->height, pi->i_size);

	first_blocknr = (start + (1UL << data_bits) - 1) >> data_bits;

	if (pi->i_flags & cpu_to_le32(PMFS_EOFBLOCKS_FL)) {
		last_blocknr = (1UL << (pi->height * meta_bits)) - 1;
	} else {
		if (end == 0)
			goto end_truncate_blocks;
		last_blocknr = (end - 1) >> data_bits;
		last_blocknr = pmfs_sparse_last_blocknr(pi->height,
			last_blocknr);
	}

	if (first_blocknr > last_blocknr)
		goto end_truncate_blocks;
	root = pi->root;

	if (pi->height == 0) {
		first_blocknr = pmfs_get_blocknr(sb, le64_to_cpu(root),
			pi->i_blk_type);
		pmfs_free_block(sb, first_blocknr, pi->i_blk_type);
		root = 0;
		freed = 1;
	} else {
		freed = recursive_truncate_blocks(sb, root, pi->height,
			pi->i_blk_type, first_blocknr, last_blocknr, &mpty);
		if (mpty) {
			first_blocknr = pmfs_get_blocknr(sb, le64_to_cpu(root),
				PMFS_BLOCK_TYPE_4K);
			pmfs_free_block(sb, first_blocknr, PMFS_BLOCK_TYPE_4K);
			root = 0;
		}
	}
	/* if we are called during mount, a power/system failure had happened.
	 * Don't trust inode->i_blocks; recalculate it by rescanning the inode
	 */
	if (pmfs_is_mounting(sb))
		inode->i_blocks = pmfs_inode_count_iblocks(sb, pi, root);
	else
		inode->i_blocks -= (freed * (1 << (data_bits -
				sb->s_blocksize_bits)));

	pmfs_memunlock_inode(sb, pi);
	pi->i_blocks = cpu_to_le64(inode->i_blocks);
	pi->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	pmfs_decrease_btree_height(sb, pi, start, root);
	/* Check for the flag EOFBLOCKS is still valid after the set size */
	check_eof_blocks(sb, pi, inode->i_size);
	pmfs_memlock_inode(sb, pi);
	/* now flush the inode's first cacheline which was modified */
	pmfs_flush_buffer(pi, 1, false);
	return;
end_truncate_blocks:
	/* we still need to update ctime and mtime */
	pmfs_memunlock_inode(sb, pi);
	pi->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	pmfs_memlock_inode(sb, pi);
	pmfs_flush_buffer(pi, 1, false);
}


static int pmfs_increase_btree_height(struct super_block *sb,
		struct pmfs_inode *pi, u32 new_height)
{
	u32 height = pi->height;
	__le64 *root, prev_root = pi->root;
	unsigned long blocknr;
	int errval = 0;

	pmfs_dbg_verbose("increasing tree height %x:%x\n", height, new_height);
	while (height < new_height) {
		/* allocate the meta block */
		errval = pmfs_new_block(sb, &blocknr, PMFS_BLOCK_TYPE_4K, 1);
		if (errval) {
			pmfs_err(sb, "failed to increase btree height\n");
			break;
		}
		blocknr = pmfs_get_block_off(sb, blocknr, PMFS_BLOCK_TYPE_4K);
		root = pmfs_get_block(sb, blocknr);
		pmfs_memunlock_block(sb, root);
		root[0] = prev_root;
		pmfs_memlock_block(sb, root);
		pmfs_flush_buffer(root, sizeof(*root), false);
		prev_root = cpu_to_le64(blocknr);
		height++;
	}
	pmfs_memunlock_inode(sb, pi);
	pi->root = prev_root;
	pi->height = height;
	pmfs_memlock_inode(sb, pi);
	return errval;
}

/* recursive_alloc_blocks: recursively allocate a range of blocks from
 * first_blocknr to last_blocknr in the inode's btree.
 * Input:
 * block: points to the root of the b-tree where the blocks need to be allocated
 * height: height of the btree
 * first_blocknr: first block in the specified range
 * last_blocknr: last_blocknr in the specified range
 * zero: whether to zero-out the allocated block(s)
 */
static int recursive_alloc_blocks(pmfs_transaction_t *trans,
	struct super_block *sb, struct pmfs_inode *pi, __le64 block, u32 height,
	unsigned long first_blocknr, unsigned long last_blocknr, bool new_node,
	bool zero)
{
	int i, errval;
	unsigned int meta_bits = META_BLK_SHIFT, node_bits;
	__le64 *node;
	bool journal_saved = 0;
	unsigned long blocknr, first_blk, last_blk;
	unsigned int first_index, last_index;
	unsigned int flush_bytes;

	node = pmfs_get_block(sb, le64_to_cpu(block));

	node_bits = (height - 1) * meta_bits;

	first_index = first_blocknr >> node_bits;
	last_index = last_blocknr >> node_bits;

	for (i = first_index; i <= last_index; i++) {
		if (height == 1) {
			if (node[i] == 0) {
				errval = pmfs_new_data_block(sb, pi, &blocknr,
							zero);
				if (errval) {
					pmfs_dbg_verbose("alloc data blk failed"
						" %d\n", errval);
					/* For later recovery in truncate... */
					pmfs_memunlock_inode(sb, pi);
					pi->i_flags |= cpu_to_le32(
							PMFS_EOFBLOCKS_FL);
					pmfs_memlock_inode(sb, pi);
					return errval;
				}
				/* save the meta-data into the journal before
				 * modifying */
				if (new_node == 0 && journal_saved == 0) {
					int le_size = (last_index - i + 1) << 3;
					pmfs_add_logentry(sb, trans, &node[i],
						le_size, LE_DATA);
					journal_saved = 1;
				}
				pmfs_memunlock_block(sb, node);
				node[i] = cpu_to_le64(pmfs_get_block_off(sb,
						blocknr, pi->i_blk_type));
				pmfs_memlock_block(sb, node);
			}
		} else {
			if (node[i] == 0) {
				/* allocate the meta block */
				errval = pmfs_new_block(sb, &blocknr,
						PMFS_BLOCK_TYPE_4K, 1);
				if (errval) {
					pmfs_dbg_verbose("alloc meta blk"
						" failed\n");
					goto fail;
				}
				/* save the meta-data into the journal before
				 * modifying */
				if (new_node == 0 && journal_saved == 0) {
					int le_size = (last_index - i + 1) << 3;
					pmfs_add_logentry(sb, trans, &node[i],
						le_size, LE_DATA);
					journal_saved = 1;
				}
				pmfs_memunlock_block(sb, node);
				node[i] = cpu_to_le64(pmfs_get_block_off(sb,
					    blocknr, PMFS_BLOCK_TYPE_4K));
				pmfs_memlock_block(sb, node);
				new_node = 1;
			}

			first_blk = (i == first_index) ? (first_blocknr &
				((1 << node_bits) - 1)) : 0;

			last_blk = (i == last_index) ? (last_blocknr &
				((1 << node_bits) - 1)) : (1 << node_bits) - 1;

			errval = recursive_alloc_blocks(trans, sb, pi, node[i],
			height - 1, first_blk, last_blk, new_node, zero);
			if (errval < 0)
				goto fail;
		}
	}
	if (new_node || trans == NULL) {
		/* if the changes were not logged, flush the cachelines we may
	 	* have modified */
		flush_bytes = (last_index - first_index + 1) * sizeof(node[0]);
		pmfs_flush_buffer(&node[first_index], flush_bytes, false);
	}
	errval = 0;
fail:
	return errval;
}

int __pmfs_alloc_blocks(pmfs_transaction_t *trans, struct super_block *sb,
	struct pmfs_inode *pi, unsigned long file_blocknr, unsigned int num,
	bool zero)
{
	int errval;
	unsigned long max_blocks;
	unsigned int height;
	unsigned int data_bits = blk_type_to_shift[pi->i_blk_type];
	unsigned int blk_shift, meta_bits = META_BLK_SHIFT;
	unsigned long blocknr, first_blocknr, last_blocknr, total_blocks = 0;
	timing_t alloc_time;

	/* convert the 4K blocks into the actual blocks the inode is using */
	blk_shift = data_bits - sb->s_blocksize_bits;

	PMFS_START_TIMING(alloc_blocks_t, alloc_time);
	first_blocknr = file_blocknr >> blk_shift;
	last_blocknr = (file_blocknr + num - 1) >> blk_shift;

	pmfs_dbg_verbose("alloc_blocks height %d file_blocknr %lx num %x, "
		   "first blocknr 0x%lx, last_blocknr 0x%lx\n",
		   pi->height, file_blocknr, num, first_blocknr, last_blocknr);

	height = pi->height;

	blk_shift = height * meta_bits;

	max_blocks = 0x1UL << blk_shift;

	if (last_blocknr > max_blocks - 1) {
		/* B-tree height increases as a result of this allocation */
		total_blocks = last_blocknr >> blk_shift;
		while (total_blocks > 0) {
			total_blocks = total_blocks >> meta_bits;
			height++;
		}
		if (height > 3) {
			pmfs_dbg("[%s:%d] Max file size. Cant grow the file\n",
				__func__, __LINE__);
			errval = -ENOSPC;
			goto fail;
		}
	}

	if (!pi->root) {
		if (height == 0) {
			__le64 root;
			errval = pmfs_new_data_block(sb, pi, &blocknr, zero);
			if (errval) {
				pmfs_dbg_verbose("[%s:%d] failed: alloc data"
					" block\n", __func__, __LINE__);
				goto fail;
			}
			root = cpu_to_le64(pmfs_get_block_off(sb, blocknr,
					   pi->i_blk_type));
			pmfs_memunlock_inode(sb, pi);
			pi->root = root;
			pi->height = height;
			pmfs_memlock_inode(sb, pi);
		} else {
			errval = pmfs_increase_btree_height(sb, pi, height);
			if (errval) {
				pmfs_dbg_verbose("[%s:%d] failed: inc btree"
					" height\n", __func__, __LINE__);
				goto fail;
			}
			errval = recursive_alloc_blocks(trans, sb, pi, pi->root,
			pi->height, first_blocknr, last_blocknr, 1, zero);
			if (errval < 0)
				goto fail;
		}
	} else {
		/* Go forward only if the height of the tree is non-zero. */
		if (height == 0)
			return 0;

		if (height > pi->height) {
			errval = pmfs_increase_btree_height(sb, pi, height);
			if (errval) {
				pmfs_dbg_verbose("Err: inc height %x:%x tot %lx"
					"\n", pi->height, height, total_blocks);
				goto fail;
			}
		}
		errval = recursive_alloc_blocks(trans, sb, pi, pi->root, height,
				first_blocknr, last_blocknr, 0, zero);
		if (errval < 0)
			goto fail;
	}
	PMFS_END_TIMING(alloc_blocks_t, alloc_time);
	return 0;
fail:
	PMFS_END_TIMING(alloc_blocks_t, alloc_time);
	return errval;
}

/*
 * Allocate num data blocks for inode, starting at given file-relative
 * block number.
 */
inline int pmfs_alloc_blocks(pmfs_transaction_t *trans, struct inode *inode,
		unsigned long file_blocknr, unsigned int num, bool zero)
{
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);
	int errval;

	errval = __pmfs_alloc_blocks(trans, sb, pi, file_blocknr, num, zero);
	inode->i_blocks = le64_to_cpu(pi->i_blocks);

	return errval;
}

/* Initialize the inode table. The pmfs_inode struct corresponding to the
 * inode table has already been zero'd out */
int pmfs_init_inode_table(struct super_block *sb)
{
	struct pmfs_inode *pi = pmfs_get_inode_table(sb);
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	unsigned long num_blocks = 0, init_inode_table_size;
	int errval;

	if (sbi->num_inodes == 0) {
		/* initial inode table size was not specified. */
		if (sbi->initsize >= PMFS_LARGE_INODE_TABLE_THREASHOLD)
			init_inode_table_size = PMFS_LARGE_INODE_TABLE_SIZE;
		else
			init_inode_table_size = PMFS_DEF_BLOCK_SIZE_4K;
	} else {
		init_inode_table_size = sbi->num_inodes << PMFS_INODE_BITS;
	}

	pmfs_memunlock_inode(sb, pi);
	pi->i_mode = 0;
	pi->i_uid = 0;
	pi->i_gid = 0;
	pi->i_links_count = cpu_to_le16(1);
	pi->i_flags = 0;
	pi->height = 0;
	pi->i_dtime = 0;
	if (init_inode_table_size >= PMFS_LARGE_INODE_TABLE_SIZE)
		pi->i_blk_type = PMFS_BLOCK_TYPE_2M;
	else
		pi->i_blk_type = PMFS_BLOCK_TYPE_4K;

	num_blocks = (init_inode_table_size + pmfs_inode_blk_size(pi) - 1) >>
				pmfs_inode_blk_shift(pi);

	pi->i_size = cpu_to_le64(num_blocks << pmfs_inode_blk_shift(pi));
	/* pmfs_sync_inode(pi); */
	pmfs_memlock_inode(sb, pi);

	sbi->s_inodes_count = num_blocks <<
			(pmfs_inode_blk_shift(pi) - PMFS_INODE_BITS);
	/* calculate num_blocks in terms of 4k blocksize */
	num_blocks = num_blocks << (pmfs_inode_blk_shift(pi) -
					sb->s_blocksize_bits);
	errval = __pmfs_alloc_blocks(NULL, sb, pi, 0, num_blocks, true);

	if (errval != 0) {
		pmfs_err(sb, "Err: initializing the Inode Table: %d\n", errval);
		return errval;
	}

	/* inode 0 is considered invalid and hence never used */
	sbi->s_free_inodes_count =
		(sbi->s_inodes_count - PMFS_FREE_INODE_HINT_START);
	sbi->s_free_inode_hint = (PMFS_FREE_INODE_HINT_START);

	return 0;
}

static int pmfs_read_inode(struct inode *inode, struct pmfs_inode *pi)
{
	int ret = -EIO;

#if 0
	if (pmfs_calc_checksum((u8 *)pi, PMFS_INODE_SIZE)) {
		pmfs_err(inode->i_sb, "checksum error in inode %lx\n",
			  (u64)inode->i_ino);
		goto bad_inode;
	}
#endif

	inode->i_mode = le16_to_cpu(pi->i_mode);
	i_uid_write(inode, le32_to_cpu(pi->i_uid));
	i_gid_write(inode, le32_to_cpu(pi->i_gid));
	set_nlink(inode, le16_to_cpu(pi->i_links_count));
	inode->i_size = le64_to_cpu(pi->i_size);
	inode->i_atime.tv_sec = le32_to_cpu(pi->i_atime);
	inode->i_ctime.tv_sec = le32_to_cpu(pi->i_ctime);
	inode->i_mtime.tv_sec = le32_to_cpu(pi->i_mtime);
	inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec =
					 inode->i_ctime.tv_nsec = 0;
	inode->i_generation = le32_to_cpu(pi->i_generation);
	pmfs_set_inode_flags(inode, pi);

	/* check if the inode is active. */
	if (inode->i_nlink == 0 &&
	   (inode->i_mode == 0 || le32_to_cpu(pi->i_dtime))) {
		/* this inode is deleted */
		ret = -ESTALE;
		goto bad_inode;
	}

	inode->i_blocks = le64_to_cpu(pi->i_blocks);
	inode->i_mapping->a_ops = &pmfs_aops_xip;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &pmfs_file_inode_operations;
		inode->i_fop = &pmfs_xip_file_operations;
		break;
	case S_IFDIR:
		inode->i_op = &pmfs_dir_inode_operations;
		inode->i_fop = &pmfs_dir_operations;
		break;
	case S_IFLNK:
		inode->i_op = &pmfs_symlink_inode_operations;
		break;
	default:
		inode->i_size = 0;
		inode->i_op = &pmfs_special_inode_operations;
		init_special_inode(inode, inode->i_mode,
				   le32_to_cpu(pi->dev.rdev));
		break;
	}

	return 0;

bad_inode:
	make_bad_inode(inode);
	return ret;
}

static void pmfs_update_inode(struct inode *inode, struct pmfs_inode *pi)
{
	pmfs_memunlock_inode(inode->i_sb, pi);
	pi->i_mode = cpu_to_le16(inode->i_mode);
	pi->i_uid = cpu_to_le32(i_uid_read(inode));
	pi->i_gid = cpu_to_le32(i_gid_read(inode));
	pi->i_links_count = cpu_to_le16(inode->i_nlink);
	pi->i_size = cpu_to_le64(inode->i_size);
	pi->i_blocks = cpu_to_le64(inode->i_blocks);
	pi->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
	pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	pi->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	pi->i_generation = cpu_to_le32(inode->i_generation);
	pmfs_get_inode_flags(inode, pi);

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		pi->dev.rdev = cpu_to_le32(inode->i_rdev);

	pmfs_memlock_inode(inode->i_sb, pi);
}

/*
 * NOTE! When we get the inode, we're the only people
 * that have access to it, and as such there are no
 * race conditions we have to worry about. The inode
 * is not on the hash-lists, and it cannot be reached
 * through the filesystem because the directory entry
 * has been deleted earlier.
 */
static int pmfs_free_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct pmfs_inode *pi;
	unsigned long inode_nr;
	pmfs_transaction_t *trans;
	int err = 0;

	mutex_lock(&PMFS_SB(sb)->inode_table_mutex);

	pmfs_dbg_verbose("free_inode: %lx free_nodes %x tot nodes %x hint %x\n",
		   inode->i_ino, sbi->s_free_inodes_count, sbi->s_inodes_count,
		   sbi->s_free_inode_hint);
	inode_nr = inode->i_ino >> PMFS_INODE_BITS;

	pi = pmfs_get_inode(sb, inode->i_ino);

	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out;
	}

	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	pmfs_memunlock_inode(sb, pi);
	pi->root = 0;
	/* pi->i_links_count = 0;
	pi->i_xattr = 0; */
	pi->i_size = 0;
	pi->i_dtime = cpu_to_le32(get_seconds());
	pmfs_memlock_inode(sb, pi);

	pmfs_commit_transaction(sb, trans);

	/* increment s_free_inodes_count */
	if (inode_nr < (sbi->s_free_inode_hint))
		sbi->s_free_inode_hint = (inode_nr);

	sbi->s_free_inodes_count += 1;

	if ((sbi->s_free_inodes_count) ==
	    (sbi->s_inodes_count) - PMFS_FREE_INODE_HINT_START) {
		/* filesystem is empty */
		pmfs_dbg_verbose("fs is empty!\n");
		sbi->s_free_inode_hint = (PMFS_FREE_INODE_HINT_START);
	}

	pmfs_dbg_verbose("free_inode: free_nodes %x total_nodes %x hint %x\n",
		   sbi->s_free_inodes_count, sbi->s_inodes_count,
		   sbi->s_free_inode_hint);
out:
	mutex_unlock(&PMFS_SB(sb)->inode_table_mutex);
	return err;
}

struct inode *pmfs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
	struct pmfs_inode *pi;
	int err;

	inode = iget_locked(sb, ino);
	if (unlikely(!inode))
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	pi = pmfs_get_inode(sb, ino);
	if (!pi) {
		err = -EACCES;
		goto fail;
	}
	err = pmfs_read_inode(inode, pi);
	if (unlikely(err))
		goto fail;
	inode->i_ino = ino;

	unlock_new_inode(inode);
	return inode;
fail:
	iget_failed(inode);
	return ERR_PTR(err);
}

void pmfs_evict_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);
	__le64 root;
	unsigned long last_blocknr;
	unsigned int height, btype;
	int err = 0;
	timing_t evict_time;

	PMFS_START_TIMING(evict_inode_t, evict_time);
	if (!inode->i_nlink && !is_bad_inode(inode)) {
		if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
			S_ISLNK(inode->i_mode)))
			goto out;
		if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
			goto out;

		root = pi->root;
		height = pi->height;
		btype = pi->i_blk_type;

		if (pi->i_flags & cpu_to_le32(PMFS_EOFBLOCKS_FL)) {
			last_blocknr = (1UL << (pi->height * META_BLK_SHIFT))
			    - 1;
		} else {
			if (likely(inode->i_size))
				last_blocknr = (inode->i_size - 1) >>
					pmfs_inode_blk_shift(pi);
			else
				last_blocknr = 0;
			last_blocknr = pmfs_sparse_last_blocknr(pi->height,
				last_blocknr);
		}

		/* first free the inode */
		err = pmfs_free_inode(inode);
		if (err)
			goto out;
		pi = NULL; /* we no longer own the pmfs_inode */

		/* then free the blocks from the inode's b-tree */
		pmfs_free_inode_subtree(sb, root, height, btype, last_blocknr);
		inode->i_mtime = inode->i_ctime = current_time(inode);
		inode->i_size = 0;
	}
out:
	/* now it is safe to remove the inode from the truncate list */
	pmfs_truncate_del(inode);
	/* TODO: Since we don't use page-cache, do we really need the following
	 * call? */
	truncate_inode_pages(&inode->i_data, 0);

	clear_inode(inode);
	PMFS_END_TIMING(evict_inode_t, evict_time);
}

static int pmfs_increase_inode_table_size(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct pmfs_inode *pi = pmfs_get_inode_table(sb);
	pmfs_transaction_t *trans;
	int errval;

	/* 1 log entry for inode-table inode, 1 lentry for inode-table b-tree */
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	errval = __pmfs_alloc_blocks(trans, sb, pi,
			le64_to_cpup(&pi->i_size) >> sb->s_blocksize_bits,
			1, true);

	if (errval == 0) {
		u64 i_size = le64_to_cpu(pi->i_size);

		sbi->s_free_inode_hint = i_size >> PMFS_INODE_BITS;
		i_size += pmfs_inode_blk_size(pi);

		pmfs_memunlock_inode(sb, pi);
		pi->i_size = cpu_to_le64(i_size);
		pmfs_memlock_inode(sb, pi);

		sbi->s_free_inodes_count += INODES_PER_BLOCK(pi->i_blk_type);
		sbi->s_inodes_count = i_size >> PMFS_INODE_BITS;
	} else
		pmfs_dbg_verbose("no space left to inc inode table!\n");
	/* commit the transaction */
	pmfs_commit_transaction(sb, trans);
	return errval;
}

struct inode *pmfs_new_inode(pmfs_transaction_t *trans, struct inode *dir,
		umode_t mode, const struct qstr *qstr)
{
	struct super_block *sb;
	struct pmfs_sb_info *sbi;
	struct inode *inode;
	struct pmfs_inode *pi = NULL, *inode_table;
	struct pmfs_inode *diri = NULL;
	int i, errval;
	u32 num_inodes, inodes_per_block;
	ino_t ino = 0;

	sb = dir->i_sb;
	sbi = (struct pmfs_sb_info *)sb->s_fs_info;
	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode_init_owner(inode, dir, mode);
	inode->i_blocks = inode->i_size = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);

	inode->i_generation = atomic_add_return(1, &sbi->next_generation);

	inode_table = pmfs_get_inode_table(sb);

	pmfs_dbg_verbose("inode: %p free_inodes %x total_inodes %x hint %x\n",
		inode, sbi->s_free_inodes_count, sbi->s_inodes_count,
		sbi->s_free_inode_hint);

	diri = pmfs_get_inode(sb, dir->i_ino);
	if (!diri)
		return ERR_PTR(-EACCES);

	mutex_lock(&sbi->inode_table_mutex);

	/* find the oldest unused pmfs inode */
	i = (sbi->s_free_inode_hint);
	inodes_per_block = INODES_PER_BLOCK(inode_table->i_blk_type);
retry:
	num_inodes = (sbi->s_inodes_count);
	while (i < num_inodes) {
		u32 end_ino;
		end_ino = i + (inodes_per_block - (i & (inodes_per_block - 1)));
		ino = i <<  PMFS_INODE_BITS;
		pi = pmfs_get_inode(sb, ino);
		for (; i < end_ino; i++) {
			/* check if the inode is active. */
			if (le16_to_cpu(pi->i_links_count) == 0 &&
			(le16_to_cpu(pi->i_mode) == 0 ||
			 le32_to_cpu(pi->i_dtime)))
				/* this inode is free */
				break;
			pi = (struct pmfs_inode *)((void *)pi +
							PMFS_INODE_SIZE);
		}
		/* found a free inode */
		if (i < end_ino)
			break;
	}
	if (unlikely(i >= num_inodes)) {
		errval = pmfs_increase_inode_table_size(sb);
		if (errval == 0)
			goto retry;
		mutex_unlock(&PMFS_SB(sb)->inode_table_mutex);
		pmfs_dbg("PMFS: could not find a free inode\n");
		goto fail1;
	}

	ino = i << PMFS_INODE_BITS;
	pmfs_dbg_verbose("allocating inode %lx\n", ino);

	/* chosen inode is in ino */
	inode->i_ino = ino;
	pmfs_add_logentry(sb, trans, pi, sizeof(*pi), LE_DATA);

	pmfs_memunlock_inode(sb, pi);
	pi->i_blk_type = PMFS_DEFAULT_BLOCK_TYPE;
	pi->i_flags = pmfs_mask_flags(mode, diri->i_flags);
	pi->height = 0;
	pi->i_dtime = 0;
	pmfs_memlock_inode(sb, pi);

	sbi->s_free_inodes_count -= 1;

	if (i < (sbi->s_inodes_count) - 1)
		sbi->s_free_inode_hint = (i + 1);
	else
		sbi->s_free_inode_hint = (PMFS_FREE_INODE_HINT_START);

	mutex_unlock(&sbi->inode_table_mutex);

	pmfs_update_inode(inode, pi);

	pmfs_set_inode_flags(inode, pi);

	if (insert_inode_locked(inode) < 0) {
		pmfs_err(sb, "pmfs_new_inode failed ino %lx\n", inode->i_ino);
		errval = -EINVAL;
		goto fail1;
	}

	return inode;
fail1:
	make_bad_inode(inode);
	iput(inode);
	return ERR_PTR(errval);
}

inline void pmfs_update_nlink(struct inode *inode, struct pmfs_inode *pi)
{
	pmfs_memunlock_inode(inode->i_sb, pi);
	pi->i_links_count = cpu_to_le16(inode->i_nlink);
	pmfs_memlock_inode(inode->i_sb, pi);
}

inline void pmfs_update_isize(struct inode *inode, struct pmfs_inode *pi)
{
	pmfs_memunlock_inode(inode->i_sb, pi);
	pi->i_size = cpu_to_le64(inode->i_size);
	/*
	printk(KERN_INFO "%s: flushing addr = %p\n",
	       __func__, &pi->i_size);
	*/
	pmfs_flush_buffer(&pi->i_size, sizeof(pi->i_size), true);
	pmfs_memlock_inode(inode->i_sb, pi);
}

inline void pmfs_update_time(struct inode *inode, struct pmfs_inode *pi)
{
	pmfs_memunlock_inode(inode->i_sb, pi);
	pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	pi->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	pmfs_memlock_inode(inode->i_sb, pi);
}

/* This function checks if VFS's inode and PMFS's inode are not in sync */
static bool pmfs_is_inode_dirty(struct inode *inode, struct pmfs_inode *pi)
{
	if (inode->i_ctime.tv_sec != le32_to_cpu(pi->i_ctime) ||
		inode->i_mtime.tv_sec != le32_to_cpu(pi->i_mtime) ||
		inode->i_size != le64_to_cpu(pi->i_size) ||
		inode->i_mode != le16_to_cpu(pi->i_mode) ||
		i_uid_read(inode) != le32_to_cpu(pi->i_uid) ||
		i_gid_read(inode) != le32_to_cpu(pi->i_gid) ||
		inode->i_nlink != le16_to_cpu(pi->i_links_count) ||
		inode->i_blocks != le64_to_cpu(pi->i_blocks) ||
		inode->i_atime.tv_sec != le32_to_cpu(pi->i_atime))
		return true;
	return false;
}

int pmfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	/* write_inode should never be called because we always keep our inodes
	 * clean. So let us know if write_inode ever gets called. */
//	BUG();
	return 0;
}

/*
 * dirty_inode() is called from mark_inode_dirty_sync()
 * usually dirty_inode should not be called because PMFS always keeps its inodes
 * clean. Only exception is touch_atime which calls dirty_inode to update the
 * i_atime field.
 */
void pmfs_dirty_inode(struct inode *inode, int flags)
{
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);

	/* only i_atime should have changed if at all.
	 * we can do in-place atomic update */
	pmfs_memunlock_inode(sb, pi);
	pi->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
	pmfs_memlock_inode(sb, pi);
	pmfs_flush_buffer(&pi->i_atime, sizeof(pi->i_atime), true);

	/* FIXME: Is this check needed? */
	if (pmfs_is_inode_dirty(inode, pi))
		printk_ratelimited(KERN_ERR "pmfs: inode was dirty!\n");
}

/*
 * Called to zeros out a single block. It's used in the "resize"
 * to avoid to keep data in case the file grow up again.
 */
/* Make sure to zero out just a single 4K page in case of 2M or 1G blocks */
static void pmfs_block_truncate_page(struct inode *inode, loff_t newsize)
{
	struct super_block *sb = inode->i_sb;
	unsigned long offset = newsize & (sb->s_blocksize - 1);
	unsigned long blocknr, length;
	u64 blockoff;
	char *bp;

	/* Block boundary or extending ? */
	if (!offset || newsize > inode->i_size)
		return;

	length = sb->s_blocksize - offset;
	blocknr = newsize >> sb->s_blocksize_bits;

	blockoff = pmfs_find_data_block(inode, blocknr);

	/* Hole ? */
	if (!blockoff)
		return;

	bp = pmfs_get_block(sb, blockoff);
	if (!bp)
		return;
	pmfs_memunlock_block(sb, bp);
	memset(bp + offset, 0, length);
	pmfs_memlock_block(sb, bp);
	pmfs_flush_buffer(bp + offset, length, false);
}

void pmfs_truncate_del(struct inode *inode)
{
	struct list_head *prev;
	struct pmfs_inode_info *si = PMFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct pmfs_inode_truncate_item *head = pmfs_get_truncate_list_head(sb);
	struct pmfs_inode_truncate_item *li;
	unsigned long ino_next;

	mutex_lock(&sbi->s_truncate_lock);
	if (list_empty(&si->i_truncated))
		goto out;
	/* Make sure all truncate operation is persistent before removing the
	 * inode from the truncate list */
	PERSISTENT_MARK();

	li = pmfs_get_truncate_item(sb, inode->i_ino);

	ino_next = le64_to_cpu(li->i_next_truncate);
	prev = si->i_truncated.prev;

	list_del_init(&si->i_truncated);
	PERSISTENT_BARRIER();

	/* Atomically delete the inode from the truncate list */
	if (prev == &sbi->s_truncate) {
		pmfs_memunlock_range(sb, head, sizeof(*head));
		head->i_next_truncate = cpu_to_le64(ino_next);
		pmfs_memlock_range(sb, head, sizeof(*head));
		pmfs_flush_buffer(&head->i_next_truncate,
			sizeof(head->i_next_truncate), false);
	} else {
		struct inode *i_prv = &list_entry(prev,
			struct pmfs_inode_info, i_truncated)->vfs_inode;
		struct pmfs_inode_truncate_item *li_prv = 
				pmfs_get_truncate_item(sb, i_prv->i_ino);
		pmfs_memunlock_range(sb, li_prv, sizeof(*li_prv));
		li_prv->i_next_truncate = cpu_to_le64(ino_next);
		pmfs_memlock_range(sb, li_prv, sizeof(*li_prv));
		pmfs_flush_buffer(&li_prv->i_next_truncate,
			sizeof(li_prv->i_next_truncate), false);
	}
	PERSISTENT_MARK();
	PERSISTENT_BARRIER();
out:
	mutex_unlock(&sbi->s_truncate_lock);
}

/* PMFS maintains a so-called truncate list, which is a linked list of inodes
 * which require further processing in case of a power failure. Currently, PMFS
 * uses the truncate list for two purposes.
 * 1) When removing a file, if the i_links_count becomes zero (i.e., the file
 * is not referenced by any directory entry), the inode needs to be freed.
 * However, if the file is currently in use (e.g., opened) it can't be freed
 * until all references are closed. Hence PMFS adds the inode to the truncate
 * list during directory entry removal, and removes it from the truncate list
 * when VFS calls evict_inode. If a power failure happens before evict_inode,
 * the inode is freed during the next mount when we recover the truncate list
 * 2) When truncating a file (reducing the file size and freeing the blocks),
 * we don't want to return the freed blocks to the free list until the whole
 * truncate operation is complete. So we add the inode to the truncate list with
 * the specified truncate_size. Now we can return freed blocks to the free list
 * even before the transaction is complete. Because if a power failure happens
 * before freeing of all the blocks is complete, PMFS will free the remaining
 * blocks during the next mount when we recover the truncate list */
void pmfs_truncate_add(struct inode *inode, u64 truncate_size)
{
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode_truncate_item *head = pmfs_get_truncate_list_head(sb);
	struct pmfs_inode_truncate_item *li;

	mutex_lock(&PMFS_SB(sb)->s_truncate_lock);
	if (!list_empty(&PMFS_I(inode)->i_truncated))
		goto out_unlock;

	li = pmfs_get_truncate_item(sb, inode->i_ino);

	pmfs_memunlock_range(sb, li, sizeof(*li));
	li->i_next_truncate = head->i_next_truncate;
	li->i_truncatesize = cpu_to_le64(truncate_size);
	pmfs_memlock_range(sb, li, sizeof(*li));
	pmfs_flush_buffer(li, sizeof(*li), false);
	/* make sure above is persistent before changing the head pointer */
	PERSISTENT_MARK();
	PERSISTENT_BARRIER();
	/* Atomically insert this inode at the head of the truncate list. */
	pmfs_memunlock_range(sb, head, sizeof(*head));
	head->i_next_truncate = cpu_to_le64(inode->i_ino);
	pmfs_memlock_range(sb, head, sizeof(*head));
	pmfs_flush_buffer(&head->i_next_truncate,
		sizeof(head->i_next_truncate), false);
	/* No need to make the head persistent here if we are called from
	 * within a transaction, because the transaction will provide a
	 * subsequent persistent barrier */
	if (pmfs_current_transaction() == NULL) {
		PERSISTENT_MARK();
		PERSISTENT_BARRIER();
	}
	list_add(&PMFS_I(inode)->i_truncated, &PMFS_SB(sb)->s_truncate);

out_unlock:
	mutex_unlock(&PMFS_SB(sb)->s_truncate_lock);
}

void pmfs_setsize(struct inode *inode, loff_t newsize)
{
	loff_t oldsize = inode->i_size;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode))) {
		pmfs_err(inode->i_sb, "%s:wrong file mode %x\n", inode->i_mode);
		return;
	}

	if (newsize != oldsize) {
		pmfs_block_truncate_page(inode, newsize);
		i_size_write(inode, newsize);
	}
	/* FIXME: we should make sure that there is nobody reading the inode
	 * before truncating it. Also we need to munmap the truncated range
	 * from application address space, if mmapped. */
	/* synchronize_rcu(); */
	__pmfs_truncate_blocks(inode, newsize, oldsize);
	/* No need to make the b-tree persistent here if we are called from
	 * within a transaction, because the transaction will provide a
	 * subsequent persistent barrier */
	if (pmfs_current_transaction() == NULL) {
		PERSISTENT_MARK();
		PERSISTENT_BARRIER();
	}
}

int pmfs_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int flags)
{
	struct inode *inode;

	inode = path->dentry->d_inode;
	generic_fillattr(inode, stat);
	/* stat->blocks should be the number of 512B blocks */
	stat->blocks = (inode->i_blocks << inode->i_sb->s_blocksize_bits) >> 9;
	return 0;
}

/* update a single inode field atomically without using a transaction */
static int pmfs_update_single_field(struct super_block *sb, struct inode *inode,
	struct pmfs_inode *pi, unsigned int ia_valid)
{
	pmfs_memunlock_inode(sb, pi);
	switch (ia_valid) {
		case ATTR_MODE:
			pi->i_mode = cpu_to_le16(inode->i_mode);
			break;
		case ATTR_UID:
			pi->i_uid = cpu_to_le32(i_uid_read(inode));
			break;
		case ATTR_GID:
			pi->i_gid = cpu_to_le32(i_gid_read(inode));
			break;
		case ATTR_SIZE:
			pi->i_size = cpu_to_le64(inode->i_size);
			break;
		case ATTR_ATIME:
			pi->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
			break;
		case ATTR_CTIME:
			pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
			break;
		case ATTR_MTIME:
			pi->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
			break;
	}
	pmfs_memlock_inode(sb, pi);
	pmfs_flush_buffer(pi, sizeof(*pi), true);
	return 0;
}

int pmfs_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);
	pmfs_transaction_t *trans;
	int ret;
	unsigned int ia_valid = attr->ia_valid, attr_mask;

	if (!pi)
		return -EACCES;

	ret = setattr_prepare(dentry, attr);
	if (ret)
		return ret;

	if ((ia_valid & ATTR_SIZE) && (attr->ia_size != inode->i_size ||
			pi->i_flags & cpu_to_le32(PMFS_EOFBLOCKS_FL))) {

		pmfs_truncate_add(inode, attr->ia_size);
		/* set allocation hint */
		pmfs_set_blocksize_hint(sb, pi, attr->ia_size);

		/* now we can freely truncate the inode */
		pmfs_setsize(inode, attr->ia_size);
		pmfs_update_isize(inode, pi);
		pmfs_flush_buffer(pi, CACHELINE_SIZE, false);
		/* we have also updated the i_ctime and i_mtime, so no
		 * need to update them again */
		ia_valid = ia_valid & ~(ATTR_CTIME | ATTR_MTIME);
		/* now it is safe to remove the inode from the truncate list */
		pmfs_truncate_del(inode);
	}
	setattr_copy(inode, attr);

	/* we have already handled ATTR_SIZE above so no need to check for it */
	attr_mask = ATTR_MODE | ATTR_UID | ATTR_GID | ATTR_ATIME | ATTR_MTIME |
		ATTR_CTIME;

	ia_valid = ia_valid & attr_mask;

	if (ia_valid == 0)
		return ret;
	/* check if we need to update only a single field. we could avoid using
	 * a transaction */
	if ((ia_valid & (ia_valid - 1)) == 0) {
		pmfs_update_single_field(sb, inode, pi, ia_valid);
		return ret;
	}

	BUG_ON(pmfs_current_transaction());
	/* multiple fields are modified. Use a transaction for atomicity */
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES);
	if (IS_ERR(trans))
		return PTR_ERR(trans);
	pmfs_add_logentry(sb, trans, pi, sizeof(*pi), LE_DATA);

	pmfs_update_inode(inode, pi);

	pmfs_commit_transaction(sb, trans);

	return ret;
}

void pmfs_set_inode_flags(struct inode *inode, struct pmfs_inode *pi)
{
	unsigned int flags = le32_to_cpu(pi->i_flags);

	inode->i_flags &=
		~(S_SYNC | S_APPEND | S_IMMUTABLE | S_NOATIME | S_DIRSYNC);
	if (flags & FS_SYNC_FL)
		inode->i_flags |= S_SYNC;
	if (flags & FS_APPEND_FL)
		inode->i_flags |= S_APPEND;
	if (flags & FS_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;
	if (flags & FS_NOATIME_FL)
		inode->i_flags |= S_NOATIME;
	if (flags & FS_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
	if (!pi->i_xattr)
		inode_has_no_xattr(inode);
	inode->i_flags |= S_DAX;
}

void pmfs_get_inode_flags(struct inode *inode, struct pmfs_inode *pi)
{
	unsigned int flags = inode->i_flags;
	unsigned int pmfs_flags = le32_to_cpu(pi->i_flags);

	pmfs_flags &= ~(FS_SYNC_FL | FS_APPEND_FL | FS_IMMUTABLE_FL |
			 FS_NOATIME_FL | FS_DIRSYNC_FL);
	if (flags & S_SYNC)
		pmfs_flags |= FS_SYNC_FL;
	if (flags & S_APPEND)
		pmfs_flags |= FS_APPEND_FL;
	if (flags & S_IMMUTABLE)
		pmfs_flags |= FS_IMMUTABLE_FL;
	if (flags & S_NOATIME)
		pmfs_flags |= FS_NOATIME_FL;
	if (flags & S_DIRSYNC)
		pmfs_flags |= FS_DIRSYNC_FL;

	pi->i_flags = cpu_to_le32(pmfs_flags);
}

static ssize_t pmfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *filp = iocb->ki_filp;
	struct inode *inode = filp->f_mapping->host;
	loff_t end = iocb->ki_pos;
	ssize_t ret = -EINVAL;
	ssize_t written = 0;
	unsigned long seg;
	unsigned long nr_segs = iter->nr_segs;
	const struct iovec *iv = iter->iov;

	for (seg = 0; seg < nr_segs; seg++) {
		end += iv->iov_len;
		iv++;
	}

	if ((iov_iter_rw(iter) == WRITE) && end > i_size_read(inode)) {
		/* FIXME: Do we need to check for out of bounds IO for R/W */
		printk(KERN_ERR "pmfs: needs to grow (size = %lld)\n", end);
		return ret;
	}

	iv = iter->iov;
	for (seg = 0; seg < nr_segs; seg++) {
		if (iov_iter_rw(iter) == READ) {
			ret = pmfs_xip_file_read(filp, iv->iov_base,
					iv->iov_len, &iocb->ki_pos);
		} else if (iov_iter_rw(iter) == WRITE) {
			inode_unlock(inode);
			ret = pmfs_xip_file_write(filp, iv->iov_base,
					iv->iov_len, &iocb->ki_pos);
			inode_lock(inode);
		}
		if (ret < 0)
			goto err;

		if (iter->count > iv->iov_len)
			iter->count -= iv->iov_len;
		else
			iter->count = 0;

		written += ret;
		iter->nr_segs--;
		iv++;
	}
	if (iocb->ki_pos != end)
		printk(KERN_ERR "pmfs: direct_IO: end = %lld"
			"but offset = %lld\n", end, iocb->ki_pos);
	ret = written;
err:
	return ret;
}

const struct address_space_operations pmfs_aops_xip = {
	.direct_IO		= pmfs_direct_IO,
	/*.xip_mem_protect	= pmfs_xip_mem_protect,*/
};
