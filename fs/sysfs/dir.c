/*
 * dir.c - Operations for sysfs directories.
 */

#undef DEBUG

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include "sysfs.h"

static int sysfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int res;
	mode = (mode & (S_IRWXUGO|S_ISVTX)) | S_IFDIR;
 	res = sysfs_mknod(dir, dentry, mode, 0);
 	if (!res)
 		dir->i_nlink++;
	return res;
}


/**
 *	sysfs_create_dir - create a directory for an object.
 *	@parent:	parent parent object.
 *	@kobj:		object we're creating directory for. 
 */

int sysfs_create_dir(struct kobject * kobj)
{
	struct dentry * dentry = NULL;
	struct dentry * parent;
	int error = 0;

	if (!kobj)
		return -EINVAL;

	if (kobj->parent)
		parent = kobj->parent->dentry;
	else if (sysfs_mount && sysfs_mount->mnt_sb)
		parent = sysfs_mount->mnt_sb->s_root;
	else
		return -EFAULT;

	down(&parent->d_inode->i_sem);
	dentry = sysfs_get_dentry(parent,kobj->name);
	if (!IS_ERR(dentry)) {
		dentry->d_fsdata = (void *)kobj;
		kobj->dentry = dentry;
		error = sysfs_mkdir(parent->d_inode,dentry,
				    (S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO));
	} else
		error = PTR_ERR(dentry);
	up(&parent->d_inode->i_sem);

	return error;
}



/**
 *	sysfs_remove_dir - remove an object's directory.
 *	@kobj:	object. 
 *
 *	The only thing special about this is that we remove any files in 
 *	the directory before we remove the directory, and we've inlined
 *	what used to be sysfs_rmdir() below, instead of calling separately.
 */

void sysfs_remove_dir(struct kobject * kobj)
{
	struct list_head * node, * next;
	struct dentry * dentry = dget(kobj->dentry);
	struct dentry * parent;

	if (!dentry)
		return;

	pr_debug("sysfs %s: removing dir\n",dentry->d_name.name);
	parent = dget(dentry->d_parent);
	down(&parent->d_inode->i_sem);
	down(&dentry->d_inode->i_sem);

	list_for_each_safe(node,next,&dentry->d_subdirs) {
		struct dentry * d = dget(list_entry(node,struct dentry,d_child));
		/** 
		 * Make sure dentry is still there 
		 */
		pr_debug(" o %s: ",d->d_name.name);
		if (d->d_inode) {

			pr_debug("removing");
			/**
			 * Unlink and unhash.
			 */
			simple_unlink(dentry->d_inode,d);
			d_delete(d);

			/**
			 * Drop reference from initial sysfs_get_dentry().
			 */
			dput(d);
		}
		pr_debug(" done (%d)\n",atomic_read(&d->d_count));
		/**
		 * drop reference from dget() above.
		 */
		dput(d);
	}

	up(&dentry->d_inode->i_sem);
	d_invalidate(dentry);
	simple_rmdir(parent->d_inode,dentry);
	d_delete(dentry);

	pr_debug(" o %s removing done (%d)\n",dentry->d_name.name,
		 atomic_read(&dentry->d_count));
	/**
	 * Drop reference from initial sysfs_get_dentry().
	 */
	dput(dentry);

	/**
	 * Drop reference from dget() on entrance.
	 */
	dput(dentry);
	up(&parent->d_inode->i_sem);
	dput(parent);
}


EXPORT_SYMBOL(sysfs_create_dir);
EXPORT_SYMBOL(sysfs_remove_dir);

