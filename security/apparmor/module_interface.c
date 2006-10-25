/*
 *	Copyright (C) 1998-2005 Novell/SUSE
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
#include "module_interface.h"
#include "match/match.h"

/* aa_code defined in module_interface.h */

const int aacode_datasize[] = { 1, 2, 4, 8, 2, 2, 4, 0, 0, 0, 0, 0, 0 };

struct aa_taskreplace_data {
	struct aaprofile *old_profile;
	struct aaprofile *new_profile;
};

/* inlines must be forward of there use in newer version of gcc,
   just forward declaring with a prototype won't work anymore */

static inline void free_aa_entry(struct aa_entry *entry)
{
	if (entry) {
		kfree(entry->filename);
		aamatch_free(entry->extradata);
		kfree(entry);
	}
}

/**
 * alloc_aa_entry - create new empty aa_entry
 * This routine allocates, initializes, and returns a new aa_entry
 * file entry structure.  Structure is zeroed.  Returns new structure on
 * success, %NULL on failure.
 */
static inline struct aa_entry *alloc_aa_entry(void)
{
	struct aa_entry *entry;

	AA_DEBUG("%s\n", __FUNCTION__);
	entry = kzalloc(sizeof(struct aa_entry), GFP_KERNEL);
	if (entry) {
		int i;
		INIT_LIST_HEAD(&entry->list);
		for (i = 0; i <= POS_AA_FILE_MAX; i++) {
			INIT_LIST_HEAD(&entry->listp[i]);
		}
	}
	return entry;
}

/**
 * free_aaprofile_rcu - rcu callback for free profiles
 * @head: rcu_head struct of the profile whose reference is being put.
 *
 * the rcu callback routine, which delays the freeing of a profile when
 * its last reference is put.
 */
static void free_aaprofile_rcu(struct rcu_head *head)
{
	struct aaprofile *p = container_of(head, struct aaprofile, rcu);
	free_aaprofile(p);
}

/**
 * task_remove - remove profile from a task's subdomain
 * @sd: task's subdomain
 *
 * remove the active profile from a task's subdomain, switching the task
 * to an unconfined state.
 */
static inline void task_remove(struct subdomain *sd)
{
	/* spin_lock(&sd_lock) held here */
	AA_DEBUG("%s: removing profile from task %s(%d) profile %s active %s\n",
		 __FUNCTION__,
		 sd->task->comm,
		 sd->task->pid,
		 BASE_PROFILE(sd->active)->name,
		 sd->active->name);

	aa_switch_unconfined(sd);
}

/** taskremove_iter - Iterator to unconfine subdomains which match cookie
 * @sd: subdomain to consider for profile removal
 * @cookie: pointer to the oldprofile which is being removed
 *
 * If the subdomain's active profile matches old_profile,  then call
 * task_remove() to remove the profile leaving the task (subdomain) unconfined.
 */
static int taskremove_iter(struct subdomain *sd, void *cookie)
{
	struct aaprofile *old_profile = (struct aaprofile *)cookie;
	unsigned long flags;

	spin_lock_irqsave(&sd_lock, flags);

	if (__aa_is_confined(sd) && BASE_PROFILE(sd->active) == old_profile) {
		task_remove(sd);
	}

	spin_unlock_irqrestore(&sd_lock, flags);

	return 0;
}

/** task_replace - replace subdomain's current profile with a new profile
 * @sd: subdomain to replace the profile on
 * @new: new profile
 *
 * Replace a task's (subdomain's) active profile with a new profile.  If
 * task was in a hat then the new profile will also be in the equivalent
 * hat in the new profile if it exists.  If it doesn't exist the
 * task will be placed in the special null_profile state.
 */
static inline void task_replace(struct subdomain *sd, struct aaprofile *new)
{
	AA_DEBUG("%s: replacing profile for task %s(%d) "
		 "profile=%s (%p) active=%s (%p)\n",
		 __FUNCTION__,
		 sd->task->comm, sd->task->pid,
		 BASE_PROFILE(sd->active)->name, BASE_PROFILE(sd->active),
		 sd->active->name, sd->active);

	if (!sd->active)
		goto out;

	if (IN_SUBPROFILE(sd->active)) {
		struct aaprofile *nactive;

		/* The old profile was in a hat, check to see if the new
		 * profile has an equivalent hat */
		nactive = __aa_find_profile(sd->active->name, &new->sub);

		if (!nactive)
			nactive = get_aaprofile(new->null_profile);

		aa_switch(sd, nactive);
		put_aaprofile(nactive);
	} else {
		aa_switch(sd, new);
	}

 out:
	return;
}

/** taskreplace_iter - Iterator to replace a subdomain's profile
 * @sd: subdomain to consider for profile replacement
 * @cookie: pointer to the old profile which is being replaced.
 *
 * If the subdomain's active profile matches old_profile call
 * task_replace() to replace with the subdomain's active profile with
 * the new profile.
 */
static int taskreplace_iter(struct subdomain *sd, void *cookie)
{
	struct aa_taskreplace_data *data = (struct aa_taskreplace_data *)cookie;
	unsigned long flags;

	spin_lock_irqsave(&sd_lock, flags);

	if (__aa_is_confined(sd) &&
	    BASE_PROFILE(sd->active) == data->old_profile)
		task_replace(sd, data->new_profile);

	spin_unlock_irqrestore(&sd_lock, flags);

	return 0;
}

static inline int aa_inbounds(struct aa_ext *e, size_t size)
{
	return (e->pos + size <= e->end);
}

/**
 * aaconvert - convert trailing values of serialized type codes
 * @code: type code
 * @dest: pointer to object to receive the converted value
 * @src:  pointer to value to convert
 *
 * for serialized type codes which have a trailing value, convert it
 * and place it in @dest.  If a code does not have a trailing value nop.
 */
static void aaconvert(enum aa_code code, void *dest, void *src)
{
	switch (code) {
	case AA_U8:
		*(u8 *)dest = *(u8 *) src;
		break;
	case AA_U16:
	case AA_NAME:
	case AA_DYN_STRING:
		*(u16 *)dest = le16_to_cpu(get_unaligned((u16 *)src));
		break;
	case AA_U32:
	case AA_STATIC_BLOB:
		*(u32 *)dest = le32_to_cpu(get_unaligned((u32 *)src));
		break;
	case AA_U64:
		*(u64 *)dest = le64_to_cpu(get_unaligned((u64 *)src));
		break;
	default:
		/* nop - all other type codes do not have a trailing value */
		;
	}
}

/**
 * aa_is_X - check if the next element is of type X
 * @e: serialized data extent information
 * @code: type code
 * @data: object located at @e->pos (of type @code) is written into @data
 *        if @data is non-null.  if data is null it means skip this
 *        entry
 * check to see if the next element in the serialized data stream is of type
 * X and check that it is with in bounds, if so put the associated value in
 * @data.
 * return the size of bytes associated with the returned data
 *        for complex object like blob and string a pointer to the allocated
 *        data is returned in data, but the size of the blob or string is
 *        returned.
 */
static u32 aa_is_X(struct aa_ext *e, enum aa_code code, void *data)
{
	void *pos = e->pos;
	int ret = 0;
	if (!aa_inbounds(e, AA_CODE_BYTE + aacode_datasize[code]))
		goto fail;
	if (code != *(u8 *)e->pos)
		goto out;
	e->pos += AA_CODE_BYTE;
	if (code == AA_NAME) {
		u16 size;
		/* name codes are followed by X bytes */
		size = le16_to_cpu(get_unaligned((u16 *)e->pos));
		if (!aa_inbounds(e, (size_t) size))
			goto fail;
		if (data)
			*(u16 *)data = size;
		e->pos += aacode_datasize[code];
		ret = 1 + aacode_datasize[code];
	} else if (code == AA_DYN_STRING) {
		u16 size;
		char *str;
		/* strings codes are followed by X bytes */
		size = le16_to_cpu(get_unaligned((u16 *)e->pos));
		e->pos += aacode_datasize[code];
		if (!aa_inbounds(e, (size_t) size))
			goto fail;
		if (data) {
			* (char **)data = NULL;
			str = kmalloc(size, GFP_KERNEL);
			if (!str)
				goto fail;
			memcpy(str, e->pos, (size_t) size);
			str[size-1] = '\0';
			* (char **)data = str;
		}
		e->pos += size;
		ret = size;
	} else if (code == AA_STATIC_BLOB) {
		u32 size;
		/* blobs are followed by X bytes, that can be 2^32 */
		size = le32_to_cpu(get_unaligned((u32 *)e->pos));
		e->pos += aacode_datasize[code];
		if (!aa_inbounds(e, (size_t) size))
			goto fail;
		if (data)
			memcpy(data, e->pos, (size_t) size);
		e->pos += size;
		ret = size;
	} else {
		if (data)
			aaconvert(code, data, e->pos);
		e->pos += aacode_datasize[code];
		ret = 1 + aacode_datasize[code];
	}
out:
	return ret;
fail:
	e->pos = pos;
	return 0;
}

/**
 * aa_is_nameX - check is the next element is of type X with a name of @name
 * @e: serialized data extent information
 * @code: type code
 * @data: location to store deserialized data if match isX criteria
 * @name: name to match to the serialized element.
 *
 * check that the next serialized data element is of type X and has a tag
 * name @name.  If the code matches and name (if specified) matches then
 * the packed data is unpacked into *data.  (Note for strings this is the
 * size, and the next data in the stream is the string data)
 * returns %0 if either match failes
 */
static int aa_is_nameX(struct aa_ext *e, enum aa_code code, void *data,
		       const char *name)
{
	void *pos = e->pos;
	u16 size;
	u32 ret;
	/* check for presence of a tagname, and if present name size
	 * AA_NAME tag value is a u16 */
	if (aa_is_X(e, AA_NAME, &size)) {
		/* if a name is specified it must match. otherwise skip tag */
		if (name && ((strlen(name) != size-1) ||
			     strncmp(name, (char *)e->pos, (size_t)size-1)))
			goto fail;
		e->pos += size;
	} else if (name) {
		goto fail;
	}

	/* now check if data actually matches */
	ret = aa_is_X(e, code, data);
	if (!ret)
		goto fail;
	return ret;

fail:
	e->pos = pos;
	return 0;
}

/* macro to wrap error case to make a block of reads look nicer */
#define AA_READ_X(E, C, D, N) \
	do { \
		u32 __ret; \
		__ret = aa_is_nameX((E), (C), (D), (N)); \
		if (!__ret) \
			goto fail; \
	} while (0)

/**
 * aa_activate_net_entry - unpacked serialized net entries
 * @e: serialized data extent information
 *
 * Ignore/skips net entries if they are present in the serialized data
 * stream.  Network confinement rules are currently unsupported but some
 * user side tools can generate them so they are currently ignored.
 */
static inline int aa_activate_net_entry(struct aa_ext *e)
{
	AA_READ_X(e, AA_STRUCT, NULL, "ne");
	AA_READ_X(e, AA_U32, NULL, NULL);
	AA_READ_X(e, AA_U32, NULL, NULL);
	AA_READ_X(e, AA_U32, NULL, NULL);
	AA_READ_X(e, AA_U16, NULL, NULL);
	AA_READ_X(e, AA_U16, NULL, NULL);
	AA_READ_X(e, AA_U32, NULL, NULL);
	AA_READ_X(e, AA_U32, NULL, NULL);
	AA_READ_X(e, AA_U16, NULL, NULL);
	AA_READ_X(e, AA_U16, NULL, NULL);
	/* interface name is optional so just ignore return code */
	aa_is_nameX(e, AA_DYN_STRING, NULL, NULL);
	AA_READ_X(e, AA_STRUCTEND, NULL, NULL);

	return 1;
fail:
	return 0;
}

/**
 * aa_activate_file_entry - unpack serialized file entry
 * @e: serialized data extent information
 *
 * unpack the information used for a file ACL entry.
 */
static inline struct aa_entry *aa_activate_file_entry(struct aa_ext *e)
{
	struct aa_entry *entry = NULL;

	if (!(entry = alloc_aa_entry()))
		goto fail;

	AA_READ_X(e, AA_STRUCT, NULL, "fe");
	AA_READ_X(e, AA_DYN_STRING, &entry->filename, NULL);
	AA_READ_X(e, AA_U32, &entry->mode, NULL);
	AA_READ_X(e, AA_U32, &entry->type, NULL);

	entry->extradata = aamatch_alloc(entry->type);
	if (IS_ERR(entry->extradata)) {
		entry->extradata = NULL;
		goto fail;
	}

	if (entry->extradata &&
	    aamatch_serialize(entry->extradata, e, aa_is_nameX) != 0) {
		goto fail;
	}
	AA_READ_X(e, AA_STRUCTEND, NULL, NULL);

	switch (entry->type) {
	case aa_entry_literal:
		AA_DEBUG("%s: %s [no pattern] mode=0x%x\n",
			 __FUNCTION__,
			 entry->filename,
			 entry->mode);
		break;
	case aa_entry_tailglob:
		AA_DEBUG("%s: %s [tailglob] mode=0x%x\n",
			 __FUNCTION__,
			 entry->filename,
			 entry->mode);
		break;
	case aa_entry_pattern:
		AA_DEBUG("%s: %s mode=0x%x\n",
			 __FUNCTION__,
			 entry->filename,
			 entry->mode);
		break;
	default:
		AA_WARN("%s: INVALID entry_match_type %d\n",
			__FUNCTION__,
			(int)entry->type);
		goto fail;
	}

	return entry;

fail:
	aamatch_free(entry->extradata);
	free_aa_entry(entry);
	return NULL;
}

/**
 * check_rule_and_add - check a file rule is valid and add to a profile
 * @file_entry: file rule to add
 * @profile: profile to add the rule to
 * @message: error message returned if the addition failes.
 *
 * perform consistency check to ensure that a file rule entry is valid.
 * If the rule is valid it is added to the profile.
 */
static inline int check_rule_and_add(struct aa_entry *file_entry,
				     struct aaprofile *profile,
				     const char **message)
{
	/* verify consistency of x, px, ix, ux for entry against
	   possible duplicates for this entry */
	int mode = AA_EXEC_MODIFIER_MASK(file_entry->mode);
	int i;

	if (mode && !(AA_MAY_EXEC & file_entry->mode)) {
		*message = "inconsistent rule, x modifiers without x";
		goto out;
	}

	/* check that only 1 of the modifiers is set */
	if (mode && (mode & (mode - 1))) {
		*message = "inconsistent rule, multiple x modifiers";
		goto out;
	}

	/* ix -> m (required so that target exec binary may map itself) */
  	         if (mode & AA_EXEC_INHERIT)
  	                 file_entry->mode |= AA_EXEC_MMAP;

	list_add(&file_entry->list, &profile->file_entry);
	profile->num_file_entries++;

	mode = file_entry->mode;

	/* Handle partitioned lists
	 * Chain entries onto sublists based on individual
	 * permission bits. This allows more rapid searching.
	 */
	for (i = 0; i <= POS_AA_FILE_MAX; i++) {
		if (mode & (1 << i))
			/* profile->file_entryp[i] initially set to
			 * NULL in alloc_aaprofile() */
			list_add(&file_entry->listp[i],
				 &profile->file_entryp[i]);
	}

	return 1;

out:
	free_aa_entry(file_entry);
	return 0;
}

#define AA_ENTRY_LIST(NAME) \
	do { \
	if (aa_is_nameX(e, AA_LIST, NULL, (NAME))) { \
		rulename = ""; \
		error_string = "Invalid file entry"; \
		while (!aa_is_nameX(e, AA_LISTEND, NULL, NULL)) { \
			struct aa_entry *file_entry; \
			file_entry = aa_activate_file_entry(e); \
			if (!file_entry) \
				goto fail; \
			if (!check_rule_and_add(file_entry, profile, \
						&error_string)) { \
				rulename = file_entry->filename; \
				goto fail; \
			} \
		} \
	} \
	} while (0)

/**
 * aa_activate_profile - unpack a serialized profile
 * @e: serialized data extent information
 * @error: error code returned if unpacking fails
 */
static struct aaprofile *aa_activate_profile(struct aa_ext *e, ssize_t *error)
{
	struct aaprofile *profile = NULL;
	const char *rulename = "";
	const char *error_string = "Invalid Profile";

	*error = -EPROTO;

	profile = alloc_aaprofile();
	if (!profile) {
		error_string = "Could not allocate profile";
		*error = -ENOMEM;
		goto fail;
	}

	/* check that we have the right struct being passed */
	AA_READ_X(e, AA_STRUCT, NULL, "profile");
	AA_READ_X(e, AA_DYN_STRING, &profile->name, NULL);

	error_string = "Invalid flags";
	/* per profile debug flags (debug, complain, audit) */
	AA_READ_X(e, AA_STRUCT, NULL, "flags");
	AA_READ_X(e, AA_U32, &(profile->flags.debug), NULL);
	AA_READ_X(e, AA_U32, &(profile->flags.complain), NULL);
	AA_READ_X(e, AA_U32, &(profile->flags.audit), NULL);
	AA_READ_X(e, AA_STRUCTEND, NULL, NULL);

	error_string = "Invalid capabilities";
	AA_READ_X(e, AA_U32, &(profile->capabilities), NULL);

	/* get the file entries. */
	AA_ENTRY_LIST("pgent");		/* pcre rules */
	AA_ENTRY_LIST("sgent");		/* simple globs */
	AA_ENTRY_LIST("fent");		/* regular file entries */

	/* get the net entries */
	if (aa_is_nameX(e, AA_LIST, NULL, "net")) {
		error_string = "Invalid net entry";
		while (!aa_is_nameX(e, AA_LISTEND, NULL, NULL)) {
			if (!aa_activate_net_entry(e))
				goto fail;
		}
	}
	rulename = "";

	/* get subprofiles */
	if (aa_is_nameX(e, AA_LIST, NULL, "hats")) {
		error_string = "Invalid profile hat";
		while (!aa_is_nameX(e, AA_LISTEND, NULL, NULL)) {
			struct aaprofile *subprofile;
			subprofile = aa_activate_profile(e, error);
			if (!subprofile)
				goto fail;
			subprofile->parent = profile;
			list_add(&subprofile->list, &profile->sub);
		}
	}

	error_string = "Invalid end of profile";
	AA_READ_X(e, AA_STRUCTEND, NULL, NULL);

	return profile;

fail:
	AA_WARN("%s: %s %s in profile %s\n", INTERFACE_ID, rulename,
		error_string, profile && profile->name ? profile->name
		: "unknown");

	if (profile) {
		free_aaprofile(profile);
		profile = NULL;
	}

	return NULL;
}

/**
 * aa_activate_top_profile - unpack a serialized base profile
 * @e: serialized data extent information
 * @error: error code returned if unpacking fails
 *
 * check interface version unpack a profile and all its hats and patch
 * in any extra information that the profile needs.
 */
static void *aa_activate_top_profile(struct aa_ext *e, ssize_t *error)
{
	struct aaprofile *profile = NULL;

	/* get the interface version */
	if (!aa_is_nameX(e, AA_U32, &e->version, "version")) {
		AA_WARN("%s: version missing\n", INTERFACE_ID);
		*error = -EPROTONOSUPPORT;
		goto fail;
	}

	/* check that the interface version is currently supported */
	if (e->version != 2) {
		AA_WARN("%s: unsupported interface version (%d)\n",
			INTERFACE_ID, e->version);
		*error = -EPROTONOSUPPORT;
		goto fail;
	}

	profile = aa_activate_profile(e, error);
	if (!profile)
		goto fail;

	if (!list_empty(&profile->sub) || profile->flags.complain) {
		if (attach_nullprofile(profile))
			goto fail;
	}
	return profile;

fail:
	free_aaprofile(profile);
	return NULL;
}

/**
 * aa_file_prof_add - add a new profile to the profile list
 * @data: serialized data stream
 * @size: size of the serialized data stream
 *
 * unpack and add a profile to the profile list.  Return %0 or error
 */
ssize_t aa_file_prof_add(void *data, size_t size)
{
	struct aaprofile *profile = NULL;

	struct aa_ext e = {
		.start = data,
		.end = data + size,
		.pos = data
	};
	ssize_t error;

	profile = aa_activate_top_profile(&e, &error);
	if (!profile) {
		AA_DEBUG("couldn't activate profile\n");
		goto out;
	}

	/* aa_activate_top_profile allocates profile with initial 1 count
	 * aa_profilelist_add transfers that ref to profile list without
	 * further incrementing
	 */
	if (aa_profilelist_add(profile)) {
		error = size;
	} else {
		AA_WARN("trying to add profile (%s) that already exists.\n",
			profile->name);
		put_aaprofile(profile);
		error = -EEXIST;
	}

out:
	return error;
}

/**
 * aa_file_prof_repl - replace a profile on the profile list
 * @udata: serialized data stream
 * @size: size of the serialized data stream
 *
 * unpack and replace a profile on the profile list and uses of that profile
 * by any subdomain.  If the profile does not exist on the profile list
 * it is added.  Return %0 or error.
 */
ssize_t aa_file_prof_repl(void *udata, size_t size)
{
	struct aa_taskreplace_data data;
	struct aa_ext e = {
		.start = udata,
		.end = udata + size,
		.pos = udata
	};

	ssize_t error;

	data.new_profile = aa_activate_top_profile(&e, &error);
	if (!data.new_profile) {
		AA_DEBUG("couldn't activate profile\n");
		goto out;
	}

	/* Refcount on data.new_profile is 1 (aa_activate_top_profile).
	 *
	 * This reference will be inherited by aa_profilelist_replace for it's
	 * profile list reference but this isn't sufficient.
	 *
	 * Another replace (*for-same-profile*) may race us here.
	 * Task A calls aa_profilelist_replace(new_profile) and is interrupted.
	 * Task B old_profile = aa_profilelist_replace() will return task A's
	 * new_profile with the count of 1.  If task B proceeeds to put this
	 * profile it will dissapear from under task A.
	 *
	 * Grab extra reference on new_profile to prevent this
	 */

	get_aaprofile(data.new_profile);

	data.old_profile = aa_profilelist_replace(data.new_profile);

	/* If there was an old profile,  find all currently executing tasks
	 * using this profile and replace the old profile with the new.
	 */
	if (data.old_profile) {
		AA_DEBUG("%s: try to replace profile (%p)%s\n",
			 __FUNCTION__,
			 data.old_profile,
			 data.old_profile->name);

		aa_subdomainlist_iterate(taskreplace_iter, (void *)&data);

		/* it's off global list, and we are done replacing */
		put_aaprofile(data.old_profile);
	}

	/* release extra reference obtained above (race) */
	put_aaprofile(data.new_profile);

	error = size;

out:
	return error;
}

/**
 * aa_file_prof_remove - remove a profile from the system
 * @name: name of the profile to remove
 * @size: size of the name
 *
 * remove a profile from the profile list and all subdomain references
 * to said profile.  Return %0 on success, else error.
 */
ssize_t aa_file_prof_remove(const char *name, size_t size)
{
	struct aaprofile *old_profile;

	/* if the old profile exists it will be removed from the list and
	 * a reference is returned.
	 */
	old_profile = aa_profilelist_remove(name);

	if (old_profile) {
		/* remove profile from any tasks using it */
		aa_subdomainlist_iterate(taskremove_iter, (void *)old_profile);

		/* drop reference obtained by aa_profilelist_remove */
		put_aaprofile(old_profile);
	} else {
		AA_WARN("%s: trying to remove profile (%s) that "
			"doesn't exist - skipping.\n", __FUNCTION__, name);
		return -ENOENT;
	}

	return size;
}

/**
 * free_aaprofile_kref - free aaprofile by kref (called by put_aaprofile)
 * @kr: kref callback for freeing of a profile
 */
void free_aaprofile_kref(struct kref *kr)
{
	struct aaprofile *p=container_of(kr, struct aaprofile, count);

	call_rcu(&p->rcu, free_aaprofile_rcu);
}

/**
 * free_aaprofile - free aaprofile structure
 * @profile: the profile to free
 *
 * free a profile, its file entries hats and null_profile.  All references
 * to the profile, its hats and null_profile must have been put.
 * If the profile was referenced by a subdomain free_aaprofile should be
 * called from an rcu callback routine.
 */
void free_aaprofile(struct aaprofile *profile)
{
	struct aa_entry *ent, *tmp;
	struct aaprofile *p, *ptmp;

	AA_DEBUG("%s(%p)\n", __FUNCTION__, profile);

	if (!profile)
		return;

	/* profile is still on global profile list -- invalid */
	if (!list_empty(&profile->list)) {
		AA_ERROR("%s: internal error, "
			 "profile '%s' still on global list\n",
			 __FUNCTION__,
			 profile->name);
		BUG();
	}

	list_for_each_entry_safe(ent, tmp, &profile->file_entry, list) {
		if (ent->filename)
			AA_DEBUG("freeing aa_entry: %p %s\n",
				 ent->filename, ent->filename);
		list_del_init(&ent->list);
		free_aa_entry(ent);
	}

	/* use free_aaprofile instead of put_aaprofile to destroy the
	 * null_profile, because the null_profile use the same reference
	 * counting as hats, ie. the count goes to the base profile.
	 */
	free_aaprofile(profile->null_profile);
	list_for_each_entry_safe(p, ptmp, &profile->sub, list) {
		list_del_init(&p->list);
		p->parent = NULL;
		put_aaprofile(p);
	}

	if (profile->name) {
		AA_DEBUG("%s: %s\n", __FUNCTION__, profile->name);
		kfree(profile->name);
	}

	kfree(profile);
}
