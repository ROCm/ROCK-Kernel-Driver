/*
 * mm/shmem_acl.c
 *
 * (C) 2005 Andreas Gruenbacher <agruen@suse.de>
 *
 * This file is released under the GPL.
 */

#include <linux/fs.h>
#include <linux/shmem_fs.h>
#include <linux/xattr.h>
#include <linux/generic_acl.h>

static struct posix_acl *
shmem_get_acl(struct inode *inode, int type)
{
	struct posix_acl *acl = NULL;

	spin_lock(&inode->i_lock);
	switch(type) {
		case ACL_TYPE_ACCESS:
			acl = posix_acl_dup(SHMEM_I(inode)->i_acl);
			break;

		case ACL_TYPE_DEFAULT:
			acl = posix_acl_dup(SHMEM_I(inode)->i_default_acl);
			break;
	}
	spin_unlock(&inode->i_lock);

	return acl;
}

static void
shmem_set_acl(struct inode *inode, int type, struct posix_acl *acl)
{
	spin_lock(&inode->i_lock);
	switch(type) {
		case ACL_TYPE_ACCESS:
			if (SHMEM_I(inode)->i_acl)
				posix_acl_release(SHMEM_I(inode)->i_acl);
			SHMEM_I(inode)->i_acl = posix_acl_dup(acl);
			break;
			
		case ACL_TYPE_DEFAULT:
			if (SHMEM_I(inode)->i_default_acl)
				posix_acl_release(SHMEM_I(inode)->i_default_acl);
			SHMEM_I(inode)->i_default_acl = posix_acl_dup(acl);
			break;
	}
	spin_unlock(&inode->i_lock);
}

struct generic_acl_operations shmem_acl_ops = {
	.getacl = shmem_get_acl,
	.setacl = shmem_set_acl,
};

static size_t
shmem_list_acl_access(struct inode *inode, char *list, size_t list_size,
		      const char *name, size_t name_len)
{
	return generic_acl_list(inode, &shmem_acl_ops, ACL_TYPE_ACCESS,
				list, list_size);
}

static size_t
shmem_list_acl_default(struct inode *inode, char *list, size_t list_size,
		       const char *name, size_t name_len)
{
	return generic_acl_list(inode, &shmem_acl_ops, ACL_TYPE_DEFAULT,
				list, list_size);
}

static int
shmem_get_acl_access(struct inode *inode, const char *name, void *buffer,
		     size_t size)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_get(inode, &shmem_acl_ops, ACL_TYPE_ACCESS, buffer,
			       size);
}

static int
shmem_get_acl_default(struct inode *inode, const char *name, void *buffer,
		      size_t size)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_get(inode, &shmem_acl_ops, ACL_TYPE_DEFAULT, buffer,
			       size);
}

static int
shmem_set_acl_access(struct inode *inode, const char *name, const void *value,
		     size_t size, int flags)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_set(inode, &shmem_acl_ops, ACL_TYPE_ACCESS, value,
			       size);
}

static int
shmem_set_acl_default(struct inode *inode, const char *name, const void *value,
		      size_t size, int flags)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_set(inode, &shmem_acl_ops, ACL_TYPE_DEFAULT, value,
			       size);
}

struct xattr_handler shmem_xattr_acl_access_handler = {
	.prefix = XATTR_NAME_ACL_ACCESS,
	.list	= shmem_list_acl_access,
	.get	= shmem_get_acl_access,
	.set	= shmem_set_acl_access,
};

struct xattr_handler shmem_xattr_acl_default_handler = {
	.prefix = XATTR_NAME_ACL_DEFAULT,
	.list	= shmem_list_acl_default,
	.get	= shmem_get_acl_default,
	.set	= shmem_set_acl_default,
};

int
shmem_acl_init(struct inode *inode, struct inode *dir)
{
	return generic_acl_init(inode, dir, &shmem_acl_ops);
}

void
shmem_acl_destroy_inode(struct inode *inode)
{
	if (SHMEM_I(inode)->i_acl)
		posix_acl_release(SHMEM_I(inode)->i_acl);
	if (SHMEM_I(inode)->i_default_acl)
		posix_acl_release(SHMEM_I(inode)->i_default_acl);
	SHMEM_I(inode)->i_acl = NULL;
	SHMEM_I(inode)->i_default_acl = NULL;
}

int
shmem_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int error;
	
	error = inode_change_ok(inode, iattr);
	if (error)
		return error;
	error = inode_setattr(inode, iattr);
	if (!error && (iattr->ia_valid & ATTR_MODE))
		error = generic_acl_chmod(inode, &shmem_acl_ops);
	return error;
}

static int
shmem_check_acl(struct inode *inode, int mask)
{
	struct posix_acl *acl = shmem_get_acl(inode, ACL_TYPE_ACCESS);

	if (acl) {
		int error = posix_acl_permission(inode, acl, mask);
		posix_acl_release(acl);
		return error;
	}
	return -EAGAIN;
}

int
shmem_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	return generic_permission(inode, mask, shmem_check_acl);
}
