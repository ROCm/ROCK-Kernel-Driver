/*
 * dir.c - Operations for sysfs directories.
 */

#undef DEBUG

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include "sysfs.h"

DECLARE_RWSEM(sysfs_rename_sem);

static void sysfs_d_iput(struct dentry * dentry, struct inode * inode)
{
	struct sysfs_dirent * sd = dentry->d_fsdata;

	if (sd) {
		BUG_ON(sd->s_dentry != dentry);
		sd->s_dentry = NULL;
		sysfs_put(sd);
	}
	iput(inode);
}

static struct dentry_operations sysfs_dentry_ops = {
	.d_iput		= sysfs_d_iput,
};

/*
 * Allocates a new sysfs_dirent and links it to the parent sysfs_dirent
 */
static struct sysfs_dirent * sysfs_new_dirent(struct sysfs_dirent * parent_sd,
						void * element)
{
	struct sysfs_dirent * sd;

	sd = kmalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return ERR_PTR(-ENOMEM);

	memset(sd, 0, sizeof(*sd));
	atomic_set(&sd->s_count, 1);
	INIT_LIST_HEAD(&sd->s_children);
	list_add(&sd->s_sibling, &parent_sd->s_children);
	sd->s_element = element;

	return sd;
}

int sysfs_make_dirent(struct sysfs_dirent * parent_sd, struct dentry * dentry,
			void * element, umode_t mode, int type)
{
	struct sysfs_dirent * sd;

	sd = sysfs_new_dirent(parent_sd, element);
	if (!sd)
		return -ENOMEM;

	sd->s_mode = mode;
	sd->s_type = type;
	sd->s_dentry = dentry;
	dentry->d_fsdata = sysfs_get(sd);
	dentry->d_op = &sysfs_dentry_ops;

	return 0;
}

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
	umode_t mode = S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO;

	down(&p->d_inode->i_sem);
	*d = sysfs_get_dentry(p,n);
	if (!IS_ERR(*d)) {
		error = sysfs_create(*d, mode, init_dir);
		if (!error) {
			error = sysfs_make_dirent(p->d_fsdata, *d, k, mode,
						SYSFS_DIR);
			if (!error)
				p->d_inode->i_nlink++;
		}
		if (error)
			d_drop(*d);
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

	BUG_ON(!kobj);

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
	struct sysfs_dirent * sd;

	down(&parent->d_inode->i_sem);
	d_delete(d);
	sd = d->d_fsdata;
 	list_del_init(&sd->s_sibling);
	sysfs_put(sd);
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
	struct dentry * dentry = dget(kobj->dentry);
	struct sysfs_dirent * parent_sd = dentry->d_fsdata;
	struct sysfs_dirent * sd, * tmp;

	if (!dentry)
		return;

	pr_debug("sysfs %s: removing dir\n",dentry->d_name.name);
	down(&dentry->d_inode->i_sem);
	list_for_each_entry_safe(sd, tmp, &parent_sd->s_children, s_sibling) {
		if (!sd->s_element)
			continue;
		list_del_init(&sd->s_sibling);
		sysfs_drop_dentry(sd, dentry);
		sysfs_put(sd);
	}
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

	down_write(&sysfs_rename_sem);
	parent = kobj->parent->dentry;

	down(&parent->d_inode->i_sem);

	new_dentry = sysfs_get_dentry(parent, new_name);
	if (!IS_ERR(new_dentry)) {
  		if (!new_dentry->d_inode) {
			error = kobject_set_name(kobj, "%s", new_name);
			if (!error)
				d_move(kobj->dentry, new_dentry);
			else
				d_drop(new_dentry);
		} else
			error = -EEXIST;
		dput(new_dentry);
	}
	up(&parent->d_inode->i_sem);	
	up_write(&sysfs_rename_sem);

	return error;
}

EXPORT_SYMBOL_GPL(sysfs_create_dir);
EXPORT_SYMBOL_GPL(sysfs_remove_dir);
EXPORT_SYMBOL_GPL(sysfs_rename_dir);

