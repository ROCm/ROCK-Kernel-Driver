/*
 *  linux/fs/hfsplus/ioctl.c
 *
 * Copyright (C) 2003
 * Ethan Benson <erbenson@alaska.net>
 * partially derived from linux/fs/ext2/ioctl.c
 * Copyright (C) 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 * hfsplus ioctls
 */

#include <linux/fs.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include "hfsplus_fs.h"

int hfsplus_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
		  unsigned long arg)
{
	unsigned int flags;

	switch (cmd) {
	case HFSPLUS_IOC_EXT2_GETFLAGS:
		flags = 0;
		if (HFSPLUS_I(inode).rootflags & HFSPLUS_FLG_IMMUTABLE)
			flags |= EXT2_FLAG_IMMUTABLE; /* EXT2_IMMUTABLE_FL */
		if (HFSPLUS_I(inode).rootflags & HFSPLUS_FLG_APPEND)
			flags |= EXT2_FLAG_APPEND; /* EXT2_APPEND_FL */
		if (HFSPLUS_I(inode).userflags & HFSPLUS_FLG_NODUMP)
			flags |= EXT2_FLAG_NODUMP; /* EXT2_NODUMP_FL */
		return put_user(flags, (int __user *)arg);
	case HFSPLUS_IOC_EXT2_SETFLAGS: {
		if (IS_RDONLY(inode))
			return -EROFS;

		if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
			return -EACCES;

		if (get_user(flags, (int __user *)arg))
			return -EFAULT;

		if (flags & (EXT2_FLAG_IMMUTABLE|EXT2_FLAG_APPEND) ||
		    HFSPLUS_I(inode).rootflags & (HFSPLUS_FLG_IMMUTABLE|HFSPLUS_FLG_APPEND)) {
			if (!capable(CAP_LINUX_IMMUTABLE))
				return -EPERM;
		}

		/* don't silently ignore unsupported ext2 flags */
		if (flags & ~(EXT2_FLAG_IMMUTABLE|EXT2_FLAG_APPEND|
			      EXT2_FLAG_NODUMP))
			return -EOPNOTSUPP;

		if (flags & EXT2_FLAG_IMMUTABLE) { /* EXT2_IMMUTABLE_FL */
			inode->i_flags |= S_IMMUTABLE;
			HFSPLUS_I(inode).rootflags |= HFSPLUS_FLG_IMMUTABLE;
		} else {
			inode->i_flags &= ~S_IMMUTABLE;
			HFSPLUS_I(inode).rootflags &= ~HFSPLUS_FLG_IMMUTABLE;
		}
		if (flags & EXT2_FLAG_APPEND) { /* EXT2_APPEND_FL */
			inode->i_flags |= S_APPEND;
			HFSPLUS_I(inode).rootflags |= HFSPLUS_FLG_APPEND;
		} else {
			inode->i_flags &= ~S_APPEND;
			HFSPLUS_I(inode).rootflags &= ~HFSPLUS_FLG_APPEND;
		}
		if (flags & EXT2_FLAG_NODUMP) /* EXT2_NODUMP_FL */
			HFSPLUS_I(inode).userflags |= HFSPLUS_FLG_NODUMP;
		else
			HFSPLUS_I(inode).userflags &= ~HFSPLUS_FLG_NODUMP;

		inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(inode);
		return 0;
	}
	default:
		return -ENOTTY;
	}
}
