/*
 *  linux/fs/xip2fs/ioctl.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 *            Gerald Schaefer <geraldsc@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */


#include "xip2.h"
#include <linux/time.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/uaccess.h>


int xip2_ioctl (struct inode * inode, struct file * filp, unsigned int cmd,
		unsigned long arg)
{
	struct xip2_inode_info *ei = XIP2_I(inode);
	unsigned int flags;

	ext2_debug ("cmd = %u, arg = %lu\n", cmd, arg);

	switch (cmd) {
	case EXT2_IOC_GETFLAGS:
		flags = ei->i_flags & EXT2_FL_USER_VISIBLE;
		return put_user(flags, (int *) arg);
	case EXT2_IOC_GETVERSION:
		return put_user(inode->i_generation, (int *) arg);
	default:
		return -ENOTTY;
	}
}
