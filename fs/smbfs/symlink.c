/*
 *  symlink.c
 *
 *  Copyright (C) 2002 by John Newbigin
 *
 *  Please add a note about your changes to smbfs in the ChangeLog file.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/net.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/smbno.h>
#include <linux/smb_fs.h>

#include "smb_debug.h"
#include "proto.h"

int smb_read_link(struct dentry *dentry, char *buffer, int len)
{
	char link[256];		/* FIXME: pain ... */
	int r;
	DEBUG1("read link buffer len = %d\n", len);

	r = smb_proc_read_link(server_from_dentry(dentry), dentry, link,
			       sizeof(link) - 1);
	if (r < 0)
		return -ENOENT;
	return vfs_readlink(dentry, buffer, len, link);
}

int smb_symlink(struct inode *inode, struct dentry *dentry, const char *oldname)
{
	DEBUG1("create symlink %s -> %s/%s\n", oldname, DENTRY_PATH(dentry));

	return smb_proc_symlink(server_from_dentry(dentry), dentry, oldname);
}

int smb_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char link[256];		/* FIXME: pain ... */
	int len;
	DEBUG1("followlink of %s/%s\n", DENTRY_PATH(dentry));

	len = smb_proc_read_link(server_from_dentry(dentry), dentry, link,
				 sizeof(link) - 1);
	if(len < 0)
		return -ENOENT;

	link[len] = 0;
	return vfs_follow_link(nd, link);
}


struct inode_operations smb_link_inode_operations =
{
	.readlink	= smb_read_link,
	.follow_link	= smb_follow_link,
};
