/*
 * symlink.c - operations for sysfs symlinks.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/kobject.h>

#include "sysfs.h"


static int init_symlink(struct inode * inode)
{
	inode->i_op = &page_symlink_inode_operations;
	return 0;
}

static int sysfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	int error;

	error = sysfs_create(dentry, S_IFLNK|S_IRWXUGO, init_symlink);
	if (!error) {
		int l = strlen(symname)+1;
		error = page_symlink(dentry->d_inode, symname, l);
		if (error)
			iput(dentry->d_inode);
	}
	return error;
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
		length += strlen(p->name) + 1;
		p = p->parent;
	} while (p);
	return length;
}

static void fill_object_path(struct kobject * kobj, char * buffer, int length)
{
	struct kobject * p;

	--length;
	for (p = kobj; p; p = p->parent) {
		int cur = strlen(p->name);

		/* back up enough to print this bus id with '/' */
		length -= cur;
		strncpy(buffer + length,p->name,cur);
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
	int size;
	int depth;
	char * path;
	char * s;

	depth = object_depth(kobj);
	size = object_path_length(target) + depth * 3 - 1;
	if (size > PATH_MAX)
		return -ENAMETOOLONG;
	pr_debug("%s: depth = %d, size = %d\n",__FUNCTION__,depth,size);

	path = kmalloc(size,GFP_KERNEL);
	if (!path)
		return -ENOMEM;
	memset(path,0,size);

	for (s = path; depth--; s += 3)
		strcpy(s,"../");

	fill_object_path(target,path,size);
	pr_debug("%s: path = '%s'\n",__FUNCTION__,path);

	down(&dentry->d_inode->i_sem);
	d = sysfs_get_dentry(dentry,name);
	if (!IS_ERR(d))
		error = sysfs_symlink(dentry->d_inode,d,path);
	else
		error = PTR_ERR(d);
	dput(d);
	up(&dentry->d_inode->i_sem);
	kfree(path);
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


EXPORT_SYMBOL(sysfs_create_link);
EXPORT_SYMBOL(sysfs_remove_link);

