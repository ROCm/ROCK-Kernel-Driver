/*
 *  linux/fs/xip2fs/xattr_user.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 *            Gerald Schaefer <geraldsc@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include "xip2.h"
#include "xattr.h"

#define XATTR_USER_PREFIX "user."

static size_t
xip2_xattr_user_list(char *list, struct inode *inode,
		     const char *name, int name_len)
{
	const int prefix_len = sizeof(XATTR_USER_PREFIX)-1;

	if (!test_opt(inode->i_sb, XATTR_USER))
		return 0;

	if (list) {
		memcpy(list, XATTR_USER_PREFIX, prefix_len);
		memcpy(list+prefix_len, name, name_len);
		list[prefix_len + name_len] = '\0';
	}
	return prefix_len + name_len + 1;
}

static int
xip2_xattr_user_get(struct inode *inode, const char *name,
		    void *buffer, size_t size)
{
	int error;

	if (strcmp(name, "") == 0)
		return -EINVAL;
	if (!test_opt(inode->i_sb, XATTR_USER))
		return -EOPNOTSUPP;
	error = permission(inode, MAY_READ, NULL);
	if (error)
		return error;

	return xip2_xattr_get(inode, XIP2_XATTR_INDEX_USER, name, buffer, size);
}

struct xip2_xattr_handler xip2_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.list	= xip2_xattr_user_list,
	.get	= xip2_xattr_user_get,
};

int __init
init_xip2_xattr_user(void)
{
	return xip2_xattr_register(XIP2_XATTR_INDEX_USER,
				   &xip2_xattr_user_handler);
}

void
exit_xip2_xattr_user(void)
{
	xip2_xattr_unregister(XIP2_XATTR_INDEX_USER,
			      &xip2_xattr_user_handler);
}
