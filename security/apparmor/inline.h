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

static inline int __aa_is_confined(struct subdomain *sd)
{
	return (sd && sd->active);
}

/**
 *  aa_is_confined
 *  Determine whether current task contains a valid profile (confined).
 *  Return %1 if confined, %0 otherwise.
 */
static inline int aa_is_confined(void)
{
	struct subdomain *sd = AA_SUBDOMAIN(current->security);
	return __aa_is_confined(sd);
}

static inline int __aa_sub_defined(struct subdomain *sd)
{
    return __aa_is_confined(sd) && !list_empty(&BASE_PROFILE(sd->active)->sub);
}

/**
 * aa_sub_defined - check to see if current task has any subprofiles
 * Return 1 if true, 0 otherwise
 */
static inline int aa_sub_defined(void)
{
	struct subdomain *sd = AA_SUBDOMAIN(current->security);
	return __aa_sub_defined(sd);
}

/**
 * get_aaprofile - increment refcount on profile @p
 * @p: profile
 */
static inline struct aaprofile *get_aaprofile(struct aaprofile *p)
{
	if (p)
		kref_get(&(BASE_PROFILE(p)->count));

	return p;
}

/**
 * put_aaprofile - decrement refcount on profile @p
 * @p: profile
 */
static inline void put_aaprofile(struct aaprofile *p)
{
	if (p)
		kref_put(&BASE_PROFILE(p)->count, free_aaprofile_kref);
}

/**
 * get_task_activeptr_rcu - get pointer to @tsk's active profile.
 * @tsk: task to get active profile from
 *
 * Requires rcu_read_lock is held
 */
static inline struct aaprofile *get_task_activeptr_rcu(struct task_struct *tsk)
{
	struct subdomain *sd = AA_SUBDOMAIN(tsk->security);
	struct aaprofile *active = NULL;

	if (sd)
		active = (struct aaprofile *) rcu_dereference(sd->active);

	return active;
}

/**
 * get_activeptr_rcu - get pointer to current task's active profile
 * Requires rcu_read_lock is held
 */
static inline struct aaprofile *get_activeptr_rcu(void)
{
	return get_task_activeptr_rcu(current);
}

/**
 * get_task_active_aaprofile - get a reference to tsk's active profile.
 * @tsk: the task to get the active profile reference for
 */
static inline struct aaprofile *get_task_active_aaprofile(struct task_struct *tsk)
{
	struct aaprofile *active;

	rcu_read_lock();
	active = get_aaprofile(get_task_activeptr_rcu(tsk));
	rcu_read_unlock();

	return active;
}

/**
 * get_active_aaprofile - get a reference to the current tasks active profile
 */
static inline struct aaprofile *get_active_aaprofile(void)
{
	return get_task_active_aaprofile(current);
}

/**
 * aa_switch - change subdomain to use a new profile
 * @sd: subdomain to switch the active profile on
 * @newactive: new active profile
 *
 * aa_switch handles the changing of a subdomain's active profile.  The
 * sd_lock must be held to ensure consistency against other writers.
 * Some write paths (ex. aa_register) require sd->active not to change
 * over several operations, so the calling function is responsible
 * for grabing the sd_lock to meet its consistency constraints before
 * calling aa_switch
 */
static inline void aa_switch(struct subdomain *sd, struct aaprofile *newactive)
{
	struct aaprofile *oldactive = sd->active;

	/* noop if NULL */
	rcu_assign_pointer(sd->active, get_aaprofile(newactive));
	put_aaprofile(oldactive);
}

/**
 * aa_switch_unconfined - change subdomain to be unconfined (no profile)
 * @sd: subdomain to switch
 *
 * aa_switch_unconfined handles the removal of a subdomain's active profile.
 * The sd_lock must be held to ensure consistency against other writers.
 * Like aa_switch the sd_lock is used to maintain consistency.
 */
static inline void aa_switch_unconfined(struct subdomain *sd)
{
	aa_switch(sd, NULL);

	/* reset magic in case we were in a subhat before */
	sd->hat_magic = 0;
}

/**
 * alloc_subdomain - allocate a new subdomain
 * @tsk: task struct
 *
 * Allocate a new subdomain including a backpointer to it's referring task.
 */
static inline struct subdomain *alloc_subdomain(struct task_struct *tsk)
{
	struct subdomain *sd;

	sd = kzalloc(sizeof(struct subdomain), GFP_KERNEL);
	if (!sd)
		goto out;

	/* back pointer to task */
	sd->task = tsk;

	/* any readers of the list must make sure that they can handle
	 * case where sd->active is not yet set (null)
	 */
	aa_subdomainlist_add(sd);

out:
	return sd;
}

/**
 * free_subdomain - Free a subdomain previously allocated by alloc_subdomain
 * @sd: subdomain
 */
static inline void free_subdomain(struct subdomain *sd)
{
	aa_subdomainlist_remove(sd);
	kfree(sd);
}

/**
 * alloc_aaprofile - Allocate, initialize and return a new zeroed profile.
 * Returns NULL on failure.
 */
static inline struct aaprofile *alloc_aaprofile(void)
{
	struct aaprofile *profile;

	profile = (struct aaprofile *)kzalloc(sizeof(struct aaprofile),
					      GFP_KERNEL);
	AA_DEBUG("%s(%p)\n", __FUNCTION__, profile);
	if (profile) {
		int i;

		INIT_LIST_HEAD(&profile->list);
		INIT_LIST_HEAD(&profile->sub);
		INIT_LIST_HEAD(&profile->file_entry);
		for (i = 0; i <= POS_AA_FILE_MAX; i++) {
			INIT_LIST_HEAD(&profile->file_entryp[i]);
		}
		INIT_RCU_HEAD(&profile->rcu);
		kref_init(&profile->count);
	}
	return profile;
}

/**
 * aa_put_name
 * @name: name to release.
 *
 * Release space (free_page) allocated to hold pathname
 * name may be NULL (checked for by free_page)
 */
static inline void aa_put_name(const char *name)
{
	free_page((unsigned long)name);
}

/** __aa_find_profile
 * @name: name of profile to find
 * @head: list to search
 *
 * Return reference counted copy of profile. NULL if not found
 * Caller must hold any necessary locks
 */
static inline struct aaprofile *__aa_find_profile(const char *name,
						  struct list_head *head)
{
	struct aaprofile *p;

	if (!name || !head)
		return NULL;

	AA_DEBUG("%s: finding profile %s\n", __FUNCTION__, name);
	list_for_each_entry(p, head, list) {
		if (!strcmp(p->name, name)) {
			/* return refcounted object */
			p = get_aaprofile(p);
			return p;
		} else {
			AA_DEBUG("%s: skipping %s\n", __FUNCTION__, p->name);
		}
	}
	return NULL;
}

/** __aa_path_begin
 * @rdentry: filesystem root dentry (searching for vfsmnts matching this)
 * @dentry: dentry object to obtain pathname from (relative to matched vfsmnt)
 *
 * Setup data for iterating over vfsmounts (in current tasks namespace).
 */
static inline void __aa_path_begin(struct dentry *rdentry,
				   struct dentry *dentry,
				   struct aa_path_data *data)
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

/** aa_path_begin
 * @dentry: filesystem root dentry and object to obtain pathname from
 *
 * Utility function for calling _aa_path_begin for when the dentry we are
 * looking for and the root are the same (this is the usual case).
 */
static inline void aa_path_begin(struct dentry *dentry,
				     struct aa_path_data *data)
{
	__aa_path_begin(dentry, dentry, data);
}

/** aa_path_end
 * @data: data object previously initialized by aa_path_begin
 *
 * End iterating over vfsmounts.
 * If an error occured in begin or get, it is returned. Otherwise 0.
 */
static inline int aa_path_end(struct aa_path_data *data)
{
	up_read(&namespace_sem);
	dput(data->root);

	return data->errno;
}

/** aa_path_getname
 * @data: data object previously initialized by aa_path_begin
 *
 * Return the next mountpoint which has the same root dentry as data->root.
 * If no more mount points exist (or in case of error) NULL is returned
 * (caller should call aa_path_end() and inspect return code to differentiate)
 */
static inline char *aa_path_getname(struct aa_path_data *data)
{
	char *name = NULL;
	struct vfsmount *mnt;

	while (data->pos != data->head) {
		mnt = list_entry(data->pos, struct vfsmount, mnt_list);

		/* advance to next -- so that it is done before we break */
		data->pos = data->pos->next;
		prefetch(data->pos->next);

		if (mnt->mnt_root == data->root) {
			name = aa_get_name(data->dentry, mnt);
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
