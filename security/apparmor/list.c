/*
 *	Copyright (C) 1998-2007 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor Profile List Management
 */

#include <linux/seq_file.h>
#include "apparmor.h"
#include "inline.h"

/* list of profile namespaces and lock */
LIST_HEAD(profile_ns_list);
rwlock_t profile_ns_list_lock = RW_LOCK_UNLOCKED;

/**
 * __aa_find_namespace  -  look up a profile namespace on the namespace list
 * @name: name of namespace to find
 * @head: list to search
 *
 * Returns a pointer to the namespace on the list, or NULL if no namespace
 * called @name exists. The caller must hold the profile_ns_list_lock.
 */
struct aa_namespace *__aa_find_namespace(const char *name,
				       struct list_head *head)
{
	struct aa_namespace *ns;

	list_for_each_entry(ns, head, list) {
		if (!strcmp(ns->name, name))
			return ns;
	}

	return NULL;
}

/**
 * __aa_find_profile  -  look up a profile on the profile list
 * @name: name of profile to find
 * @head: list to search
 *
 * Returns a pointer to the profile on the list, or NULL if no profile
 * called @name exists. The caller must hold the profile_list_lock.
 */
struct aa_profile *__aa_find_profile(const char *name, struct list_head *head)
{
	struct aa_profile *profile;

	list_for_each_entry(profile, head, list) {
		if (!strcmp(profile->name, name))
			return profile;
	}

	return NULL;
}

static void aa_profile_list_release(struct list_head *head)
{
	struct aa_profile *profile, *tmp;
	list_for_each_entry_safe(profile, tmp, head, list) {
		/* Remove the profile from each task context it is on. */
		lock_profile(profile);
		profile->isstale = 1;
		aa_unconfine_tasks(profile);
		list_del_init(&profile->list);
		unlock_profile(profile);
		aa_put_profile(profile);
	}
}

/**
 * aa_profilelist_release - Remove all profiles from profile_list
 */
void aa_profile_ns_list_release(void)
{
	struct aa_namespace *ns, *tmp;

	/* Remove and release all the profiles on namespace profile lists. */
	write_lock(&profile_ns_list_lock);
	list_for_each_entry_safe(ns, tmp, &profile_ns_list, list) {
		write_lock(&ns->lock);
		aa_profile_list_release(&ns->profiles);
		list_del_init(&ns->list);
		write_unlock(&ns->lock);
		aa_put_namespace(ns);
	}
	write_unlock(&profile_ns_list_lock);
}

static void *p_start(struct seq_file *f, loff_t *pos)
{
	struct aa_namespace *ns;
	struct aa_profile *profile;
	loff_t l = *pos;
	read_lock(&profile_ns_list_lock);
	if (l--)
		return NULL;
	list_for_each_entry(ns, &profile_ns_list, list) {
		read_lock(&ns->lock);
		list_for_each_entry(profile, &ns->profiles, list)
			return profile;
		read_unlock(&ns->lock);
	}
	return NULL;
}

static void *p_next(struct seq_file *f, void *p, loff_t *pos)
{
	struct aa_profile *profile = (struct aa_profile *) p;
	struct list_head *lh = profile->list.next;
	struct aa_namespace *ns;
	(*pos)++;
	if (lh != &profile->ns->profiles)
		return list_entry(lh, struct aa_profile, list);

	lh = profile->ns->list.next;
	read_unlock(&profile->ns->lock);
	while (lh != &profile_ns_list) {
		ns = list_entry(lh, struct aa_namespace, list);
		read_lock(&ns->lock);
		list_for_each_entry(profile, &ns->profiles, list)
			return profile;
		read_unlock(&ns->lock);
		lh = ns->list.next;
	}
	return NULL;
}

static void p_stop(struct seq_file *f, void *v)
{
	read_unlock(&profile_ns_list_lock);
}

static int seq_show_profile(struct seq_file *f, void *v)
{
	struct aa_profile *profile = (struct aa_profile *)v;
	if (profile->ns == default_namespace)
	    seq_printf(f, "%s (%s)\n", profile->name,
		       PROFILE_COMPLAIN(profile) ? "complain" : "enforce");
	else
	    seq_printf(f, ":%s:%s (%s)\n", profile->ns->name, profile->name,
		       PROFILE_COMPLAIN(profile) ? "complain" : "enforce");
	return 0;
}

/* Used in apparmorfs.c */
struct seq_operations apparmorfs_profiles_op = {
	.start =	p_start,
	.next =		p_next,
	.stop =		p_stop,
	.show =		seq_show_profile,
};
