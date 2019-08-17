/*
 * BRIEF DESCRIPTION
 *
 * Ioctl operations.
 *
 * Copyright 2012-2013 Intel Corporation
 * Copyright 2010-2011 Marco Stornelli <marco.stornelli@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/capability.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include "pmfs.h"

#define	FS_PMFS_FSYNC	0xBCD0000E

struct sync_range
{
	off_t	offset;
	size_t	length;
};

long pmfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct address_space *mapping = filp->f_mapping;
	struct inode    *inode = mapping->host;
	struct pmfs_inode *pi;
	struct super_block *sb = inode->i_sb;
	unsigned int flags;
	int ret;
	pmfs_transaction_t *trans;

	pi = pmfs_get_inode(sb, inode->i_ino);
	if (!pi)
		return -EACCES;

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		flags = le32_to_cpu(pi->i_flags) & PMFS_FL_USER_VISIBLE;
		return put_user(flags, (int __user *)arg);
	case FS_IOC_SETFLAGS: {
		unsigned int oldflags;

		ret = mnt_want_write_file(filp);
		if (ret)
			return ret;

		if (!inode_owner_or_capable(inode)) {
			ret = -EPERM;
			goto flags_out;
		}

		if (get_user(flags, (int __user *)arg)) {
			ret = -EFAULT;
			goto flags_out;
		}

		inode_lock(inode);
		oldflags = le32_to_cpu(pi->i_flags);

		if ((flags ^ oldflags) &
		    (FS_APPEND_FL | FS_IMMUTABLE_FL)) {
			if (!capable(CAP_LINUX_IMMUTABLE)) {
				inode_unlock(inode);
				ret = -EPERM;
				goto flags_out;
			}
		}

		if (!S_ISDIR(inode->i_mode))
			flags &= ~FS_DIRSYNC_FL;

		flags = flags & FS_FL_USER_MODIFIABLE;
		flags |= oldflags & ~FS_FL_USER_MODIFIABLE;
		inode->i_ctime = current_time(inode);
		trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}
		pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);
		pmfs_memunlock_inode(sb, pi);
		pi->i_flags = cpu_to_le32(flags);
		pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
		pmfs_set_inode_flags(inode, pi);
		pmfs_memlock_inode(sb, pi);
		pmfs_commit_transaction(sb, trans);
out:
		inode_unlock(inode);
flags_out:
		mnt_drop_write_file(filp);
		return ret;
	}
	case FS_IOC_GETVERSION:
		return put_user(inode->i_generation, (int __user *)arg);
	case FS_IOC_SETVERSION: {
		__u32 generation;
		if (!inode_owner_or_capable(inode))
			return -EPERM;
		ret = mnt_want_write_file(filp);
		if (ret)
			return ret;
		if (get_user(generation, (int __user *)arg)) {
			ret = -EFAULT;
			goto setversion_out;
		}
		inode_lock(inode);
		trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}
		pmfs_add_logentry(sb, trans, pi, sizeof(*pi), LE_DATA);
		inode->i_ctime = current_time(inode);
		inode->i_generation = generation;
		pmfs_memunlock_inode(sb, pi);
		pi->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
		pi->i_generation = cpu_to_le32(inode->i_generation);
		pmfs_memlock_inode(sb, pi);
		pmfs_commit_transaction(sb, trans);
		inode_unlock(inode);
setversion_out:
		mnt_drop_write_file(filp);
		return ret;
	}
	case FS_PMFS_FSYNC: {
		struct sync_range packet;
		copy_from_user(&packet, (void *)arg, sizeof(struct sync_range));
		pmfs_fsync(filp, packet.offset, packet.offset + packet.length, 1);
		return 0;
	}
	case PMFS_PRINT_TIMING: {
		pmfs_print_timing_stats();
		return 0;
	}
	case PMFS_CLEAR_STATS: {
		pmfs_clear_stats();
		return 0;
	}
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long pmfs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC32_GETFLAGS:
		cmd = FS_IOC_GETFLAGS;
		break;
	case FS_IOC32_SETFLAGS:
		cmd = FS_IOC_SETFLAGS;
		break;
	case FS_IOC32_GETVERSION:
		cmd = FS_IOC_GETVERSION;
		break;
	case FS_IOC32_SETVERSION:
		cmd = FS_IOC_SETVERSION;
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return pmfs_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif
