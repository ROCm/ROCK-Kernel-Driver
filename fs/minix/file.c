/*
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  minix regular file handling primitives
 */

#include <linux/fs.h>
#include <linux/minix_fs.h>

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the minix filesystem.
 */
static int minix_sync_file(struct file *, struct dentry *, int);

struct file_operations minix_file_operations = {
	read:		generic_file_read,
	write:		generic_file_write,
	mmap:		generic_file_mmap,
	fsync:		minix_sync_file,
};

struct inode_operations minix_file_inode_operations = {
	truncate:	minix_truncate,
};

static int minix_sync_file(struct file * file,
			   struct dentry *dentry,
			   int datasync)
{
	struct inode *inode = dentry->d_inode;

	if (INODE_VERSION(inode) == MINIX_V1)
		return V1_minix_sync_file(inode);
	else
		return V2_minix_sync_file(inode);
}
