/* internal.h: internal AFS stuff
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef AFS_INTERNAL_H
#define AFS_INTERNAL_H

#include <linux/version.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

/*
 * debug tracing
 */
#define kenter(FMT,...)	printk("==> %s("FMT")\n",__FUNCTION__,##__VA_ARGS__)
#define kleave(FMT,...)	printk("<== %s()"FMT"\n",__FUNCTION__,##__VA_ARGS__)
#define kdebug(FMT,...)	printk(FMT"\n",##__VA_ARGS__)
#define kproto(FMT,...)	printk("### "FMT"\n",##__VA_ARGS__)
#define knet(FMT,...)	printk(FMT"\n",##__VA_ARGS__)

#if 0
#define _enter(FMT,...)	kenter(FMT,##__VA_ARGS__)
#define _leave(FMT,...)	kleave(FMT,##__VA_ARGS__)
#define _debug(FMT,...)	kdebug(FMT,##__VA_ARGS__)
#define _proto(FMT,...)	kproto(FMT,##__VA_ARGS__)
#define _net(FMT,...)	knet(FMT,##__VA_ARGS__)
#else
#define _enter(FMT,...)	do { } while(0)
#define _leave(FMT,...)	do { } while(0)
#define _debug(FMT,...)	do { } while(0)
#define _proto(FMT,...)	do { } while(0)
#define _net(FMT,...)	do { } while(0)
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,0)
#define wait_on_page_locked wait_on_page
#define PageUptodate Page_Uptodate

static inline struct proc_dir_entry *PDE(const struct inode *inode)
{
	return (struct proc_dir_entry *)inode->u.generic_ip;
}
#endif

static inline void afs_discard_my_signals(void)
{
	while (signal_pending(current)) {
		siginfo_t sinfo;

		spin_lock_irq(&current->sig->siglock);
		dequeue_signal(&current->blocked,&sinfo);
		spin_unlock_irq(&current->sig->siglock);
	}
}

/*
 * cell.c
 */
extern struct rw_semaphore afs_proc_cells_sem;
extern struct list_head afs_proc_cells;

/*
 * dir.c
 */
extern struct inode_operations afs_dir_inode_operations;
extern struct file_operations afs_dir_file_operations;

/*
 * file.c
 */
extern struct address_space_operations afs_fs_aops;
extern struct inode_operations afs_file_inode_operations;
extern struct file_operations afs_file_file_operations;

/*
 * inode.c
 */
extern int afs_iget(struct super_block *sb, afs_fid_t *fid, struct inode **_inode);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
extern int afs_inode_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat);
#else
extern void afs_read_inode2(struct inode *inode, void *opaque);
extern int afs_inode_revalidate(struct dentry *dentry);
#endif
extern void afs_clear_inode(struct inode *inode);

/*
 * mntpt.c
 */
extern struct inode_operations afs_mntpt_inode_operations;
extern struct file_operations afs_mntpt_file_operations;

extern int afs_mntpt_check_symlink(afs_vnode_t *vnode);

/*
 * super.c
 */
extern int afs_fs_init(void);
extern void afs_fs_exit(void);

#define AFS_CB_HASH_COUNT (PAGE_SIZE/sizeof(struct list_head))

extern struct list_head afs_cb_hash_tbl[];
extern spinlock_t afs_cb_hash_lock;

#define afs_cb_hash(SRV,FID) \
	afs_cb_hash_tbl[((unsigned)(SRV) + (FID)->vid + (FID)->vnode + (FID)->unique) % \
			AFS_CB_HASH_COUNT]

/*
 * proc.c
 */
extern int afs_proc_init(void);
extern void afs_proc_cleanup(void);
extern int afs_proc_cell_setup(afs_cell_t *cell);
extern void afs_proc_cell_remove(afs_cell_t *cell);

#endif /* AFS_INTERNAL_H */
