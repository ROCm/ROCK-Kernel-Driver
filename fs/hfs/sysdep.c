/*
 * linux/fs/hfs/sysdep.c
 *
 * Copyright (C) 1996  Paul H. Hargrove
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains the code to do various system dependent things.
 *
 * "XXX" in a comment is a note to myself to consider changing something.
 *
 * In function preconditions the term "valid" applied to a pointer to
 * a structure means that the pointer is non-NULL and the structure it
 * points to has all fields initialized to consistent values.
 */

#include "hfs_fs.h"

/* dentry case-handling: just lowercase everything */

static int hfs_revalidate_dentry(struct dentry *dentry,
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
		struct nameidata *nd
#else
		int flags
#endif
		)
{
	struct inode *inode = dentry->d_inode;
	int diff;

	if(!inode)
		return 1;

	/* fix up inode on a timezone change */
	diff = sys_tz.tz_minuteswest * 60 - HFS_I(inode)->tz_secondswest;
	if (diff) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
		inode->i_ctime.tv_sec += diff;
		inode->i_atime.tv_sec += diff;
		inode->i_mtime.tv_sec += diff;
#else
		inode->i_ctime += diff;
		inode->i_atime += diff;
		inode->i_mtime += diff;
#endif
		HFS_I(inode)->tz_secondswest += diff;
	}
	return 1;
}

struct dentry_operations hfs_dentry_operations =
{
	.d_revalidate	= hfs_revalidate_dentry,
	.d_hash		= hfs_hash_dentry,
	.d_compare	= hfs_compare_dentry,
};

