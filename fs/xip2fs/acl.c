/*
 *  linux/fs/xip2fs/acl.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */


#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include "xip2.h"
#include "xattr.h"
#include "acl.h"

/*
 * Convert from filesystem to in-memory representation.
 */
static struct posix_acl *
xip2_acl_from_disk(const void *value, size_t size)
{
	const char *end = (char *)value + size;
	int n, count;
	struct posix_acl *acl;

	if (!value)
		return NULL;
	if (size < sizeof(xip2_acl_header))
		 return ERR_PTR(-EINVAL);
	if (((xip2_acl_header *)value)->a_version !=
	    cpu_to_le32(XIP2_ACL_VERSION))
		return ERR_PTR(-EINVAL);
	value = (char *)value + sizeof(xip2_acl_header);
	count = xip2_acl_count(size);
	if (count < 0)
		return ERR_PTR(-EINVAL);
	if (count == 0)
		return NULL;
	acl = posix_acl_alloc(count, GFP_KERNEL);
	if (!acl)
		return ERR_PTR(-ENOMEM);
	for (n=0; n < count; n++) {
		xip2_acl_entry *entry =
			(xip2_acl_entry *)value;
		if ((char *)value + sizeof(xip2_acl_entry_short) > end)
			goto fail;
		acl->a_entries[n].e_tag  = le16_to_cpu(entry->e_tag);
		acl->a_entries[n].e_perm = le16_to_cpu(entry->e_perm);
		switch(acl->a_entries[n].e_tag) {
			case ACL_USER_OBJ:
			case ACL_GROUP_OBJ:
			case ACL_MASK:
			case ACL_OTHER:
				value = (char *)value +
					sizeof(xip2_acl_entry_short);
				acl->a_entries[n].e_id = ACL_UNDEFINED_ID;
				break;

			case ACL_USER:
			case ACL_GROUP:
				value = (char *)value + sizeof(xip2_acl_entry);
				if ((char *)value > end)
					goto fail;
				acl->a_entries[n].e_id =
					le32_to_cpu(entry->e_id);
				break;

			default:
				goto fail;
		}
	}
	if (value != end)
		goto fail;
	return acl;

fail:
	posix_acl_release(acl);
	return ERR_PTR(-EINVAL);
}

static inline struct posix_acl *
xip2_iget_acl(struct inode *inode, struct posix_acl **i_acl)
{
	struct posix_acl *acl = XIP2_ACL_NOT_CACHED;

	spin_lock(&inode->i_lock);
	if (*i_acl != XIP2_ACL_NOT_CACHED)
		acl = posix_acl_dup(*i_acl);
	spin_unlock(&inode->i_lock);

	return acl;
}

static inline void
xip2_iset_acl(struct inode *inode, struct posix_acl **i_acl,
		   struct posix_acl *acl)
{
	spin_lock(&inode->i_lock);
	if (*i_acl != XIP2_ACL_NOT_CACHED)
		posix_acl_release(*i_acl);
	*i_acl = posix_acl_dup(acl);
	spin_unlock(&inode->i_lock);
}

/*
 * inode->i_sem: don't care
 */
static struct posix_acl *
xip2_get_acl(struct inode *inode, int type)
{
	struct xip2_inode_info *ei = XIP2_I(inode);
	int name_index;
	char *value = NULL;
	struct posix_acl *acl;
	int retval;

	if (!test_opt(inode->i_sb, POSIX_ACL))
		return 0;

	switch(type) {
		case ACL_TYPE_ACCESS:
			acl = xip2_iget_acl(inode, &ei->i_acl);
			if (acl != XIP2_ACL_NOT_CACHED)
				return acl;
			name_index = XIP2_XATTR_INDEX_POSIX_ACL_ACCESS;
			break;

		case ACL_TYPE_DEFAULT:
			acl = xip2_iget_acl(inode, &ei->i_default_acl);
			if (acl != XIP2_ACL_NOT_CACHED)
				return acl;
			name_index = XIP2_XATTR_INDEX_POSIX_ACL_DEFAULT;
			break;

		default:
			return ERR_PTR(-EINVAL);
	}
	retval = xip2_xattr_get(inode, name_index, "", NULL, 0);
	if (retval > 0) {
		value = kmalloc(retval, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-ENOMEM);
		retval = xip2_xattr_get(inode, name_index, "", value, retval);
	}
	if (retval > 0)
		acl = xip2_acl_from_disk(value, retval);
	else if (retval == -ENODATA || retval == -ENOSYS)
		acl = NULL;
	else
		acl = ERR_PTR(retval);
	if (value)
		kfree(value);

	if (!IS_ERR(acl)) {
		switch(type) {
			case ACL_TYPE_ACCESS:
				xip2_iset_acl(inode, &ei->i_acl, acl);
				break;

			case ACL_TYPE_DEFAULT:
				xip2_iset_acl(inode, &ei->i_default_acl, acl);
				break;
		}
	}
	return acl;
}
/*
 * Inode operation permission().
 *
 * inode->i_sem: don't care
 */
int
xip2_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	int mode = inode->i_mode;

	/* Nobody gets write access to a read-only fs */
	if ((mask & MAY_WRITE) && IS_RDONLY(inode) &&
	    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
		return -EROFS;
	/* Nobody gets write access to an immutable file */
	if ((mask & MAY_WRITE) && IS_IMMUTABLE(inode))
	    return -EACCES;
	if (current->fsuid == inode->i_uid) {
		mode >>= 6;
	} else if (test_opt(inode->i_sb, POSIX_ACL)) {
		struct posix_acl *acl;

		/* The access ACL cannot grant access if the group class
		   permission bits don't contain all requested permissions. */
		if (((mode >> 3) & mask & S_IRWXO) != mask)
			goto check_groups;
		acl = xip2_get_acl(inode, ACL_TYPE_ACCESS);
		if (acl) {
			int error = posix_acl_permission(inode, acl, mask);
			posix_acl_release(acl);
			if (error == -EACCES)
				goto check_capabilities;
			return error;
		} else
			goto check_groups;
	} else {
check_groups:
		if (in_group_p(inode->i_gid))
			mode >>= 3;
	}
	if ((mode & mask & S_IRWXO) == mask)
		return 0;

check_capabilities:
	/* Allowed to override Discretionary Access Control? */
	if (!(mask & MAY_EXEC) ||
	    (inode->i_mode & S_IXUGO) || S_ISDIR(inode->i_mode))
		if (capable(CAP_DAC_OVERRIDE))
			return 0;
	/* Read and search granted if capable(CAP_DAC_READ_SEARCH) */
	if (capable(CAP_DAC_READ_SEARCH) && ((mask == MAY_READ) ||
	    (S_ISDIR(inode->i_mode) && !(mask & MAY_WRITE))))
		return 0;
	return -EACCES;
}

/*
 * Extended attribut handlers
 */
static size_t
xip2_xattr_list_acl_access(char *list, struct inode *inode,
			   const char *name, int name_len)
{
	const size_t size = sizeof(XATTR_NAME_ACL_ACCESS);

	if (!test_opt(inode->i_sb, POSIX_ACL))
		return 0;
	if (list)
		memcpy(list, XATTR_NAME_ACL_ACCESS, size);
	return size;
}

static size_t
xip2_xattr_list_acl_default(char *list, struct inode *inode,
			    const char *name, int name_len)
{
	const size_t size = sizeof(XATTR_NAME_ACL_DEFAULT);

	if (!test_opt(inode->i_sb, POSIX_ACL))
		return 0;
	if (list)
		memcpy(list, XATTR_NAME_ACL_DEFAULT, size);
	return size;
}

static int
xip2_xattr_get_acl(struct inode *inode, int type, void *buffer, size_t size)
{
	struct posix_acl *acl;
	int error;

	if (!test_opt(inode->i_sb, POSIX_ACL))
		return -EOPNOTSUPP;

	acl = xip2_get_acl(inode, type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;
	error = posix_acl_to_xattr(acl, buffer, size);
	posix_acl_release(acl);

	return error;
}

static int
xip2_xattr_get_acl_access(struct inode *inode, const char *name,
			  void *buffer, size_t size)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return xip2_xattr_get_acl(inode, ACL_TYPE_ACCESS, buffer, size);
}

static int
xip2_xattr_get_acl_default(struct inode *inode, const char *name,
			   void *buffer, size_t size)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return xip2_xattr_get_acl(inode, ACL_TYPE_DEFAULT, buffer, size);
}

struct xip2_xattr_handler xip2_xattr_acl_access_handler = {
	.prefix	= XATTR_NAME_ACL_ACCESS,
	.list	= xip2_xattr_list_acl_access,
	.get	= xip2_xattr_get_acl_access,
};

struct xip2_xattr_handler xip2_xattr_acl_default_handler = {
	.prefix	= XATTR_NAME_ACL_DEFAULT,
	.list	= xip2_xattr_list_acl_default,
	.get	= xip2_xattr_get_acl_default,
};

void
exit_xip2_acl(void)
{
	xip2_xattr_unregister(XIP2_XATTR_INDEX_POSIX_ACL_ACCESS,
			      &xip2_xattr_acl_access_handler);
	xip2_xattr_unregister(XIP2_XATTR_INDEX_POSIX_ACL_DEFAULT,
			      &xip2_xattr_acl_default_handler);
}

int __init
init_xip2_acl(void)
{
	int error;

	error = xip2_xattr_register(XIP2_XATTR_INDEX_POSIX_ACL_ACCESS,
				    &xip2_xattr_acl_access_handler);
	if (error)
		goto fail;
	error = xip2_xattr_register(XIP2_XATTR_INDEX_POSIX_ACL_DEFAULT,
				    &xip2_xattr_acl_default_handler);
	if (error)
		goto fail;
	return 0;

fail:
	exit_xip2_acl();
	return error;
}
