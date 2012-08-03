/*
 * Copyright (C) 2010  Novell, Inc.
 * Written by Andreas Gruenbacher <agruen@suse.de>
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

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/richacl.h>

/**
 * richacl_may_create  -  helper for implementing iop->may_create
 */
int
richacl_may_create(struct inode *dir, int isdir,
		int (*richacl_permission)(struct inode *, unsigned int))
{
	if (IS_RICHACL(dir))
		return richacl_permission(dir,
				ACE4_EXECUTE | (isdir ?
				ACE4_ADD_SUBDIRECTORY : ACE4_ADD_FILE));
	else
		return generic_permission(dir, MAY_WRITE | MAY_EXEC);
}
EXPORT_SYMBOL(richacl_may_create);

static int
check_sticky(struct inode *dir, struct inode *inode)
{
	if (!(dir->i_mode & S_ISVTX))
		return 0;
	if (inode->i_uid == current_fsuid())
		return 0;
	if (dir->i_uid == current_fsuid())
		return 0;
	return !capable(CAP_FOWNER);
}

/**
 * richacl_may_delete  -  helper for implementing iop->may_delete
 */
int
richacl_may_delete(struct inode *dir, struct inode *inode, int replace,
		   int (*richacl_permission)(struct inode *, unsigned int))
{
	int error;

	if (IS_RICHACL(inode)) {
		error = richacl_permission(dir,
				ACE4_EXECUTE | ACE4_DELETE_CHILD);
		if (!error && check_sticky(dir, inode))
			error = -EPERM;
		if (error && !richacl_permission(inode, ACE4_DELETE))
			error = 0;
		if (!error && replace)
			error = richacl_permission(dir,
					ACE4_EXECUTE | (S_ISDIR(inode->i_mode) ?
					ACE4_ADD_SUBDIRECTORY : ACE4_ADD_FILE));
	} else {
		error = generic_permission(dir, MAY_WRITE | MAY_EXEC);
		if (!error && check_sticky(dir, inode))
			error = -EPERM;
	}

	return error;
}
EXPORT_SYMBOL(richacl_may_delete);

/**
 * richacl_inode_permission  -  helper for implementing iop->permission
 * @inode:	inode to check
 * @acl:	rich acl of the inode (may be NULL)
 * @mask:	requested access (ACE4_* bitmask)
 *
 * This function is supposed to be used by file systems for implementing the
 * permission inode operation.
 */
int
richacl_inode_permission(struct inode *inode, const struct richacl *acl,
			 unsigned int mask)
{
	if (acl) {
		if (!richacl_permission(inode, acl, mask))
			return 0;
	} else {
		int mode = inode->i_mode;

		if (current_fsuid() == inode->i_uid)
			mode >>= 6;
		else if (in_group_p(inode->i_gid))
			mode >>= 3;
		if (!(mask & ~richacl_mode_to_mask(mode)))
			return 0;
	}

	/*
	 * Keep in sync with the capability checks in generic_permission().
	 */
	if (!(mask & ~ACE4_POSIX_MODE_ALL)) {
		/*
		 * Read/write DACs are always overridable.
		 * Executable DACs are overridable if at
		 * least one exec bit is set.
		 */
		if (!(mask & ACE4_POSIX_MODE_EXEC) || execute_ok(inode))
			if (capable(CAP_DAC_OVERRIDE))
				return 0;
	}
	/*
	 * Searching includes executable on directories, else just read.
	 */
	if (!(mask & ~(ACE4_READ_DATA | ACE4_LIST_DIRECTORY | ACE4_EXECUTE)) &&
	    (S_ISDIR(inode->i_mode) || !(mask & ACE4_EXECUTE)))
		if (capable(CAP_DAC_READ_SEARCH))
			return 0;

	return -EACCES;
}
EXPORT_SYMBOL_GPL(richacl_inode_permission);

/**
 * richacl_inode_change_ok  -  helper for implementing iop->setattr
 * @inode:	inode to check
 * @attr:	requested inode attribute changes
 * @richacl_permission:	permission function taking an inode and ACE4_* flags
 *
 * Keep in sync with inode_change_ok().
 */
int
richacl_inode_change_ok(struct inode *inode, struct iattr *attr,
			int (*richacl_permission)(struct inode *, unsigned int))
{
	unsigned int ia_valid = attr->ia_valid;

	/* If force is set do it anyway. */
	if (ia_valid & ATTR_FORCE)
		return 0;

	/* Make sure a caller can chown. */
	if ((ia_valid & ATTR_UID) &&
	    (current_fsuid() != inode->i_uid ||
	     attr->ia_uid != inode->i_uid) &&
	    (current_fsuid() != attr->ia_uid ||
	     richacl_permission(inode, ACE4_WRITE_OWNER)) &&
	    !capable(CAP_CHOWN))
		goto error;

	/* Make sure caller can chgrp. */
	if ((ia_valid & ATTR_GID)) {
		int in_group = in_group_p(attr->ia_gid);
		if ((current_fsuid() != inode->i_uid ||
		    (!in_group && attr->ia_gid != inode->i_gid)) &&
		    (!in_group ||
		     richacl_permission(inode, ACE4_WRITE_OWNER)) &&
		    !capable(CAP_CHOWN))
			goto error;
	}

	/* Make sure a caller can chmod. */
	if (ia_valid & ATTR_MODE) {
		if (current_fsuid() != inode->i_uid &&
		    richacl_permission(inode, ACE4_WRITE_ACL) &&
		    !capable(CAP_FOWNER))
			goto error;
		/* Also check the setgid bit! */
		if (!in_group_p((ia_valid & ATTR_GID) ? attr->ia_gid :
				inode->i_gid) && !capable(CAP_FSETID))
			attr->ia_mode &= ~S_ISGID;
	}

	/* Check for setting the inode time. */
	if (ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET)) {
		if (current_fsuid() != inode->i_uid &&
		    richacl_permission(inode, ACE4_WRITE_ATTRIBUTES) &&
		    !capable(CAP_FOWNER))
			goto error;
	}
	return 0;
error:
	return -EPERM;
}
EXPORT_SYMBOL_GPL(richacl_inode_change_ok);
