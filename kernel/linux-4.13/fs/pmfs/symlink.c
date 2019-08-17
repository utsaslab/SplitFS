/*
 * BRIEF DESCRIPTION
 *
 * Symlink operations
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
#include <linux/namei.h>
#include "pmfs.h"

int pmfs_block_symlink(struct inode *inode, const char *symname, int len)
{
	struct super_block *sb = inode->i_sb;
	u64 block;
	char *blockp;
	int err;

	err = pmfs_alloc_blocks(NULL, inode, 0, 1, false);
	if (err)
		return err;

	block = pmfs_find_data_block(inode, 0);
	blockp = pmfs_get_block(sb, block);

	pmfs_memunlock_block(sb, blockp);
	memcpy(blockp, symname, len);
	blockp[len] = '\0';
	pmfs_memlock_block(sb, blockp);
	pmfs_flush_buffer(blockp, len+1, false);
	return 0;
}

/* FIXME: Temporary workaround */
static int pmfs_readlink_copy(char __user *buffer, int buflen, const char *link)
{
	int len = PTR_ERR(link);
	if (IS_ERR(link))
		goto out;

	len = strlen(link);
	if (len > (unsigned) buflen)
		len = buflen;
	if (copy_to_user(buffer, link, len))
		len = -EFAULT;
out:
	return len;
}

static int pmfs_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	u64 block;
	char *blockp;

	block = pmfs_find_data_block(inode, 0);
	blockp = pmfs_get_block(sb, block);
	return pmfs_readlink_copy(buffer, buflen, blockp);
}

static const char *pmfs_get_link(struct dentry *dentry, struct inode *inode,
	struct delayed_call *done)
{
	struct super_block *sb = inode->i_sb;
	off_t block;
	char *blockp;

	block = pmfs_find_data_block(inode, 0);
	blockp = pmfs_get_block(sb, block);
	return blockp;
}

const struct inode_operations pmfs_symlink_inode_operations = {
	.readlink	= pmfs_readlink,
	.get_link	= pmfs_get_link,
	.setattr	= pmfs_notify_change,
};
