/*
 * Copyright (C) 2006, 2010  Novell, Inc.
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

MODULE_LICENSE("GPL");

/*
 * Special e_who identifiers:  ACEs which have ACE4_SPECIAL_WHO set in
 * ace->e_flags use these constants in ace->u.e_who.
 *
 * For efficiency, we compare pointers instead of comparing strings.
 */
const char richace_owner_who[]	  = "OWNER@";
EXPORT_SYMBOL_GPL(richace_owner_who);
const char richace_group_who[]	  = "GROUP@";
EXPORT_SYMBOL_GPL(richace_group_who);
const char richace_everyone_who[] = "EVERYONE@";
EXPORT_SYMBOL_GPL(richace_everyone_who);

/**
 * richacl_alloc  -  allocate a richacl
 * @count:	number of entries
 */
struct richacl *
richacl_alloc(int count)
{
	size_t size = sizeof(struct richacl) + count * sizeof(struct richace);
	struct richacl *acl = kzalloc(size, GFP_KERNEL);

	if (acl) {
		atomic_set(&acl->a_refcount, 1);
		acl->a_count = count;
	}
	return acl;
}
EXPORT_SYMBOL_GPL(richacl_alloc);

/**
 * richacl_clone  -  create a copy of a richacl
 */
static struct richacl *
richacl_clone(const struct richacl *acl)
{
	int count = acl->a_count;
	size_t size = sizeof(struct richacl) + count * sizeof(struct richace);
	struct richacl *dup = kmalloc(size, GFP_KERNEL);

	if (dup) {
		memcpy(dup, acl, size);
		atomic_set(&dup->a_refcount, 1);
	}
	return dup;
}

/**
 * richace_is_same_identifier  -  are both identifiers the same?
 */
int
richace_is_same_identifier(const struct richace *a, const struct richace *b)
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
 * richacl_set_who  -  set a special who value
 * @ace:	acl entry
 * @who:	who value to use
 */
int
richace_set_who(struct richace *ace, const char *who)
{
	if (!strcmp(who, richace_owner_who))
		who = richace_owner_who;
	else if (!strcmp(who, richace_group_who))
		who = richace_group_who;
	else if (!strcmp(who, richace_everyone_who))
		who = richace_everyone_who;
	else
		return -EINVAL;

	ace->u.e_who = who;
	ace->e_flags |= ACE4_SPECIAL_WHO;
	ace->e_flags &= ~ACE4_IDENTIFIER_GROUP;
	return 0;
}
EXPORT_SYMBOL_GPL(richace_set_who);
