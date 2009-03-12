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

/**
 * struct nfs4acl_alloc  -  remember how many entries are actually allocated
 * @acl:	acl with a_count <= @count
 * @count:	the actual number of entries allocated in @acl
 *
 * We pass around this structure while modifying an acl, so that we do
 * not have to reallocate when we remove existing entries followed by
 * adding new entries.
 */
struct nfs4acl_alloc {
	struct nfs4acl *acl;
	unsigned int count;
};

/**
 * nfs4acl_delete_entry  -  delete an entry in an acl
 * @x:		acl and number of allocated entries
 * @ace:	an entry in @x->acl
 *
 * Updates @ace so that it points to the entry before the deleted entry
 * on return. (When deleting the first entry, @ace will point to the
 * (non-existant) entry before the first entry). This behavior is the
 * expected behavior when deleting entries while forward iterating over
 * an acl.
 */
static void
nfs4acl_delete_entry(struct nfs4acl_alloc *x, struct nfs4ace **ace)
{
	void *end = x->acl->a_entries + x->acl->a_count;

	memmove(*ace, *ace + 1, end - (void *)(*ace + 1));
	(*ace)--;
	x->acl->a_count--;
}

/**
 * nfs4acl_insert_entry  -  insert an entry in an acl
 * @x:		acl and number of allocated entries
 * @ace:	entry before which the new entry shall be inserted
 *
 * Insert a new entry in @x->acl at position @ace, and zero-initialize
 * it.  This may require reallocating @x->acl.
 */
static int
nfs4acl_insert_entry(struct nfs4acl_alloc *x, struct nfs4ace **ace)
{
	if (x->count == x->acl->a_count) {
		int n = *ace - x->acl->a_entries;
		struct nfs4acl *acl2;

		acl2 = nfs4acl_alloc(x->acl->a_count + 1);
		if (!acl2)
			return -1;
		acl2->a_flags = x->acl->a_flags;
		acl2->a_owner_mask = x->acl->a_owner_mask;
		acl2->a_group_mask = x->acl->a_group_mask;
		acl2->a_other_mask = x->acl->a_other_mask;
		memcpy(acl2->a_entries, x->acl->a_entries,
		       n * sizeof(struct nfs4ace));
		memcpy(acl2->a_entries + n + 1, *ace,
		       (x->acl->a_count - n) * sizeof(struct nfs4ace));
		kfree(x->acl);
		x->acl = acl2;
		x->count = acl2->a_count;
		*ace = acl2->a_entries + n;
	} else {
		void *end = x->acl->a_entries + x->acl->a_count;

		memmove(*ace + 1, *ace, end - (void *)*ace);
		x->acl->a_count++;
	}
	memset(*ace, 0, sizeof(struct nfs4ace));
	return 0;
}

/**
 * nfs4ace_change_mask  -  change the mask in @ace to @mask
 * @x:		acl and number of allocated entries
 * @ace:	entry to modify
 * @mask:	new mask for @ace
 *
 * Set the effective mask of @ace to @mask. This will require splitting
 * off a separate acl entry if @ace is inheritable. In that case, the
 * effective- only acl entry is inserted after the inheritable acl
 * entry, end the inheritable acl entry is set to inheritable-only. If
 * @mode is 0, either set the original acl entry to inheritable-only if
 * it was inheritable, or remove it otherwise.  The returned @ace points
 * to the modified or inserted effective-only acl entry if that entry
 * exists, to the entry that has become inheritable-only, or else to the
 * previous entry in the acl. This is the expected behavior when
 * modifying masks while forward iterating over an acl.
 */
static int
nfs4ace_change_mask(struct nfs4acl_alloc *x, struct nfs4ace **ace,
			   unsigned int mask)
{
	if (mask && (*ace)->e_mask == mask)
		return 0;
	if (mask & ~ACE4_POSIX_ALWAYS_ALLOWED) {
		if (nfs4ace_is_inheritable(*ace)) {
			if (nfs4acl_insert_entry(x, ace))
				return -1;
			memcpy(*ace, *ace + 1, sizeof(struct nfs4ace));
			(*ace)->e_flags |= ACE4_INHERIT_ONLY_ACE;
			(*ace)++;
			nfs4ace_clear_inheritance_flags(*ace);
		}
		(*ace)->e_mask = mask;
	} else {
		if (nfs4ace_is_inheritable(*ace))
			(*ace)->e_flags |= ACE4_INHERIT_ONLY_ACE;
		else
			nfs4acl_delete_entry(x, ace);
	}
	return 0;
}

/**
 * nfs4acl_move_everyone_aces_down  -  move everyone@ acl entries to the end
 * @x:		acl and number of allocated entries
 *
 * Move all everyone acl entries to the bottom of the acl so that only a
 * single everyone@ allow acl entry remains at the end, and update the
 * mask fields of all acl entries on the way. If everyone@ is not
 * granted any permissions, no empty everyone@ acl entry is inserted.
 *
 * This transformation does not modify the permissions that the acl
 * grants, but we need it to simplify successive transformations.
 */
static int
nfs4acl_move_everyone_aces_down(struct nfs4acl_alloc *x)
{
	struct nfs4ace *ace;
	unsigned int allowed = 0, denied = 0;

	nfs4acl_for_each_entry(ace, x->acl) {
		if (nfs4ace_is_inherit_only(ace))
			continue;
		if (nfs4ace_is_everyone(ace)) {
			if (nfs4ace_is_allow(ace))
				allowed |= (ace->e_mask & ~denied);
			else if (nfs4ace_is_deny(ace))
				denied |= (ace->e_mask & ~allowed);
			else
				continue;
			if (nfs4ace_change_mask(x, &ace, 0))
				return -1;
		} else {
			if (nfs4ace_is_allow(ace)) {
				if (nfs4ace_change_mask(x, &ace, allowed |
						(ace->e_mask & ~denied)))
					return -1;
			} else if (nfs4ace_is_deny(ace)) {
				if (nfs4ace_change_mask(x, &ace, denied |
						(ace->e_mask & ~allowed)))
					return -1;
			}
		}
	}
	if (allowed & ~ACE4_POSIX_ALWAYS_ALLOWED) {
		struct nfs4ace *last_ace = ace - 1;

		if (nfs4ace_is_everyone(last_ace) &&
		    nfs4ace_is_allow(last_ace) &&
		    nfs4ace_is_inherit_only(last_ace) &&
		    last_ace->e_mask == allowed)
			last_ace->e_flags &= ~ACE4_INHERIT_ONLY_ACE;
		else {
			if (nfs4acl_insert_entry(x, &ace))
				return -1;
			ace->e_type = ACE4_ACCESS_ALLOWED_ACE_TYPE;
			ace->e_flags = ACE4_SPECIAL_WHO;
			ace->e_mask = allowed;
			ace->u.e_who = nfs4ace_everyone_who;
		}
	}
	return 0;
}

/**
 * __nfs4acl_propagate_everyone  -  propagate everyone@ mask flags up for @who
 * @x:		acl and number of allocated entries
 * @who:	identifier to propagate mask flags for
 * @allow:	mask flags to propagate up
 *
 * Propagate mask flags from the trailing everyone@ allow acl entry up
 * for the specified @who.
 *
 * The idea here is to precede the trailing EVERYONE@ ALLOW entry by an
 * additional @who ALLOW entry, but with the following optimizations:
 * (1) we don't bother setting any flags in the new @who ALLOW entry
 * that has already been allowed or denied by a previous @who entry, (2)
 * we merge the new @who entry with a previous @who entry if there is
 * such a previous @who entry and there are no intervening DENY entries
 * with mask flags that overlap the flags we care about.
 */
static int
__nfs4acl_propagate_everyone(struct nfs4acl_alloc *x, struct nfs4ace *who,
			  unsigned int allow)
{
	struct nfs4ace *allow_last = NULL, *ace;

	/* Remove the mask flags from allow that are already determined for
	   this who value, and figure out if there is an ALLOW entry for
	   this who value that is "reachable" from the trailing EVERYONE@
	   ALLOW ACE. */
	nfs4acl_for_each_entry(ace, x->acl) {
		if (nfs4ace_is_inherit_only(ace))
			continue;
		if (nfs4ace_is_allow(ace)) {
			if (nfs4ace_is_same_who(ace, who)) {
				allow &= ~ace->e_mask;
				allow_last = ace;
			}
		} else if (nfs4ace_is_deny(ace)) {
			if (nfs4ace_is_same_who(ace, who))
				allow &= ~ace->e_mask;
			if (allow & ace->e_mask)
				allow_last = NULL;
		}
	}

	if (allow) {
		if (allow_last)
			return nfs4ace_change_mask(x, &allow_last,
						   allow_last->e_mask | allow);
		else {
			struct nfs4ace who_copy;

			ace = x->acl->a_entries + x->acl->a_count - 1;
			memcpy(&who_copy, who, sizeof(struct nfs4ace));
			if (nfs4acl_insert_entry(x, &ace))
				return -1;
			memcpy(ace, &who_copy, sizeof(struct nfs4ace));
			ace->e_type = ACE4_ACCESS_ALLOWED_ACE_TYPE;
			nfs4ace_clear_inheritance_flags(ace);
			ace->e_mask = allow;
		}
	}
	return 0;
}

/**
 * nfs4acl_propagate_everyone  -  propagate everyone@ mask flags up the acl
 * @x:		acl and number of allocated entries
 *
 * Make sure for owner@, group@, and all other users, groups, and
 * special identifiers that they are allowed or denied all permissions
 * that are granted be the trailing everyone@ acl entry. If they are
 * not, try to add the missing permissions to existing allow acl entries
 * for those users, or introduce additional acl entries if that is not
 * possible.
 *
 * We do this so that no mask flags will get lost when finally applying
 * the file masks to the acl entries: otherwise, with an other file mask
 * that is more restrictive than the owner and/or group file mask, mask
 * flags that were allowed to processes in the owner and group classes
 * and that the other mask denies would be lost. For example, the
 * following two acls show the problem when mode 0664 is applied to
 * them:
 *
 *    masking without propagation (wrong)
 *    ===========================================================
 *    joe:r::allow		=> joe:r::allow
 *    everyone@:rwx::allow	=> everyone@:r::allow
 *    -----------------------------------------------------------
 *    joe:w::deny		=> joe:w::deny
 *    everyone@:rwx::allow	   everyone@:r::allow
 *
 * Note that the permissions of joe end up being more restrictive than
 * what the acl would allow when first computing the allowed flags and
 * then applying the respective mask. With propagation of permissions,
 * we get:
 *
 *    masking after propagation (correct)
 *    ===========================================================
 *    joe:r::allow		=> joe:rw::allow
 *				   owner@:rw::allow
 *				   group@:rw::allow
 *    everyone@:rwx::allow	   everyone@:r::allow
 *    -----------------------------------------------------------
 *    joe:w::deny		=> owner@:x::deny
 *				   joe:w::deny
 *				   owner@:rw::allow
 *				   owner@:rw::allow
 *				   joe:r::allow
 *    everyone@:rwx::allow	   everyone@:r::allow
 *
 * The examples show the acls that would result from propagation with no
 * masking performed. In fact, we do apply the respective mask to the
 * acl entries before computing the propagation because this will save
 * us from adding acl entries that would end up with empty mask fields
 * after applying the masks.
 *
 * It is ensured that no more than one entry will be inserted for each
 * who value, no matter how many entries each who value has already.
 */
static int
nfs4acl_propagate_everyone(struct nfs4acl_alloc *x)
{
	int write_through = (x->acl->a_flags & ACL4_WRITE_THROUGH);
	struct nfs4ace who = { .e_flags = ACE4_SPECIAL_WHO };
	struct nfs4ace *ace;
	unsigned int owner_allow, group_allow;
	int retval;

	if (!((x->acl->a_owner_mask | x->acl->a_group_mask) &
	      ~x->acl->a_other_mask))
		return 0;
	if (!x->acl->a_count)
		return 0;
	ace = x->acl->a_entries + x->acl->a_count - 1;
	if (nfs4ace_is_inherit_only(ace) || !nfs4ace_is_everyone(ace))
		return 0;
	if (!(ace->e_mask & ~x->acl->a_other_mask)) {
		/* None of the allowed permissions will get masked. */
		return 0;
	}
	owner_allow = ace->e_mask & x->acl->a_owner_mask;
	group_allow = ace->e_mask & x->acl->a_group_mask;

	/* Propagate everyone@ permissions through to owner@. */
	if (owner_allow && !write_through &&
	    (x->acl->a_owner_mask & ~x->acl->a_other_mask)) {
		who.u.e_who = nfs4ace_owner_who;
		retval = __nfs4acl_propagate_everyone(x, &who, owner_allow);
		if (retval)
			return -1;
	}

	if (group_allow && (x->acl->a_group_mask & ~x->acl->a_other_mask)) {
		int n;

		if (!write_through) {
			/* Propagate everyone@ permissions through to group@. */
			who.u.e_who = nfs4ace_group_who;
			retval = __nfs4acl_propagate_everyone(x, &who,
							      group_allow);
			if (retval)
				return -1;
		}

		/* Start from the entry before the trailing EVERYONE@ ALLOW
		   entry. We will not hit EVERYONE@ entries in the loop. */
		for (n = x->acl->a_count - 2; n != -1; n--) {
			ace = x->acl->a_entries + n;

			if (nfs4ace_is_inherit_only(ace) ||
			    nfs4ace_is_owner(ace) ||
			    nfs4ace_is_group(ace))
				continue;
			if (nfs4ace_is_allow(ace) || nfs4ace_is_deny(ace)) {
				/* Any inserted entry will end up below the
				   current entry. */
				retval = __nfs4acl_propagate_everyone(x, ace,
								   group_allow);
				if (retval)
					return -1;
			}
		}
	}
	return 0;
}

/**
 * __nfs4acl_apply_masks  -  apply the masks to the acl entries
 * @x:		acl and number of allocated entries
 *
 * Apply the owner file mask to owner@ entries, the intersection of the
 * group and other file masks to everyone@ entries, and the group file
 * mask to all other entries.
 */
static int
__nfs4acl_apply_masks(struct nfs4acl_alloc *x)
{
	struct nfs4ace *ace;

	nfs4acl_for_each_entry(ace, x->acl) {
		unsigned int mask;

		if (nfs4ace_is_inherit_only(ace) || !nfs4ace_is_allow(ace))
			continue;
		if (nfs4ace_is_owner(ace))
			mask = x->acl->a_owner_mask;
		else if (nfs4ace_is_everyone(ace))
			mask = x->acl->a_other_mask;
		else
			mask = x->acl->a_group_mask;
		if (nfs4ace_change_mask(x, &ace, ace->e_mask & mask))
			return -1;
	}
	return 0;
}

/**
 * nfs4acl_max_allowed  -  maximum mask flags that anybody is allowed
 */
static unsigned int
nfs4acl_max_allowed(struct nfs4acl *acl)
{
	struct nfs4ace *ace;
	unsigned int allowed = 0;

	nfs4acl_for_each_entry_reverse(ace, acl) {
		if (nfs4ace_is_inherit_only(ace))
			continue;
		if (nfs4ace_is_allow(ace))
			allowed |= ace->e_mask;
		else if (nfs4ace_is_deny(ace)) {
			if (nfs4ace_is_everyone(ace))
				allowed &= ~ace->e_mask;
		}
	}
	return allowed;
}

/**
 * nfs4acl_isolate_owner_class  -  limit the owner class to the owner file mask
 * @x:		acl and number of allocated entries
 *
 * Make sure the owner class (owner@) is granted no more than the owner
 * mask by first checking which permissions anyone is granted, and then
 * denying owner@ all permissions beyond that.
 */
static int
nfs4acl_isolate_owner_class(struct nfs4acl_alloc *x)
{
	struct nfs4ace *ace;
	unsigned int allowed = 0;

	allowed = nfs4acl_max_allowed(x->acl);
	if (allowed & ~x->acl->a_owner_mask) {
		/* Figure out if we can update an existig OWNER@ DENY entry. */
		nfs4acl_for_each_entry(ace, x->acl) {
			if (nfs4ace_is_inherit_only(ace))
				continue;
			if (nfs4ace_is_deny(ace)) {
				if (nfs4ace_is_owner(ace))
					break;
			} else if (nfs4ace_is_allow(ace)) {
				ace = x->acl->a_entries + x->acl->a_count;
				break;
			}
		}
		if (ace != x->acl->a_entries + x->acl->a_count) {
			if (nfs4ace_change_mask(x, &ace, ace->e_mask |
					(allowed & ~x->acl->a_owner_mask)))
				return -1;
		} else {
			/* Insert an owner@ deny entry at the front. */
			ace = x->acl->a_entries;
			if (nfs4acl_insert_entry(x, &ace))
				return -1;
			ace->e_type = ACE4_ACCESS_DENIED_ACE_TYPE;
			ace->e_flags = ACE4_SPECIAL_WHO;
			ace->e_mask = allowed & ~x->acl->a_owner_mask;
			ace->u.e_who = nfs4ace_owner_who;
		}
	}
	return 0;
}

/**
 * __nfs4acl_isolate_who  -  isolate entry from EVERYONE@ ALLOW entry
 * @x:		acl and number of allocated entries
 * @who:	identifier to isolate
 * @deny:	mask flags this identifier should not be allowed
 *
 * Make sure that @who is not allowed any mask flags in @deny by checking
 * which mask flags this identifier is allowed, and adding excess allowed
 * mask flags to an existing DENY entry before the trailing EVERYONE@ ALLOW
 * entry, or inserting such an entry.
 */
static int
__nfs4acl_isolate_who(struct nfs4acl_alloc *x, struct nfs4ace *who,
		      unsigned int deny)
{
	struct nfs4ace *ace;
	unsigned int allowed = 0, n;

	/* Compute the mask flags granted to this who value. */
	nfs4acl_for_each_entry_reverse(ace, x->acl) {
		if (nfs4ace_is_inherit_only(ace))
			continue;
		if (nfs4ace_is_same_who(ace, who)) {
			if (nfs4ace_is_allow(ace))
				allowed |= ace->e_mask;
			else if (nfs4ace_is_deny(ace))
				allowed &= ~ace->e_mask;
			deny &= ~ace->e_mask;
		}
	}
	if (!deny)
		return 0;

	/* Figure out if we can update an existig DENY entry.  Start
	   from the entry before the trailing EVERYONE@ ALLOW entry. We
	   will not hit EVERYONE@ entries in the loop. */
	for (n = x->acl->a_count - 2; n != -1; n--) {
		ace = x->acl->a_entries + n;
		if (nfs4ace_is_inherit_only(ace))
			continue;
		if (nfs4ace_is_deny(ace)) {
			if (nfs4ace_is_same_who(ace, who))
				break;
		} else if (nfs4ace_is_allow(ace) &&
			   (ace->e_mask & deny)) {
			n = -1;
			break;
		}
	}
	if (n != -1) {
		if (nfs4ace_change_mask(x, &ace, ace->e_mask | deny))
			return -1;
	} else {
		/* Insert a eny entry before the trailing EVERYONE@ DENY
		   entry. */
		struct nfs4ace who_copy;

		ace = x->acl->a_entries + x->acl->a_count - 1;
		memcpy(&who_copy, who, sizeof(struct nfs4ace));
		if (nfs4acl_insert_entry(x, &ace))
			return -1;
		memcpy(ace, &who_copy, sizeof(struct nfs4ace));
		ace->e_type = ACE4_ACCESS_DENIED_ACE_TYPE;
		nfs4ace_clear_inheritance_flags(ace);
		ace->e_mask = deny;
	}
	return 0;
}

/**
 * nfs4acl_isolate_group_class  -  limit the group class to the group file mask
 * @x:		acl and number of allocated entries
 *
 * Make sure the group class (all entries except owner@ and everyone@) is
 * granted no more than the group mask by inserting DENY entries for group
 * class entries where necessary.
 */
static int
nfs4acl_isolate_group_class(struct nfs4acl_alloc *x)
{
	struct nfs4ace who = {
		.e_flags = ACE4_SPECIAL_WHO,
		.u.e_who = nfs4ace_group_who,
	};
	struct nfs4ace *ace;
	unsigned int deny;

	if (!x->acl->a_count)
		return 0;
	ace = x->acl->a_entries + x->acl->a_count - 1;
	if (nfs4ace_is_inherit_only(ace) || !nfs4ace_is_everyone(ace))
		return 0;
	deny = ace->e_mask & ~x->acl->a_group_mask;

	if (deny) {
		unsigned int n;

		if (__nfs4acl_isolate_who(x, &who, deny))
			return -1;

		/* Start from the entry before the trailing EVERYONE@ ALLOW
		   entry. We will not hit EVERYONE@ entries in the loop. */
		for (n = x->acl->a_count - 2; n != -1; n--) {
			ace = x->acl->a_entries + n;

			if (nfs4ace_is_inherit_only(ace) ||
			    nfs4ace_is_owner(ace) ||
			    nfs4ace_is_group(ace))
				continue;
			if (__nfs4acl_isolate_who(x, ace, deny))
				return -1;
		}
	}
	return 0;
}

/**
 * __nfs4acl_write_through  -  grant the full masks to owner@, group@, everyone@
 *
 * Make sure that owner, group@, and everyone@ are allowed the full mask
 * permissions, and not only the permissions granted both by the acl and
 * the masks.
 */
static int
__nfs4acl_write_through(struct nfs4acl_alloc *x)
{
	struct nfs4ace *ace;
	unsigned int allowed;

	/* Remove all owner@ and group@ ACEs: we re-insert them at the
	   top. */
	nfs4acl_for_each_entry(ace, x->acl) {
		if (nfs4ace_is_inherit_only(ace))
			continue;
		if ((nfs4ace_is_owner(ace) || nfs4ace_is_group(ace)) &&
		    nfs4ace_change_mask(x, &ace, 0))
			return -1;
	}

	/* Insert the everyone@ allow entry at the end, or update the
	   existing entry. */
	allowed = x->acl->a_other_mask;
	if (allowed & ~ACE4_POSIX_ALWAYS_ALLOWED) {
		ace = x->acl->a_entries + x->acl->a_count - 1;
		if (x->acl->a_count && nfs4ace_is_everyone(ace) &&
		    !nfs4ace_is_inherit_only(ace)) {
			if (nfs4ace_change_mask(x, &ace, allowed))
				return -1;
		} else {
			ace = x->acl->a_entries + x->acl->a_count;
			if (nfs4acl_insert_entry(x, &ace))
				return -1;
			ace->e_type = ACE4_ACCESS_ALLOWED_ACE_TYPE;
			ace->e_flags = ACE4_SPECIAL_WHO;
			ace->e_mask = allowed;
			ace->u.e_who = nfs4ace_everyone_who;
		}
	}

	/* Compute the permissions that owner@ and group@ are already granted
	   though the everyone@ allow entry at the end. Note that the acl
	   contains no owner@ or group@ entries at this point. */
	allowed = 0;
	nfs4acl_for_each_entry_reverse(ace, x->acl) {
		if (nfs4ace_is_inherit_only(ace))
			continue;
		if (nfs4ace_is_allow(ace)) {
			if (nfs4ace_is_everyone(ace))
				allowed |= ace->e_mask;
		} else if (nfs4ace_is_deny(ace))
				allowed &= ~ace->e_mask;
	}

	/* Insert the appropriate group@ allow entry at the front. */
	if (x->acl->a_group_mask & ~allowed) {
		ace = x->acl->a_entries;
		if (nfs4acl_insert_entry(x, &ace))
			return -1;
		ace->e_type = ACE4_ACCESS_ALLOWED_ACE_TYPE;
		ace->e_flags = ACE4_SPECIAL_WHO;
		ace->e_mask = x->acl->a_group_mask /*& ~allowed*/;
		ace->u.e_who = nfs4ace_group_who;
	}

	/* Insert the appropriate owner@ allow entry at the front. */
	if (x->acl->a_owner_mask & ~allowed) {
		ace = x->acl->a_entries;
		if (nfs4acl_insert_entry(x, &ace))
			return -1;
		ace->e_type = ACE4_ACCESS_ALLOWED_ACE_TYPE;
		ace->e_flags = ACE4_SPECIAL_WHO;
		ace->e_mask = x->acl->a_owner_mask /*& ~allowed*/;
		ace->u.e_who = nfs4ace_owner_who;
	}

	/* Insert the appropriate owner@ deny entry at the front. */
	allowed = nfs4acl_max_allowed(x->acl);
	if (allowed & ~x->acl->a_owner_mask) {
		nfs4acl_for_each_entry(ace, x->acl) {
			if (nfs4ace_is_inherit_only(ace))
				continue;
			if (nfs4ace_is_allow(ace)) {
				ace = x->acl->a_entries + x->acl->a_count;
				break;
			}
			if (nfs4ace_is_deny(ace) && nfs4ace_is_owner(ace))
				break;
		}
		if (ace != x->acl->a_entries + x->acl->a_count) {
			if (nfs4ace_change_mask(x, &ace, ace->e_mask |
					(allowed & ~x->acl->a_owner_mask)))
				return -1;
		} else {
			ace = x->acl->a_entries;
			if (nfs4acl_insert_entry(x, &ace))
				return -1;
			ace->e_type = ACE4_ACCESS_DENIED_ACE_TYPE;
			ace->e_flags = ACE4_SPECIAL_WHO;
			ace->e_mask = allowed & ~x->acl->a_owner_mask;
			ace->u.e_who = nfs4ace_owner_who;
		}
	}

	return 0;
}

/**
 * nfs4acl_apply_masks  -  apply the masks to the acl
 *
 * Apply the masks so that the acl allows no more flags than the
 * intersection between the flags that the original acl allows and the
 * mask matching the process.
 *
 * Note: this algorithm may push the number of entries in the acl above
 * ACL4_XATTR_MAX_COUNT, so a read-modify-write cycle would fail.
 */
int
nfs4acl_apply_masks(struct nfs4acl **acl)
{
	struct nfs4acl_alloc x = {
		.acl = *acl,
		.count = (*acl)->a_count,
	};
	int retval = 0;

	if (nfs4acl_move_everyone_aces_down(&x) ||
	    nfs4acl_propagate_everyone(&x) ||
	    __nfs4acl_apply_masks(&x) ||
	    nfs4acl_isolate_owner_class(&x) ||
	    nfs4acl_isolate_group_class(&x))
		retval = -ENOMEM;

	*acl = x.acl;
	return retval;
}
EXPORT_SYMBOL(nfs4acl_apply_masks);

int nfs4acl_write_through(struct nfs4acl **acl)
{
	struct nfs4acl_alloc x = {
		.acl = *acl,
		.count = (*acl)->a_count,
	};
	int retval = 0;

	if (!((*acl)->a_flags & ACL4_WRITE_THROUGH))
		goto out;

	if (nfs4acl_move_everyone_aces_down(&x) ||
	    nfs4acl_propagate_everyone(&x) ||
	    __nfs4acl_write_through(&x))
		retval = -ENOMEM;

	*acl = x.acl;
out:
	return retval;
}
