/*
 * AppArmor security module
 *
 * This file contains AppArmor functions for unpacking policy loaded from
 * userspace.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * AppArmor uses a serialized binary format for loading policy.
 * The policy format is documented in Documentation/???
 * All policy is validated all before it is used.
 */

#include <asm/unaligned.h>
#include <linux/errno.h>

#include "include/security/apparmor.h"
#include "include/audit.h"
#include "include/context.h"
#include "include/match.h"
#include "include/policy.h"
#include "include/policy_interface.h"
#include "include/sid.h"

/* FIXME: convert profiles to internal hieracy, accounting
 * FIXME: have replacement routines set replaced_by profile instead of error
 * FIXME: name mapping to hierarchy
 */

/*
 * The AppArmor interface treats data as a type byte followed by the
 * actual data.  The interface has the notion of a a named entry
 * which has a name (AA_NAME typecode followed by name string) followed by
 * the entries typecode and data.  Named types allow for optional
 * elements and extensions to be added and tested for without breaking
 * backwards compatability.
 */

enum aa_code {
	AA_U8,
	AA_U16,
	AA_U32,
	AA_U64,
	AA_NAME,	/* same as string except it is items name */
	AA_STRING,
	AA_BLOB,
	AA_STRUCT,
	AA_STRUCTEND,
	AA_LIST,
	AA_LISTEND,
	AA_ARRAY,
	AA_ARRAYEND,
};

/*
 * aa_ext is the read of the buffer containing the serialized profile.  The
 * data is copied into a kernel buffer in apparmorfs and then handed off to
 * the unpack routines.
 */
struct aa_ext {
	void *start;
	void *end;
	void *pos;	/* pointer to current position in the buffer */
	u32 version;
	char *ns_name;
};


struct aa_audit_iface {
	struct aa_audit base;

	const char *name;
	const char *name2;
	const struct aa_ext *e;
};

static void aa_audit_init(struct aa_audit_iface *sa, const char *operation,
			  struct aa_ext *e)
{
	memset(sa, 0, sizeof(*sa));
	sa->base.operation = operation;
	sa->base.gfp_mask = GFP_KERNEL;
	sa->e = e;
}

static void audit_cb(struct audit_buffer *ab, void *va)
{
	struct aa_audit_iface *sa = va;

	if (sa->name)
		audit_log_format(ab, " name=%s", sa->name);
	if (sa->name2)
		audit_log_format(ab, " namespace=%s", sa->name2);
	if (sa->base.error && sa->e) {
		long len = sa->e->pos - sa->e->start;
		audit_log_format(ab, " offset=%ld", len);
	}
}

static int aa_audit_iface(struct aa_audit_iface *sa)
{
	struct aa_profile *profile;
	struct cred *cred = aa_get_task_policy(current, &profile);
	int error = aa_audit(AUDIT_APPARMOR_STATUS, profile, &sa->base,
			     audit_cb);
	put_cred(cred);
	return error;
}

static int aa_inbounds(struct aa_ext *e, size_t size)
{
	return (size <= e->end - e->pos);
}

/**
 * aa_u16_chunck - test and do bounds checking for a u16 size based chunk
 * @e: serialized data read head
 * @chunk: start address for chunk of data
 *
 * return the size of chunk found with the read head at the end of
 * the chunk.
 */
static size_t aa_is_u16_chunk(struct aa_ext *e, char **chunk)
{
	void *pos = e->pos;
	size_t size = 0;

	if (!aa_inbounds(e, sizeof(u16)))
		goto fail;
	size = le16_to_cpu(get_unaligned((u16 *)e->pos));
	e->pos += sizeof(u16);
	if (!aa_inbounds(e, size))
		goto fail;
	*chunk = e->pos;
	e->pos += size;
	return size;

fail:
	e->pos = pos;
	return 0;
}

static int aa_is_X(struct aa_ext *e, enum aa_code code)
{
	if (!aa_inbounds(e, 1))
		return 0;
	if (*(u8 *) e->pos != code)
		return 0;
	e->pos++;
	return 1;
}

/**
 * aa_is_nameX - check is the next element is of type X with a name of @name
 * @e: serialized data extent information
 * @code: type code
 * @name: name to match to the serialized element.
 *
 * check that the next serialized data element is of type X and has a tag
 * name @name.  If @name is specified then there must be a matching
 * name element in the stream.  If @name is NULL any name element will be
 * skipped and only the typecode will be tested.
 * returns 1 on success (both type code and name tests match) and the read
 * head is advanced past the headers
 * returns %0 if either match failes, the read head does not move
 */
static int aa_is_nameX(struct aa_ext *e, enum aa_code code, const char *name)
{
	void *pos = e->pos;
	/*
	 * Check for presence of a tagname, and if present name size
	 * AA_NAME tag value is a u16.
	 */
	if (aa_is_X(e, AA_NAME)) {
		char *tag = NULL;
		size_t size = aa_is_u16_chunk(e, &tag);
		/* if a name is specified it must match. otherwise skip tag */
		if (name && (!size || strcmp(name, tag)))
			goto fail;
	} else if (name) {
		/* if a name is specified and there is no name tag fail */
		goto fail;
	}

	/* now check if type code matches */
	if (aa_is_X(e, code))
		return 1;

fail:
	e->pos = pos;
	return 0;
}

static int aa_is_u16(struct aa_ext *e, u16 *data, const char *name)
{
	void *pos = e->pos;
	if (aa_is_nameX(e, AA_U16, name)) {
		if (!aa_inbounds(e, sizeof(u16)))
			goto fail;
		if (data)
			*data = le16_to_cpu(get_unaligned((u16 *)e->pos));
		e->pos += sizeof(u16);
		return 1;
	}
fail:
	e->pos = pos;
	return 0;
}

static int aa_is_u32(struct aa_ext *e, u32 *data, const char *name)
{
	void *pos = e->pos;
	if (aa_is_nameX(e, AA_U32, name)) {
		if (!aa_inbounds(e, sizeof(u32)))
			goto fail;
		if (data)
			*data = le32_to_cpu(get_unaligned((u32 *)e->pos));
		e->pos += sizeof(u32);
		return 1;
	}
fail:
	e->pos = pos;
	return 0;
}

static int aa_is_u64(struct aa_ext *e, u64 *data, const char *name)
{
	void *pos = e->pos;
	if (aa_is_nameX(e, AA_U64, name)) {
		if (!aa_inbounds(e, sizeof(u64)))
			goto fail;
		if (data)
			*data = le64_to_cpu(get_unaligned((u64 *)e->pos));
		e->pos += sizeof(u64);
		return 1;
	}
fail:
	e->pos = pos;
	return 0;
}

static size_t aa_is_array(struct aa_ext *e, const char *name)
{
	void *pos = e->pos;
	if (aa_is_nameX(e, AA_ARRAY, name)) {
		int size;
		if (!aa_inbounds(e, sizeof(u16)))
			goto fail;
		size = (int) le16_to_cpu(get_unaligned((u16 *)e->pos));
		e->pos += sizeof(u16);
		return size;
	}
fail:
	e->pos = pos;
	return 0;
}

static size_t aa_is_blob(struct aa_ext *e, char **blob, const char *name)
{
	void *pos = e->pos;
	if (aa_is_nameX(e, AA_BLOB, name)) {
		u32 size;
		if (!aa_inbounds(e, sizeof(u32)))
			goto fail;
		size = le32_to_cpu(get_unaligned((u32 *)e->pos));
		e->pos += sizeof(u32);
		if (aa_inbounds(e, (size_t) size)) {
			*blob = e->pos;
			e->pos += size;
			return size;
		}
	}
fail:
	e->pos = pos;
	return 0;
}

static int aa_is_string(struct aa_ext *e, char **string, const char *name)
{
	char *src_str;
	size_t size = 0;
	void *pos = e->pos;
	*string = NULL;
	if (aa_is_nameX(e, AA_STRING, name) &&
	    (size = aa_is_u16_chunk(e, &src_str))) {
		/* strings are null terminated, length is size - 1 */
		if (src_str[size - 1] != 0)
			goto fail;
		*string = src_str;
	}

	return size;

fail:
	e->pos = pos;
	return 0;
}

static int aa_is_dynstring(struct aa_ext *e, char **string, const char *name)
{
	char *tmp;
	void *pos = e->pos;
	int res = aa_is_string(e, &tmp, name);
	*string = NULL;

	if (!res)
		return res;

	*string = kstrdup(tmp, GFP_KERNEL);
	if (!*string) {
		e->pos = pos;
		return 0;
	}

	return res;
}

/**
 * aa_unpack_dfa - unpack a file rule dfa
 * @e: serialized data extent information
 *
 * returns dfa or ERR_PTR
 */
static struct aa_dfa *aa_unpack_dfa(struct aa_ext *e)
{
	char *blob = NULL;
	size_t size, error = 0;
	struct aa_dfa *dfa = NULL;

	size = aa_is_blob(e, &blob, "aadfa");
	if (size) {
		dfa = aa_match_alloc();
		if (dfa) {
			/*
			 * The dfa is aligned with in the blob to 8 bytes
			 * from the beginning of the stream.
			 */
			size_t sz = blob - (char *) e->start;
			size_t pad = ALIGN(sz, 8) - sz;
			error = unpack_dfa(dfa, blob + pad, size - pad);
			if (!error)
				error = verify_dfa(dfa);
		} else {
			error = -ENOMEM;
		}

		if (error) {
			aa_match_free(dfa);
			dfa = ERR_PTR(error);
		}
	}

	return dfa;
}

static int aa_unpack_trans_table(struct aa_ext *e, struct aa_profile *profile)
{
	void *pos = e->pos;

	/* exec table is optional */
	if (aa_is_nameX(e, AA_STRUCT, "xtable")) {
		int i, size;

		size = aa_is_array(e, NULL);
		/* currently 4 exec bits and entries 0-3 are reserved iupcx */
		if (size > 16 - 4)
			goto fail;
		profile->file.trans.table = kzalloc(sizeof(char *) * size,
							  GFP_KERNEL);
		if (!profile->file.trans.table)
			goto fail;

		for (i = 0; i < size; i++) {
		    char *tmp;
			if (!aa_is_dynstring(e, &tmp, NULL))
				goto fail;
			/* note: strings beginning with a : have an embedded
			   \0 seperating the profile ns name from the profile
			   name */
			profile->file.trans.table[i] = tmp;
		}
		if (!aa_is_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!aa_is_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
		profile->file.trans.size = size;
	}
	return 1;

fail:
	e->pos = pos;
	return 0;
}

int aa_unpack_rlimits(struct aa_ext *e, struct aa_profile *profile)
{
	void *pos = e->pos;

	/* rlimits are optional */
	if (aa_is_nameX(e, AA_STRUCT, "rlimits")) {
		int i, size;
		u32 tmp = 0;
		if (!aa_is_u32(e, &tmp, NULL))
			goto fail;
		profile->rlimits.mask = tmp;

		size = aa_is_array(e, NULL);
		if (size > RLIM_NLIMITS)
			goto fail;
		for (i = 0; i < size; i++) {
			u64 tmp = 0;
			if (!aa_is_u64(e, &tmp, NULL))
				goto fail;
			profile->rlimits.limits[i].rlim_max = tmp;
		}
		if (!aa_is_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!aa_is_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}
	return 1;

fail:
	e->pos = pos;
	return 0;
}

/**
 * aa_unpack_profile - unpack a serialized profile
 * @e: serialized data extent information
 * @sa: audit struct for the operation
 */
static struct aa_profile *aa_unpack_profile(struct aa_ext *e,
					    struct aa_audit_iface *sa)
{
	struct aa_profile *profile = NULL;
	char *name;
	size_t size = 0;
	int i, error = -EPROTO;
	u32 tmp;

	/* check that we have the right struct being passed */
	if (!aa_is_nameX(e, AA_STRUCT, "profile"))
		goto fail;
	if (!aa_is_string(e, &name, NULL))
		goto fail;

	profile = alloc_aa_profile(name);
	if (!profile)
		return ERR_PTR(-ENOMEM);

	/* xmatch is optional and may be NULL */
	profile->xmatch = aa_unpack_dfa(e);
	if (IS_ERR(profile->xmatch)) {
		error = PTR_ERR(profile->xmatch);
		profile->xmatch = NULL;
		goto fail;
	}
	/* xmatch_len is not optional is xmatch is set */
	if (profile->xmatch && !aa_is_u32(e, &tmp, NULL))
		goto fail;
	profile->xmatch_len = tmp;

	/* per profile debug flags (complain, audit) */
	if (!aa_is_nameX(e, AA_STRUCT, "flags"))
		goto fail;
	if (!aa_is_u32(e, &tmp, NULL))
		goto fail;
	if (tmp)
		profile->flags |= PFLAG_HAT;
	if (!aa_is_u32(e, &tmp, NULL))
		goto fail;
	if (tmp)
		profile->mode = APPARMOR_COMPLAIN;
	if (!aa_is_u32(e, &tmp, NULL))
		goto fail;
	if (tmp)
		profile->audit = AUDIT_ALL;

	if (!aa_is_nameX(e, AA_STRUCTEND, NULL))
		goto fail;

	if (!aa_is_u32(e, &(profile->caps.allowed.cap[0]), NULL))
		goto fail;
	if (!aa_is_u32(e, &(profile->caps.audit.cap[0]), NULL))
		goto fail;
	if (!aa_is_u32(e, &(profile->caps.quiet.cap[0]), NULL))
		goto fail;
	if (!aa_is_u32(e, &(profile->caps.set.cap[0]), NULL))
		goto fail;

	if (aa_is_nameX(e, AA_STRUCT, "caps64")) {
		/* optional upper half of 64 bit caps */
		if (!aa_is_u32(e, &(profile->caps.allowed.cap[1]), NULL))
			goto fail;
		if (!aa_is_u32(e, &(profile->caps.audit.cap[1]), NULL))
			goto fail;
		if (!aa_is_u32(e, &(profile->caps.quiet.cap[1]), NULL))
			goto fail;
		if (!aa_is_u32(e, &(profile->caps.set.cap[1]), NULL))
			goto fail;
		if (!aa_is_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
	}

	if (!aa_unpack_rlimits(e, profile))
		goto fail;

	size = aa_is_array(e, "net_allowed_af");
	if (size) {
		if (size > AF_MAX)
			goto fail;

		for (i = 0; i < size; i++) {
			if (!aa_is_u16(e, &profile->net.allowed[i], NULL))
				goto fail;
			if (!aa_is_u16(e, &profile->net.audit[i], NULL))
				goto fail;
			if (!aa_is_u16(e, &profile->net.quiet[i], NULL))
				goto fail;
		}
		if (!aa_is_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		/* allow unix domain and netlink sockets they are handled
		 * by IPC
		 */
	}
	profile->net.allowed[AF_UNIX] = 0xffff;
	profile->net.allowed[AF_NETLINK] = 0xffff;

	/* get file rules */
	profile->file.dfa = aa_unpack_dfa(e);
	if (IS_ERR(profile->file.dfa)) {
		error = PTR_ERR(profile->file.dfa);
		profile->file.dfa = NULL;
		goto fail;
	}

	if (!aa_unpack_trans_table(e, profile))
		goto fail;

	if (!aa_is_nameX(e, AA_STRUCTEND, NULL))
		goto fail;

	return profile;

fail:
	sa->name = profile && profile->base.name ? profile->base.name :
						   "unknown";
	if (!sa->base.info)
		sa->base.info = "failed to unpack profile";
	aa_audit_iface(sa);

	free_aa_profile(profile);

	return ERR_PTR(error);
}

/**
 * aa_verify_head - unpack serialized stream header
 * @e: serialized data read head
 * @operation: operation header is being verified for
 *
 * returns error or 0 if header is good
 */
static int aa_verify_header(struct aa_ext *e, struct aa_audit_iface *sa)
{
	/* get the interface version */
	if (!aa_is_u32(e, &e->version, "version")) {
		sa->base.info = "invalid profile format";
		aa_audit_iface(sa);
		return -EPROTONOSUPPORT;
	}

	/* check that the interface version is currently supported */
	if (e->version != 5) {
		sa->base.info = "unsupported interface version";
		aa_audit_iface(sa);
		return -EPROTONOSUPPORT;
	}

	/* read the namespace if present */
	if (!aa_is_string(e, &e->ns_name, "namespace"))
		e->ns_name = NULL;

	return 0;
}



/**
 * aa_interface_add_profiles - Unpack and add new profile(s) to the profile list
 * @data: serialized data stream
 * @size: size of the serialized data stream
 */
ssize_t aa_interface_add_profiles(void *data, size_t size)
{
	struct aa_profile *profile;
	struct aa_namespace *ns = NULL;
	struct aa_policy_common *common;
	struct aa_ext e = {
		.start = data,
		.end = data + size,
		.pos = data,
		.ns_name = NULL
	};
	ssize_t error;
	struct aa_audit_iface sa;
	aa_audit_init(&sa, "profile_load", &e);

	error = aa_verify_header(&e, &sa);
	if (error)
		return error;

	profile = aa_unpack_profile(&e, &sa);
	if (IS_ERR(profile))
		return PTR_ERR(profile);

	sa.name2 = e.ns_name;
	ns = aa_prepare_namespace(e.ns_name);
	if (IS_ERR(ns)) {
		sa.base.info = "failed to prepare namespace";
		sa.base.error = PTR_ERR(ns);
		goto fail;
	}
	/* profiles are currently loaded flat with fqnames */
	sa.name = profile->fqname;

	write_lock(&ns->base.lock);

	common = __aa_find_parent_by_fqname(ns, sa.name);
	if (!common) {
		sa.base.info = "parent does not exist";
		sa.base.error = -ENOENT;
		goto fail2;
	}

	if (common != &ns->base)
		profile->parent = aa_get_profile((struct aa_profile *) common);

	if (__aa_find_profile(&common->profiles, profile->base.name)) {
		/* A profile with this name exists already. */
		sa.base.info = "profile already exists";
		sa.base.error = -EEXIST;
		goto fail2;
	}
	profile->sid = aa_alloc_sid(AA_ALLOC_SYS_SID);
	profile->ns = aa_get_namespace(ns);

	__aa_add_profile(common, profile);
	write_unlock(&ns->base.lock);

	aa_audit_iface(&sa);
	aa_put_namespace(ns);
	kfree(e.ns_name);
	return size;

fail2:
	write_unlock(&ns->base.lock);

fail:
	error = aa_audit_iface(&sa);
	aa_put_namespace(ns);
	aa_put_profile(profile);
	kfree(e.ns_name);
	return error;
}

/**
 * aa_interface_replace_profiles - replace profile(s) on the profile list
 * @udata: serialized data stream
 * @size: size of the serialized data stream
 *
 * unpack and replace a profile on the profile list and uses of that profile
 * by any aa_task_context.  If the profile does not exist on the profile list
 * it is added.  Return %0 or error.
 */
ssize_t aa_interface_replace_profiles(void *udata, size_t size)
{
	struct aa_policy_common *common;
	struct aa_profile *old_profile = NULL, *new_profile;
	struct aa_namespace *ns;
	struct aa_ext e = {
		.start = udata,
		.end = udata + size,
		.pos = udata,
		.ns_name = NULL
	};
	ssize_t error;
	struct aa_audit_iface sa;
	aa_audit_init(&sa, "profile_replace", &e);

	if (g_apparmor_lock_policy)
		return -EACCES;

	error = aa_verify_header(&e, &sa);
	if (error)
		return error;

	new_profile = aa_unpack_profile(&e, &sa);
	if (IS_ERR(new_profile))
		return PTR_ERR(new_profile);

	sa.name2 = e.ns_name;
	ns = aa_prepare_namespace(e.ns_name);
	if (!ns) {
		sa.base.info = "failed to prepare namespace";
		sa.base.error = -ENOMEM;
		goto fail;
	}		

	sa.name = new_profile->fqname;

	write_lock(&ns->base.lock);
	common = __aa_find_parent_by_fqname(ns, sa.name);

	if (!common) {
		sa.base.info = "parent does not exist";
		sa.base.error = -ENOENT;
		goto fail2;
	}

	if (common != &ns->base)
		new_profile->parent = aa_get_profile((struct aa_profile *)
						     common);

	old_profile = __aa_find_profile(&common->profiles,
					new_profile->base.name);
	aa_get_profile(old_profile);
	if (old_profile && old_profile->flags & PFLAG_IMMUTABLE) {
		sa.base.info = "cannot replace immutible profile";
		sa.base.error = -EPERM;
		goto fail2;
	} else  if (old_profile) {
	  //		__aa_profile_list_release(&old_profile->base.profiles);
		/* TODO: remove for new interface
		 * move children profiles over to the new profile so
		 * that replacement behaves correctly
		 */
	  //		list_replace_init(&old_profile->base.profiles,
	  //				  &new_profile->base.profiles);
	  struct aa_profile *profile, *tmp;
	  list_for_each_entry_safe(profile, tmp, &old_profile->base.profiles,
				    base.list) {
			aa_put_profile(profile->parent);
			list_del(&profile->base.list);
			profile->parent = aa_get_profile(new_profile);
			list_add(&profile->base.list,
				 &new_profile->base.profiles);
		}
		__aa_replace_profile(old_profile, new_profile);
		new_profile->sid = old_profile->sid;
	} else {
		__aa_add_profile(common, new_profile);
		new_profile->sid = aa_alloc_sid(AA_ALLOC_SYS_SID);
	}

	new_profile->ns = aa_get_namespace(ns);

	write_unlock(&ns->base.lock);

	if (!old_profile)
		sa.base.operation = "profile_load";

	aa_audit_iface(&sa);
	aa_put_namespace(ns);
	aa_put_profile(old_profile);
	kfree(e.ns_name);
	return size;

fail2:
	write_unlock(&ns->base.lock);
fail:
	error = aa_audit_iface(&sa);
	aa_put_namespace(ns);
	aa_put_profile(old_profile);
	aa_put_profile(new_profile);
	kfree(e.ns_name);
	return error;
}

/**
 * aa_interface_remove_profiles - remove profile(s) from the system
 * @name: name of the profile to remove
 * @size: size of the name
 *
 * remove a profile from the profile list and all aa_task_context references
 * to said profile.
 * NOTE: removing confinement does not restore rlimits to preconfinemnet values
 */
ssize_t aa_interface_remove_profiles(char *name, size_t size)
{
	struct aa_namespace *ns;
	struct aa_profile *profile;
	struct aa_audit_iface sa;
	aa_audit_init(&sa, "profile_remove", NULL);

	if (g_apparmor_lock_policy)
		return -EACCES;

	write_lock(&ns_list_lock);
	if (name[0] == ':') {
		char *ns_name;
		name = aa_split_name_from_ns(name, &ns_name);
		ns = __aa_find_namespace(&ns_list, ns_name);
	} else {
		ns = aa_get_namespace(default_namespace);
	}

	if (!ns) {
		sa.base.info = "failed: namespace does not exist";
		goto fail_ns_list_lock;
	}

	sa.name2 = ns->base.name;
	write_lock(&ns->base.lock);
	if (!name) {
		/* remove namespace */
	  //		__aa_remove_namespace(ns);
	} else {
		/* remove profile */
		profile = __aa_find_profile_by_fqname(ns, name);
		if (!profile) {
			sa.name = name;
			sa.base.info = "failed: profile does not exist";
			goto fail_ns_lock;
		}
		sa.name = profile->fqname;
		__aa_profile_list_release(&profile->base.profiles);
		__aa_remove_profile(profile, profile->ns->unconfined);
	}
	write_unlock(&ns->base.lock);
	write_unlock(&ns_list_lock);

	aa_audit_iface(&sa);
	aa_put_namespace(ns);
	return size;

fail_ns_lock:
	write_unlock(&ns->base.lock);

fail_ns_list_lock:
	write_unlock(&ns_list_lock);
	aa_audit_iface(&sa);
	return -ENOENT;
}
