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
#define kenter(FMT, a...)	printk("==> %s("FMT")\n",__FUNCTION__ , ## a)
#define kleave(FMT, a...)	printk("<== %s()"FMT"\n",__FUNCTION__ , ## a)
#define kdebug(FMT, a...)	printk(FMT"\n" , ## a)
#define kproto(FMT, a...)	printk("### "FMT"\n" , ## a)
#define knet(FMT, a...)		printk(FMT"\n" , ## a)

#if 0
#define _enter(FMT, a...)	kenter(FMT , ## a)
#define _leave(FMT, a...)	kleave(FMT , ## a)
#define _debug(FMT, a...)	kdebug(FMT , ## a)
#define _proto(FMT, a...)	kproto(FMT , ## a)
#define _net(FMT, a...)		knet(FMT , ## a)
#else
#define _enter(FMT, a...)	do { } while(0)
#define _leave(FMT, a...)	do { } while(0)
#define _debug(FMT, a...)	do { } while(0)
#define _proto(FMT, a...)	do { } while(0)
#define _net(FMT, a...)		do { } while(0)
#endif

static inline void afs_discard_my_signals(void)
{
	while (signal_pending(current)) {
		siginfo_t sinfo;

		spin_lock_irq(&current->sighand->siglock);
		dequeue_signal(current,&current->blocked,&sinfo);
		spin_unlock_irq(&current->sighand->siglock);
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
extern int afs_inode_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat);
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
	afs_cb_hash_tbl[((unsigned long)(SRV) + (FID)->vid + (FID)->vnode + (FID)->unique) % \
			AFS_CB_HASH_COUNT]

/*
 * proc.c
 */
extern int afs_proc_init(void);
extern void afs_proc_cleanup(void);
extern int afs_proc_cell_setup(afs_cell_t *cell);
extern void afs_proc_cell_remove(afs_cell_t *cell);

#endif /* AFS_INTERNAL_H */
