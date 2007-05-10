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

/* list of all profiles and lock */
LIST_HEAD(profile_list);
rwlock_t profile_list_lock = RW_LOCK_UNLOCKED;

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

/**
 * aa_profilelist_release - Remove all profiles from profile_list
 */
void aa_profilelist_release(void)
{
	struct aa_profile *p, *tmp;

	write_lock(&profile_list_lock);
	list_for_each_entry_safe(p, tmp, &profile_list, list) {
		list_del_init(&p->list);
		aa_put_profile(p);
	}
	write_unlock(&profile_list_lock);
}

static void *p_start(struct seq_file *f, loff_t *pos)
{
	struct aa_profile *node;
	loff_t l = *pos;

	read_lock(&profile_list_lock);
	list_for_each_entry(node, &profile_list, list)
		if (!l--)
			return node;
	return NULL;
}

static void *p_next(struct seq_file *f, void *p, loff_t *pos)
{
	struct list_head *lh = ((struct aa_profile *)p)->list.next;
	(*pos)++;
	return lh == &profile_list ?
			NULL : list_entry(lh, struct aa_profile, list);
}

static void p_stop(struct seq_file *f, void *v)
{
	read_unlock(&profile_list_lock);
}

static int seq_show_profile(struct seq_file *f, void *v)
{
	struct aa_profile *profile = (struct aa_profile *)v;
	seq_printf(f, "%s (%s)\n", profile->name,
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
