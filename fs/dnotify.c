/*
 * Directory notifications for Linux.
 *
 * Copyright (C) 2000 Stephen Rothwell
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/dnotify.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

extern void send_sigio(struct fown_struct *fown, int fd, int band);

int dir_notify_enable = 1;

static rwlock_t dn_lock = RW_LOCK_UNLOCKED;
static kmem_cache_t *dn_cache;

static void redo_inode_mask(struct inode *inode)
{
	unsigned long new_mask;
	struct dnotify_struct *dn;

	new_mask = 0;
	for (dn = inode->i_dnotify; dn != NULL; dn = dn->dn_next)
		new_mask |= dn->dn_mask & ~DN_MULTISHOT;
	inode->i_dnotify_mask = new_mask;
}

int fcntl_dirnotify(int fd, struct file *filp, unsigned long arg)
{
	struct dnotify_struct *dn = NULL;
	struct dnotify_struct *odn;
	struct dnotify_struct **prev;
	struct inode *inode;
	int turning_off = (arg & ~DN_MULTISHOT) == 0;

	if (!turning_off && !dir_notify_enable)
		return -EINVAL;
	inode = filp->f_dentry->d_inode;
	if (!S_ISDIR(inode->i_mode))
		return -ENOTDIR;
	if (!turning_off) {
		dn = kmem_cache_alloc(dn_cache, SLAB_KERNEL);
		if (dn == NULL)
			return -ENOMEM;
	}
	write_lock(&dn_lock);
	prev = &inode->i_dnotify;
	for (odn = *prev; odn != NULL; prev = &odn->dn_next, odn = *prev)
		if (odn->dn_filp == filp)
			break;
	if (odn != NULL) {
		if (turning_off) {
			*prev = odn->dn_next;
			redo_inode_mask(inode);
			dn = odn;
			goto out_free;
		}
		odn->dn_fd = fd;
		odn->dn_mask |= arg;
		inode->i_dnotify_mask |= arg & ~DN_MULTISHOT;
		goto out_free;
	}
	if (turning_off)
		goto out;
	filp->f_owner.pid = current->pid;
	filp->f_owner.uid = current->uid;
	filp->f_owner.euid = current->euid;
	dn->dn_magic = DNOTIFY_MAGIC;
	dn->dn_mask = arg;
	dn->dn_fd = fd;
	dn->dn_filp = filp;
	inode->i_dnotify_mask |= arg & ~DN_MULTISHOT;
	dn->dn_next = inode->i_dnotify;
	inode->i_dnotify = dn;
out:
	write_unlock(&dn_lock);
	return 0;
out_free:
	kmem_cache_free(dn_cache, dn);
	goto out;
}

void __inode_dir_notify(struct inode *inode, unsigned long event)
{
	struct dnotify_struct *	dn;
	struct dnotify_struct **prev;
	struct fown_struct *	fown;
	int			changed = 0;

	write_lock(&dn_lock);
	prev = &inode->i_dnotify;
	while ((dn = *prev) != NULL) {
		if (dn->dn_magic != DNOTIFY_MAGIC) {
		        printk(KERN_ERR "__inode_dir_notify: bad magic "
				"number in dnotify_struct!\n");
		        goto out;
		}
		if ((dn->dn_mask & event) == 0) {
			prev = &dn->dn_next;
			continue;
		}
		fown = &dn->dn_filp->f_owner;
		if (fown->pid)
		        send_sigio(fown, dn->dn_fd, POLL_MSG);
		if (dn->dn_mask & DN_MULTISHOT)
			prev = &dn->dn_next;
		else {
			*prev = dn->dn_next;
			changed = 1;
			kmem_cache_free(dn_cache, dn);
		}
	}
	if (changed)
		redo_inode_mask(inode);
out:
	write_unlock(&dn_lock);
}

static int __init dnotify_init(void)
{
	dn_cache = kmem_cache_create("dnotify cache",
		sizeof(struct dnotify_struct), 0, 0, NULL, NULL);
	if (!dn_cache)
		panic("cannot create dnotify slab cache");
	return 0;
}

module_init(dnotify_init)
