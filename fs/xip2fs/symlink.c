/*
 *  linux/fs/xip2fs/symlink.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */


#include "xip2.h"
#include "xattr.h"
#include <linux/pagemap.h>

static char *xip2_getlink(struct dentry * dentry)
{
	sector_t blockno;
	char * res;

	if (xip2_get_block (dentry->d_inode, 0, &blockno, 0)) {
		xip2_error (dentry->d_inode->i_sb, "xip2_getlink",
				"cannot resolve symbolic link");
		return NULL;
	}
	res = (char*) xip2_sb_bread (dentry->d_inode->i_sb, blockno);
	if (res)
		return res;
	return (char*) empty_zero_page;
}


static int xip2_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	char *s = xip2_getlink(dentry);
	int res = vfs_readlink(dentry,buffer,buflen,s);
	return res;
}

static int xip2_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char *s = xip2_getlink(dentry);
	int res = vfs_follow_link(nd, s);
	return res;
}


static int
xip2_fast_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	struct xip2_inode_info *ei = XIP2_I(dentry->d_inode);
	return vfs_readlink(dentry, buffer, buflen, (char *)ei->i_data);
}

static int xip2_fast_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct xip2_inode_info *ei = XIP2_I(dentry->d_inode);
	return vfs_follow_link(nd, (char *)ei->i_data);
}

struct inode_operations xip2_symlink_inode_operations = {
	.readlink	= xip2_readlink,
	.follow_link	= xip2_follow_link,
	.getxattr	= xip2_getxattr,
	.listxattr	= xip2_listxattr,
};
 
struct inode_operations xip2_fast_symlink_inode_operations = {
	.readlink	= xip2_fast_readlink,
	.follow_link	= xip2_fast_follow_link,
	.getxattr	= xip2_getxattr,
	.listxattr	= xip2_listxattr,
};
