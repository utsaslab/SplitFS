/*
 * BRIEF DESCRIPTION
 *
 * Inode operations for directories.
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
#include <linux/pagemap.h>
#include "pmfs.h"
#include "xip.h"

/*
 * Couple of helper functions - make the code slightly cleaner.
 */
static inline void pmfs_inc_count(struct inode *inode, struct pmfs_inode *pi)
{
	inc_nlink(inode);
	pmfs_update_nlink(inode, pi);
}

static inline void pmfs_dec_count(struct inode *inode, struct pmfs_inode *pi)
{
	if (inode->i_nlink) {
		drop_nlink(inode);
		pmfs_update_nlink(inode, pi);
	}
}

static inline int pmfs_add_nondir(pmfs_transaction_t *trans,
		struct inode *dir, struct dentry *dentry, struct inode *inode)
{
	struct pmfs_inode *pi;
	int err = pmfs_add_entry(trans, dentry, inode);

	if (!err) {
		d_instantiate(dentry, inode);
		unlock_new_inode(inode);
		return 0;
	}
	pi = pmfs_get_inode(inode->i_sb, inode->i_ino);
	pmfs_dec_count(inode, pi);
	unlock_new_inode(inode);
	iput(inode);
	return err;
}

static inline struct pmfs_direntry *pmfs_next_entry(struct pmfs_direntry *p)
{
	return (struct pmfs_direntry *)((char *)p + le16_to_cpu(p->de_len));
}

/*
 * Methods themselves.
 */
int pmfs_check_dir_entry(const char *function, struct inode *dir,
			  struct pmfs_direntry *de, u8 *base,
			  unsigned long offset)
{
	const char *error_msg = NULL;
	const int rlen = le16_to_cpu(de->de_len);

	if (unlikely(rlen < PMFS_DIR_REC_LEN(1)))
		error_msg = "de_len is smaller than minimal";
	else if (unlikely(rlen % 4 != 0))
		error_msg = "de_len % 4 != 0";
	else if (unlikely(rlen < PMFS_DIR_REC_LEN(de->name_len)))
		error_msg = "de_len is too small for name_len";
	else if (unlikely((((u8 *)de - base) + rlen > dir->i_sb->s_blocksize)))
		error_msg = "directory entry across blocks";

	if (unlikely(error_msg != NULL)) {
		pmfs_dbg("bad entry in directory #%lu: %s - "
			  "offset=%lu, inode=%lu, rec_len=%d, name_len=%d",
			  dir->i_ino, error_msg, offset,
			  (unsigned long)le64_to_cpu(de->ino), rlen,
			  de->name_len);
	}

	return error_msg == NULL ? 1 : 0;
}

/*
 * Returns 0 if not found, -1 on failure, and 1 on success
 */
int pmfs_search_dirblock(u8 *blk_base, struct inode *dir, struct qstr *child,
			  unsigned long	offset,
			  struct pmfs_direntry **res_dir,
			  struct pmfs_direntry **prev_dir)
{
	struct pmfs_direntry *de;
	struct pmfs_direntry *pde = NULL;
	char *dlimit;
	int de_len;
	const char *name = child->name;
	int namelen = child->len;

	de = (struct pmfs_direntry *)blk_base;
	dlimit = blk_base + dir->i_sb->s_blocksize;
	while ((char *)de < dlimit) {
		/* this code is executed quadratically often */
		/* do minimal checking `by hand' */

		if ((char *)de + namelen <= dlimit &&
		    pmfs_match(namelen, name, de)) {
			/* found a match - just to be sure, do a full check */
			if (!pmfs_check_dir_entry("pmfs_inode_by_name",
						   dir, de, blk_base, offset))
				return -1;
			*res_dir = de;
			if (prev_dir)
				*prev_dir = pde;
			return 1;
		}
		/* prevent looping on a bad block */
		de_len = le16_to_cpu(de->de_len);
		if (de_len <= 0)
			return -1;
		offset += de_len;
		pde = de;
		de = (struct pmfs_direntry *)((char *)de + de_len);
	}
	return 0;
}

static ino_t pmfs_inode_by_name(struct inode *dir, struct qstr *entry,
				 struct pmfs_direntry **res_entry)
{
	struct pmfs_inode *pi;
	ino_t i_no = 0;
	int namelen, nblocks, i;
	u8 *blk_base;
	const u8 *name = entry->name;
	struct super_block *sb = dir->i_sb;
	unsigned long block, start;
	struct pmfs_inode_info *si = PMFS_I(dir);

	pi = pmfs_get_inode(sb, dir->i_ino);

	namelen = entry->len;
	if (namelen > PMFS_NAME_LEN)
		return 0;
	if ((namelen <= 2) && (name[0] == '.') &&
	    (name[1] == '.' || name[1] == 0)) {
		/*
		 * "." or ".." will only be in the first block
		 */
		block = start = 0;
		nblocks = 1;
		goto restart;
	}
	nblocks = dir->i_size >> dir->i_sb->s_blocksize_bits;
	start = si->i_dir_start_lookup;
	if (start >= nblocks)
		start = 0;
	block = start;
restart:
	do {
		blk_base =
			pmfs_get_block(sb, pmfs_find_data_block(dir, block));
		if (!blk_base)
			goto done;
		i = pmfs_search_dirblock(blk_base, dir, entry,
					  block << sb->s_blocksize_bits,
					  res_entry, NULL);
		if (i == 1) {
			si->i_dir_start_lookup = block;
			i_no = le64_to_cpu((*res_entry)->ino);
			goto done;
		} else {
			if (i < 0)
				goto done;
		}
		if (++block >= nblocks)
			block = 0;
	} while (block != start);
	/*
	 * If the directory has grown while we were searching, then
	 * search the last part of the directory before giving up.
	 */
	block = nblocks;
	nblocks = dir->i_size >> sb->s_blocksize_bits;
	if (block < nblocks) {
		start = 0;
		goto restart;
	}
done:
	return i_no;
}

static struct dentry *pmfs_lookup(struct inode *dir, struct dentry *dentry,
				   unsigned int flags)
{
	struct inode *inode = NULL;
	struct pmfs_direntry *de;
	ino_t ino;

	if (dentry->d_name.len > PMFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ino = pmfs_inode_by_name(dir, &dentry->d_name, &de);
	if (ino) {
		inode = pmfs_iget(dir->i_sb, ino);
		if (inode == ERR_PTR(-ESTALE)) {
			pmfs_err(dir->i_sb, __func__,
				  "deleted inode referenced: %lu",
				  (unsigned long)ino);
			return ERR_PTR(-EIO);
		}
	}

	return d_splice_alias(inode, dentry);
}

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has no inode.
 *
 * If the create succeeds, we fill in the inode information
 * with d_instantiate().
 */
static int pmfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
			bool excl)
{
	struct inode *inode = NULL;
	int err = PTR_ERR(inode);
	struct super_block *sb = dir->i_sb;
	pmfs_transaction_t *trans;
	timing_t create_time;

	PMFS_START_TIMING(create_t, create_time);
	/* two log entries for new inode, 1 lentry for dir inode, 1 for dir
	 * inode's b-tree, 2 lentries for logging dir entry
	 */
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES * 2 +
		MAX_DIRENTRY_LENTRIES);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out;
	}

	inode = pmfs_new_inode(trans, dir, mode, &dentry->d_name);
	if (IS_ERR(inode))
		goto out_err;
	pmfs_dbg_verbose("%s: %s, ino %lu\n", __func__,
				dentry->d_name.name, inode->i_ino);
	inode->i_op = &pmfs_file_inode_operations;
	inode->i_mapping->a_ops = &pmfs_aops_xip;
	inode->i_fop = &pmfs_xip_file_operations;
	err = pmfs_add_nondir(trans, dir, dentry, inode);
	if (err)
		goto out_err;
	pmfs_commit_transaction(sb, trans);
out:
	PMFS_END_TIMING(create_t, create_time);
	return err;
out_err:
	pmfs_abort_transaction(sb, trans);
	return err;
}

static int pmfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode,
		       dev_t rdev)
{
	struct inode *inode = NULL;
	int err = PTR_ERR(inode);
	pmfs_transaction_t *trans;
	struct super_block *sb = dir->i_sb;
	struct pmfs_inode *pi;

	/* 2 log entries for new inode, 1 lentry for dir inode, 1 for dir
	 * inode's b-tree, 2 lentries for logging dir entry
	 */
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES * 2 +
			MAX_DIRENTRY_LENTRIES);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out;
	}

	inode = pmfs_new_inode(trans, dir, mode, &dentry->d_name);
	if (IS_ERR(inode))
		goto out_err;
	init_special_inode(inode, mode, rdev);
	inode->i_op = &pmfs_special_inode_operations;

	pi = pmfs_get_inode(sb, inode->i_ino);
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		pi->dev.rdev = cpu_to_le32(inode->i_rdev);
	err = pmfs_add_nondir(trans, dir, dentry, inode);
	if (err)
		goto out_err;
	pmfs_commit_transaction(sb, trans);
out:
	return err;
out_err:
	pmfs_abort_transaction(sb, trans);
	return err;
}

static int pmfs_symlink(struct inode *dir, struct dentry *dentry,
			 const char *symname)
{
	struct super_block *sb = dir->i_sb;
	int err = -ENAMETOOLONG;
	unsigned len = strlen(symname);
	struct inode *inode;
	pmfs_transaction_t *trans;
	struct pmfs_inode *pi;

	if (len + 1 > sb->s_blocksize)
		goto out;

	/* 2 log entries for new inode, 1 lentry for dir inode, 1 for dir
	 * inode's b-tree, 2 lentries for logging dir entry
	 */
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES * 2 +
			MAX_DIRENTRY_LENTRIES);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out;
	}

	inode = pmfs_new_inode(trans, dir, S_IFLNK|S_IRWXUGO, &dentry->d_name);
	err = PTR_ERR(inode);
	if (IS_ERR(inode)) {
		pmfs_abort_transaction(sb, trans);
		goto out;
	}

	inode->i_op = &pmfs_symlink_inode_operations;
	inode->i_mapping->a_ops = &pmfs_aops_xip;

	pi = pmfs_get_inode(sb, inode->i_ino);
	err = pmfs_block_symlink(inode, symname, len);
	if (err)
		goto out_fail;

	inode->i_size = len;
	pmfs_update_isize(inode, pi);

	err = pmfs_add_nondir(trans, dir, dentry, inode);
	if (err) {
		pmfs_abort_transaction(sb, trans);
		goto out;
	}

	pmfs_commit_transaction(sb, trans);
out:
	return err;

out_fail:
	pmfs_dec_count(inode, pi);
	unlock_new_inode(inode);
	iput(inode);
	pmfs_abort_transaction(sb, trans);
	goto out;
}

static int pmfs_link(struct dentry *dest_dentry, struct inode *dir,
		      struct dentry *dentry)
{
	struct inode *inode = dest_dentry->d_inode;
	int err = -ENOMEM;
	pmfs_transaction_t *trans;
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);

	if (inode->i_nlink >= PMFS_LINK_MAX)
		return -EMLINK;

	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES * 2 +
			MAX_DIRENTRY_LENTRIES);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out;
	}
	/* only need to log the first 48 bytes since we only modify ctime and
	 * i_links_count in this system call */
	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	ihold(inode);

	err = pmfs_add_entry(trans, dentry, inode);
	if (!err) {
		inode->i_ctime = current_time(inode);
		inc_nlink(inode);

		pmfs_memunlock_inode(sb, pi);
		pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
		pi->i_links_count = cpu_to_le16(inode->i_nlink);
		pmfs_memlock_inode(sb, pi);

		d_instantiate(dentry, inode);
		pmfs_commit_transaction(sb, trans);
	} else {
		iput(inode);
		pmfs_abort_transaction(sb, trans);
	}
out:
	return err;
}

static int pmfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int retval = -ENOMEM;
	pmfs_transaction_t *trans;
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);
	timing_t unlink_time;

	PMFS_START_TIMING(unlink_t, unlink_time);

	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES * 2 +
		MAX_DIRENTRY_LENTRIES);
	if (IS_ERR(trans)) {
		retval = PTR_ERR(trans);
		goto out;
	}
	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	pmfs_dbg_verbose("%s: %s, ino %lu\n", __func__,
				dentry->d_name.name, inode->i_ino);
	retval = pmfs_remove_entry(trans, dentry, inode);
	if (retval)
		goto end_unlink;

	if (inode->i_nlink == 1)
		pmfs_truncate_add(inode, inode->i_size);
	inode->i_ctime = dir->i_ctime;

	pmfs_memunlock_inode(sb, pi);
	if (inode->i_nlink) {
		drop_nlink(inode);
		pi->i_links_count = cpu_to_le16(inode->i_nlink);
	}
	pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	pmfs_memlock_inode(sb, pi);

	pmfs_commit_transaction(sb, trans);
	PMFS_END_TIMING(unlink_t, unlink_time);
	return 0;
end_unlink:
	pmfs_abort_transaction(sb, trans);
out:
	return retval;
}

static int pmfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	struct pmfs_inode *pi, *pidir;
	struct pmfs_direntry *de = NULL;
	struct super_block *sb = dir->i_sb;
	pmfs_transaction_t *trans;
	int err = -EMLINK;
	char *blk_base;

	if (dir->i_nlink >= PMFS_LINK_MAX)
		goto out;

	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES * 2 +
			MAX_DIRENTRY_LENTRIES);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out;
	}

	inode = pmfs_new_inode(trans, dir, S_IFDIR | mode, &dentry->d_name);
	err = PTR_ERR(inode);
	if (IS_ERR(inode)) {
		pmfs_abort_transaction(sb, trans);
		goto out;
	}

	pmfs_dbg_verbose("%s: %s, ino %lu\n", __func__,
				dentry->d_name.name, inode->i_ino);
	inode->i_op = &pmfs_dir_inode_operations;
	inode->i_fop = &pmfs_dir_operations;
	inode->i_mapping->a_ops = &pmfs_aops_xip;

	/* since this is a new inode so we don't need to include this
	 * pmfs_alloc_blocks in the transaction
	 */
	err = pmfs_alloc_blocks(NULL, inode, 0, 1, false);
	if (err)
		goto out_clear_inode;
	inode->i_size = sb->s_blocksize;

	blk_base = pmfs_get_block(sb, pmfs_find_data_block(inode, 0));
	de = (struct pmfs_direntry *)blk_base;
	pmfs_memunlock_range(sb, blk_base, sb->s_blocksize);
	de->ino = cpu_to_le64(inode->i_ino);
	de->name_len = 1;
	de->de_len = cpu_to_le16(PMFS_DIR_REC_LEN(de->name_len));
	strcpy(de->name, ".");
	/*de->file_type = S_IFDIR; */
	de = pmfs_next_entry(de);
	de->ino = cpu_to_le64(dir->i_ino);
	de->de_len = cpu_to_le16(sb->s_blocksize - PMFS_DIR_REC_LEN(1));
	de->name_len = 2;
	strcpy(de->name, "..");
	/*de->file_type =  S_IFDIR; */
	pmfs_memlock_range(sb, blk_base, sb->s_blocksize);

	/* No need to journal the dir entries but we need to persist them */
	pmfs_flush_buffer(blk_base, PMFS_DIR_REC_LEN(1) +
			PMFS_DIR_REC_LEN(2), true);

	set_nlink(inode, 2);

	err = pmfs_add_entry(trans, dentry, inode);
	if (err) {
		pmfs_dbg_verbose("failed to add dir entry\n");
		goto out_clear_inode;
	}
	pi = pmfs_get_inode(sb, inode->i_ino);
	pmfs_memunlock_inode(sb, pi);
	pi->i_links_count = cpu_to_le16(inode->i_nlink);
	pi->i_size = cpu_to_le64(inode->i_size);
	pmfs_memlock_inode(sb, pi);

	pidir = pmfs_get_inode(sb, dir->i_ino);
	pmfs_inc_count(dir, pidir);
	d_instantiate(dentry, inode);
	unlock_new_inode(inode);

	pmfs_commit_transaction(sb, trans);

out:
	return err;

out_clear_inode:
	clear_nlink(inode);
	unlock_new_inode(inode);
	iput(inode);
	pmfs_abort_transaction(sb, trans);
	goto out;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
static int pmfs_empty_dir(struct inode *inode)
{
	unsigned long offset;
	struct pmfs_direntry *de, *de1;
	struct super_block *sb;
	char *blk_base;
	int err = 0;

	sb = inode->i_sb;
	if (inode->i_size < PMFS_DIR_REC_LEN(1) + PMFS_DIR_REC_LEN(2)) {
		pmfs_dbg("bad directory (dir #%lu)-no data block",
			  inode->i_ino);
		return 1;
	}

	blk_base = pmfs_get_block(sb, pmfs_find_data_block(inode, 0));
	if (!blk_base) {
		pmfs_dbg("bad directory (dir #%lu)-no data block",
			  inode->i_ino);
		return 1;
	}

	de = (struct pmfs_direntry *)blk_base;
	de1 = pmfs_next_entry(de);

	if (le64_to_cpu(de->ino) != inode->i_ino || !le64_to_cpu(de1->ino) ||
	    strcmp(".", de->name) || strcmp("..", de1->name)) {
		pmfs_dbg("bad directory (dir #%lu) - no `.' or `..'",
			  inode->i_ino);
		return 1;
	}
	offset = le16_to_cpu(de->de_len) + le16_to_cpu(de1->de_len);
	de = pmfs_next_entry(de1);
	while (offset < inode->i_size) {
		if (!blk_base || (void *)de >= (void *)(blk_base +
					sb->s_blocksize)) {
			err = 0;
			blk_base = pmfs_get_block(sb, pmfs_find_data_block(
				    inode, offset >> sb->s_blocksize_bits));
			if (!blk_base) {
				pmfs_dbg("Error: reading dir #%lu offset %lu\n",
					  inode->i_ino, offset);
				offset += sb->s_blocksize;
				continue;
			}
			de = (struct pmfs_direntry *)blk_base;
		}
		if (!pmfs_check_dir_entry("empty_dir", inode, de, blk_base,
					offset)) {
			de = (struct pmfs_direntry *)(blk_base +
				sb->s_blocksize);
			offset = (offset | (sb->s_blocksize - 1)) + 1;
			continue;
		}
		if (le64_to_cpu(de->ino))
			return 0;
		offset += le16_to_cpu(de->de_len);
		de = pmfs_next_entry(de);
	}
	return 1;
}

static int pmfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct pmfs_direntry *de;
	pmfs_transaction_t *trans;
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino), *pidir;
	int err = -ENOTEMPTY;

	if (!inode)
		return -ENOENT;

	pmfs_dbg_verbose("%s: %s, ino %lu\n", __func__,
				dentry->d_name.name, inode->i_ino);
	if (pmfs_inode_by_name(dir, &dentry->d_name, &de) == 0)
		return -ENOENT;

	if (!pmfs_empty_dir(inode))
		return err;

	if (inode->i_nlink != 2)
		pmfs_dbg("empty directory has nlink!=2 (%d)", inode->i_nlink);

	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES * 2 +
			MAX_DIRENTRY_LENTRIES);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		return err;
	}
	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	err = pmfs_remove_entry(trans, dentry, inode);
	if (err)
		goto end_rmdir;

	/*inode->i_version++; */
	clear_nlink(inode);
	inode->i_ctime = dir->i_ctime;

	pmfs_memunlock_inode(sb, pi);
	pi->i_links_count = cpu_to_le16(inode->i_nlink);
	pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	pmfs_memlock_inode(sb, pi);

	/* add the inode to truncate list in case a crash happens before the
	 * subsequent evict_inode is called. It will be deleted from the
	 * truncate list during evict_inode.
	 */
	pmfs_truncate_add(inode, inode->i_size);

	pidir = pmfs_get_inode(sb, dir->i_ino);
	pmfs_dec_count(dir, pidir);

	pmfs_commit_transaction(sb, trans);
	return err;
end_rmdir:
	pmfs_abort_transaction(sb, trans);
	return err;
}

static int pmfs_rename(struct inode *old_dir,
			struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	struct inode *old_inode = old_dentry->d_inode;
	struct inode *new_inode = new_dentry->d_inode;
	struct pmfs_direntry *new_de = NULL, *old_de = NULL;
	pmfs_transaction_t *trans;
	struct super_block *sb = old_inode->i_sb;
	struct pmfs_inode *pi, *new_pidir, *old_pidir;
	int err = -ENOENT;

	pmfs_inode_by_name(new_dir, &new_dentry->d_name, &new_de);
	pmfs_inode_by_name(old_dir, &old_dentry->d_name, &old_de);

	pmfs_dbg_verbose("%s: rename %s to %s\n", __func__,
			old_dentry->d_name.name, new_dentry->d_name.name);
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES * 4 +
			MAX_DIRENTRY_LENTRIES * 2);
	if (IS_ERR(trans)) {
		return PTR_ERR(trans);
	}

	if (new_inode) {
		err = -ENOTEMPTY;
		if (S_ISDIR(old_inode->i_mode) && !pmfs_empty_dir(new_inode))
			goto out;
	} else {
		if (S_ISDIR(old_inode->i_mode)) {
			err = -EMLINK;
			if (new_dir->i_nlink >= PMFS_LINK_MAX)
				goto out;
		}
	}

	new_pidir = pmfs_get_inode(sb, new_dir->i_ino);

	pi = pmfs_get_inode(sb, old_inode->i_ino);
	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	if (!new_de) {
		/* link it into the new directory. */
		err = pmfs_add_entry(trans, new_dentry, old_inode);
		if (err)
			goto out;
	} else {
		pmfs_add_logentry(sb, trans, &new_de->ino, sizeof(new_de->ino),
			LE_DATA);

		pmfs_memunlock_range(sb, new_de, sb->s_blocksize);
		new_de->ino = cpu_to_le64(old_inode->i_ino);
		/*new_de->file_type = old_de->file_type; */
		pmfs_memlock_range(sb, new_de, sb->s_blocksize);

		pmfs_add_logentry(sb, trans, new_pidir, MAX_DATA_PER_LENTRY,
			LE_DATA);
		/*new_dir->i_version++; */
		new_dir->i_ctime = new_dir->i_mtime = current_time(new_dir);
		pmfs_update_time(new_dir, new_pidir);
	}

	/* and unlink the inode from the old directory ... */
	err = pmfs_remove_entry(trans, old_dentry, old_inode);
	if (err)
		goto out;

	if (new_inode) {
		pi = pmfs_get_inode(sb, new_inode->i_ino);
		pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);
		new_inode->i_ctime = current_time(new_inode);

		pmfs_memunlock_inode(sb, pi);
		if (S_ISDIR(old_inode->i_mode)) {
			if (new_inode->i_nlink)
				drop_nlink(new_inode);
		}
		pi->i_ctime = cpu_to_le32(new_inode->i_ctime.tv_sec);
		if (new_inode->i_nlink)
			drop_nlink(new_inode);
		pi->i_links_count = cpu_to_le16(new_inode->i_nlink);
		pmfs_memlock_inode(sb, pi);

		if (!new_inode->i_nlink)
			pmfs_truncate_add(new_inode, new_inode->i_size);
	} else {
		if (S_ISDIR(old_inode->i_mode)) {
			pmfs_inc_count(new_dir, new_pidir);
			old_pidir = pmfs_get_inode(sb, old_dir->i_ino);
			pmfs_dec_count(old_dir, old_pidir);
		}
	}

	pmfs_commit_transaction(sb, trans);
	return 0;
out:
	pmfs_abort_transaction(sb, trans);
	return err;
}

struct dentry *pmfs_get_parent(struct dentry *child)
{
	struct inode *inode;
	struct qstr dotdot = QSTR_INIT("..", 2);
	struct pmfs_direntry *de = NULL;
	ino_t ino;

	pmfs_inode_by_name(child->d_inode, &dotdot, &de);
	if (!de)
		return ERR_PTR(-ENOENT);
	ino = le64_to_cpu(de->ino);

	if (ino)
		inode = pmfs_iget(child->d_inode->i_sb, ino);
	else
		return ERR_PTR(-ENOENT);

	return d_obtain_alias(inode);
}

const struct inode_operations pmfs_dir_inode_operations = {
	.create		= pmfs_create,
	.lookup		= pmfs_lookup,
	.link		= pmfs_link,
	.unlink		= pmfs_unlink,
	.symlink	= pmfs_symlink,
	.mkdir		= pmfs_mkdir,
	.rmdir		= pmfs_rmdir,
	.mknod		= pmfs_mknod,
	.rename		= pmfs_rename,
	.setattr	= pmfs_notify_change,
	.get_acl	= NULL,
};

const struct inode_operations pmfs_special_inode_operations = {
	.setattr	= pmfs_notify_change,
	.get_acl	= NULL,
};
