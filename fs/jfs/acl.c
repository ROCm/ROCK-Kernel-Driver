/*
 *   Copyright (c) International Business Machines  Corp., 2002
 *   Copyright (c) Andreas Gruenbacher, 2001
 *   Copyright (c) Linus Torvalds, 1991, 1992
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include "jfs_incore.h"
#include "jfs_xattr.h"
#include "jfs_acl.h"

struct posix_acl *jfs_get_acl(struct inode *inode, int type)
{
	struct posix_acl *acl;
	char *ea_name;
	struct jfs_inode_info *ji = JFS_IP(inode);
	struct posix_acl **p_acl;
	int size;
	char *value = NULL;

	switch(type) {
		case ACL_TYPE_ACCESS:
			ea_name = XATTR_NAME_ACL_ACCESS;
			p_acl = &ji->i_acl;
			break;
		case ACL_TYPE_DEFAULT:
			ea_name = XATTR_NAME_ACL_DEFAULT;
			p_acl = &ji->i_default_acl;
			break;
		default:
			return ERR_PTR(-EINVAL);
	}

	if (*p_acl != JFS_ACL_NOT_CACHED)
		return posix_acl_dup(*p_acl);

	size = __jfs_getxattr(inode, ea_name, NULL, 0);

	if (size > 0) {
		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-ENOMEM);
		size = __jfs_getxattr(inode, ea_name, value, size);
	}

	if (size < 0) {
		if (size == -ENODATA) {
			*p_acl = NULL;
			acl = NULL;
		} else
			acl = ERR_PTR(size);
	} else {
		acl = posix_acl_from_xattr(value, size);
		if (!IS_ERR(acl))
			*p_acl = posix_acl_dup(acl);
	}
	if (value)
		kfree(value);
	return acl;
}

int jfs_set_acl(struct inode *inode, int type, struct posix_acl *acl)
{
	char *ea_name;
	struct jfs_inode_info *ji = JFS_IP(inode);
	struct posix_acl **p_acl;
	int rc;
	int size = 0;
	char *value = NULL;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	switch(type) {
		case ACL_TYPE_ACCESS:
			ea_name = XATTR_NAME_ACL_ACCESS;
			p_acl = &ji->i_acl;
			break;
		case ACL_TYPE_DEFAULT:
			ea_name = XATTR_NAME_ACL_DEFAULT;
			p_acl = &ji->i_default_acl;
			if (!S_ISDIR(inode->i_mode))
				return acl ? -EACCES : 0;
			break;
		default:
			return -EINVAL;
	}
	if (acl) {
		size = xattr_acl_size(acl->a_count);
		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return -ENOMEM;
		rc = posix_acl_to_xattr(acl, value, size);
		if (rc < 0)
			goto out;
	}
	rc = __jfs_setxattr(inode, ea_name, value, size, 0);
out:
	if (value)
		kfree(value);

	if (!rc) {
		if (*p_acl && (*p_acl != JFS_ACL_NOT_CACHED))
			posix_acl_release(*p_acl);
		*p_acl = posix_acl_dup(acl);
	}
	return rc;
}

/*
 *	jfs_permission()
 *
 * modified vfs_permission to check posix acl
 */
int jfs_permission(struct inode * inode, int mask, struct nameidata *nd)
{
	umode_t mode = inode->i_mode;
	struct jfs_inode_info *ji = JFS_IP(inode);

	if (mask & MAY_WRITE) {
		/*
		 * Nobody gets write access to a read-only fs.
		 */
		if (IS_RDONLY(inode) &&
		    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
			return -EROFS;

		/*
		 * Nobody gets write access to an immutable file.
		 */
		if (IS_IMMUTABLE(inode))
			return -EACCES;
	}

	if (current->fsuid == inode->i_uid) {
		mode >>= 6;
		goto check_mode;
	}
	/*
	 * ACL can't contain additional permissions if the ACL_MASK entry
	 * is zero.
	 */
	if (!(mode & S_IRWXG))
		goto check_groups;

	if (ji->i_acl == JFS_ACL_NOT_CACHED) {
		struct posix_acl *acl;

		acl = jfs_get_acl(inode, ACL_TYPE_ACCESS);

		if (IS_ERR(acl))
			return PTR_ERR(acl);
		posix_acl_release(acl);
	}

	if (ji->i_acl) {
		int rc = posix_acl_permission(inode, ji->i_acl, mask);
		if (rc == -EACCES)
			goto check_capabilities;
		return rc;
	}

check_groups:
	if (in_group_p(inode->i_gid))
		mode >>= 3;

check_mode:
	/*
	 * If the DACs are ok we don't need any capability check.
	 */
	if (((mode & mask & (MAY_READ|MAY_WRITE|MAY_EXEC)) == mask))
		return 0;

check_capabilities:
	/*
	 * Read/write DACs are always overridable.
	 * Executable DACs are overridable if at least one exec bit is set.
	 */
	if ((mask & (MAY_READ|MAY_WRITE)) || (inode->i_mode & S_IXUGO))
		if (capable(CAP_DAC_OVERRIDE))
			return 0;

	/*
	 * Searching includes executable on directories, else just read.
	 */
	if (mask == MAY_READ || (S_ISDIR(inode->i_mode) && !(mask & MAY_WRITE)))
		if (capable(CAP_DAC_READ_SEARCH))
			return 0;

	return -EACCES;
}

int jfs_init_acl(struct inode *inode, struct inode *dir)
{
	struct posix_acl *acl = NULL;
	struct posix_acl *clone;
	mode_t mode;
	int rc = 0;

	if (S_ISLNK(inode->i_mode))
		return 0;

	acl = jfs_get_acl(dir, ACL_TYPE_DEFAULT);
	if (IS_ERR(acl))
		return PTR_ERR(acl);

	if (acl) {
		if (S_ISDIR(inode->i_mode)) {
			rc = jfs_set_acl(inode, ACL_TYPE_DEFAULT, acl);
			if (rc)
				goto cleanup;
		}
		clone = posix_acl_clone(acl, GFP_KERNEL);
		if (!clone) {
			rc = -ENOMEM;
			goto cleanup;
		}
		mode = inode->i_mode;
		rc = posix_acl_create_masq(clone, &mode);
		if (rc >= 0) {
			inode->i_mode = mode;
			if (rc > 0)
				rc = jfs_set_acl(inode, ACL_TYPE_ACCESS, clone);
		}
		posix_acl_release(clone);
cleanup:
		posix_acl_release(acl);
	} else
		inode->i_mode &= ~current->fs->umask;

	return rc;
}

int jfs_acl_chmod(struct inode *inode)
{
	struct posix_acl *acl, *clone;
	int rc;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	acl = jfs_get_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl) || !acl)
		return PTR_ERR(acl);

	clone = posix_acl_clone(acl, GFP_KERNEL);
	posix_acl_release(acl);
	if (!clone)
		return -ENOMEM;

	rc = posix_acl_chmod_masq(clone, inode->i_mode);
	if (!rc)
		rc = jfs_set_acl(inode, ACL_TYPE_ACCESS, clone);

	posix_acl_release(clone);
	return rc;
}

int jfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int rc;

	rc = inode_change_ok(inode, iattr);
	if (rc)
		return rc;

	inode_setattr(inode, iattr);

	if (iattr->ia_valid & ATTR_MODE)
		rc = jfs_acl_chmod(inode);

	return rc;
}
