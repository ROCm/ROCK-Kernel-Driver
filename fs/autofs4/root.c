/* -*- c -*- --------------------------------------------------------------- *
 *
 * linux/fs/autofs/root.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *  Copyright 1999-2000 Jeremy Fitzhardinge <jeremy@goop.org>
 *  Copyright 2001-2003 Ian Kent <raven@themaw.net>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/smp_lock.h>
#include "autofs_i.h"

static struct dentry *autofs4_dir_lookup(struct inode *,struct dentry *, struct nameidata *);
static int autofs4_dir_symlink(struct inode *,struct dentry *,const char *);
static int autofs4_dir_unlink(struct inode *,struct dentry *);
static int autofs4_dir_rmdir(struct inode *,struct dentry *);
static int autofs4_dir_mkdir(struct inode *,struct dentry *,int);
static int autofs4_root_ioctl(struct inode *, struct file *,unsigned int,unsigned long);
static int autofs4_dir_open(struct inode *inode, struct file *file);
static int autofs4_dir_close(struct inode *inode, struct file *file);
static int autofs4_dir_readdir(struct file * filp, void * dirent, filldir_t filldir);
static struct dentry *autofs4_root_lookup(struct inode *,struct dentry *, struct nameidata *);

struct file_operations autofs4_root_operations = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.read		= generic_read_dir,
	.readdir	= dcache_readdir,
	.ioctl		= autofs4_root_ioctl,
};

struct file_operations autofs4_dir_operations = {
	.open		= autofs4_dir_open,
	.release	= autofs4_dir_close,
	.read		= generic_read_dir,
	.readdir	= autofs4_dir_readdir,
};

struct inode_operations autofs4_root_inode_operations = {
	.lookup		= autofs4_root_lookup,
	.unlink		= autofs4_dir_unlink,
	.symlink	= autofs4_dir_symlink,
	.mkdir		= autofs4_dir_mkdir,
	.rmdir		= autofs4_dir_rmdir,
};

struct inode_operations autofs4_dir_inode_operations = {
	.lookup		= autofs4_dir_lookup,
	.unlink		= autofs4_dir_unlink,
	.symlink	= autofs4_dir_symlink,
	.mkdir		= autofs4_dir_mkdir,
	.rmdir		= autofs4_dir_rmdir,
};

/* Update usage from here to top of tree, so that scan of
   top-level directories will give a useful result */
static void autofs4_update_usage(struct dentry *dentry)
{
	struct dentry *top = dentry->d_sb->s_root;

	for(; dentry != top; dentry = dentry->d_parent) {
		struct autofs_info *ino = autofs4_dentry_ino(dentry);

		if (ino) {
			update_atime(dentry->d_inode);
			ino->last_used = jiffies;
		}
	}
}

/*
 * From 2.4 kernel readdir.c
 */
static int autofs4_dcache_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	int i;
	struct dentry *dentry = filp->f_dentry;

	i = filp->f_pos;
	switch (i) {
		case 0:
			if (filldir(dirent, ".", 1, i, dentry->d_inode->i_ino, DT_DIR) < 0)
				break;
			i++;
			filp->f_pos++;
			/* fallthrough */
		case 1:
			if (filldir(dirent, "..", 2, i, dentry->d_parent->d_inode->i_ino, DT_DIR) < 0)
				break;
			i++;
			filp->f_pos++;
			/* fallthrough */
		default: {
			struct list_head *list;
			int j = i-2;

			spin_lock(&dcache_lock);
			list = dentry->d_subdirs.next;

			for (;;) {
				if (list == &dentry->d_subdirs) {
					spin_unlock(&dcache_lock);
					return 0;
				}
				if (!j)
					break;
				j--;
				list = list->next;
			}

			while(1) {
				struct dentry *de = list_entry(list, struct dentry, d_child);

				if (!d_unhashed(de) && de->d_inode) {
					spin_unlock(&dcache_lock);
					if (filldir(dirent, de->d_name.name, de->d_name.len, filp->f_pos, de->d_inode->i_ino, DT_UNKNOWN) < 0)
						break;
					spin_lock(&dcache_lock);
				}
				filp->f_pos++;
				list = list->next;
				if (list != &dentry->d_subdirs)
					continue;
				spin_unlock(&dcache_lock);
				break;
			}
		}
	}
	return 0;
}

static int autofs4_dir_open(struct inode *inode, struct file *file)
{
	struct dentry *dentry = file->f_dentry;
	struct vfsmount *mnt = file->f_vfsmnt;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	int status;

	DPRINTK(("autofs4_dir_open: file=%p dentry=%p %.*s\n",
		file, dentry, dentry->d_name.len, dentry->d_name.name));

	if (autofs4_oz_mode(sbi))
		goto out;

	if (autofs4_ispending(dentry)) {
		DPRINTK(("autofs4_dir_open: dentry busy\n"));
		return -EBUSY;
	}

	if (!d_mountpoint(dentry) && dentry->d_op && dentry->d_op->d_revalidate) {
		struct nameidata nd;
		int empty;

		/* In case there are stale directory dentrys from a failed mount */
		spin_lock(&dcache_lock);
		empty = list_empty(&dentry->d_subdirs);
		spin_unlock(&dcache_lock);

		if (!empty)
			d_invalidate(dentry);

		nd.flags = LOOKUP_DIRECTORY;
		status = (dentry->d_op->d_revalidate)(dentry, &nd);

		if (!status)
			return -ENOENT;
	}

	if (d_mountpoint(dentry)) {
		struct file *fp = NULL;
		struct vfsmount *fp_mnt = mntget(mnt);
		struct dentry *fp_dentry = dget(dentry);

		while (follow_down(&fp_mnt, &fp_dentry) && d_mountpoint(fp_dentry));

		fp = dentry_open(fp_dentry, fp_mnt, file->f_flags);
		status = PTR_ERR(fp);
		if (IS_ERR(fp)) {
			file->private_data = NULL;
			return status;
		}
		file->private_data = fp;
	}
out:
	return 0;
}

static int autofs4_dir_close(struct inode *inode, struct file *file)
{
	struct dentry *dentry = file->f_dentry;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);

	DPRINTK(("autofs4_dir_close: file=%p dentry=%p %.*s\n",
		file, dentry, dentry->d_name.len, dentry->d_name.name));

	if (autofs4_oz_mode(sbi))
		goto out;

	if (autofs4_ispending(dentry)) {
		DPRINTK(("autofs4_dir_close: dentry busy\n"));
		return -EBUSY;
	}

	if (d_mountpoint(dentry)) {
		struct file *fp = file->private_data;

		if (!fp)
			return -ENOENT;

		filp_close(fp, current->files);
		file->private_data = NULL;
	}
out:
	return 0;
}

static int autofs4_dir_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = file->f_dentry;
	struct autofs_sb_info *sbi = autofs4_sbi(dentry->d_sb);
	int status;

	DPRINTK(("autofs4_readdir: file=%p dentry=%p %.*s\n",
		file, dentry, dentry->d_name.len, dentry->d_name.name));

	if (autofs4_oz_mode(sbi))
		goto out;

	if (autofs4_ispending(dentry)) {
		DPRINTK(("autofs4_readdir: dentry busy\n"));
		return -EBUSY;
	}

	if (d_mountpoint(dentry)) {
		struct file *fp = file->private_data;

		if (!fp)
			return -ENOENT;

		if (!fp->f_op || !fp->f_op->readdir)
			goto out;

		status = vfs_readdir(fp, filldir, dirent);
		file->f_pos = fp->f_pos;
		if (status)
			autofs4_copy_atime(file, fp);
		return status;
	}
out:
	return autofs4_dcache_readdir(file, dirent, filldir);
}

static int try_to_fill_dentry(struct dentry *dentry, 
			      struct super_block *sb,
			      struct autofs_sb_info *sbi, int flags)
{
	struct autofs_info *de_info = autofs4_dentry_ino(dentry);
	int status = 0;

	/* Block on any pending expiry here; invalidate the dentry
           when expiration is done to trigger mount request with a new
           dentry */
	if (de_info && (de_info->flags & AUTOFS_INF_EXPIRING)) {
		DPRINTK(("try_to_fill_entry: waiting for expire %p name=%.*s\n",
			 dentry, dentry->d_name.len, dentry->d_name.name));

		status = autofs4_wait(sbi, dentry, NFY_NONE);
		
		DPRINTK(("try_to_fill_entry: expire done status=%d\n", status));
		
		return 0;
	}

	DPRINTK(("try_to_fill_entry: dentry=%p %.*s ino=%p\n", 
		 dentry, dentry->d_name.len, dentry->d_name.name, dentry->d_inode));

	/* Wait for a pending mount, triggering one if there isn't one already */
	if (dentry->d_inode == NULL) {
		DPRINTK(("try_to_fill_entry: waiting for mount name=%.*s\n",
			 dentry->d_name.len, dentry->d_name.name));

		status = autofs4_wait(sbi, dentry, NFY_MOUNT);
		 
		DPRINTK(("try_to_fill_entry: mount done status=%d\n", status));

		if (status && dentry->d_inode)
			return 0; /* Try to get the kernel to invalidate this dentry */
		
		/* Turn this into a real negative dentry? */
		if (status == -ENOENT) {
			dentry->d_time = jiffies + AUTOFS_NEGATIVE_TIMEOUT;
			spin_lock(&dentry->d_lock);
			dentry->d_flags &= ~DCACHE_AUTOFS_PENDING;
			spin_unlock(&dentry->d_lock);
			return 1;
		} else if (status) {
			/* Return a negative dentry, but leave it "pending" */
			return 1;
		}
	/* Trigger mount for path component or follow link */
	} else if (flags & (LOOKUP_CONTINUE | LOOKUP_DIRECTORY) ||
			current->link_count) {
		DPRINTK(("try_to_fill_entry: waiting for mount name=%.*s\n",
			dentry->d_name.len, dentry->d_name.name));

		spin_lock(&dentry->d_lock);
		dentry->d_flags |= DCACHE_AUTOFS_PENDING;
		spin_unlock(&dentry->d_lock);
		status = autofs4_wait(sbi, dentry, NFY_MOUNT);

		DPRINTK(("try_to_fill_entry: mount done status=%d\n", status));

		if (status) {
			spin_lock(&dentry->d_lock);
			dentry->d_flags &= ~DCACHE_AUTOFS_PENDING;
			spin_unlock(&dentry->d_lock);
			return 0;
		}
	}

	/* We don't update the usages for the autofs daemon itself, this
	   is necessary for recursive autofs mounts */
	if (!autofs4_oz_mode(sbi))
		autofs4_update_usage(dentry);

	spin_lock(&dentry->d_lock);
	dentry->d_flags &= ~DCACHE_AUTOFS_PENDING;
	spin_unlock(&dentry->d_lock);
	return 1;
}

/*
 * Revalidate is called on every cache lookup.  Some of those
 * cache lookups may actually happen while the dentry is not
 * yet completely filled in, and revalidate has to delay such
 * lookups..
 */
static int autofs4_revalidate(struct dentry * dentry, struct nameidata *nd)
{
	struct inode * dir = dentry->d_parent->d_inode;
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	int oz_mode = autofs4_oz_mode(sbi);
	int flags = nd ? nd->flags : 0;
	int status = 1;

	/* Pending dentry */
	if (autofs4_ispending(dentry)) {
		if (!oz_mode)
			status = try_to_fill_dentry(dentry, dir->i_sb, sbi, flags);
		return status;
	}

	/* Negative dentry.. invalidate if "old" */
	if (dentry->d_inode == NULL)
		return (dentry->d_time - jiffies <= AUTOFS_NEGATIVE_TIMEOUT);

	/* Check for a non-mountpoint directory with no contents */
	spin_lock(&dcache_lock);
	if (S_ISDIR(dentry->d_inode->i_mode) &&
	    !d_mountpoint(dentry) && 
	    list_empty(&dentry->d_subdirs)) {
		DPRINTK(("autofs4_root_revalidate: dentry=%p %.*s, emptydir\n",
			 dentry, dentry->d_name.len, dentry->d_name.name));
		spin_unlock(&dcache_lock);
		if (!oz_mode)
			status = try_to_fill_dentry(dentry, dir->i_sb, sbi, flags);
		return status;
	}
	spin_unlock(&dcache_lock);

	/* Update the usage list */
	if (!oz_mode)
		autofs4_update_usage(dentry);

	return 1;
}

static void autofs4_dentry_release(struct dentry *de)
{
	struct autofs_info *inf;

	DPRINTK(("autofs4_dentry_release: releasing %p\n", de));

	inf = autofs4_dentry_ino(de);
	de->d_fsdata = NULL;

	if (inf) {
		inf->dentry = NULL;
		inf->inode = NULL;

		autofs4_free_ino(inf);
	}
}

/* For dentries of directories in the root dir */
static struct dentry_operations autofs4_root_dentry_operations = {
	.d_revalidate	= autofs4_revalidate,
	.d_release	= autofs4_dentry_release,
};

/* For other dentries */
static struct dentry_operations autofs4_dentry_operations = {
	.d_revalidate	= autofs4_revalidate,
	.d_release	= autofs4_dentry_release,
};

/* Lookups in non-root dirs never find anything - if it's there, it's
   already in the dcache */
static struct dentry *autofs4_dir_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
#if 0
	DPRINTK(("autofs4_dir_lookup: iignoring lookup of %.*s/%.*s\n",
		 dentry->d_parent->d_name.len, dentry->d_parent->d_name.name,
		 dentry->d_name.len, dentry->d_name.name));
#endif

	dentry->d_fsdata = NULL;
	d_add(dentry, NULL);
	return NULL;
}

/* Lookups in the root directory */
static struct dentry *autofs4_root_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct autofs_sb_info *sbi;
	int oz_mode;

	DPRINTK(("autofs4_root_lookup: name = %.*s\n",
		 dentry->d_name.len, dentry->d_name.name));

	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);/* File name too long to exist */

	sbi = autofs4_sbi(dir->i_sb);

	oz_mode = autofs4_oz_mode(sbi);
	DPRINTK(("autofs4_lookup: pid = %u, pgrp = %u, catatonic = %d, oz_mode = %d\n",
		 current->pid, process_group(current), sbi->catatonic, oz_mode));

	/*
	 * Mark the dentry incomplete, but add it. This is needed so
	 * that the VFS layer knows about the dentry, and we can count
	 * on catching any lookups through the revalidate.
	 *
	 * Let all the hard work be done by the revalidate function that
	 * needs to be able to do this anyway..
	 *
	 * We need to do this before we release the directory semaphore.
	 */
	dentry->d_op = &autofs4_root_dentry_operations;

	if (!oz_mode) {
		spin_lock(&dentry->d_lock);
		dentry->d_flags |= DCACHE_AUTOFS_PENDING;
		spin_unlock(&dentry->d_lock);
	}
	dentry->d_fsdata = NULL;
	d_add(dentry, NULL);

	if (dentry->d_op && dentry->d_op->d_revalidate) {
		up(&dir->i_sem);
		(dentry->d_op->d_revalidate)(dentry, nd);
		down(&dir->i_sem);
	}

	/*
	 * If we are still pending, check if we had to handle
	 * a signal. If so we can force a restart..
	 */
	if (dentry->d_flags & DCACHE_AUTOFS_PENDING) {
		/* See if we were interrupted */
		if (signal_pending(current)) {
			sigset_t *sigset = &current->pending.signal;
			if (sigismember (sigset, SIGKILL) ||
			    sigismember (sigset, SIGQUIT) ||
			    sigismember (sigset, SIGINT)) {
			    return ERR_PTR(-ERESTARTNOINTR);
			}
		}
	}

	/*
	 * If this dentry is unhashed, then we shouldn't honour this
	 * lookup even if the dentry is positive.  Returning ENOENT here
	 * doesn't do the right thing for all system calls, but it should
	 * be OK for the operations we permit from an autofs.
	 */
	if ( dentry->d_inode && d_unhashed(dentry) )
		return ERR_PTR(-ENOENT);

	return NULL;
}

static int autofs4_dir_symlink(struct inode *dir, 
			       struct dentry *dentry,
			       const char *symname)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct inode *inode;
	char *cp;

	DPRINTK(("autofs4_dir_symlink: %s <- %.*s\n", symname,
		 dentry->d_name.len, dentry->d_name.name));

	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	ino = autofs4_init_ino(ino, sbi, S_IFLNK | 0555);
	if (ino == NULL)
		return -ENOSPC;

	ino->size = strlen(symname);
	ino->u.symlink = cp = kmalloc(ino->size + 1, GFP_KERNEL);

	if (cp == NULL) {
		kfree(ino);
		return -ENOSPC;
	}

	strcpy(cp, symname);

	inode = autofs4_get_inode(dir->i_sb, ino);
	d_instantiate(dentry, inode);

	if (dir == dir->i_sb->s_root->d_inode)
		dentry->d_op = &autofs4_root_dentry_operations;
	else
		dentry->d_op = &autofs4_dentry_operations;

	dentry->d_fsdata = ino;
	ino->dentry = dget(dentry);
	ino->inode = inode;

	dir->i_mtime = CURRENT_TIME;

	return 0;
}

/*
 * NOTE!
 *
 * Normal filesystems would do a "d_delete()" to tell the VFS dcache
 * that the file no longer exists. However, doing that means that the
 * VFS layer can turn the dentry into a negative dentry.  We don't want
 * this, because since the unlink is probably the result of an expire.
 * We simply d_drop it, which allows the dentry lookup to remount it
 * if necessary.
 *
 * If a process is blocked on the dentry waiting for the expire to finish,
 * it will invalidate the dentry and try to mount with a new one.
 *
 * Also see autofs4_dir_rmdir()..
 */
static int autofs4_dir_unlink(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	
	/* This allows root to remove symlinks */
	if ( !autofs4_oz_mode(sbi) && !capable(CAP_SYS_ADMIN) )
		return -EACCES;

	dput(ino->dentry);

	dentry->d_inode->i_size = 0;
	dentry->d_inode->i_nlink = 0;

	dir->i_mtime = CURRENT_TIME;

	d_drop(dentry);

	return 0;
}

static int autofs4_dir_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	
	if (!autofs4_oz_mode(sbi))
		return -EACCES;

	spin_lock(&dcache_lock);
	if (!list_empty(&dentry->d_subdirs)) {
		spin_unlock(&dcache_lock);
		return -ENOTEMPTY;
	}
	__d_drop(dentry);
	spin_unlock(&dcache_lock);

	dput(ino->dentry);

	dentry->d_inode->i_size = 0;
	dentry->d_inode->i_nlink = 0;

	if (dir->i_nlink)
		dir->i_nlink--;

	return 0;
}

static int autofs4_dir_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct autofs_sb_info *sbi = autofs4_sbi(dir->i_sb);
	struct autofs_info *ino = autofs4_dentry_ino(dentry);
	struct inode *inode;

	if ( !autofs4_oz_mode(sbi) )
		return -EACCES;

	DPRINTK(("autofs4_dir_mkdir: dentry %p, creating %.*s\n",
		 dentry, dentry->d_name.len, dentry->d_name.name));

	ino = autofs4_init_ino(ino, sbi, S_IFDIR | 0555);
	if (ino == NULL)
		return -ENOSPC;

	inode = autofs4_get_inode(dir->i_sb, ino);
	d_instantiate(dentry, inode);

	if (dir == dir->i_sb->s_root->d_inode)
		dentry->d_op = &autofs4_root_dentry_operations;
	else
		dentry->d_op = &autofs4_dentry_operations;

	dentry->d_fsdata = ino;
	ino->dentry = dget(dentry);
	ino->inode = inode;
	dir->i_nlink++;
	dir->i_mtime = CURRENT_TIME;

	return 0;
}

/* Get/set timeout ioctl() operation */
static inline int autofs4_get_set_timeout(struct autofs_sb_info *sbi,
					 unsigned long *p)
{
	int rv;
	unsigned long ntimeout;

	if ( (rv = get_user(ntimeout, p)) ||
	     (rv = put_user(sbi->exp_timeout/HZ, p)) )
		return rv;

	if ( ntimeout > ULONG_MAX/HZ )
		sbi->exp_timeout = 0;
	else
		sbi->exp_timeout = ntimeout * HZ;

	return 0;
}

/* Return protocol version */
static inline int autofs4_get_protover(struct autofs_sb_info *sbi, int *p)
{
	return put_user(sbi->version, p);
}

/* Return protocol sub version */
static inline int autofs4_get_protosubver(struct autofs_sb_info *sbi, int *p)
{
	return put_user(sbi->sub_version, p);
}

/*
* Tells the daemon whether it can umount the autofs mount.
*/
static inline int autofs4_ask_umount(struct vfsmount *mnt, int *p)
{
	int status = 0;

	if (may_umount(mnt) == 0)
		status = 1;

	DPRINTK(("autofs_ask_umount: returning %d\n", status));

	status = put_user(status, p);

	return status;
}

/* Identify autofs4_dentries - this is so we can tell if there's
   an extra dentry refcount or not.  We only hold a refcount on the
   dentry if its non-negative (ie, d_inode != NULL)
*/
int is_autofs4_dentry(struct dentry *dentry)
{
	return dentry && dentry->d_inode &&
		(dentry->d_op == &autofs4_root_dentry_operations ||
		 dentry->d_op == &autofs4_dentry_operations) &&
		dentry->d_fsdata != NULL;
}

/*
 * ioctl()'s on the root directory is the chief method for the daemon to
 * generate kernel reactions
 */
static int autofs4_root_ioctl(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg)
{
	struct autofs_sb_info *sbi = autofs4_sbi(inode->i_sb);

	DPRINTK(("autofs4_root_ioctl: cmd = 0x%08x, arg = 0x%08lx, sbi = %p, pgrp = %u\n",
		 cmd,arg,sbi,process_group(current)));

	if ( _IOC_TYPE(cmd) != _IOC_TYPE(AUTOFS_IOC_FIRST) ||
	     _IOC_NR(cmd) - _IOC_NR(AUTOFS_IOC_FIRST) >= AUTOFS_IOC_COUNT )
		return -ENOTTY;
	
	if ( !autofs4_oz_mode(sbi) && !capable(CAP_SYS_ADMIN) )
		return -EPERM;
	
	switch(cmd) {
	case AUTOFS_IOC_READY:	/* Wait queue: go ahead and retry */
		return autofs4_wait_release(sbi,(autofs_wqt_t)arg,0);
	case AUTOFS_IOC_FAIL:	/* Wait queue: fail with ENOENT */
		return autofs4_wait_release(sbi,(autofs_wqt_t)arg,-ENOENT);
	case AUTOFS_IOC_CATATONIC: /* Enter catatonic mode (daemon shutdown) */
		autofs4_catatonic_mode(sbi);
		return 0;
	case AUTOFS_IOC_PROTOVER: /* Get protocol version */
		return autofs4_get_protover(sbi, (int *)arg);
	case AUTOFS_IOC_PROTOSUBVER: /* Get protocol sub version */
		return autofs4_get_protosubver(sbi, (int *)arg);
	case AUTOFS_IOC_SETTIMEOUT:
		return autofs4_get_set_timeout(sbi,(unsigned long *)arg);

	case AUTOFS_IOC_ASKUMOUNT:
		return autofs4_ask_umount(filp->f_vfsmnt, (int *) arg);

	/* return a single thing to expire */
	case AUTOFS_IOC_EXPIRE:
		return autofs4_expire_run(inode->i_sb,filp->f_vfsmnt,sbi,
					  (struct autofs_packet_expire *)arg);
	/* same as above, but can send multiple expires through pipe */
	case AUTOFS_IOC_EXPIRE_MULTI:
		return autofs4_expire_multi(inode->i_sb,filp->f_vfsmnt,sbi,
					    (int *)arg);

	default:
		return -ENOSYS;
	}
}
