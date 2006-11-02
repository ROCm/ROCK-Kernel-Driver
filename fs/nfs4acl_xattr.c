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
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/nfs4acl_xattr.h>

MODULE_LICENSE("GPL");

struct nfs4acl *
nfs4acl_from_xattr(const void *value, size_t size)
{
	const struct nfs4acl_xattr *xattr_acl = value;
	const struct nfs4ace_xattr *xattr_ace = (void *)(xattr_acl + 1);
	struct nfs4acl *acl;
	struct nfs4ace *ace;
	int count;

	if (size < sizeof(struct nfs4acl_xattr) ||
	    xattr_acl->a_version != ACL4_XATTR_VERSION ||
	    xattr_acl->a_flags != 0)
		return ERR_PTR(-EINVAL);

	count = be16_to_cpu(xattr_acl->a_count);
	if (count > ACL4_XATTR_MAX_COUNT)
		return ERR_PTR(-EINVAL);

	acl = nfs4acl_alloc(count);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	acl->a_owner_mask = be32_to_cpu(xattr_acl->a_owner_mask);
	if (acl->a_owner_mask & ~ACE4_VALID_MASK)
		goto fail_einval;
	acl->a_group_mask = be32_to_cpu(xattr_acl->a_group_mask);
	if (acl->a_group_mask & ~ACE4_VALID_MASK)
		goto fail_einval;
	acl->a_other_mask = be32_to_cpu(xattr_acl->a_other_mask);
	if (acl->a_other_mask & ~ACE4_VALID_MASK)
		goto fail_einval;

	nfs4acl_for_each_entry(ace, acl) {
		const char *who = (void *)(xattr_ace + 1), *end;
		ssize_t used = (void *)who - value;

		if (used > size)
			goto fail_einval;
		end = memchr(who, 0, size - used);
		if (!end)
			goto fail_einval;

		ace->e_type = be16_to_cpu(xattr_ace->e_type);
		ace->e_flags = be16_to_cpu(xattr_ace->e_flags);
		ace->e_mask = be32_to_cpu(xattr_ace->e_mask);
		ace->u.e_id = be32_to_cpu(xattr_ace->e_id);

		if (ace->e_flags & ~ACE4_VALID_FLAGS) {
			memset(ace, 0, sizeof(struct nfs4ace));
			goto fail_einval;
		}
		if (ace->e_type > ACE4_ACCESS_DENIED_ACE_TYPE ||
		    (ace->e_mask & ~ACE4_VALID_MASK))
			goto fail_einval;

		if (who == end) {
			if (ace->u.e_id == -1)
				goto fail_einval;  /* uid/gid needed */
		} else if (!strcmp(who, nfs4ace_owner_who)) {
			ace->e_flags |= ACE4_SPECIAL_WHO;
			ace->u.e_who = nfs4ace_owner_who;
		} else if (!strcmp(who, nfs4ace_group_who)) {
			ace->e_flags |= ACE4_SPECIAL_WHO;
			ace->u.e_who = nfs4ace_group_who;
		} else if (!strcmp(who, nfs4ace_everyone_who)) {
			ace->e_flags |= ACE4_SPECIAL_WHO;
			ace->u.e_who = nfs4ace_everyone_who;
		} else
			goto fail_einval;

		xattr_ace = (void *)who + ALIGN(end - who + 1, 4);
	}

	return acl;

fail_einval:
	nfs4acl_free(acl);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(nfs4acl_from_xattr);

size_t
nfs4acl_xattr_size(const struct nfs4acl *acl)
{
	size_t size = sizeof(struct nfs4acl_xattr);
	const struct nfs4ace *ace;

	nfs4acl_for_each_entry(ace, acl) {
		size += sizeof(struct nfs4ace_xattr) +
			(nfs4ace_is_unix_id(ace) ? 4 :
			 ALIGN(strlen(ace->u.e_who) + 1, 4));
	}
	return size;
}
EXPORT_SYMBOL(nfs4acl_xattr_size);

void
nfs4acl_to_xattr(const struct nfs4acl *acl, void *buffer)
{
	struct nfs4acl_xattr *xattr_acl = buffer;
	struct nfs4ace_xattr *xattr_ace;
	const struct nfs4ace *ace;

	xattr_acl->a_version = ACL4_XATTR_VERSION;
	xattr_acl->a_flags = 0;
	xattr_acl->a_count = cpu_to_be16(acl->a_count);

	xattr_acl->a_owner_mask = cpu_to_be32(acl->a_owner_mask);
	xattr_acl->a_group_mask = cpu_to_be32(acl->a_group_mask);
	xattr_acl->a_other_mask = cpu_to_be32(acl->a_other_mask);

	xattr_ace = (void *)(xattr_acl + 1);
	nfs4acl_for_each_entry(ace, acl) {
		xattr_ace->e_type = cpu_to_be16(ace->e_type);
		xattr_ace->e_flags = cpu_to_be16(ace->e_flags &
			ACE4_VALID_FLAGS);
		xattr_ace->e_mask = cpu_to_be32(ace->e_mask);
		if (nfs4ace_is_unix_id(ace)) {
			xattr_ace->e_id = cpu_to_be32(ace->u.e_id);
			memset(xattr_ace->e_who, 0, 4);
			xattr_ace = (void *)xattr_ace->e_who + 4;
		} else {
			int sz = ALIGN(strlen(ace->u.e_who) + 1, 4);

			xattr_ace->e_id = cpu_to_be32(-1);
			memset(xattr_ace->e_who + sz - 4, 0, 4);
			strcpy(xattr_ace->e_who, ace->u.e_who);
			xattr_ace = (void *)xattr_ace->e_who + sz;
		}
	}
}
EXPORT_SYMBOL_GPL(nfs4acl_to_xattr);
