/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: symlink.c,v 1.12 2003/10/04 08:33:07 dwmw2 Exp $
 *
 */


#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include "nodelist.h"

int jffs2_readlink(struct dentry *dentry, char *buffer, int buflen);
int jffs2_follow_link(struct dentry *dentry, struct nameidata *nd);

struct inode_operations jffs2_symlink_inode_operations =
{	
	.readlink =	jffs2_readlink,
	.follow_link =	jffs2_follow_link,
	.setattr =	jffs2_setattr
};

int jffs2_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	unsigned char *kbuf;
	int ret;

	kbuf = jffs2_getlink(JFFS2_SB_INFO(dentry->d_inode->i_sb), JFFS2_INODE_INFO(dentry->d_inode));
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	ret = vfs_readlink(dentry, buffer, buflen, kbuf);
	kfree(kbuf);
	return ret;
}

int jffs2_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	unsigned char *buf;
	int ret;

	buf = jffs2_getlink(JFFS2_SB_INFO(dentry->d_inode->i_sb), JFFS2_INODE_INFO(dentry->d_inode));

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = vfs_follow_link(nd, buf);
	kfree(buf);
	return ret;
}
