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


static int create_dir(struct kobject * k, struct dentry * p,
		      const char * n, struct dentry ** d)
{
	int error;

	down(&p->d_inode->i_sem);
	*d = sysfs_get_dentry(p,n);
	if (!IS_ERR(*d)) {
		error = sysfs_create(*d,
					 S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO,
					 init_dir);
		if (!error) {
			(*d)->d_fsdata = k;
			p->d_inode->i_nlink++;
		}
		dput(*d);
	} else
		error = PTR_ERR(*d);
	up(&p->d_inode->i_sem);
	return error;
}


int sysfs_create_subdir(struct kobject * k, const char * n, struct dentry ** d)
{
	return create_dir(k,k->dentry,n,d);
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

	error = create_dir(kobj,parent,kobject_name(kobj),&dentry);
	if (!error)
		kobj->dentry = dentry;
	return error;
}


static void remove_dir(struct dentry * d)
{
	struct dentry * parent = dget(d->d_parent);
	down(&parent->d_inode->i_sem);
	d_delete(d);
	if (d->d_inode)
		simple_rmdir(parent->d_inode,d);

	pr_debug(" o %s removing done (%d)\n",d->d_name.name,
		 atomic_read(&d->d_count));

	up(&parent->d_inode->i_sem);
	dput(parent);
}

void sysfs_remove_subdir(struct dentry * d)
{
	remove_dir(d);
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

	if (!dentry)
		return;

	pr_debug("sysfs %s: removing dir\n",dentry->d_name.name);
	down(&dentry->d_inode->i_sem);

	spin_lock(&dcache_lock);
restart:
	node = dentry->d_subdirs.next;
	while (node != &dentry->d_subdirs) {
		struct dentry * d = list_entry(node,struct dentry,d_child);

		node = node->next;
		pr_debug(" o %s (%d): ",d->d_name.name,atomic_read(&d->d_count));
		if (!d_unhashed(d) && (d->d_inode)) {
			d = dget_locked(d);
			pr_debug("removing");

			/**
			 * Unlink and unhash.
			 */
			spin_unlock(&dcache_lock);
			d_delete(d);
			simple_unlink(dentry->d_inode,d);
			dput(d);
			pr_debug(" done\n");
			spin_lock(&dcache_lock);
			/* re-acquired dcache_lock, need to restart */
			goto restart;
		}
	}
	spin_unlock(&dcache_lock);
	up(&dentry->d_inode->i_sem);

	remove_dir(dentry);
	/**
	 * Drop reference from dget() on entrance.
	 */
	dput(dentry);
}

int sysfs_rename_dir(struct kobject * kobj, const char *new_name)
{
	int error = 0;
	struct dentry * new_dentry, * parent;

	if (!strcmp(kobject_name(kobj), new_name))
		return -EINVAL;

	if (!kobj->parent)
		return -EINVAL;

	parent = kobj->parent->dentry;

	down(&parent->d_inode->i_sem);

	new_dentry = sysfs_get_dentry(parent, new_name);
	if (!IS_ERR(new_dentry)) {
  		if (!new_dentry->d_inode) {
			error = kobject_set_name(kobj,new_name);
			if (!error)
				d_move(kobj->dentry, new_dentry);
		}
		dput(new_dentry);
	}
	up(&parent->d_inode->i_sem);	

	return error;
}

EXPORT_SYMBOL(sysfs_create_dir);
EXPORT_SYMBOL(sysfs_remove_dir);
EXPORT_SYMBOL(sysfs_rename_dir);

