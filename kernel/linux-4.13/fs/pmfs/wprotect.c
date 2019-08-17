/*
 * BRIEF DESCRIPTION
 *
 * Write protection for the filesystem pages.
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

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/io.h>
#include "pmfs.h"

static inline void wprotect_disable(void)
{
	unsigned long cr0_val;

	cr0_val = read_cr0();
	cr0_val &= (~X86_CR0_WP);
	write_cr0(cr0_val);
}

static inline void wprotect_enable(void)
{
	unsigned long cr0_val;

	cr0_val = read_cr0();
	cr0_val |= X86_CR0_WP;
	write_cr0(cr0_val);
}

/* FIXME: Assumes that we are always called in the right order.
 * pmfs_writeable(vaddr, size, 1);
 * pmfs_writeable(vaddr, size, 0);
 */
int pmfs_writeable(void *vaddr, unsigned long size, int rw)
{
	static unsigned long flags;
	if (rw) {
		local_irq_save(flags);
		wprotect_disable();
	} else {
		wprotect_enable();
		local_irq_restore(flags);
	}
	return 0;
}

int pmfs_xip_mem_protect(struct super_block *sb, void *vaddr,
			  unsigned long size, int rw)
{
	if (!pmfs_is_wprotected(sb))
		return 0;
	return pmfs_writeable(vaddr, size, rw);
}
