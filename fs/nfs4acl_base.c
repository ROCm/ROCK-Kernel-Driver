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

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/nfs4acl.h>

MODULE_LICENSE("GPL");

/**
 * ACL entries that have ACE4_SPECIAL_WHO set in ace->e_flags use the
 * pointer values of these constants in ace->u.e_who to avoid massive
 * amounts of string comparisons.
 */

const char *nfs4ace_owner_who	 = "OWNER@";
const char *nfs4ace_group_who	 = "GROUP@";
const char *nfs4ace_everyone_who = "EVERYONE@";

EXPORT_SYMBOL_GPL(nfs4ace_owner_who);
EXPORT_SYMBOL_GPL(nfs4ace_group_who);
EXPORT_SYMBOL_GPL(nfs4ace_everyone_who);

/**
 * nfs4acl_alloc  -  allocate an acl
 * @count:	number of entries
 */
struct nfs4acl *
nfs4acl_alloc(int count)
{
	size_t size = sizeof(struct nfs4acl) + count * sizeof(struct nfs4ace);
	struct nfs4acl *acl = kmalloc(size, GFP_KERNEL);

	if (acl) {
		memset(acl, 0, size);
		acl->a_count = count;
	}
	return acl;
}

#if 0
/**
 * nfs4acl_clone  -  create a copy of an acl
 */
struct nfs4acl *
nfs4acl_clone(struct nfs4acl *acl)
{
	struct nfs4acl *dup = nfs4acl_alloc(acl->a_count);

	if (acl) {
		memcpy(dup, acl, sizeof(struct nfs4acl) +
		       sizeof(struct nfs4ace) * acl->a_count);
	}
	return acl;
}
EXPORT_SYMBOL(nfs4acl_clone);
#endif

/**
 * The POSIX permissions are supersets of the below mask flags.
 *
 * The ACE4_READ_ATTRIBUTES and ACE4_READ_ACL flags are always granted
 * in POSIX. The ACE4_SYNCHRONIZE flag has no meaning under POSIX. We
 * make sure that we do not mask them if they are set, so that users who
 * rely on these flags won't get confused.
 */
#define ACE4_GENERIC_READ ( \
	ACE4_READ_DATA)
#define ACE4_GENERIC_WRITE ( \
	ACE4_WRITE_DATA | \
	ACE4_APPEND_DATA | \
	ACE4_DELETE_CHILD)
#define ACE4_GENERIC_EXEC ( \
	ACE4_EXECUTE)
#define ACE4_ALWAYS_ALLOWED ( \
	ACE4_SYNCHRONIZE | \
	ACE4_READ_ATTRIBUTES | \
	ACE4_READ_ACL )

static int
nfs4acl_mask_to_mode(unsigned int mask)
{
	int mode = 0;

	if (mask & ACE4_GENERIC_READ)
		mode |= MAY_READ;
	if (mask & ACE4_GENERIC_WRITE)
		mode |= MAY_WRITE;
	if (mask & ACE4_GENERIC_EXEC)
		mode |= MAY_EXEC;

	return mode;
}

/**
 * nfs4acl_masks_to_mode  -  compute file mode permission bits from file masks
 *
 * Compute the file mode permission bits from the file masks in the acl.
 */
int
nfs4acl_masks_to_mode(struct nfs4acl *acl)
{
	return nfs4acl_mask_to_mode(acl->a_owner_mask) << 6 |
	       nfs4acl_mask_to_mode(acl->a_group_mask) << 3 |
	       nfs4acl_mask_to_mode(acl->a_other_mask);
}
EXPORT_SYMBOL_GPL(nfs4acl_masks_to_mode);

static unsigned int
nfs4acl_mode_to_mask(mode_t mode)
{
	unsigned int mask = 0;

	if (mode & MAY_READ)
		mask |= ACE4_ALWAYS_ALLOWED | ACE4_GENERIC_READ;
	if (mode & MAY_WRITE)
		mask |= ACE4_ALWAYS_ALLOWED | ACE4_GENERIC_WRITE;
	if (mode & MAY_EXEC)
		mask |= ACE4_ALWAYS_ALLOWED | ACE4_GENERIC_EXEC;

	return mask;
}

/**
 * nfs4acl_chmod  -  update the file masks to reflect the new mode
 * @mode:	file mode permission bits to apply to the @acl
 *
 * Converts the mask flags corresponding to the owner, group, and other file
 * permissions and updates the file masks in @acl accordingly.
 */
void
nfs4acl_chmod(struct nfs4acl *acl, mode_t mode)
{
	acl->a_owner_mask = nfs4acl_mode_to_mask(mode >> 6);
	acl->a_group_mask = nfs4acl_mode_to_mask(mode >> 3);
	acl->a_other_mask = nfs4acl_mode_to_mask(mode);
}
EXPORT_SYMBOL_GPL(nfs4acl_chmod);

/**
 * nfs4acl_want_to_mask  - convert permission want argument to a mask
 * @want:	@want argument of the permission inode operation
 */
static unsigned int
nfs4acl_want_to_mask(int want)
{
	unsigned int mask = 0;

	if (want & MAY_READ)
		mask |= ACE4_READ_DATA;
	if (want & MAY_WRITE)
		mask |= ACE4_WRITE_DATA;
	if (want & MAY_EXEC)
		mask |= ACE4_EXECUTE;

	return mask;
}

/**
 * __nfs4acl_permission  -  permission check algorithm without masking
 * @inode:	inode to check permissions for
 * @acl:	nfs4 acl if the inode
 * @mask:	requested mask flags
 * @in_group_class: returns if the process matches a group class acl entry
 *
 * Checks if the current process is granted @mask flags in @acl. The
 * @inode determines the file type, current owner and owning group.
 *
 * In addition to checking permissions, this function checks if the
 * process matches a group class acl entry even after the result of the
 * permission check has been determined: for non-owners, this property
 * determines which file mask applies to the acl.
 */
static int
__nfs4acl_permission(struct inode *inode, const struct nfs4acl *acl,
		     unsigned int mask, int *in_group_class)
{
	const struct nfs4ace *ace;
	int retval = -EACCES;

	nfs4acl_for_each_entry(ace, acl) {
		unsigned int ace_mask = ace->e_mask;

		if (nfs4ace_is_inherit_only(ace))
			continue;

		/**
		 * Check if the ACE matches the process. Remember when a
		 * group class ACE matches the process.
		 *
		 * For ACEs other than OWNER@ and EVERYONE@, we apply the
		 * file group mask: this would not strictly be necessary for
		 * access checking, but it saves us from allowing accesses
		 * that the ACL that nfs4acl_apply_masks() would deny:
		 * otherwise, ACLs like 'group@:rw::allow' with mode 0600
		 * would allow processes that match the owner and the
		 * owning group rw access. This scenario cannot be expressed
		 * as an ACL.
		 */
		if (nfs4ace_is_owner(ace)) {
			if (current->fsuid != inode->i_uid)
				continue;
		} else if (nfs4ace_is_group(ace)) {
			if (!in_group_p(inode->i_gid))
				continue;
			ace_mask &= acl->a_group_mask;
			*in_group_class = 1;
		} else if (nfs4ace_is_unix_id(ace)) {
			if (ace->e_flags & ACE4_IDENTIFIER_GROUP) {
				if (!in_group_p(ace->u.e_id))
					continue;
			} else {
				if (current->fsuid != ace->u.e_id)
					continue;
			}
			ace_mask &= acl->a_group_mask;
			*in_group_class = 1;
		} else if (!nfs4ace_is_everyone(ace))
			continue;

		/* Check which mask flags the ACE allows or denies. */
		if (nfs4ace_is_allow(ace)) {
			if (!S_ISDIR(inode->i_mode)) {
				/* Everybody who is allowed ACE4_WRITE_DATA is
				   also allowed ACE4_APPEND_DATA. */
				if (ace_mask & ACE4_WRITE_DATA)
					ace_mask |= ACE4_APPEND_DATA;
			}
			mask &= ~ace_mask;
			if (mask == 0) {
				retval = 0;
				goto check_remaining_aces;
			}
		} else if (nfs4ace_is_deny(ace)) {
			unsigned int ace_mask = ace->e_mask;

			if (!S_ISDIR(inode->i_mode)) {
				/* Everybody who is denied ACE4_APPEND_DATA is
				   also denied ACE4_WRITE_DATA. */
				if (ace_mask & ACE4_APPEND_DATA)
					ace_mask |= ACE4_WRITE_DATA;
			}
			if (mask & ace_mask)
				goto check_remaining_aces;
		}
	}
	return retval;

check_remaining_aces:
	/* Check if any of the remaining group class ACEs match the process. */
	for (ace++; ace != acl->a_entries + acl->a_count; ace++) {
		if (nfs4ace_is_group(ace)) {
			if (in_group_p(inode->i_gid))
				*in_group_class = 1;
		} else if (nfs4ace_is_unix_id(ace)) {
			if (ace->e_flags & ACE4_IDENTIFIER_GROUP) {
				if (in_group_p(ace->u.e_id))
					*in_group_class = 1;
			} else {
				if (current->fsuid == ace->u.e_id)
					*in_group_class = 1;
			}
		}
	}
	return retval;
}

/**
 * nfs4acl_permission  -  permission check algorithm with masking
 * @inode:	inode to check permissions for
 * @acl:	nfs4 acl if the inode
 * @want:	requested access (permission want argument)
 * @write_through: assume that the masks "writes through" to the acl
 *
 * Checks if the current process is granted @mask flags in @acl. If
 * @write_through is true, then the OWNER@ is always granted the owner
 * mask, the GROUP@ is always granted the group mask, and EVERYONE@ is
 * always granted the other mask. Otherwise, the OWNER@, GROUP@, and
 * EVERYONE@ are only granted mask flags which they are granted both in
 * the @acl and in the file mask that applies to the process (which
 * depends on which file class the process is in).
 */
int
nfs4acl_permission(struct inode *inode, const struct nfs4acl *acl, int want,
		   int write_through)
{
	unsigned int mask = nfs4acl_want_to_mask(want);
	int in_group_class = 0, retval;

	retval = __nfs4acl_permission(inode, acl, mask, &in_group_class);
	if (write_through) {
		if (current->fsuid == inode->i_uid ||
		    in_group_p(inode->i_gid) ||
		    !in_group_class)
			retval = 0;  /* only check the mask */
	}
	if (retval)
		goto capability_check;
	if (current->fsuid == inode->i_uid) {
		if (mask & ~acl->a_owner_mask)
			goto capability_check;
	} else if (in_group_p(inode->i_gid) || in_group_class) {
		if (mask & ~acl->a_group_mask)
			goto capability_check;
	} else {
		if (mask & ~acl->a_other_mask)
			goto capability_check;
	}
	return 0;

capability_check:
	/*
	 * Read/write DACs are always overridable.
	 * Executable DACs are overridable if at least one exec bit is set.
	 */
	if (!(want & MAY_EXEC) ||
	    (inode->i_mode & S_IXUGO) || S_ISDIR(inode->i_mode))
		if (capable(CAP_DAC_OVERRIDE))
			return 0;

	/*
	 * Searching includes executable on directories, else just read.
	 */
	if (want == MAY_READ || (S_ISDIR(inode->i_mode) && !(want & MAY_WRITE)))
		if (capable(CAP_DAC_READ_SEARCH))
			return 0;
	return -EACCES;

}
EXPORT_SYMBOL_GPL(nfs4acl_permission);

/**
 * nfs4ace_is_same_who  -  do both acl entries refer to the same identifier?
 */
int
nfs4ace_is_same_who(struct nfs4ace *a, struct nfs4ace *b)
{
#define WHO_FLAGS (ACE4_SPECIAL_WHO | ACE4_IDENTIFIER_GROUP)
	if ((a->e_flags & WHO_FLAGS) != (b->e_flags & WHO_FLAGS))
		return 0;
	if (a->e_flags & ACE4_SPECIAL_WHO)
		return a->u.e_who == b->u.e_who;
	else
		return a->u.e_id == b->u.e_id;
#undef WHO_FLAGS
}

/**
 * nfs4acl_allowed_to_who  -  mask flags allowed to a specific who value
 *
 * Computes the mask values allowed to a specific who value, taking
 * EVERYONE@ entries into account.
 */
static unsigned int
nfs4acl_allowed_to_who(struct nfs4acl *acl, struct nfs4ace *who)
{
	struct nfs4ace *ace;
	unsigned int allowed = 0;

	nfs4acl_for_each_entry_reverse(ace, acl) {
		if (nfs4ace_is_inherit_only(ace))
			continue;
		if (nfs4ace_is_same_who(ace, who) ||
		    nfs4ace_is_everyone(ace)) {
			if (nfs4ace_is_allow(ace))
				allowed |= ace->e_mask;
			else if (nfs4ace_is_deny(ace))
				allowed &= ~ace->e_mask;
		}
	}
	return allowed;
}

/**
 * nfs4acl_compute_max_masks  -  compute upper bound masks
 *
 * Computes upper bound owner, group, and other masks so that none of
 * the mask flags allowed by the acl are disabled (for any choice of the
 * file owner or group membership).
 */
static void
nfs4acl_compute_max_masks(struct nfs4acl *acl)
{
	struct nfs4ace *ace;

	acl->a_owner_mask = 0;
	acl->a_group_mask = 0;
	acl->a_other_mask = 0;

	nfs4acl_for_each_entry_reverse(ace, acl) {
		if (nfs4ace_is_inherit_only(ace))
			continue;

		if (nfs4ace_is_owner(ace)) {
			if (nfs4ace_is_allow(ace))
				acl->a_owner_mask |= ace->e_mask;
			else if (nfs4ace_is_deny(ace))
				acl->a_owner_mask &= ~ace->e_mask;
		} else if (nfs4ace_is_everyone(ace)) {
			if (nfs4ace_is_allow(ace)) {
				struct nfs4ace who = {
					.e_flags = ACE4_SPECIAL_WHO,
					.u.e_who = nfs4ace_group_who,
				};

				acl->a_other_mask |= ace->e_mask;
				acl->a_group_mask |=
					nfs4acl_allowed_to_who(acl, &who);
				acl->a_owner_mask |= ace->e_mask;
			} else if (nfs4ace_is_deny(ace)) {
				acl->a_other_mask &= ~ace->e_mask;
				acl->a_group_mask &= ~ace->e_mask;
				acl->a_owner_mask &= ~ace->e_mask;
			}
		} else {
			if (nfs4ace_is_allow(ace)) {
				unsigned int mask =
					nfs4acl_allowed_to_who(acl, ace);

				acl->a_group_mask |= mask;
				acl->a_owner_mask |= mask;
			}
		}
	}
}

/**
 * nfs4acl_inherit  -  compute the acl a new file will inherit
 * @dir_acl:	acl of the containing direcory
 * @mode:	file type and create mode of the new file
 * @write_through: assume that the mode "writes through" to the acl
 *
 * Given the containing directory's acl, this function will compute the
 * acl that new files in that directory will inherit, or %NULL if
 * @dir_acl does not contain acl entries inheritable by this file.
 *
 * Without @write_through, the file masks in the returned acl are set to
 * the intersection of the create mode and the maximum permissions
 * allowed to each file class. With @write_through, the file masks are
 * set to the create mode.
 */
struct nfs4acl *
nfs4acl_inherit(struct nfs4acl *dir_acl, mode_t mode, int write_through)
{
	struct nfs4ace *dir_ace;
	struct nfs4acl *acl;
	struct nfs4ace *ace;
	int count = 0;

	if (S_ISDIR(mode)) {
		nfs4acl_for_each_entry(dir_ace, dir_acl) {
			if (!nfs4ace_is_inheritable(dir_ace))
				continue;
			count++;
		}
		if (!count)
			return NULL;
		acl = nfs4acl_alloc(count);
		if (!acl)
			return ERR_PTR(-ENOMEM);
		ace = acl->a_entries;
		nfs4acl_for_each_entry(dir_ace, dir_acl) {
			if (!nfs4ace_is_inheritable(dir_ace))
				continue;
			memcpy(ace, dir_ace, sizeof(struct nfs4ace));
			if (dir_ace->e_flags & ACE4_NO_PROPAGATE_INHERIT_ACE)
				nfs4ace_clear_inheritance_flags(ace);
			if ((dir_ace->e_flags & ACE4_FILE_INHERIT_ACE) &&
			    !(dir_ace->e_flags & ACE4_DIRECTORY_INHERIT_ACE))
				ace->e_flags |= ACE4_INHERIT_ONLY_ACE;
			ace++;
		}
	} else {
		nfs4acl_for_each_entry(dir_ace, dir_acl) {
			if (!(dir_ace->e_flags & ACE4_FILE_INHERIT_ACE))
				continue;
			count++;
		}
		if (!count)
			return NULL;
		acl = nfs4acl_alloc(count);
		if (!acl)
			return ERR_PTR(-ENOMEM);
		ace = acl->a_entries;
		nfs4acl_for_each_entry(dir_ace, dir_acl) {
			if (!(dir_ace->e_flags & ACE4_FILE_INHERIT_ACE))
				continue;
			memcpy(ace, dir_ace, sizeof(struct nfs4ace));
			nfs4ace_clear_inheritance_flags(ace);
			ace++;
		}
	}

	/* The maximum max flags that the owner, group, and other classes
	   are allowed. */
	if (write_through) {
		acl->a_owner_mask = ACE4_VALID_MASK;
		acl->a_group_mask = ACE4_VALID_MASK;
		acl->a_other_mask = ACE4_VALID_MASK;

		mode &= ~current->fs->umask;
	} else
		nfs4acl_compute_max_masks(acl);

	/* Apply the create mode. */
	acl->a_owner_mask &= nfs4acl_mode_to_mask(mode >> 6);
	acl->a_group_mask &= nfs4acl_mode_to_mask(mode >> 3);
	acl->a_other_mask &= nfs4acl_mode_to_mask(mode);

	return acl;
}
EXPORT_SYMBOL_GPL(nfs4acl_inherit);
