/* mntpt.c: mountpoint management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include "volume.h"
#include "vnode.h"
#include "internal.h"


static struct dentry *afs_mntpt_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *);
static int afs_mntpt_open(struct inode *inode, struct file *file);

struct file_operations afs_mntpt_file_operations = {
	.open		= afs_mntpt_open,
};

struct inode_operations afs_mntpt_inode_operations = {
	.lookup		= afs_mntpt_lookup,
	.readlink	= page_readlink,
	.getattr	= afs_inode_getattr,
};

/*****************************************************************************/
/*
 * check a symbolic link to see whether it actually encodes a mountpoint
 * - sets the AFS_VNODE_MOUNTPOINT flag on the vnode appropriately
 */
int afs_mntpt_check_symlink(afs_vnode_t *vnode)
{
	struct page *page;
	size_t size;
	char *buf;
	int ret;

	_enter("{%u,%u}",vnode->fid.vnode,vnode->fid.unique);

	/* read the contents of the symlink into the pagecache */
	page = read_cache_page(AFS_VNODE_TO_I(vnode)->i_mapping,0,
			       (filler_t*)AFS_VNODE_TO_I(vnode)->i_mapping->a_ops->readpage,NULL);
	if (IS_ERR(page)) {
		ret = PTR_ERR(page);
		goto out;
	}

	ret = -EIO;
	wait_on_page_locked(page);
	buf = kmap(page);
	if (!PageUptodate(page))
		goto out_free;
	if (PageError(page))
		goto out_free;

	/* examine the symlink's contents */
	size = vnode->status.size;
	_debug("symlink to %*.*s",size,(int)size,buf);

	if (size>2 &&
	    (buf[0]=='%' || buf[0]=='#') &&
	    buf[size-1]=='.'
	    ) {
		_debug("symlink is a mountpoint");
		spin_lock(&vnode->lock);
		vnode->flags |= AFS_VNODE_MOUNTPOINT;
		spin_unlock(&vnode->lock);
	}

	ret = 0;

 out_free:
	kunmap(page);
	page_cache_release(page);
 out:
	_leave(" = %d",ret);
	return ret;

} /* end afs_mntpt_check_symlink() */

/*****************************************************************************/
/*
 * no valid lookup procedure on this sort of dir
 */
static struct dentry *afs_mntpt_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	return ERR_PTR(-EREMOTE);
} /* end afs_mntpt_lookup() */

/*****************************************************************************/
/*
 * no valid open procedure on this sort of dir
 */
static int afs_mntpt_open(struct inode *inode, struct file *file)
{
	return -EREMOTE;
} /* end afs_mntpt_open() */
