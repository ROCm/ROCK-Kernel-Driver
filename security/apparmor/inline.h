/*
 *	Copyright (C) 2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 */

#ifndef __INLINE_H
#define __INLINE_H

#include <linux/namespace.h>

static inline int __sd_is_confined(struct subdomain *sd)
{
	int rc = 0;

	if (sd && sd->sd_magic == SD_ID_MAGIC && sd->profile) {
		BUG_ON(!sd->active);
		rc = 1;
	}

	return rc;
}

/**
 *  sd_is_confined
 *  @sd: subdomain
 *
 *  Check if @sd is confined (contains a valid profile)
 *  Return 1 if confined, 0 otherwise.
 */
static inline int sd_is_confined(void)
{
	struct subdomain *sd = SD_SUBDOMAIN(current->security);
	return __sd_is_confined(sd);
}

static inline int __sd_sub_defined(struct subdomain *sd)
{
	return __sd_is_confined(sd) && !list_empty(&sd->profile->sub);
}

/**
 * sd_sub_defined
 * @sd: subdomain
 *
 * Check if @sd has at least one subprofile
 * Return 1 if true, 0 otherwise
 */
static inline int sd_sub_defined(void)
{
	struct subdomain *sd = SD_SUBDOMAIN(current->security);
	return __sd_sub_defined(sd);
}

/**
 * get_sdprofile
 * @p: profile
 *
 * Increment refcount on profile
 */
static inline struct sdprofile *get_sdprofile(struct sdprofile *p)
{
	if (p)
		atomic_inc(&p->count);
	return p;
}

/**
 * put_sdprofile
 * @p: profile
 *
 * Decrement refcount on profile
 */
static inline void put_sdprofile(struct sdprofile *p)
{
	if (p)
		if (atomic_dec_and_test(&p->count))
			free_sdprofile(p);
}

/**
 * sd_switch
 * @sd: subdomain to switch
 * @profile: new profile
 * @active:  new active
 *
 * Change subdomain to use new profiles.
 */
static inline void sd_switch(struct subdomain *sd,
		      		 struct sdprofile *profile,
				 struct sdprofile *active)
{
	/* noop if NULL */
	put_sdprofile(sd->profile);
	put_sdprofile(sd->active);

	sd->profile = get_sdprofile(profile);
	sd->active = get_sdprofile(active);
}

/**
 * sd_switch_unconfined
 * @sd: subdomain to switch
 *
 * Change subdomain to unconfined
 */
static inline void sd_switch_unconfined(struct subdomain *sd)
{
	sd_switch(sd, NULL, NULL);

	/* reset magic in case we were in a subhat before */
	sd->sd_hat_magic = 0;
}

/**
 * alloc_subdomain
 * @tsk: task struct
 *
 * Allocate a new subdomain including a backpointer to it's referring task.
 */
static inline struct subdomain *alloc_subdomain(struct task_struct *tsk)
{
	struct subdomain *sd;

	sd = kmalloc(sizeof(struct subdomain), GFP_KERNEL);
	if (!sd)
		goto out;

	/* zero it first */
	memset(sd, 0, sizeof(struct subdomain));
	sd->sd_magic = SD_ID_MAGIC;

	/* back pointer to task */
	sd->task = tsk;

	/* any readers of the list must make sure that they can handle
	 * case where sd->profile and sd->active are not yet set (null)
	 */
	sd_subdomainlist_add(sd);

out:
	return sd;
}

/**
 * free_subdomain
 * @sd: subdomain
 *
 * Free a subdomain previously allocated by alloc_subdomain
 */
static inline void free_subdomain(struct subdomain *sd)
{
	sd_subdomainlist_remove(sd);
	kfree(sd);
}

/**
 * alloc_sdprofile
 *
 * Allocate, initialize and return a new zeroed profile.
 * Returns NULL on failure.
 */
static inline struct sdprofile *alloc_sdprofile(void)
{
	struct sdprofile *profile;

	profile = (struct sdprofile *)kmalloc(sizeof(struct sdprofile),
					      GFP_KERNEL);
	SD_DEBUG("%s(%p)\n", __FUNCTION__, profile);
	if (profile) {
		int i;
		memset(profile, 0, sizeof(struct sdprofile));
		INIT_LIST_HEAD(&profile->list);
		INIT_LIST_HEAD(&profile->sub);
		INIT_LIST_HEAD(&profile->file_entry);
		for (i = 0; i <= POS_SD_FILE_MAX; i++) {
			INIT_LIST_HEAD(&profile->file_entryp[i]);
		}
	}
	return profile;
}

/**
 * sd_put_name
 * @name: name to release.
 *
 * Release space (free_page) allocated to hold pathname
 * name may be NULL (checked for by free_page)
 */
static inline void sd_put_name(const char *name)
{
	free_page((unsigned long)name);
}

/** __sd_find_profile
 * @name: name of profile to find
 * @head: list to search
 *
 * Return reference counted copy of profile. NULL if not found
 * Caller must hold any necessary locks
 */
static inline struct sdprofile *__sd_find_profile(const char *name,
						      struct list_head *head)
{
	struct sdprofile *p;

	if (!name || !head)
		return NULL;

	SD_DEBUG("%s: finding profile %s\n", __FUNCTION__, name);
	list_for_each_entry(p, head, list) {
		if (!strcmp(p->name, name)) {
			/* return refcounted object */
			p = get_sdprofile(p);
			return p;
		} else {
			SD_DEBUG("%s: skipping %s\n", __FUNCTION__, p->name);
		}
	}
	return NULL;
}

static inline struct subdomain *__get_sdcopy(struct subdomain *new,
						 struct task_struct *tsk)
{
	struct subdomain *old, *temp = NULL;

	old = SD_SUBDOMAIN(tsk->security);

	if (old) {
		new->sd_magic = old->sd_magic;
		new->sd_hat_magic = old->sd_hat_magic;

		new->active = get_sdprofile(old->active);

		if (old->profile == old->active)
			new->profile = new->active;
		else
			new->profile = get_sdprofile(old->profile);

		temp = new;
	}

	return temp;
}

/** get_sdcopy
 * @new: subdomain to hold copy
 *
 * Make copy of current subdomain containing refcounted profile and active
 * Used to protect readers against racing writers (changehat and profile
 * replacement).
 */
static inline struct subdomain *get_sdcopy(struct subdomain *new)
{
	struct subdomain *temp;
	unsigned long flags;

	read_lock_irqsave(&sd_lock, flags);

	temp = __get_sdcopy(new, current);

	read_unlock_irqrestore(&sd_lock, flags);

	return temp;
}

/** get_sdcopy
 * @temp: subdomain to drop refcounts on
 *
 * Drop refcounted profile/active in copy of subdomain made by get_sdcopy
 */
static inline void put_sdcopy(struct subdomain *temp)
{
	if (temp) {
		put_sdprofile(temp->active);
		if (temp->active != temp->profile)
			(void)put_sdprofile(temp->profile);
	}
}

/** sd_path_begin2
 * @rdentry: filesystem root dentry (searching for vfsmnts matching this)
 * @dentry: dentry object to obtain pathname from (relative to matched vfsmnt)
 *
 * Setup data for iterating over vfsmounts (in current tasks namespace).
 */
static inline void sd_path_begin2(struct dentry *rdentry,
				      struct dentry *dentry,
				      struct sd_path_data *data)
{
	data->dentry = dentry;
	data->root = dget(rdentry->d_sb->s_root);
	data->namespace = current->namespace;
	data->head = &data->namespace->list;
	data->pos = data->head->next;
	prefetch(data->pos->next);
	data->errno = 0;

	down_read(&namespace_sem);
}

/** sd_path_begin
 * @dentry filesystem root dentry and object to obtain pathname from
 *
 * Utility function for calling _sd_path_begin for when the dentry we are
 * looking for and the root are the same (this is the usual case).
 */
static inline void sd_path_begin(struct dentry *dentry,
				     struct sd_path_data *data)
{
	sd_path_begin2(dentry, dentry, data);
}

/** sd_path_end
 * @data: data object previously initialized by sd_path_begin
 *
 * End iterating over vfsmounts.
 * If an error occured in begin or get, it is returned. Otherwise 0.
 */
static inline int sd_path_end(struct sd_path_data *data)
{
	up_read(&namespace_sem);
	dput(data->root);

	return data->errno;
}

/** sd_path_getname
 * @data: data object previously initialized by sd_path_begin
 *
 * Return the next mountpoint which has the same root dentry as data->root.
 * If no more mount points exist (or in case of error) NULL is returned
 * (caller should call sd_path_end() and inspect return code to differentiate)
 */
static inline char *sd_path_getname(struct sd_path_data *data)
{
	char *name = NULL;
	struct vfsmount *mnt;

	while (data->pos != data->head) {
		mnt = list_entry(data->pos, struct vfsmount, mnt_list);

		/* advance to next -- so that it is done before we break */
		data->pos = data->pos->next;
		prefetch(data->pos->next);

		if (mnt->mnt_root == data->root) {
			name = sd_get_name(data->dentry, mnt);
			if (IS_ERR(name)) {
				data->errno = PTR_ERR(name);
				name = NULL;
			}
			break;
		}
	}

	return name;
}

#endif /* __INLINE_H__ */
