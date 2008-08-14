/*
 *	Copyright (C) 1998-2007 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor userspace policy interface
 */

#include <asm/unaligned.h>

#include "apparmor.h"
#include "inline.h"

/*
 * This mutex is used to synchronize profile adds, replacements, and
 * removals: we only allow one of these operations at a time.
 * We do not use the profile list lock here in order to avoid blocking
 * exec during those operations.  (Exec involves a profile list lookup
 * for named-profile transitions.)
 */
DEFINE_MUTEX(aa_interface_lock);

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

static inline int aa_inbounds(struct aa_ext *e, size_t size)
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

static inline int aa_is_X(struct aa_ext *e, enum aa_code code)
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
		char *tag;
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
			* blob = e->pos;
			e->pos += size;
			return size;
		}
	}
fail:
	e->pos = pos;
	return 0;
}

static int aa_is_dynstring(struct aa_ext *e, char **string, const char *name)
{
	char *src_str;
	size_t size = 0;
	void *pos = e->pos;
	*string = NULL;
	if (aa_is_nameX(e, AA_STRING, name) &&
	    (size = aa_is_u16_chunk(e, &src_str))) {
		char *str;
		if (!(str = kmalloc(size, GFP_KERNEL)))
			goto fail;
		memcpy(str, src_str, size);
		*string = str;
	}

	return size;

fail:
	e->pos = pos;
	return 0;
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

static int aa_unpack_exec_table(struct aa_ext *e, struct aa_profile *profile)
{
	void *pos = e->pos;

	/* exec table is optional */
	if (aa_is_nameX(e, AA_STRUCT, "xtable")) {
		int i, size;

		size = aa_is_array(e, NULL);
		/* currently 4 exec bits and entries 0-3 are reserved iupcx */
		if (size > 16 - 4)
			goto fail;
		profile->exec_table = kzalloc(sizeof(char *) * size,
					      GFP_KERNEL);
		if (!profile->exec_table)
			goto fail;

		for (i = 0; i < size; i++) {
		    char *tmp;
			if (!aa_is_dynstring(e, &tmp, NULL))
				goto fail;
			/* note: strings beginning with a : have an embedded
			   \0 seperating the profile ns name from the profile
			   name */
			profile->exec_table[i] = tmp;
		}
		if (!aa_is_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		if (!aa_is_nameX(e, AA_STRUCTEND, NULL))
			goto fail;
		profile->exec_table_size = size;
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
					    struct aa_audit *sa)
{
	struct aa_profile *profile = NULL;
	size_t size = 0;
	int i, error = -EPROTO;

	profile = alloc_aa_profile();
	if (!profile)
		return ERR_PTR(-ENOMEM);

	/* check that we have the right struct being passed */
	if (!aa_is_nameX(e, AA_STRUCT, "profile"))
		goto fail;
	if (!aa_is_dynstring(e, &profile->name, NULL))
		goto fail;

	/* per profile debug flags (complain, audit) */
	if (!aa_is_nameX(e, AA_STRUCT, "flags"))
		goto fail;
	if (!aa_is_u32(e, &(profile->flags.hat), NULL))
		goto fail;
	if (!aa_is_u32(e, &(profile->flags.complain), NULL))
		goto fail;
	if (!aa_is_u32(e, &(profile->flags.audit), NULL))
		goto fail;
	if (!aa_is_nameX(e, AA_STRUCTEND, NULL))
		goto fail;

	if (!aa_is_u32(e, &(profile->capabilities.cap[0]), NULL))
		goto fail;
	if (!aa_is_u32(e, &(profile->audit_caps.cap[0]), NULL))
		goto fail;
	if (!aa_is_u32(e, &(profile->quiet_caps.cap[0]), NULL))
		goto fail;
	if (!aa_is_u32(e, &(profile->set_caps.cap[0]), NULL))
		goto fail;

	if (aa_is_nameX(e, AA_STRUCT, "caps64")) {
		/* optional upper half of 64 bit caps */
		if (!aa_is_u32(e, &(profile->capabilities.cap[1]), NULL))
			goto fail;
		if (!aa_is_u32(e, &(profile->audit_caps.cap[1]), NULL))
			goto fail;
		if (!aa_is_u32(e, &(profile->quiet_caps.cap[1]), NULL))
			goto fail;
		if (!aa_is_u32(e, &(profile->set_caps.cap[1]), NULL))
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
			if (!aa_is_u16(e, &profile->network_families[i], NULL))
				goto fail;
			if (!aa_is_u16(e, &profile->audit_network[i], NULL))
				goto fail;
			if (!aa_is_u16(e, &profile->quiet_network[i], NULL))
				goto fail;
		}
		if (!aa_is_nameX(e, AA_ARRAYEND, NULL))
			goto fail;
		/* allow unix domain and netlink sockets they are handled
		 * by IPC
		 */
	}
	profile->network_families[AF_UNIX] = 0xffff;
	profile->network_families[AF_NETLINK] = 0xffff;

	/* get file rules */
	profile->file_rules = aa_unpack_dfa(e);
	if (IS_ERR(profile->file_rules)) {
		error = PTR_ERR(profile->file_rules);
		profile->file_rules = NULL;
		goto fail;
	}

	if (!aa_unpack_exec_table(e, profile))
		goto fail;

	if (!aa_is_nameX(e, AA_STRUCTEND, NULL))
		goto fail;

	return profile;

fail:
	sa->name = profile && profile->name ? profile->name : "unknown";
	if (!sa->info)
		sa->info = "failed to unpack profile";
	aa_audit_status(NULL, sa);

	if (profile)
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
static int aa_verify_header(struct aa_ext *e, struct aa_audit *sa)
{
	/* get the interface version */
	if (!aa_is_u32(e, &e->version, "version")) {
		sa->info = "invalid profile format";
		aa_audit_status(NULL, sa);
		return -EPROTONOSUPPORT;
	}

	/* check that the interface version is currently supported */
	if (e->version != 5) {
		sa->info = "unsupported interface version";
		aa_audit_status(NULL, sa);
		return -EPROTONOSUPPORT;
	}

	/* read the namespace if present */
	if (!aa_is_dynstring(e, &e->ns_name, "namespace")) {
		e->ns_name = NULL;
	}

	return 0;
}

/**
 * aa_add_profile - Unpack and add a new profile to the profile list
 * @data: serialized data stream
 * @size: size of the serialized data stream
 */
ssize_t aa_add_profile(void *data, size_t size)
{
	struct aa_profile *profile = NULL;
	struct aa_namespace *ns = NULL;
	struct aa_ext e = {
		.start = data,
		.end = data + size,
		.pos = data,
		.ns_name = NULL
	};
	ssize_t error;
	struct aa_audit sa;
	memset(&sa, 0, sizeof(sa));
	sa.operation = "profile_load";
	sa.gfp_mask = GFP_KERNEL;

	error = aa_verify_header(&e, &sa);
	if (error)
		return error;

	profile = aa_unpack_profile(&e, &sa);
	if (IS_ERR(profile))
		return PTR_ERR(profile);

	mutex_lock(&aa_interface_lock);
	write_lock(&profile_ns_list_lock);
	if (e.ns_name)
		ns = __aa_find_namespace(e.ns_name, &profile_ns_list);
	else
		ns = default_namespace;
	if (!ns) {
		struct aa_namespace *new_ns;
		write_unlock(&profile_ns_list_lock);
		new_ns = alloc_aa_namespace(e.ns_name);
		if (!new_ns) {
			mutex_unlock(&aa_interface_lock);
			return -ENOMEM;
		}
		write_lock(&profile_ns_list_lock);
		ns = __aa_find_namespace(e.ns_name, &profile_ns_list);
		if (!ns) {
			list_add(&new_ns->list, &profile_ns_list);
			ns = new_ns;
		} else
			free_aa_namespace(new_ns);
	}

	write_lock(&ns->lock);
	if (__aa_find_profile(profile->name, &ns->profiles)) {
		/* A profile with this name exists already. */
		write_unlock(&ns->lock);
		write_unlock(&profile_ns_list_lock);
		sa.name = profile->name;
		sa.name2 = ns->name;
		sa.info = "failed: profile already loaded";
		aa_audit_status(NULL, &sa);
		mutex_unlock(&aa_interface_lock);
		aa_put_profile(profile);
		return -EEXIST;
	}
	profile->ns = aa_get_namespace(ns);
	ns->profile_count++;
	list_add(&profile->list, &ns->profiles);
	write_unlock(&ns->lock);
	write_unlock(&profile_ns_list_lock);

	sa.name = profile->name;
	sa.name2 = ns->name;
	aa_audit_status(NULL, &sa);
	mutex_unlock(&aa_interface_lock);
	return size;
}

/**
 * task_replace - replace a task's profile
 * @task: task to replace profile on
 * @new_cxt: new aa_task_context to do replacement with
 * @new_profile: new profile
 */
static inline void task_replace(struct task_struct *task,
				struct aa_task_context *new_cxt,
				struct aa_profile *new_profile)
{
	struct aa_task_context *cxt = aa_task_context(task);

	AA_DEBUG("%s: replacing profile for task %d "
		 "profile=%s (%p)\n",
		 __FUNCTION__,
		 cxt->task->pid,
		 cxt->profile->name, cxt->profile);

	aa_change_task_context(task, new_cxt, new_profile, cxt->cookie,
			       cxt->previous_profile);
}

/**
 * aa_replace_profile - replace a profile on the profile list
 * @udata: serialized data stream
 * @size: size of the serialized data stream
 *
 * unpack and replace a profile on the profile list and uses of that profile
 * by any aa_task_context.  If the profile does not exist on the profile list
 * it is added.  Return %0 or error.
 */
ssize_t aa_replace_profile(void *udata, size_t size)
{
	struct aa_profile *old_profile, *new_profile;
	struct aa_namespace *ns;
	struct aa_task_context *new_cxt;
	struct aa_ext e = {
		.start = udata,
		.end = udata + size,
		.pos = udata,
		.ns_name = NULL
	};
	ssize_t error;
	struct aa_audit sa;
	memset(&sa, 0, sizeof(sa));
	sa.operation = "profile_replace";
	sa.gfp_mask = GFP_KERNEL;

	error = aa_verify_header(&e, &sa);
	if (error)
		return error;

	new_profile = aa_unpack_profile(&e, &sa);
	if (IS_ERR(new_profile))
		return PTR_ERR(new_profile);

	mutex_lock(&aa_interface_lock);
	write_lock(&profile_ns_list_lock);
	if (e.ns_name)
		ns = __aa_find_namespace(e.ns_name, &profile_ns_list);
	else
		ns = default_namespace;
	if (!ns) {
		struct aa_namespace *new_ns;
		write_unlock(&profile_ns_list_lock);
		new_ns = alloc_aa_namespace(e.ns_name);
		if (!new_ns) {
			mutex_unlock(&aa_interface_lock);
			return -ENOMEM;
		}
		write_lock(&profile_ns_list_lock);
		ns = __aa_find_namespace(e.ns_name, &profile_ns_list);
		if (!ns) {
			list_add(&new_ns->list, &profile_ns_list);
			ns = new_ns;
		} else
			free_aa_namespace(new_ns);
	}

	write_lock(&ns->lock);
	old_profile = __aa_find_profile(new_profile->name, &ns->profiles);
	if (old_profile) {
		lock_profile(old_profile);
		old_profile->isstale = 1;
		list_del_init(&old_profile->list);
		unlock_profile(old_profile);
		ns->profile_count--;
	}
	new_profile->ns = aa_get_namespace(ns);
	ns->profile_count++;
	/* not don't need an extra ref count to keep new_profile as
	 * it is protect by the interface mutex */
	list_add(&new_profile->list, &ns->profiles);
	write_unlock(&ns->lock);
	write_unlock(&profile_ns_list_lock);

	if (!old_profile) {
		sa.operation = "profile_load";
		goto out;
	}
	/* do not fail replacement based off of profile's NPROC rlimit */

	/*
	 * Replacement needs to allocate a new aa_task_context for each
	 * task confined by old_profile.  To do this the profile locks
	 * are only held when the actual switch is done per task.  While
	 * looping to allocate a new aa_task_context the old_task list
	 * may get shorter if tasks exit/change their profile but will
	 * not get longer as new task will not use old_profile detecting
	 * that is stale.
	 */
	do {
		new_cxt = aa_alloc_task_context(GFP_KERNEL | __GFP_NOFAIL);

		lock_both_profiles(old_profile, new_profile);
		if (!list_empty(&old_profile->task_contexts)) {
			struct task_struct *task =
				list_entry(old_profile->task_contexts.next,
					   struct aa_task_context, list)->task;
			task_lock(task);
			task_replace(task, new_cxt, new_profile);
			task_unlock(task);
			aa_set_rlimits(task, new_profile);
			new_cxt = NULL;
		}
		unlock_both_profiles(old_profile, new_profile);
	} while (!new_cxt);
	aa_free_task_context(new_cxt);
	aa_put_profile(old_profile);

out:
	sa.name = new_profile->name;
	sa.name2 = ns->name;
	aa_audit_status(NULL, &sa);
	mutex_unlock(&aa_interface_lock);
	return size;
}

/**
 * aa_remove_profile - remove a profile from the system
 * @name: name of the profile to remove
 * @size: size of the name
 *
 * remove a profile from the profile list and all aa_task_context references
 * to said profile.
 * NOTE: removing confinement does not restore rlimits to preconfinemnet values
 */
ssize_t aa_remove_profile(char *name, size_t size)
{
	struct aa_namespace *ns;
	struct aa_profile *profile;
	struct aa_audit sa;
	memset(&sa, 0, sizeof(sa));
	sa.operation = "profile_remove";
	sa.gfp_mask = GFP_KERNEL;

	mutex_lock(&aa_interface_lock);
	write_lock(&profile_ns_list_lock);

	if (name[0] == ':') {
		char *split = strchr(name + 1, ':');
		if (!split)
			goto noent;
		*split = 0;
		ns = __aa_find_namespace(name + 1, &profile_ns_list);
		name = split + 1;
	} else {
		ns = default_namespace;
	}

	if (!ns)
		goto noent;
	sa.name2 = ns->name;
	write_lock(&ns->lock);
	profile = __aa_find_profile(name, &ns->profiles);
	if (!profile) {
		write_unlock(&ns->lock);
		goto noent;
	}
	sa.name = profile->name;

	/* Remove the profile from each task context it is on. */
	lock_profile(profile);
	profile->isstale = 1;
	aa_unconfine_tasks(profile);
	list_del_init(&profile->list);
	ns->profile_count--;
	unlock_profile(profile);
	/* Release the profile itself. */
	write_unlock(&ns->lock);
	/* check to see if the namespace has become stale */
	if (ns != default_namespace && ns->profile_count == 0) {
		list_del_init(&ns->list);
		aa_put_namespace(ns);
	}
	write_unlock(&profile_ns_list_lock);

	aa_audit_status(NULL, &sa);
	mutex_unlock(&aa_interface_lock);
	aa_put_profile(profile);

	return size;

noent:
	write_unlock(&profile_ns_list_lock);
	sa.info = "failed: profile does not exist";
	aa_audit_status(NULL, &sa);
	mutex_unlock(&aa_interface_lock);
	return -ENOENT;
}

/**
 * free_aa_namespace_kref - free aa_namespace by kref (see aa_put_namespace)
 * @kr: kref callback for freeing of a namespace
 */
void free_aa_namespace_kref(struct kref *kref)
{
	struct aa_namespace *ns=container_of(kref, struct aa_namespace, count);

	free_aa_namespace(ns);
}

/**
 * alloc_aa_namespace - allocate, initialize and return a new namespace
 * @name: a preallocated name
 * Returns NULL on failure.
 */
struct aa_namespace *alloc_aa_namespace(char *name)
{
	struct aa_namespace *ns;

	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	AA_DEBUG("%s(%p)\n", __FUNCTION__, ns);
	if (ns) {
		ns->name = name;
		INIT_LIST_HEAD(&ns->list);
		INIT_LIST_HEAD(&ns->profiles);
		kref_init(&ns->count);
		rwlock_init(&ns->lock);

		ns->null_complain_profile = alloc_aa_profile();
		if (!ns->null_complain_profile) {
			if (!name)
				kfree(ns->name);
			kfree(ns);
			return NULL;
		}
		ns->null_complain_profile->name =
			kstrdup("null-complain-profile", GFP_KERNEL);
		if (!ns->null_complain_profile->name) {
			free_aa_profile(ns->null_complain_profile);
			if (!name)
				kfree(ns->name);
			kfree(ns);
			return NULL;
		}
		ns->null_complain_profile->flags.complain = 1;
		/* null_complain_profile doesn't contribute to ns ref count */
		ns->null_complain_profile->ns = ns;
	}
	return ns;
}

/**
 * free_aa_namespace - free a profile namespace
 * @namespace: the namespace to free
 *
 * Free a namespace.  All references to the namespace must have been put.
 * If the namespace was referenced by a profile confining a task,
 * free_aa_namespace will be called indirectly (through free_aa_profile)
 * from an rcu callback routine, so we must not sleep here.
 */
void free_aa_namespace(struct aa_namespace *ns)
{
	AA_DEBUG("%s(%p)\n", __FUNCTION__, ns);

	if (!ns)
		return;

	/* namespace still contains profiles -- invalid */
	if (!list_empty(&ns->profiles)) {
		AA_ERROR("%s: internal error, "
			 "namespace '%s' still contains profiles\n",
			 __FUNCTION__,
			 ns->name);
		BUG();
	}
	if (!list_empty(&ns->list)) {
		AA_ERROR("%s: internal error, "
			 "namespace '%s' still on list\n",
			 __FUNCTION__,
			 ns->name);
		BUG();
	}
	/* null_complain_profile doesn't contribute to ns ref counting */
	ns->null_complain_profile->ns = NULL;
	aa_put_profile(ns->null_complain_profile);
	kfree(ns->name);
	kfree(ns);
}

/**
 * free_aa_profile_kref - free aa_profile by kref (called by aa_put_profile)
 * @kr: kref callback for freeing of a profile
 */
void free_aa_profile_kref(struct kref *kref)
{
	struct aa_profile *p=container_of(kref, struct aa_profile, count);

	free_aa_profile(p);
}

/**
 * alloc_aa_profile - allocate, initialize and return a new profile
 * Returns NULL on failure.
 */
struct aa_profile *alloc_aa_profile(void)
{
	struct aa_profile *profile;

	profile = kzalloc(sizeof(*profile), GFP_KERNEL);
	AA_DEBUG("%s(%p)\n", __FUNCTION__, profile);
	if (profile) {
		INIT_LIST_HEAD(&profile->list);
		kref_init(&profile->count);
		INIT_LIST_HEAD(&profile->task_contexts);
		spin_lock_init(&profile->lock);
	}
	return profile;
}

/**
 * free_aa_profile - free a profile
 * @profile: the profile to free
 *
 * Free a profile, its hats and null_profile. All references to the profile,
 * its hats and null_profile must have been put.
 *
 * If the profile was referenced from a task context, free_aa_profile() will
 * be called from an rcu callback routine, so we must not sleep here.
 */
void free_aa_profile(struct aa_profile *profile)
{
	AA_DEBUG("%s(%p)\n", __FUNCTION__, profile);

	if (!profile)
		return;

	/* profile is still on profile namespace list -- invalid */
	if (!list_empty(&profile->list)) {
		AA_ERROR("%s: internal error, "
			 "profile '%s' still on global list\n",
			 __FUNCTION__,
			 profile->name);
		BUG();
	}
	aa_put_namespace(profile->ns);

	aa_match_free(profile->file_rules);

	if (profile->name) {
		AA_DEBUG("%s: %s\n", __FUNCTION__, profile->name);
		kfree(profile->name);
	}

	kfree(profile);
}

/**
 * aa_unconfine_tasks - remove tasks on a profile's task context list
 * @profile: profile to remove tasks from
 *
 * Assumes that @profile lock is held.
 */
void aa_unconfine_tasks(struct aa_profile *profile)
{
	while (!list_empty(&profile->task_contexts)) {
		struct task_struct *task =
			list_entry(profile->task_contexts.next,
				   struct aa_task_context, list)->task;
		task_lock(task);
		aa_change_task_context(task, NULL, NULL, 0, NULL);
		task_unlock(task);
	}
}
