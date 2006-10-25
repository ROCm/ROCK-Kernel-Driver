/*
 *	Copyright (C) 1998-2005 Novell/SUSE
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
static LIST_HEAD(profile_list);
static rwlock_t profile_lock = RW_LOCK_UNLOCKED;

/* list of all subdomains and lock */
static LIST_HEAD(subdomain_list);
static rwlock_t subdomain_lock = RW_LOCK_UNLOCKED;

/**
 * aa_profilelist_find
 * @name: profile name (program name)
 *
 * Search the profile list for profile @name.  Return refcounted profile on
 * success, NULL on failure.
 */
struct aaprofile *aa_profilelist_find(const char *name)
{
	struct aaprofile *p = NULL;
	if (name) {
		read_lock(&profile_lock);
		p = __aa_find_profile(name, &profile_list);
		read_unlock(&profile_lock);
	}
	return p;
}

/**
 * aa_profilelist_add - add new profile to list
 * @profile: new profile to add to list
 *
 * NOTE: Caller must allocate necessary reference count that will be used
 * by the profile_list.  This is because profile allocation alloc_aaprofile()
 * returns an unreferenced object with a initial count of %1.
 *
 * Return %1 on success, %0 on failure (already exists)
 */
int aa_profilelist_add(struct aaprofile *profile)
{
	struct aaprofile *old_profile;
	int ret = 0;

	if (!profile)
		goto out;

	write_lock(&profile_lock);
	old_profile = __aa_find_profile(profile->name, &profile_list);
	if (old_profile) {
		put_aaprofile(old_profile);
		goto out;
	}

	list_add(&profile->list, &profile_list);
	ret = 1;
 out:
	write_unlock(&profile_lock);
	return ret;
}

/**
 * aa_profilelist_remove - remove a profile from the list by name
 * @name: name of profile to be removed
 *
 * If the profile exists remove profile from list and return its reference.
 * The reference count on profile is not decremented and should be decremented
 * when the profile is no longer needed
 */
struct aaprofile *aa_profilelist_remove(const char *name)
{
	struct aaprofile *profile = NULL;
	struct aaprofile *p, *tmp;

	if (!name)
		goto out;

	write_lock(&profile_lock);
	list_for_each_entry_safe(p, tmp, &profile_list, list) {
		if (!strcmp(p->name, name)) {
			list_del_init(&p->list);
			/* mark old profile as stale */
			p->isstale = 1;
			profile = p;
			break;
		}
	}
	write_unlock(&profile_lock);

out:
	return profile;
}

/**
 * aa_profilelist_replace - replace a profile on the list
 * @profile: new profile
 *
 * Replace a profile on the profile list.  Find the old profile by name in
 * the list, and replace it with the new profile.   NOTE: Caller must allocate
 * necessary initial reference count for new profile as aa_profilelist_add().
 *
 * This is an atomic list operation.  Returns the old profile (which is still
 * refcounted) if there was one, or NULL.
 */
struct aaprofile *aa_profilelist_replace(struct aaprofile *profile)
{
	struct aaprofile *oldprofile;

	write_lock(&profile_lock);
	oldprofile = __aa_find_profile(profile->name, &profile_list);
	if (oldprofile) {
		list_del_init(&oldprofile->list);
		/* mark old profile as stale */
		oldprofile->isstale = 1;

		/* __aa_find_profile incremented count, so adjust down */
		put_aaprofile(oldprofile);
	}

	list_add(&profile->list, &profile_list);
	write_unlock(&profile_lock);

	return oldprofile;
}

/**
 * aa_profilelist_release - Remove all profiles from profile_list
 */
void aa_profilelist_release(void)
{
	struct aaprofile *p, *tmp;

	write_lock(&profile_lock);
	list_for_each_entry_safe(p, tmp, &profile_list, list) {
		list_del_init(&p->list);
		put_aaprofile(p);
	}
	write_unlock(&profile_lock);
}

/**
 * aa_subdomainlist_add - Add subdomain to subdomain_list
 * @sd: new subdomain
 */
void aa_subdomainlist_add(struct subdomain *sd)
{
	unsigned long flags;

	if (!sd) {
		AA_INFO("%s: bad subdomain\n", __FUNCTION__);
		return;
	}

	write_lock_irqsave(&subdomain_lock, flags);
	/* new subdomains must be added to the end of the list due to a
	 * subtle interaction between fork and profile replacement.
	 */
	list_add_tail(&sd->list, &subdomain_list);
	write_unlock_irqrestore(&subdomain_lock, flags);
}

/**
 * aa_subdomainlist_remove - Remove subdomain from subdomain_list
 * @sd: subdomain to be removed
 */
void aa_subdomainlist_remove(struct subdomain *sd)
{
	unsigned long flags;

	if (sd) {
		write_lock_irqsave(&subdomain_lock, flags);
		list_del_init(&sd->list);
		write_unlock_irqrestore(&subdomain_lock, flags);
	}
}

/**
 * aa_subdomainlist_iterate - iterate over the subdomain list applying @func
 * @func: method to be called for each element
 * @cookie: user passed data
 *
 * Iterate over subdomain list applying @func, stop when @func returns
 * non zero
 */
void aa_subdomainlist_iterate(aa_iter func, void *cookie)
{
	struct subdomain *node;
	int ret = 0;
	unsigned long flags;

	read_lock_irqsave(&subdomain_lock, flags);
	list_for_each_entry(node, &subdomain_list, list) {
		ret = (*func) (node, cookie);
		if (ret != 0)
			break;
	}
	read_unlock_irqrestore(&subdomain_lock, flags);
}

/**
 * aa_subdomainlist_release - Remove all subdomains from subdomain_list
 */
void aa_subdomainlist_release(void)
{
	struct subdomain *node, *tmp;
	unsigned long flags;

	write_lock_irqsave(&subdomain_lock, flags);
	list_for_each_entry_safe(node, tmp, &subdomain_list, list) {
		list_del_init(&node->list);
	}
	write_unlock_irqrestore(&subdomain_lock, flags);
}

/* seq_file helper routines
 * Used by apparmorfs.c to iterate over profile_list
 */
static void *p_start(struct seq_file *f, loff_t *pos)
{
	struct aaprofile *node;
	loff_t l = *pos;

	read_lock(&profile_lock);
	list_for_each_entry(node, &profile_list, list)
		if (!l--)
			return node;
	return NULL;
}

static void *p_next(struct seq_file *f, void *p, loff_t *pos)
{
	struct list_head *lh = ((struct aaprofile *)p)->list.next;
	(*pos)++;
	return lh == &profile_list ?
			NULL : list_entry(lh, struct aaprofile, list);
}

static void p_stop(struct seq_file *f, void *v)
{
	read_unlock(&profile_lock);
}

static int seq_show_profile(struct seq_file *f, void *v)
{
	struct aaprofile *profile = (struct aaprofile *)v;
	seq_printf(f, "%s (%s)\n", profile->name,
		   PROFILE_COMPLAIN(profile) ? "complain" : "enforce");
	return 0;
}

struct seq_operations apparmorfs_profiles_op = {
	.start =	p_start,
	.next =		p_next,
	.stop =		p_stop,
	.show =		seq_show_profile,
};
