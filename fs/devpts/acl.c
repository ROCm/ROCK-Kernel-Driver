/*
 * fs/devpts/acl.c
 *
 * (C) 2005 Andreas Gruenbacher <agruen@suse.de>
 *
 * This file is released under the GPL.
 */

#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/devpts_fs.h>
#include <linux/xattr.h>
#include <linux/generic_acl.h>

static struct posix_acl *
devpts_get_acl(struct inode *inode, int type)
{
	struct posix_acl *acl = NULL;

	spin_lock(&inode->i_lock);
	if (type == ACL_TYPE_ACCESS)
		acl = posix_acl_dup(DEVPTS_I(inode)->i_acl);
	spin_unlock(&inode->i_lock);

	return acl;
}

static void
devpts_set_acl(struct inode *inode, int type, struct posix_acl *acl)
{
	spin_lock(&inode->i_lock);
	if (type == ACL_TYPE_ACCESS) {
		if (DEVPTS_I(inode)->i_acl)
			posix_acl_release(DEVPTS_I(inode)->i_acl);
		DEVPTS_I(inode)->i_acl = posix_acl_dup(acl);
	}
	spin_unlock(&inode->i_lock);
}

struct generic_acl_operations devpts_acl_ops = {
	.getacl = devpts_get_acl,
	.setacl = devpts_set_acl,
};

static size_t
devpts_list_acl_access(struct inode *inode, char *list, size_t list_size,
		       const char *name, size_t name_len)
{
	return generic_acl_list(inode, &devpts_acl_ops, ACL_TYPE_ACCESS,
				list, list_size);
}

static int
devpts_get_acl_access(struct inode *inode, const char *name, void *buffer,
		      size_t size)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_get(inode, &devpts_acl_ops, ACL_TYPE_ACCESS, buffer,
			       size);
}

static int
devpts_set_acl_access(struct inode *inode, const char *name, const void *value,
		      size_t size, int flags)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_set(inode, &devpts_acl_ops, ACL_TYPE_ACCESS, value,
			       size);
}

struct xattr_handler devpts_xattr_acl_access_handler = {
	.prefix = XATTR_NAME_ACL_ACCESS,
	.list   = devpts_list_acl_access,
	.get    = devpts_get_acl_access,
	.set    = devpts_set_acl_access,
};

int
devpts_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int error;

	error = inode_change_ok(inode, iattr);
	if (error)
		return error;
	error = inode_setattr(inode, iattr);
	if (!error && (iattr->ia_valid & ATTR_MODE))
		error = generic_acl_chmod(inode, &devpts_acl_ops);
	return error;
}

static int
devpts_check_acl(struct inode *inode, int mask)
{
        struct posix_acl *acl = devpts_get_acl(inode, ACL_TYPE_ACCESS);

        if (acl) {
                int error = posix_acl_permission(inode, acl, mask);
                posix_acl_release(acl);
                return error;
        }
        return -EAGAIN;
}

int
devpts_permission(struct inode *inode, int mask, struct nameidata *nd)
{
        return generic_permission(inode, mask, devpts_check_acl);
}
