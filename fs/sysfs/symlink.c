/*
 * symlink.c - operations for sysfs symlinks.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/kobject.h>

#include "sysfs.h"

static struct inode_operations sysfs_symlink_inode_operations = {
	.readlink = sysfs_readlink,
	.follow_link = sysfs_follow_link,
};

static int init_symlink(struct inode * inode)
{
	inode->i_op = &sysfs_symlink_inode_operations;
	return 0;
}

static int object_depth(struct kobject * kobj)
{
	struct kobject * p = kobj;
	int depth = 0;
	do { depth++; } while ((p = p->parent));
	return depth;
}

static int object_path_length(struct kobject * kobj)
{
	struct kobject * p = kobj;
	int length = 1;
	do {
		length += strlen(kobject_name(p)) + 1;
		p = p->parent;
	} while (p);
	return length;
}

static void fill_object_path(struct kobject * kobj, char * buffer, int length)
{
	struct kobject * p;

	--length;
	for (p = kobj; p; p = p->parent) {
		int cur = strlen(kobject_name(p));

		/* back up enough to print this bus id with '/' */
		length -= cur;
		strncpy(buffer + length,kobject_name(p),cur);
		*(buffer + --length) = '/';
	}
}

/**
 *	sysfs_create_link - create symlink between two objects.
 *	@kobj:	object whose directory we're creating the link in.
 *	@target:	object we're pointing to.
 *	@name:		name of the symlink.
 */
int sysfs_create_link(struct kobject * kobj, struct kobject * target, char * name)
{
	struct dentry * dentry = kobj->dentry;
	struct dentry * d;
	int error = 0;

	down(&dentry->d_inode->i_sem);
	d = sysfs_get_dentry(dentry,name);
	if (!IS_ERR(d)) {
		error = sysfs_create(d, S_IFLNK|S_IRWXUGO, init_symlink);
		if (!error)
			/* 
			 * associate the link dentry with the target kobject 
			 */
			d->d_fsdata = kobject_get(target);
		dput(d);
	} else 
		error = PTR_ERR(d);
	up(&dentry->d_inode->i_sem);
	return error;
}


/**
 *	sysfs_remove_link - remove symlink in object's directory.
 *	@kobj:	object we're acting for.
 *	@name:	name of the symlink to remove.
 */

void sysfs_remove_link(struct kobject * kobj, char * name)
{
	sysfs_hash_and_remove(kobj->dentry,name);
}

static int sysfs_get_target_path(struct kobject * kobj, struct kobject * target,
				   char *path)
{
	char * s;
	int depth, size;

	depth = object_depth(kobj);
	size = object_path_length(target) + depth * 3 - 1;
	if (size > PATH_MAX)
		return -ENAMETOOLONG;

	pr_debug("%s: depth = %d, size = %d\n", __FUNCTION__, depth, size);

	for (s = path; depth--; s += 3)
		strcpy(s,"../");

	fill_object_path(target, path, size);
	pr_debug("%s: path = '%s'\n", __FUNCTION__, path);

	return 0;
}

static int sysfs_getlink(struct dentry *dentry, char * path)
{
	struct kobject *kobj, *target_kobj;
	int error = 0;

	kobj = sysfs_get_kobject(dentry->d_parent);
	if (!kobj)
		return -EINVAL;

	target_kobj = sysfs_get_kobject(dentry);
	if (!target_kobj) {
		kobject_put(kobj);
		return -EINVAL;
	}

	down_read(&sysfs_rename_sem);
	error = sysfs_get_target_path(kobj, target_kobj, path);
	up_read(&sysfs_rename_sem);
	
	kobject_put(kobj);
	kobject_put(target_kobj);
	return error;

}

int sysfs_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	int error = 0;
	unsigned long page = get_zeroed_page(GFP_KERNEL);

	if (!page)
		return -ENOMEM;

	error = sysfs_getlink(dentry, (char *) page);
	if (!error)
	        error = vfs_readlink(dentry, buffer, buflen, (char *) page);

	free_page(page);

	return error;
}

int sysfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	int error = 0;
	unsigned long page = get_zeroed_page(GFP_KERNEL);

	if (!page)
		return -ENOMEM;

	error = sysfs_getlink(dentry, (char *) page); 
	if (!error)
	        error = vfs_follow_link(nd, (char *) page);

	free_page(page);

	return error;
}

EXPORT_SYMBOL(sysfs_create_link);
EXPORT_SYMBOL(sysfs_remove_link);

