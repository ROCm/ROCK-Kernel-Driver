/*
 *  linux/fs/xip2fs/namei.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 *            Gerald Schaefer <geraldsc@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */

#include <linux/pagemap.h>
#include "xip2.h"
#include "xattr.h"
#include "acl.h"

/*
 * Methods themselves.
 */

static struct dentry
*xip2_lookup(struct inode * dir, struct dentry *dentry, struct nameidata *nd)
{
	struct inode * inode;
	ino_t ino;

	if (dentry->d_name.len > EXT2_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ino = xip2_inode_by_name(dir, dentry);
	inode = NULL;
	if (ino) {
		inode = iget(dir->i_sb, ino);
		if (!inode)
			return ERR_PTR(-EACCES);
	}
	if (inode)
		return d_splice_alias(inode, dentry);
	d_add(dentry, inode);
	return NULL;
}

struct dentry *xip2_get_parent(struct dentry *child)
{
	unsigned long ino;
	struct dentry *parent;
	struct inode *inode;
	struct dentry dotdot;

	dotdot.d_name.name = "..";
	dotdot.d_name.len = 2;

	ino = xip2_inode_by_name(child->d_inode, &dotdot);
	if (!ino)
		return ERR_PTR(-ENOENT);
	inode = iget(child->d_inode->i_sb, ino);

	if (!inode)
		return ERR_PTR(-EACCES);
	parent = d_alloc_anon(inode);
	if (!parent) {
		iput(inode);
		parent = ERR_PTR(-ENOMEM);
	}
	return parent;
}

struct inode_operations xip2_dir_inode_operations = {
	.lookup		= xip2_lookup,
	.getxattr	= xip2_getxattr,
	.listxattr	= xip2_listxattr,
	.permission	= xip2_permission,
};

struct inode_operations xip2_special_inode_operations = {
	.getxattr	= xip2_getxattr,
	.listxattr	= xip2_listxattr,
	.permission	= xip2_permission,
};
