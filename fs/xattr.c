/*
  File: fs/xattr.c

  Extended attribute handling.

  Copyright (C) 2001 by Andreas Gruenbacher <a.gruenbacher@computer.org>
  Copyright (C) 2001 SGI - Silicon Graphics, Inc <linux-xfs@oss.sgi.com>
 */
#include <linux/fs.h>
#include <linux/fshooks.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/file.h>
#include <linux/xattr.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <asm/uaccess.h>

/*
 * Extended attribute SET operations
 */
static long
setxattr(struct dentry *d, char __user *name, void __user *value,
	 size_t size, int flags)
{
	int error;
	void *kvalue = NULL;
	char kname[XATTR_NAME_MAX + 1];

	if (flags & ~(XATTR_CREATE|XATTR_REPLACE))
		return -EINVAL;

	error = strncpy_from_user(kname, name, sizeof(kname));
	if (error == 0 || error == sizeof(kname))
		error = -ERANGE;
	if (error < 0)
		return error;

	if (size) {
		if (size > XATTR_SIZE_MAX)
			return -E2BIG;
		kvalue = kmalloc(size, GFP_KERNEL);
		if (!kvalue)
			return -ENOMEM;
		if (copy_from_user(kvalue, value, size)) {
			kfree(kvalue);
			return -EFAULT;
		}
	}

	error = -EOPNOTSUPP;
	if (d->d_inode->i_op && d->d_inode->i_op->setxattr) {
		down(&d->d_inode->i_sem);
		error = security_inode_setxattr(d, kname, kvalue, size, flags);
		if (error)
			goto out;
		error = d->d_inode->i_op->setxattr(d, kname, kvalue, size, flags);
		if (!error)
			security_inode_post_setxattr(d, kname, kvalue, size, flags);
out:
		up(&d->d_inode->i_sem);
	}
	if (kvalue)
		kfree(kvalue);
	return error;
}

asmlinkage long
sys_setxattr(char __user *path, char __user *name, void __user *value,
	     size_t size, int flags)
{
	struct nameidata nd;
	int error;

	FSHOOK_BEGIN_USER_PATH_WALK(setxattr,
		error,
		path,
		nd,
		path,
		.name = name,
		.value = value,
		.size = size,
		.flags = flags,
		.link = false)

	error = setxattr(nd.dentry, name, value, size, flags);
	path_release(&nd);

	FSHOOK_END_USER_WALK(setxattr, error, path)

	return error;
}

asmlinkage long
sys_lsetxattr(char __user *path, char __user *name, void __user *value,
	      size_t size, int flags)
{
	struct nameidata nd;
	int error;

	FSHOOK_BEGIN_USER_PATH_WALK_LINK(setxattr,
		error,
		path,
		nd,
		path,
		.name = name,
		.value = value,
		.size = size,
		.flags = flags,
		.link = true)

	error = setxattr(nd.dentry, name, value, size, flags);
	path_release(&nd);

	FSHOOK_END_USER_WALK(setxattr, error, path)

	return error;
}

asmlinkage long
sys_fsetxattr(int fd, char __user *name, void __user *value,
	      size_t size, int flags)
{
	struct file *f;
	int error;

	FSHOOK_BEGIN(fsetxattr,
		error,
		.fd = fd,
		.name = name,
		.value = value,
		.size = size,
		.flags = flags)

	f = fget(fd);
	if (f) {
		error = setxattr(f->f_dentry, name, value, size, flags);
		fput(f);
	}
	else
		error = -EBADF;

	FSHOOK_END(fsetxattr, error)

	return error;
}

/*
 * Extended attribute GET operations
 */
static ssize_t
getxattr(struct dentry *d, char __user *name, void __user *value, size_t size)
{
	ssize_t error;
	void *kvalue = NULL;
	char kname[XATTR_NAME_MAX + 1];

	error = strncpy_from_user(kname, name, sizeof(kname));
	if (error == 0 || error == sizeof(kname))
		error = -ERANGE;
	if (error < 0)
		return error;

	if (size) {
		if (size > XATTR_SIZE_MAX)
			size = XATTR_SIZE_MAX;
		kvalue = kmalloc(size, GFP_KERNEL);
		if (!kvalue)
			return -ENOMEM;
	}

	error = -EOPNOTSUPP;
	if (d->d_inode->i_op && d->d_inode->i_op->getxattr) {
		error = security_inode_getxattr(d, kname);
		if (error)
			goto out;
		error = d->d_inode->i_op->getxattr(d, kname, kvalue, size);
		if (error > 0) {
			if (size && copy_to_user(value, kvalue, error))
				error = -EFAULT;
		} else if (error == -ERANGE && size >= XATTR_SIZE_MAX) {
			/* The file system tried to returned a value bigger
			   than XATTR_SIZE_MAX bytes. Not possible. */
			error = -E2BIG;
		}
	}
out:
	if (kvalue)
		kfree(kvalue);
	return error;
}

asmlinkage ssize_t
sys_getxattr(char __user *path, char __user *name, void __user *value,
	     size_t size)
{
	struct nameidata nd;
	ssize_t error;

	FSHOOK_BEGIN_USER_PATH_WALK(getxattr,
		error,
		path,
		nd,
		path,
		.name = name,
		.link = false)

	error = getxattr(nd.dentry, name, value, size);
	path_release(&nd);

	FSHOOK_END_USER_WALK(getxattr, error, path)

	return error;
}

asmlinkage ssize_t
sys_lgetxattr(char __user *path, char __user *name, void __user *value,
	      size_t size)
{
	struct nameidata nd;
	ssize_t error;

	FSHOOK_BEGIN_USER_PATH_WALK_LINK(getxattr,
		error,
		path,
		nd,
		path,
		.name = name,
		.link = true)

	error = getxattr(nd.dentry, name, value, size);
	path_release(&nd);

	FSHOOK_END_USER_WALK(getxattr, error, path)

	return error;
}

asmlinkage ssize_t
sys_fgetxattr(int fd, char __user *name, void __user *value, size_t size)
{
	struct file *f;
	ssize_t error;

	FSHOOK_BEGIN(fgetxattr, error, .fd = fd, .name = name)

	f = fget(fd);
	if (f) {
		error = getxattr(f->f_dentry, name, value, size);
		fput(f);
	}
	else
		error = -EBADF;

	FSHOOK_END(fgetxattr, error)

	return error;
}

/*
 * Extended attribute LIST operations
 */
static ssize_t
listxattr(struct dentry *d, char __user *list, size_t size)
{
	ssize_t error;
	char *klist = NULL;

	if (size) {
		if (size > XATTR_LIST_MAX)
			size = XATTR_LIST_MAX;
		klist = kmalloc(size, GFP_KERNEL);
		if (!klist)
			return -ENOMEM;
	}

	error = -EOPNOTSUPP;
	if (d->d_inode->i_op && d->d_inode->i_op->listxattr) {
		error = security_inode_listxattr(d);
		if (error)
			goto out;
		error = d->d_inode->i_op->listxattr(d, klist, size);
		if (error > 0) {
			if (size && copy_to_user(list, klist, error))
				error = -EFAULT;
		} else if (error == -ERANGE && size >= XATTR_LIST_MAX) {
			/* The file system tried to returned a list bigger
			   than XATTR_LIST_MAX bytes. Not possible. */
			error = -E2BIG;
		}
	}
out:
	if (klist)
		kfree(klist);
	return error;
}

asmlinkage ssize_t
sys_listxattr(char __user *path, char __user *list, size_t size)
{
	struct nameidata nd;
	ssize_t error;

	FSHOOK_BEGIN_USER_PATH_WALK(listxattr,
		error,
		path,
		nd,
		path,
		.link = false)

	error = listxattr(nd.dentry, list, size);
	path_release(&nd);

	FSHOOK_END_USER_WALK(listxattr, error, path)

	return error;
}

asmlinkage ssize_t
sys_llistxattr(char __user *path, char __user *list, size_t size)
{
	struct nameidata nd;
	ssize_t error;

	FSHOOK_BEGIN_USER_PATH_WALK_LINK(listxattr,
		error,
		path,
		nd,
		path,
		.link = true)

	error = listxattr(nd.dentry, list, size);
	path_release(&nd);

	FSHOOK_END_USER_WALK(listxattr, error, path)

	return error;
}

asmlinkage ssize_t
sys_flistxattr(int fd, char __user *list, size_t size)
{
	struct file *f;
	ssize_t error;

	FSHOOK_BEGIN(flistxattr, error, .fd = fd)

	f = fget(fd);
	if (f) {
		error = listxattr(f->f_dentry, list, size);
		fput(f);
	}
	else
		error = -EBADF;

	FSHOOK_END(flistxattr, error)

	return error;
}

/*
 * Extended attribute REMOVE operations
 */
static long
removexattr(struct dentry *d, char __user *name)
{
	int error;
	char kname[XATTR_NAME_MAX + 1];

	error = strncpy_from_user(kname, name, sizeof(kname));
	if (error == 0 || error == sizeof(kname))
		error = -ERANGE;
	if (error < 0)
		return error;

	error = -EOPNOTSUPP;
	if (d->d_inode->i_op && d->d_inode->i_op->removexattr) {
		error = security_inode_removexattr(d, kname);
		if (error)
			goto out;
		down(&d->d_inode->i_sem);
		error = d->d_inode->i_op->removexattr(d, kname);
		up(&d->d_inode->i_sem);
	}
out:
	return error;
}

asmlinkage long
sys_removexattr(char __user *path, char __user *name)
{
	struct nameidata nd;
	int error;

	FSHOOK_BEGIN_USER_PATH_WALK(rmxattr,
		error,
		path,
		nd,
		path,
		.name = name,
		.link = false)

	error = removexattr(nd.dentry, name);
	path_release(&nd);

	FSHOOK_END_USER_WALK(rmxattr, error, path)

	return error;
}

asmlinkage long
sys_lremovexattr(char __user *path, char __user *name)
{
	struct nameidata nd;
	int error;

	FSHOOK_BEGIN_USER_PATH_WALK_LINK(rmxattr,
		error,
		path,
		nd,
		path,
		.name = name,
		.link = true)

	error = removexattr(nd.dentry, name);
	path_release(&nd);

	FSHOOK_END_USER_WALK(rmxattr, error, path)

	return error;
}

asmlinkage long
sys_fremovexattr(int fd, char __user *name)
{
	struct file *f;
	int error;

	FSHOOK_BEGIN(frmxattr, error, .fd = fd, .name = name)

	f = fget(fd);
	if (f) {
		error = removexattr(f->f_dentry, name);
		fput(f);
	}
	else
		error = -EBADF;

	FSHOOK_END(frmxattr, error)

	return error;
}
