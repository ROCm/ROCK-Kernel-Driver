/* vnode.h: AFS vnode record
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_VNODE_H
#define _LINUX_AFS_VNODE_H

#include <linux/fs.h>
#include <linux/version.h>
#include "server.h"
#include "kafstimod.h"

#ifdef __KERNEL__

struct afs_rxfs_fetch_descriptor;

/*****************************************************************************/
/*
 * AFS inode private data
 */
struct afs_vnode
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
	struct inode		vfs_inode;	/* the VFS's inode record */
#else
	struct inode		*inode;		/* the VFS's inode */
#endif

	afs_volume_t		*volume;	/* volume on which vnode resides */
	afs_fid_t		fid;		/* the file identifier for this inode */
	afs_file_status_t	status;		/* AFS status info for this file */
	unsigned		nix;		/* vnode index in cache */

	wait_queue_head_t	update_waitq;	/* status fetch waitqueue */
	unsigned		update_cnt;	/* number of outstanding ops that will update the
						 * status */
	spinlock_t		lock;		/* waitqueue/flags lock */
	unsigned		flags;
#define AFS_VNODE_CHANGED	0x00000001	/* set if vnode reported changed by callback */
#define AFS_VNODE_DELETED	0x00000002	/* set if vnode deleted on server */
#define AFS_VNODE_MOUNTPOINT	0x00000004	/* set if vnode is a mountpoint symlink */

	/* outstanding callback notification on this file */
	afs_server_t		*cb_server;	/* server that made the current promise */
	struct list_head	cb_link;	/* link in server's promises list */
	struct list_head	cb_hash_link;	/* link in master callback hash */
	afs_timer_t		cb_timeout;	/* timeout on promise */
	unsigned		cb_version;	/* callback version */
	unsigned		cb_expiry;	/* callback expiry time */
	afs_callback_type_t	cb_type;	/* type of callback */
};

static inline afs_vnode_t *AFS_FS_I(struct inode *inode)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
	return list_entry(inode,afs_vnode_t,vfs_inode);
#else
	return inode->u.generic_ip;
#endif
}

static inline struct inode *AFS_VNODE_TO_I(afs_vnode_t *vnode)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
	return &vnode->vfs_inode;
#else
	return vnode->inode;
#endif
}

extern int afs_vnode_fetch_status(afs_vnode_t *vnode);

extern int afs_vnode_fetch_data(afs_vnode_t *vnode, struct afs_rxfs_fetch_descriptor *desc);

extern int afs_vnode_give_up_callback(afs_vnode_t *vnode);

extern struct afs_timer_ops afs_vnode_cb_timed_out_ops;

#endif /* __KERNEL__ */

#endif /* _LINUX_AFS_VNODE_H */
