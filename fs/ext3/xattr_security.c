/*
 * linux/fs/ext3/xattr_security.c
 * Handler for storing security labels as extended attributes.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/ext3_jbd.h>
#include <linux/ext3_fs.h>
#include "xattr.h"

static size_t
ext3_xattr_security_list(char *list, struct inode *inode,
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
ext3_xattr_security_get(struct inode *inode, const char *name,
		       void *buffer, size_t size)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return ext3_xattr_get(inode, EXT3_XATTR_INDEX_SECURITY, name,
			      buffer, size);
}

static int
ext3_xattr_security_set(struct inode *inode, const char *name,
		       const void *value, size_t size, int flags)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return ext3_xattr_set(inode, EXT3_XATTR_INDEX_SECURITY, name,
			      value, size, flags);
}

struct ext3_xattr_handler ext3_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.list	= ext3_xattr_security_list,
	.get	= ext3_xattr_security_get,
	.set	= ext3_xattr_security_set,
};
