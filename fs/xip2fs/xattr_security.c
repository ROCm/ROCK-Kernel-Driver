/*
 *  linux/fs/xip2fs/xattr_security.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 *            Gerald Schaefer <geraldsc@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/xip2_fs.h>
#include "xattr.h"

static size_t
xip2_xattr_security_list(char *list, struct inode *inode,
			const char *name, int name_len)
{
	const int prefix_len = sizeof(XATTR_SECURITY_PREFIX)-1;

	if (list) {
		memcpy(list, XATTR_SECURITY_PREFIX, prefix_len);
		memcpy(list+prefix_len, name, name_len);
		list[prefix_len + name_len] = '\0';
	}
	return prefix_len + name_len + 1;
}

static int
xip2_xattr_security_get(struct inode *inode, const char *name,
		       void *buffer, size_t size)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return xip2_xattr_get(inode, XIP2_XATTR_INDEX_SECURITY, name,
			      buffer, size);
}

struct xip2_xattr_handler xip2_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.list	= xip2_xattr_security_list,
	.get	= xip2_xattr_security_get,
};
