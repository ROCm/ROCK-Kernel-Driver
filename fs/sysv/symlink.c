/*
 *  linux/fs/sysv/symlink.c
 *
 *  Handling of System V filesystem fast symlinks extensions.
 *  Aug 2001, Christoph Hellwig (hch@infradead.org)
 */

#include "sysv.h"

static int sysv_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	char *s = (char *)SYSV_I(dentry->d_inode)->i_data;
	return vfs_readlink(dentry, buffer, buflen, s);
}

static int sysv_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char *s = (char *)SYSV_I(dentry->d_inode)->i_data;
	return vfs_follow_link(nd, s);
}

struct inode_operations sysv_fast_symlink_inode_operations = {
	.readlink	= sysv_readlink,
	.follow_link	= sysv_follow_link,
};
