/*
 * File: fs/devpts/xattr_security.c
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/security.h>
#include "xattr.h"

static size_t
devpts_xattr_security_list(struct dentry *dentry, char *buffer)
{
	return security_inode_listsecurity(dentry, buffer);
}

static int
devpts_xattr_security_get(struct dentry *dentry, const char *name,
			  void *buffer, size_t size)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return security_inode_getsecurity(dentry, name, buffer, size);
}

static int
devpts_xattr_security_set(struct dentry *dentry, const char *name,
			  const void *value, size_t size, int flags)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return security_inode_setsecurity(dentry, name, value, size, flags);
}

struct devpts_xattr_handler devpts_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.list	= devpts_xattr_security_list,
	.get	= devpts_xattr_security_get,
	.set	= devpts_xattr_security_set,
};
