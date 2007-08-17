/*
 * Copyright (C) 2006 Andreas Gruenbacher <a.gruenbacher@computer.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/ext3_jbd.h>
#include <linux/ext3_fs.h>
#include <linux/nfs4acl.h>
#include <linux/nfs4acl_xattr.h>
#include "xattr.h"
#include "nfs4acl.h"

static inline struct nfs4acl *
ext3_iget_nfs4acl(struct inode *inode)
{
	struct nfs4acl *acl = EXT3_NFS4ACL_NOT_CACHED;
	struct ext3_inode_info *ei = EXT3_I(inode);

	spin_lock(&inode->i_lock);
	if (ei->i_nfs4acl != EXT3_NFS4ACL_NOT_CACHED)
		acl = nfs4acl_dup(ei->i_nfs4acl);
	spin_unlock(&inode->i_lock);

	return acl;
}

static inline void
ext3_iset_nfs4acl(struct inode *inode, struct nfs4acl *acl)
{
	struct ext3_inode_info *ei = EXT3_I(inode);

	spin_lock(&inode->i_lock);
	if (ei->i_nfs4acl != EXT3_NFS4ACL_NOT_CACHED)
		nfs4acl_release(ei->i_nfs4acl);
	ei->i_nfs4acl = nfs4acl_dup(acl);
	spin_unlock(&inode->i_lock);
}

static struct nfs4acl *
ext3_get_nfs4acl(struct inode *inode)
{
	const int name_index = EXT3_XATTR_INDEX_NFS4ACL;
	void *value = NULL;
	struct nfs4acl *acl;
	int retval;

	if (!test_opt(inode->i_sb, NFS4ACL))
		return NULL;

	acl = ext3_iget_nfs4acl(inode);
	if (acl != EXT3_NFS4ACL_NOT_CACHED)
		return acl;
	retval = ext3_xattr_get(inode, name_index, "", NULL, 0);
	if (retval > 0) {
		value = kmalloc(retval, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-ENOMEM);
		retval = ext3_xattr_get(inode, name_index, "", value, retval);
	}
	if (retval > 0) {
		acl = nfs4acl_from_xattr(value, retval);
		if (acl == ERR_PTR(-EINVAL))
			acl = ERR_PTR(-EIO);
	} else if (retval == -ENODATA || retval == -ENOSYS)
		acl = NULL;
	else
		acl = ERR_PTR(retval);
	kfree(value);

	if (!IS_ERR(acl))
		ext3_iset_nfs4acl(inode, acl);

	return acl;
}

static int
ext3_set_nfs4acl(handle_t *handle, struct inode *inode, struct nfs4acl *acl)
{
	const int name_index = EXT3_XATTR_INDEX_NFS4ACL;
	size_t size = 0;
	void *value = NULL;
	int retval;

	if (acl) {
		size = nfs4acl_xattr_size(acl);
		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return -ENOMEM;
		nfs4acl_to_xattr(acl, value);
	}
	if (handle)
		retval = ext3_xattr_set_handle(handle, inode, name_index, "",
					       value, size, 0);
	else
		retval = ext3_xattr_set(inode, name_index, "", value, size, 0);
	if (value)
		kfree(value);
	if (!retval)
		ext3_iset_nfs4acl(inode, acl);

	return retval;
}

int
ext3_nfs4acl_permission(struct inode *inode, int want)
{
	struct nfs4acl *acl;
	int retval;

	BUG_ON(!test_opt(inode->i_sb, NFS4ACL));

	acl = ext3_get_nfs4acl(inode);
	if (!acl)
		retval = generic_permission(inode, want, NULL);
	else if (IS_ERR(acl))
		retval = PTR_ERR(acl);
	else {
		retval = nfs4acl_permission(inode, acl, want,
					    test_opt(inode->i_sb, NFS4ACL_MAX));
		nfs4acl_release(acl);
	}

	return retval;
}

int
ext3_nfs4acl_init(handle_t *handle, struct inode *inode, struct inode *dir)
{
	struct nfs4acl *dir_acl, *acl;
	int retval;

	BUG_ON(!test_opt(inode->i_sb, NFS4ACL));

	dir_acl = ext3_get_nfs4acl(dir);
	if (!dir_acl || IS_ERR(dir_acl)) {
		inode->i_mode &= ~current->fs->umask;
		return PTR_ERR(dir_acl);
	}
	acl = nfs4acl_inherit(dir_acl, inode->i_mode,
			      test_opt(inode->i_sb, NFS4ACL_MAX));
	nfs4acl_release(dir_acl);

	retval = PTR_ERR(acl);
	if (acl && !IS_ERR(acl)) {
		retval = ext3_set_nfs4acl(handle, inode, acl);
		inode->i_mode = (inode->i_mode & ~S_IRWXUGO) |
				nfs4acl_masks_to_mode(acl);
		nfs4acl_release(acl);
	}
	return retval;
}

int
ext3_nfs4acl_chmod(struct inode *inode)
{
	struct nfs4acl *acl;
	int retval;

	BUG_ON(!test_opt(inode->i_sb, NFS4ACL));

	acl = ext3_get_nfs4acl(inode);
	if (!acl || IS_ERR(acl))
		return PTR_ERR(acl);
	acl = nfs4acl_chmod(acl, inode->i_mode);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	retval = ext3_set_nfs4acl(NULL, inode, acl);
	nfs4acl_release(acl);

	return retval;
}

static size_t
ext3_xattr_list_nfs4acl(struct inode *inode, char *list, size_t list_len,
			const char *name, size_t name_len)
{
	const size_t size = sizeof(NFS4ACL_XATTR);

	if (!test_opt(inode->i_sb, NFS4ACL))
		return 0;
	if (list && size <= list_len)
		memcpy(list, NFS4ACL_XATTR, size);
	return size;
}

static int
ext3_xattr_get_nfs4acl(struct inode *inode, const char *name, void *buffer,
		       size_t buffer_size)
{
 	struct nfs4acl *acl;
	size_t size;

	if (!test_opt(inode->i_sb, NFS4ACL))
		return -EOPNOTSUPP;
	if (strcmp(name, "") != 0)
		return -EINVAL;

	acl = ext3_get_nfs4acl(inode);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;
	size = nfs4acl_xattr_size(acl);
	if (buffer) {
		if (size > buffer_size)
			return -ERANGE;
		nfs4acl_to_xattr(acl, buffer);
	}
	nfs4acl_release(acl);

	return size;
}

#ifdef NFS4ACL_DEBUG
static size_t
ext3_xattr_list_masked_nfs4acl(struct inode *inode, char *list, size_t list_len,
			       const char *name, size_t name_len)
{
	return 0;
}

static int
ext3_xattr_get_masked_nfs4acl(struct inode *inode, const char *name,
			      void *buffer, size_t buffer_size)
{
	const int name_index = EXT3_XATTR_INDEX_NFS4ACL;
	struct nfs4acl *acl;
	void *xattr;
	size_t size;
	int retval;

	if (!test_opt(inode->i_sb, NFS4ACL))
		return -EOPNOTSUPP;
	if (strcmp(name, "") != 0)
		return -EINVAL;
	retval = ext3_xattr_get(inode, name_index, "", NULL, 0);
	if (retval <= 0)
		return retval;
	xattr = kmalloc(retval, GFP_KERNEL);
	if (!xattr)
		return -ENOMEM;
	retval = ext3_xattr_get(inode, name_index, "", xattr, retval);
	if (retval <= 0)
		return retval;
	acl = nfs4acl_from_xattr(xattr, retval);
	kfree(xattr);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	retval = nfs4acl_apply_masks(&acl, test_opt(inode->i_sb, NFS4ACL_MAX));
	if (retval) {
		nfs4acl_release(acl);
		return retval;
	}
	size = nfs4acl_xattr_size(acl);
	if (buffer) {
		if (size > buffer_size)
			return -ERANGE;
		nfs4acl_to_xattr(acl, buffer);
	}
	nfs4acl_release(acl);
	return size;
}
#endif

static int
ext3_set_mode(struct inode *inode, int mode)
{
	handle_t *handle;
	int retval;

	handle = ext3_journal_start(inode, EXT3_DATA_TRANS_BLOCKS(inode->i_sb));
	retval = PTR_ERR(handle);
	if (!IS_ERR(handle)) {
		inode->i_mode = mode;
		retval = ext3_mark_inode_dirty(handle, inode);
		ext3_journal_stop(handle);
	}
	return retval;
}

static int
ext3_xattr_set_nfs4acl(struct inode *inode, const char *name,
		       const void *value, size_t size, int flags)
{
	handle_t *handle;
	struct nfs4acl *acl = NULL;
	int retval, retries = 0;

	if (!test_opt(inode->i_sb, NFS4ACL))
		return -EOPNOTSUPP;
	if (strcmp(name, "") != 0)
		return -EINVAL;
	if (value) {
		acl = nfs4acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);

		retval = ext3_set_mode(inode, (inode->i_mode & ~S_IRWXUGO) |
					      nfs4acl_masks_to_mode(acl));
		if (retval)
			goto out;
	} else
		acl = NULL;
retry:
	handle = ext3_journal_start(inode, EXT3_DATA_TRANS_BLOCKS(inode->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	retval = ext3_set_nfs4acl(handle, inode, acl);
	ext3_journal_stop(handle);
	if (retval == ENOSPC && ext3_should_retry_alloc(inode->i_sb, &retries))
		goto retry;
out:
	nfs4acl_release(acl);
	return retval;
}

struct xattr_handler ext3_nfs4acl_xattr_handler = {
	.prefix	= NFS4ACL_XATTR,
	.list	= ext3_xattr_list_nfs4acl,
	.get	= ext3_xattr_get_nfs4acl,
	.set	= ext3_xattr_set_nfs4acl,
};

#ifdef NFS4ACL_DEBUG
struct xattr_handler ext3_masked_nfs4acl_xattr_handler = {
	.prefix	= "system.masked-nfs4acl",
	.list	= ext3_xattr_list_masked_nfs4acl,
	.get	= ext3_xattr_get_masked_nfs4acl,
	.set	= ext3_xattr_set_nfs4acl,
};
#endif
