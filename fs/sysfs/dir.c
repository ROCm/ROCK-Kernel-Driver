/*
 * dir.c - Operations for sysfs directories.
 */

#undef DEBUG

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include "sysfs.h"

static int init_dir(struct inode * inode)
{
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;

	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inode->i_nlink++;
	return 0;
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
		error = sysfs_create(dentry,(S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO),
				     init_dir);
		if (!error)
			parent->d_inode->i_nlink++;
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
	struct list_head * node;
	struct dentry * dentry = dget(kobj->dentry);
	struct dentry * parent;

	if (!dentry)
		return;

	pr_debug("sysfs %s: removing dir\n",dentry->d_name.name);
	parent = dget(dentry->d_parent);
	down(&parent->d_inode->i_sem);
	down(&dentry->d_inode->i_sem);

	spin_lock(&dcache_lock);
	node = dentry->d_subdirs.next;
	while (node != &dentry->d_subdirs) {
		struct dentry * d = list_entry(node,struct dentry,d_child);
		list_del_init(node);

		pr_debug(" o %s (%d): ",d->d_name.name,atomic_read(&d->d_count));
		if (d->d_inode) {
			d = dget_locked(d);
			pr_debug("removing");

			/**
			 * Unlink and unhash.
			 */
			spin_unlock(&dcache_lock);
			simple_unlink(dentry->d_inode,d);
			dput(d);
			spin_lock(&dcache_lock);
		}
		pr_debug(" done\n");
		node = dentry->d_subdirs.next;
	}
	spin_unlock(&dcache_lock);
	up(&dentry->d_inode->i_sem);
	d_delete(dentry);
	simple_rmdir(parent->d_inode,dentry);

	pr_debug(" o %s removing done (%d)\n",dentry->d_name.name,
		 atomic_read(&dentry->d_count));

	/**
	 * Drop reference from dget() on entrance.
	 */
	dput(dentry);
	up(&parent->d_inode->i_sem);
	dput(parent);
}

void sysfs_rename_dir(struct kobject * kobj, char *new_name)
{
	struct dentry * new_dentry, * parent;

	if (!strcmp(kobj->name, new_name))
		return;

	if (!kobj->parent)
		return;

	parent = kobj->parent->dentry;

	down(&parent->d_inode->i_sem);

	new_dentry = sysfs_get_dentry(parent, new_name);
	d_move(kobj->dentry, new_dentry);

	strlcpy(kobj->name, new_name, KOBJ_NAME_LEN);

	up(&parent->d_inode->i_sem);	
}

EXPORT_SYMBOL(sysfs_create_dir);
EXPORT_SYMBOL(sysfs_remove_dir);
EXPORT_SYMBOL(sysfs_rename_dir);

