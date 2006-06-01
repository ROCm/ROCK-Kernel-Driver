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
#include "aamatch/match.h"

/* sd_code defined in module_interface.h */

const int sdcode_datasize[] = { 1, 2, 4, 8, 2, 2, 4, 0, 0, 0, 0, 0, 0 };

struct sd_taskreplace_data {
	struct sdprofile *old_profile;
	struct sdprofile *new_profile;
};

/* inlines must be forward of there use in newer version of gcc,
   just forward declaring with a prototype won't work anymore */

static inline void free_sd_entry(struct sd_entry *entry)
{
	if (entry) {
		kfree(entry->filename);
		sdmatch_free(entry->extradata);
		kfree(entry);
	}
}

/**
 * alloc_sd_entry - create new empty sd_entry
 *
 * This routine allocates, initializes, and returns a new subdomain
 * file entry structure.  Structure is zeroed.  Returns new structure on
 * success, NULL on failure.
 */
static inline struct sd_entry *alloc_sd_entry(void)
{
	struct sd_entry *entry;

	SD_DEBUG("%s\n", __FUNCTION__);
	entry = kmalloc(sizeof(struct sd_entry), GFP_KERNEL);
	if (entry) {
		int i;
		memset(entry, 0, sizeof(struct sd_entry));
		INIT_LIST_HEAD(&entry->list);
		for (i = 0; i <= POS_SD_FILE_MAX; i++) {
			INIT_LIST_HEAD(&entry->listp[i]);
		}
	}
	return entry;
}

/**
 * free_sdprofile - free sdprofile structure
 */
void free_sdprofile(struct sdprofile *profile)
{
	struct sd_entry *sdent, *tmp;
	struct sdprofile *p, *ptmp;

	SD_DEBUG("%s(%p)\n", __FUNCTION__, profile);

	if (!profile)
		return;

	/* profile is still on global profile list -- invalid */
	if (!list_empty(&profile->list)) {
		SD_ERROR("%s: internal error, "
			 "profile '%s' still on global list\n",
			 __FUNCTION__,
			 profile->name);
		BUG();
	}

	list_for_each_entry_safe(sdent, tmp, &profile->file_entry, list) {
		if (sdent->filename)
			SD_DEBUG("freeing sd_entry: %p %s\n",
				 sdent->filename, sdent->filename);
		list_del_init(&sdent->list);
		free_sd_entry(sdent);
	}

	list_for_each_entry_safe(p, ptmp, &profile->sub, list) {
		list_del_init(&p->list);
		put_sdprofile(p);
	}

	if (profile->name) {
		SD_DEBUG("%s: %s\n", __FUNCTION__, profile->name);
		kfree(profile->name);
	}

	kfree(profile);
}

/** task_remove
 *
 * remove profile in a task's subdomain leaving the task unconfined
 *
 * @sd: task's subdomain
 */
static inline void task_remove(struct subdomain *sd)
{
	/* write_lock(&sd_lock) held here */
	SD_DEBUG("%s: removing profile from task %s(%d) profile %s active %s\n",
		 __FUNCTION__,
		 sd->task->comm,
		 sd->task->pid,
		 sd->profile->name,
		 sd->active->name);

	sd_switch_unconfined(sd);
}

/** taskremove_iter
 *
 * Iterate over all subdomains.
 *
 * If any matches old_profile,  then call task_remove to remove it.
 * This leaves the task (subdomain) unconfined.
 */
static int taskremove_iter(struct subdomain *sd, void *cookie)
{
	struct sdprofile *old_profile = (struct sdprofile *)cookie;
	int remove = 0;
	unsigned long flags;

	write_lock_irqsave(&sd_lock, flags);

	if (__sd_is_confined(sd) && sd->profile == old_profile) {
		remove = 1;	/* remove item from list */
		task_remove(sd);
	}

	write_unlock_irqrestore(&sd_lock, flags);

	return remove;
}

/** task_replace
 *
 * replace profile in a task's subdomain with newly loaded profile
 *
 * @sd: task's subdomain
 * @new: old profile
 */
static inline void task_replace(struct subdomain *sd, struct sdprofile *new)
{
	struct sdprofile *nactive = NULL;

	SD_DEBUG("%s: replacing profile for task %s(%d) "
		 "profile=%s (%p) active=%s (%p)\n",
		 __FUNCTION__,
		 sd->task->comm, sd->task->pid,
		 sd->profile->name, sd->profile,
		 sd->active->name, sd->active);

	if (sd->profile == sd->active)
		nactive = get_sdprofile(new);
	else if (sd->active) {
		/* old in hat, new profile has hats */
		nactive = __sd_find_profile(sd->active->name, &new->sub);

		if (!nactive) {
			if (new->flags.complain)
				nactive = get_sdprofile(null_complain_profile);
			else
				nactive = get_sdprofile(null_profile);
		}
	}
	sd_switch(sd, new, nactive);

	put_sdprofile(nactive);
}

/** taskreplace_iter
 *
 * Iterate over all subdomains.
 *
 * If any matches old_profile,  then call task_replace to replace with
 * new_profile
 */
static int taskreplace_iter(struct subdomain *sd, void *cookie)
{
	struct sd_taskreplace_data *data = (struct sd_taskreplace_data *)cookie;
	unsigned long flags;

	write_lock_irqsave(&sd_lock, flags);

	if (__sd_is_confined(sd) && sd->profile == data->old_profile)
		task_replace(sd, data->new_profile);

	write_unlock_irqrestore(&sd_lock, flags);

	return 0;
}

static inline int sd_inbounds(struct sd_ext *e, size_t size)
{
	return (e->pos + size <= e->end);
}

/**
 * sdconvert - for codes that have a trailing value, convert that value
 *             and put it in dest.
 *             if a code does not have a trailing value nop
 * @code: type code
 * @dest: pointer to object to receive the converted value
 * @src:  pointer to value to convert
 */
static void sdconvert(enum sd_code code, void *dest, void *src)
{
	switch (code) {
	case SD_U8:
		*(u8 *)dest = *(u8 *) src;
		break;
	case SD_U16:
	case SD_NAME:
	case SD_DYN_STRING:
		*(u16 *)dest = le16_to_cpu(get_unaligned((u16 *)src));
		break;
	case SD_U32:
	case SD_STATIC_BLOB:
		*(u32 *)dest = le32_to_cpu(get_unaligned((u32 *)src));
		break;
	case SD_U64:
		*(u64 *)dest = le64_to_cpu(get_unaligned((u64 *)src));
		break;
	default:
		/* nop - all other type codes do not have a trailing value */
		;
	}
}

/**
 * sd_is_X - check if the next element is of type X and if it is within
 *           bounds.  If it is put the associated value in data.
 * @e: extent information
 * @code: type code
 * @data: object located at @e->pos (of type @code) is written into @data
 *        if @data is non-null.  if data is null it means skip this
 *        entry
 * return the size of bytes associated with the returned data
 *        for complex object like blob and string a pointer to the allocated
 *        data is returned in data, but the size of the blob or string is
 *        returned.
 */
static u32 sd_is_X(struct sd_ext *e, enum sd_code code, void *data)
{
	void *pos = e->pos;
	int ret = 0;
	if (!sd_inbounds(e, SD_CODE_BYTE + sdcode_datasize[code]))
		goto fail;
	if (code != *(u8 *)e->pos)
		goto out;
	e->pos += SD_CODE_BYTE;
	if (code == SD_NAME) {
		u16 size;
		/* name codes are followed by X bytes */
		size = le16_to_cpu(get_unaligned((u16 *)e->pos));
		if (!sd_inbounds(e, (size_t) size))
			goto fail;
		if (data)
			*(u16 *)data = size;
		e->pos += sdcode_datasize[code];
		ret = 1 + sdcode_datasize[code];
	} else if (code == SD_DYN_STRING) {
		u16 size;
		char *str;
		/* strings codes are followed by X bytes */
		size = le16_to_cpu(get_unaligned((u16 *)e->pos));
		e->pos += sdcode_datasize[code];
		if (!sd_inbounds(e, (size_t) size))
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
	} else if (code == SD_STATIC_BLOB) {
		u32 size;
		/* blobs are followed by X bytes, that can be 2^32 */
		size = le32_to_cpu(get_unaligned((u32 *)e->pos));
		e->pos += sdcode_datasize[code];
		if (!sd_inbounds(e, (size_t) size))
			goto fail;
		if (data)
			memcpy(data, e->pos, (size_t) size);
		e->pos += size;
		ret = size;
	} else {
		if (data)
			sdconvert(code, data, e->pos);
		e->pos += sdcode_datasize[code];
		ret = 1 + sdcode_datasize[code];
	}
out:
	return ret;
fail:
	e->pos = pos;
	return 0;
}

/* sd_is_nameX - check is the next element is X, and its tag is name.
 * if the code matches and name (if specified) matches then the packed data
 * is unpacked into *data.  (Note for strings this is the size, and the next
 * data in the stream is the string data)
 * returns 0 if either match failes
 */
static int sd_is_nameX(struct sd_ext *e, enum sd_code code, void *data,
		       const char *name)
{
	void *pos = e->pos;
	u16 size;
	u32 ret;
	/* check for presence of a tagname, and if present name size
	 * SD_NAME tag value is a u16 */
	if (sd_is_X(e, SD_NAME, &size)) {
		/* if a name is specified it must match. otherwise skip tag */
		if (name && ((strlen(name) != size-1) ||
			     strncmp(name, (char *)e->pos, (size_t)size-1)))
			goto fail;
		e->pos += size;
	}
	/* now check if data actually matches */
	ret = sd_is_X(e, code, data);
	if (!ret)
		goto fail;
	return ret;

fail:
	e->pos = pos;
	return 0;
}

/* macro to wrap error case to make a block of reads look nicer */
#define SD_READ_X(E, C, D, N) \
	do { \
		u32 __ret; \
		__ret = sd_is_nameX((E), (C), (D), (N)); \
		if (!__ret) \
			goto fail; \
	} while (0)

/**
 * sd_activate_net_entry - ignores/skips net entries if the they are present
 * in the data stream.
 * @e: extent information
 */
static inline int sd_activate_net_entry(struct sd_ext *e)
{
	SD_READ_X(e, SD_STRUCT, NULL, "ne");
	SD_READ_X(e, SD_U32, NULL, NULL);
	SD_READ_X(e, SD_U32, NULL, NULL);
	SD_READ_X(e, SD_U32, NULL, NULL);
	SD_READ_X(e, SD_U16, NULL, NULL);
	SD_READ_X(e, SD_U16, NULL, NULL);
	SD_READ_X(e, SD_U32, NULL, NULL);
	SD_READ_X(e, SD_U32, NULL, NULL);
	SD_READ_X(e, SD_U16, NULL, NULL);
	SD_READ_X(e, SD_U16, NULL, NULL);
	/* interface name is optional so just ignore return code */
	sd_is_nameX(e, SD_DYN_STRING, NULL, NULL);
	SD_READ_X(e, SD_STRUCTEND, NULL, NULL);

	return 1;
fail:
	return 0;
}

static inline struct sd_entry *sd_activate_file_entry(struct sd_ext *e)
{
	struct sd_entry *entry = NULL;

	if (!(entry = alloc_sd_entry()))
		goto fail;

	SD_READ_X(e, SD_STRUCT, NULL, "fe");
	SD_READ_X(e, SD_DYN_STRING, &entry->filename, NULL);
	SD_READ_X(e, SD_U32, &entry->mode, "file.mode");
	SD_READ_X(e, SD_U32, &entry->entry_type, "file.pattern_type");

	entry->extradata = sdmatch_alloc(entry->entry_type);
	if (IS_ERR(entry->extradata)) {
		entry->extradata = NULL;
		goto fail;
	}

	if (entry->extradata &&
	    sdmatch_serialize(entry->extradata, e, sd_is_nameX) != 0) {
		goto fail;
	}
	SD_READ_X(e, SD_STRUCTEND, NULL, NULL);

	switch (entry->entry_type) {
	case sd_entry_literal:
		SD_DEBUG("%s: %s [no pattern] mode=0x%x\n",
			 __FUNCTION__,
			 entry->filename,
			 entry->mode);
		break;
	case sd_entry_tailglob:
		SD_DEBUG("%s: %s [tailglob] mode=0x%x\n",
			 __FUNCTION__,
			 entry->filename,
			 entry->mode);
		break;
	case sd_entry_pattern:
		SD_DEBUG("%s: %s mode=0x%x\n",
			 __FUNCTION__,
			 entry->filename,
			 entry->mode);
		break;
	default:
		SD_WARN("%s: INVALID entry_type %d\n",
			__FUNCTION__,
			(int)entry->entry_type);
		goto fail;
	}

	return entry;

fail:
	sdmatch_free(entry->extradata);
	free_sd_entry(entry);
	return NULL;
}

static inline int check_rule_and_add(struct sd_entry *file_entry,
				     struct sdprofile *profile,
				     const char **message)
{
	/* verify consistency of x, px, ix, ux for entry against
	   possible duplicates for this entry */
	int mode = SD_EXEC_MODIFIER_MASK(file_entry->mode);
	int i;

	if (mode && !(SD_MAY_EXEC & file_entry->mode)) {
		*message = "inconsistent rule, x modifiers without x";
		goto out;
	}

	/* check that only 1 of the modifiers is set */
	if (mode && (mode & (mode - 1))) {
		*message = "inconsistent rule, multiple x modifiers";
		goto out;
	}

	/* ix -> m (required so that target exec binary may map itself) */
	if (mode & SD_EXEC_INHERIT)
		file_entry->mode |= SD_EXEC_MMAP;

	list_add(&file_entry->list, &profile->file_entry);
	profile->num_file_entries++;

	mode = file_entry->mode;

	/* Handle partitioned lists
	 * Chain entries onto sublists based on individual
	 * permission bits. This allows more rapid searching.
	 */
	for (i = 0; i <= POS_SD_FILE_MAX; i++) {
		if (mode & (1 << i))
			/* profile->file_entryp[i] initially set to
			 * NULL in alloc_sdprofile() */
			list_add(&file_entry->listp[i],
				 &profile->file_entryp[i]);
	}

	return 1;

out:
	free_sd_entry(file_entry);
	return 0;
}

#define SD_ENTRY_LIST(NAME) \
	do { \
	if (sd_is_nameX(e, SD_LIST, NULL, (NAME))) { \
		rulename = ""; \
		error_string = "Invalid file entry"; \
		while (!sd_is_nameX(e, SD_LISTEND, NULL, NULL)) { \
			struct sd_entry *file_entry; \
			file_entry = sd_activate_file_entry(e); \
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

struct sdprofile *sd_activate_profile(struct sd_ext *e, ssize_t *error)
{
	struct sdprofile *profile = NULL;
	const char *rulename = "";
	const char *error_string = "Invalid Profile";

	*error = -EPROTO;

	profile = alloc_sdprofile();
	if (!profile) {
		error_string = "Could not allocate profile";
		*error = -ENOMEM;
		goto fail;
	}

	/* check that we have the right struct being passed */
	SD_READ_X(e, SD_STRUCT, NULL, "profile");
	SD_READ_X(e, SD_DYN_STRING, &profile->name, NULL);

	error_string = "Invalid flags";
	/* per profile debug flags (debug, complain, audit) */
	SD_READ_X(e, SD_STRUCT, NULL, "flags");
	SD_READ_X(e, SD_U32, &(profile->flags.debug), "profile.flags.debug");
	SD_READ_X(e, SD_U32, &(profile->flags.complain),
		  "profile.flags.complain");
	SD_READ_X(e, SD_U32, &(profile->flags.audit), "profile.flags.audit");
	SD_READ_X(e, SD_STRUCTEND, NULL, NULL);

	error_string = "Invalid capabilities";
	SD_READ_X(e, SD_U32, &(profile->capabilities), "profile.capabilities");

	/* get the file entries. */
	SD_ENTRY_LIST("pgent");		/* pcre rules */
	SD_ENTRY_LIST("sgent");		/* simple globs */
	SD_ENTRY_LIST("fent");		/* regular file entries */

	/* get the net entries */
	if (sd_is_nameX(e, SD_LIST, NULL, "net")) {
		error_string = "Invalid net entry";
		while (!sd_is_nameX(e, SD_LISTEND, NULL, NULL)) {
			if (!sd_activate_net_entry(e))
				goto fail;
		}
	}
	rulename = "";

	/* get subprofiles */
	if (sd_is_nameX(e, SD_LIST, NULL, "hats")) {
		error_string = "Invalid profile hat";
		while (!sd_is_nameX(e, SD_LISTEND, NULL, NULL)) {
			struct sdprofile *subprofile;
			subprofile = sd_activate_profile(e, error);
			if (!subprofile)
				goto fail;
			get_sdprofile(subprofile);
			list_add(&subprofile->list, &profile->sub);
		}
	}

	error_string = "Invalid end of profile";
	SD_READ_X(e, SD_STRUCTEND, NULL, NULL);

	return profile;

fail:
	SD_WARN("%s: %s %s in profile %s\n", INTERFACE_ID, rulename,
		error_string, profile && profile->name ? profile->name
		: "unknown");

	if (profile) {
		free_sdprofile(profile);
		profile = NULL;
	}

	return NULL;
}

void *sd_activate_top_profile(struct sd_ext *e, ssize_t *error)
{
	/* get the interface version */
	if (!sd_is_nameX(e, SD_U32, &e->version, "version")) {
		SD_WARN("%s: version missing\n", INTERFACE_ID);
		*error = -EPROTONOSUPPORT;
		goto out;
	}

	/* check that the interface version is currently supported */
	if (e->version != 2) {
		SD_WARN("%s: unsupported interface version (%d)\n",
			INTERFACE_ID, e->version);
		*error = -EPROTONOSUPPORT;
		goto out;
	}

	return sd_activate_profile(e, error);
out:
	return NULL;
}

ssize_t sd_file_prof_add(void *data, size_t size)
{
	struct sdprofile *profile = NULL;

	struct sd_ext e = { data, data + size, data };
	ssize_t error;

	profile = sd_activate_top_profile(&e, &error);
	if (!profile) {
		SD_DEBUG("couldn't activate profile\n");
		return error;
	}

	if (!sd_profilelist_add(profile)) {
		SD_WARN("trying to add profile (%s) that already exists.\n",
			profile->name);
		free_sdprofile(profile);
		return -EEXIST;
	}

	return size;
}

ssize_t sd_file_prof_repl(void *udata, size_t size)
{
	struct sd_taskreplace_data data;
	struct sd_ext e = { udata, udata + size, udata };
	ssize_t error;

	data.new_profile = sd_activate_top_profile(&e, &error);
	if (!data.new_profile) {
		SD_DEBUG("couldn't activate profile\n");
		return error;
	}
	/* Grab reference to close race window (see comment below) */
	get_sdprofile(data.new_profile);

	/* Replace the profile on the global profile list.
	 * This list is used by all new exec's to find the correct profile.
	 * If there was a previous profile, it is returned, else NULL.
	 *
	 * N.B sd_profilelist_replace does not drop the refcnt on
	 * old_profile when removing it from the global list, otherwise it
	 * could reach zero and be automatically free'd. We nust manually
	 * drop it at the end of this function when we are finished with it.
	 */
	data.old_profile = sd_profilelist_replace(data.new_profile);

	/* RACE window here.
	 * At this point another task could preempt us trying to replace
	 * the SAME profile. If it makes it to this point,  it has removed
	 * the original tasks new_profile from the global list and holds a
	 * reference of 1 to it in it's old_profile.  If the new task
	 * reaches the end of the function it will put old_profile causing
	 * the profile to be deleted.
	 * When the original task is rescheduled it will continue calling
	 * sd_subdomainlist_iterate relabelling tasks with a profile
	 * which points to free'd memory.
	 */

	/* If there was an old profile,  find all currently executing tasks
	 * using this profile and replace the old profile with the new.
	 */
	if (data.old_profile) {
		SD_DEBUG("%s: try to replace profile (%p)%s\n",
			 __FUNCTION__,
			 data.old_profile,
			 data.old_profile->name);

		sd_subdomainlist_iterate(taskreplace_iter, (void *)&data);

		/* mark old profile as stale */
		data.old_profile->isstale = 1;

		/* it's off global list, and we are done replacing */
		put_sdprofile(data.old_profile);
	}

	/* Free reference obtained above */
	put_sdprofile(data.new_profile);

	return size;
}

ssize_t sd_file_prof_remove(const char *name, size_t size)
{
	struct sdprofile *old_profile;

	/* Do this step to get a guaranteed reference to profile
	 * as sd_profilelist_remove may drop it to zero which would
	 * made subsequent attempt to iterate using it unsafe
	 */
	old_profile = sd_profilelist_find(name);

	if (old_profile) {
		if (sd_profilelist_remove(name) != 0)
			SD_WARN("%s: race trying to remove profile (%s)\n",
				__FUNCTION__, name);

		/* remove profile from any tasks using it */
		sd_subdomainlist_iterateremove(taskremove_iter,
					       (void *)old_profile);

		/* drop reference obtained by sd_profilelist_find */
		put_sdprofile(old_profile);
	} else {
		SD_WARN("%s: trying to remove profile (%s) that "
			"doesn't exist - skipping.\n", __FUNCTION__, name);
		return -ENOENT;
	}

	return size;
}
