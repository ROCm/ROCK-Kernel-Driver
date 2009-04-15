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

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/nfs4acl.h>

MODULE_LICENSE("GPL");

/*
 * ACL entries that have ACE4_SPECIAL_WHO set in ace->e_flags use the
 * pointer values of these constants in ace->u.e_who to avoid massive
 * amounts of string comparisons.
 */

const char nfs4ace_owner_who[]	  = "OWNER@";
const char nfs4ace_group_who[]	  = "GROUP@";
const char nfs4ace_everyone_who[] = "EVERYONE@";

EXPORT_SYMBOL(nfs4ace_owner_who);
EXPORT_SYMBOL(nfs4ace_group_who);
EXPORT_SYMBOL(nfs4ace_everyone_who);

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
		atomic_set(&acl->a_refcount, 1);
		acl->a_count = count;
	}
	return acl;
}
EXPORT_SYMBOL(nfs4acl_alloc);

/**
 * nfs4acl_clone  -  create a copy of an acl
 */
struct nfs4acl *
nfs4acl_clone(const struct nfs4acl *acl)
{
	int count = acl->a_count;
	size_t size = sizeof(struct nfs4acl) + count * sizeof(struct nfs4ace);
	struct nfs4acl *dup = kmalloc(size, GFP_KERNEL);

	if (dup) {
		memcpy(dup, acl, size);
		atomic_set(&dup->a_refcount, 1);
	}
	return dup;
}

/*
 * The POSIX permissions are supersets of the below mask flags.
 *
 * The ACE4_READ_ATTRIBUTES and ACE4_READ_ACL flags are always granted
 * in POSIX. The ACE4_SYNCHRONIZE flag has no meaning under POSIX. We
 * make sure that we do not mask them if they are set, so that users who
 * rely on these flags won't get confused.
 */
#define ACE4_POSIX_MODE_READ ( \
	ACE4_READ_DATA | ACE4_LIST_DIRECTORY )
#define ACE4_POSIX_MODE_WRITE ( \
	ACE4_WRITE_DATA | ACE4_ADD_FILE | \
	ACE4_APPEND_DATA | ACE4_ADD_SUBDIRECTORY | \
	ACE4_DELETE_CHILD )
#define ACE4_POSIX_MODE_EXEC ( \
	ACE4_EXECUTE)

static int
nfs4acl_mask_to_mode(unsigned int mask)
{
	int mode = 0;

	if (mask & ACE4_POSIX_MODE_READ)
		mode |= MAY_READ;
	if (mask & ACE4_POSIX_MODE_WRITE)
		mode |= MAY_WRITE;
	if (mask & ACE4_POSIX_MODE_EXEC)
		mode |= MAY_EXEC;

	return mode;
}

/**
 * nfs4acl_masks_to_mode  -  compute file mode permission bits from file masks
 *
 * Compute the file mode permission bits from the file masks in the acl.
 */
int
nfs4acl_masks_to_mode(const struct nfs4acl *acl)
{
	return nfs4acl_mask_to_mode(acl->a_owner_mask) << 6 |
	       nfs4acl_mask_to_mode(acl->a_group_mask) << 3 |
	       nfs4acl_mask_to_mode(acl->a_other_mask);
}
EXPORT_SYMBOL(nfs4acl_masks_to_mode);

static unsigned int
nfs4acl_mode_to_mask(mode_t mode)
{
	unsigned int mask = ACE4_POSIX_ALWAYS_ALLOWED;

	if (mode & MAY_READ)
		mask |= ACE4_POSIX_MODE_READ;
	if (mode & MAY_WRITE)
		mask |= ACE4_POSIX_MODE_WRITE;
	if (mode & MAY_EXEC)
		mask |= ACE4_POSIX_MODE_EXEC;

	return mask;
}

/**
 * nfs4acl_chmod  -  update the file masks to reflect the new mode
 * @mode:	file mode permission bits to apply to the @acl
 *
 * Converts the mask flags corresponding to the owner, group, and other file
 * permissions and computes the file masks. Returns @acl if it already has the
 * appropriate file masks, or updates the flags in a copy of @acl. Takes over
 * @acl.
 */
struct nfs4acl *
nfs4acl_chmod(struct nfs4acl *acl, mode_t mode)
{
	unsigned int owner_mask, group_mask, other_mask;
	struct nfs4acl *clone;

	owner_mask = nfs4acl_mode_to_mask(mode >> 6);
	group_mask = nfs4acl_mode_to_mask(mode >> 3);
	other_mask = nfs4acl_mode_to_mask(mode);

	if (acl->a_owner_mask == owner_mask &&
	    acl->a_group_mask == group_mask &&
	    acl->a_other_mask == other_mask &&
	    (!nfs4acl_is_auto_inherit(acl) || nfs4acl_is_protected(acl)))
		return acl;

	clone = nfs4acl_clone(acl);
	nfs4acl_put(acl);
	if (!clone)
		return ERR_PTR(-ENOMEM);

	clone->a_owner_mask = owner_mask;
	clone->a_group_mask = group_mask;
	clone->a_other_mask = other_mask;
	if (nfs4acl_is_auto_inherit(clone))
		clone->a_flags |= ACL4_PROTECTED;

	if (nfs4acl_write_through(&clone)) {
		nfs4acl_put(clone);
		clone = ERR_PTR(-ENOMEM);
	}
	return clone;
}
EXPORT_SYMBOL(nfs4acl_chmod);

/**
 * nfs4acl_want_to_mask  - convert permission want argument to a mask
 * @want:	@want argument of the permission inode operation
 *
 * When checking for append, @want is (MAY_WRITE | MAY_APPEND).
 */
unsigned int
nfs4acl_want_to_mask(int want)
{
	unsigned int mask = 0;

	if (want & MAY_READ)
		mask |= ACE4_READ_DATA;
	if (want & MAY_APPEND)
		mask |= ACE4_APPEND_DATA;
	else if (want & MAY_WRITE)
		mask |= ACE4_WRITE_DATA;
	if (want & MAY_EXEC)
		mask |= ACE4_EXECUTE;

	return mask;
}
EXPORT_SYMBOL(nfs4acl_want_to_mask);

/**
 * nfs4acl_capability_check  -  check for capabilities overriding read/write access
 * @inode:	inode to check
 * @mask:	requested access (ACE4_* bitmask)
 *
 * Capabilities other than CAP_DAC_OVERRIDE and CAP_DAC_READ_SEARCH must be checked
 * separately.
 */
static inline int nfs4acl_capability_check(struct inode *inode, unsigned int mask)
{
	/*
	 * Read/write DACs are always overridable.
	 * Executable DACs are overridable if at least one exec bit is set.
	 */
	if (!(mask & (ACE4_WRITE_ACL | ACE4_WRITE_OWNER)) &&
	    (!(mask & ACE4_EXECUTE) ||
	    (inode->i_mode & S_IXUGO) || S_ISDIR(inode->i_mode)))
		if (capable(CAP_DAC_OVERRIDE))
			return 0;

	/*
	 * Searching includes executable on directories, else just read.
	 */
	if (!(mask & ~(ACE4_READ_DATA | ACE4_EXECUTE)) &&
	    (S_ISDIR(inode->i_mode) || !(mask & ACE4_EXECUTE)))
		if (capable(CAP_DAC_READ_SEARCH))
			return 0;

	return -EACCES;
}

/**
 * nfs4acl_permission  -  permission check algorithm with masking
 * @inode:	inode to check
 * @acl:	nfs4 acl of the inode
 * @mask:	requested access (ACE4_* bitmask)
 *
 * Checks if the current process is granted @mask flags in @acl. With
 * write-through, the OWNER@ is always granted the owner file mask, the
 * GROUP@ is always granted the group file mask, and EVERYONE@ is always
 * granted the other file mask. Otherwise, processes are only granted
 * @mask flags which they are granted in the @acl as well as in their
 * file mask.
 */
int nfs4acl_permission(struct inode *inode, const struct nfs4acl *acl,
		       unsigned int mask)
{
	const struct nfs4ace *ace;
	unsigned int file_mask, requested = mask, denied = 0;
	int in_owning_group = in_group_p(inode->i_gid);
	int owner_or_group_class = in_owning_group;

	/*
	 * A process is in the
	 *   - owner file class if it owns the file, in the
	 *   - group file class if it is in the file's owning group or
	 *     it matches any of the user or group entries, and in the
	 *   - other file class otherwise.
	 */

	nfs4acl_for_each_entry(ace, acl) {
		unsigned int ace_mask = ace->e_mask;

		if (nfs4ace_is_inherit_only(ace))
			continue;
		if (nfs4ace_is_owner(ace)) {
			if (current_fsuid() != inode->i_uid)
				continue;
			goto is_owner;
		} else if (nfs4ace_is_group(ace)) {
			if (!in_owning_group)
				continue;
		} else if (nfs4ace_is_unix_id(ace)) {
			if (ace->e_flags & ACE4_IDENTIFIER_GROUP) {
				if (!in_group_p(ace->u.e_id))
					continue;
			} else {
				if (current_fsuid() != ace->u.e_id)
					continue;
			}
		} else
			goto is_everyone;

		/*
		 * Apply the group file mask to entries other than OWNER@ and
		 * EVERYONE@. This is not required for correct access checking
		 * but ensures that we grant the same permissions as the acl
		 * computed by nfs4acl_apply_masks().
		 *
		 * For example, without this restriction, 'group@:rw::allow'
		 * with mode 0600 would grant rw access to owner processes
		 * which are also in the owning group. This cannot be expressed
		 * in an acl.
		 */
		if (nfs4ace_is_allow(ace))
			ace_mask &= acl->a_group_mask;

	    is_owner:
		/* The process is in the owner or group file class. */
		owner_or_group_class = 1;

	    is_everyone:
		/* Check which mask flags the ACE allows or denies. */
		if (nfs4ace_is_deny(ace))
			denied |= ace_mask & mask;
		mask &= ~ace_mask;

		/* Keep going until we know which file class the process is in. */
		if (!mask && owner_or_group_class)
			break;
	}
	denied |= mask;

	/*
	 * Figure out which file mask applies.
	 * Clear write-through if the process is in the file group class but
	 * not in the owning group, and so the denied permissions apply.
	 */
	if (current_fsuid() == inode->i_uid)
		file_mask = acl->a_owner_mask;
	else if (in_owning_group || owner_or_group_class)
		file_mask = acl->a_group_mask;
	else
		file_mask = acl->a_other_mask;

	denied |= requested & ~file_mask;
	if (!denied)
		return 0;
	return nfs4acl_capability_check(inode, requested);
}
EXPORT_SYMBOL(nfs4acl_permission);

/**
 * nfs4acl_generic_permission  -  permission check algorithm without explicit acl
 * @inode:	inode to check permissions for
 * @mask:	requested access (ACE4_* bitmask)
 *
 * The file mode of a file without ACL corresponds to an ACL with a single
 * "EVERYONE:~0::ALLOW" entry, with file masks that correspond to the file mode
 * permissions. Instead of constructing a temporary ACL and applying
 * nfs4acl_permission() to it, compute the identical result directly from the file
 * mode.
 */
int nfs4acl_generic_permission(struct inode *inode, unsigned int mask)
{
	int mode = inode->i_mode;

	if (current_fsuid() == inode->i_uid)
		mode >>= 6;
	else if (in_group_p(inode->i_gid))
		mode >>= 3;
	if (!(mask & ~nfs4acl_mode_to_mask(mode)))
		return 0;
	return nfs4acl_capability_check(inode, mask);
}
EXPORT_SYMBOL(nfs4acl_generic_permission);

/*
 * nfs4ace_is_same_who  -  do both acl entries refer to the same identifier?
 */
int
nfs4ace_is_same_who(const struct nfs4ace *a, const struct nfs4ace *b)
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
 * nfs4acl_set_who  -  set a special who value
 * @ace:	acl entry
 * @who:	who value to use
 */
int
nfs4ace_set_who(struct nfs4ace *ace, const char *who)
{
	if (!strcmp(who, nfs4ace_owner_who))
		who = nfs4ace_owner_who;
	else if (!strcmp(who, nfs4ace_group_who))
		who = nfs4ace_group_who;
	else if (!strcmp(who, nfs4ace_everyone_who))
		who = nfs4ace_everyone_who;
	else
		return -EINVAL;

	ace->u.e_who = who;
	ace->e_flags |= ACE4_SPECIAL_WHO;
	ace->e_flags &= ~ACE4_IDENTIFIER_GROUP;
	return 0;
}
EXPORT_SYMBOL(nfs4ace_set_who);

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
 *
 * Given the containing directory's acl, this function will compute the
 * acl that new files in that directory will inherit, or %NULL if
 * @dir_acl does not contain acl entries inheritable by this file.
 *
 * Without write-through, the file masks in the returned acl are set to
 * the intersection of the create mode and the maximum permissions
 * allowed to each file class. With write-through, the file masks are
 * set to the create mode.
 */
struct nfs4acl *
nfs4acl_inherit(const struct nfs4acl *dir_acl, mode_t mode)
{
	const struct nfs4ace *dir_ace;
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
	if (dir_acl->a_flags & ACL4_WRITE_THROUGH) {
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

	if (nfs4acl_write_through(&acl)) {
		nfs4acl_put(acl);
		return ERR_PTR(-ENOMEM);
	}

	acl->a_flags = (dir_acl->a_flags & ~ACL4_PROTECTED);
	if (nfs4acl_is_auto_inherit(acl)) {
		nfs4acl_for_each_entry(ace, acl)
			ace->e_flags |= ACE4_INHERITED_ACE;
		acl->a_flags |= ACL4_PROTECTED;
	}

	return acl;
}
EXPORT_SYMBOL(nfs4acl_inherit);
