/*
 *	Copyright (C) 1998-2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	SubDomain Profile List Management
 */

#include <linux/seq_file.h>
#include "subdomain.h"
#include "inline.h"

/* list of all profiles and lock */
static LIST_HEAD(profile_list);
static rwlock_t profile_lock = RW_LOCK_UNLOCKED;

/* list of all subdomains and lock */
static LIST_HEAD(subdomain_list);
static rwlock_t subdomain_lock = RW_LOCK_UNLOCKED;

/**
 * sd_profilelist_find
 * @name: profile name (program name)
 *
 * Search the profile list for profile @name.  Return refcounted profile on
 * success, NULL on failure.
 */
struct sdprofile *sd_profilelist_find(const char *name)
{
	struct sdprofile *p = NULL;
	if (name) {
		read_lock(&profile_lock);
		p = __sd_find_profile(name, &profile_list);
		read_unlock(&profile_lock);
	}
	return p;
}

/**
 * sd_profilelist_add
 * @profile: new profile to add to list
 *
 * Add new profile to list.  Reference count on profile is incremented.
 * Return 1 on success, 0 on failure (bad profile or already exists)
 */
int sd_profilelist_add(struct sdprofile *profile)
{
	struct sdprofile *old_profile;
	int ret = 0;

	if (!profile)
		goto out;

	write_lock(&profile_lock);
	old_profile = __sd_find_profile(profile->name, &profile_list);
	if (old_profile) {
		put_sdprofile(old_profile);
		goto out;
	}
	profile = get_sdprofile(profile);
	list_add(&profile->list, &profile_list);
	ret = 1;
 out:
	write_unlock(&profile_lock);
	return ret;
}

/**
 * sd_profilelist_remove
 * @name: name of profile to be removed
 *
 * Remove profile from list.  Reference count on profile is decremented.
 */
int sd_profilelist_remove(const char *name)
{
	struct sdprofile *profile = NULL;
	struct sdprofile *p, *tmp;
	int error = -ENOENT;

	if (!name)
		goto out;

	write_lock(&profile_lock);
	list_for_each_entry_safe(p, tmp, &profile_list, list) {
		if (!strcmp(p->name, name)) {
			list_del_init(&p->list);
			profile = p;
			break;
		}
	}
	write_unlock(&profile_lock);
	if (!profile)
		goto out;

	put_sdprofile(profile);
	error = 0;
out:
	return error;
}

/**
 * sd_profilelist_replace
 * @profile - new profile
 *
 * Replace a profile on the profile list.  Find the old profile by name in
 * the list, and replace it with the new profile.  This is an atomic
 * list operation.  Returns the old profile (which is still refcounted) if
 * there was one, or NULL.
 */
struct sdprofile *sd_profilelist_replace(struct sdprofile *profile)
{
	struct sdprofile *oldprofile;

	write_lock(&profile_lock);
	oldprofile = __sd_find_profile(profile->name, &profile_list);
	if (oldprofile) {
		list_del_init(&oldprofile->list);

		/* __sd_find_profile incremented count, so adjust down */
		put_sdprofile(oldprofile);
	}
	profile = get_sdprofile(profile);
	list_add(&profile->list, &profile_list);
	write_unlock(&profile_lock);

	return oldprofile;
}

/**
 * sd_profilelist_release
 *
 * Remove all profiles from profile_list
 */
void sd_profilelist_release(void)
{
	struct sdprofile *p, *tmp;

	write_lock(&profile_lock);
	list_for_each_entry_safe(p, tmp, &profile_list, list) {
		list_del_init(&p->list);
		put_sdprofile(p);
	}
	write_unlock(&profile_lock);
}

/**
 * sd_subdomainlist_add
 * @sd: new subdomain
 *
 * Add subdomain to subdomain_list
 */
void sd_subdomainlist_add(struct subdomain *sd)
{
	unsigned long flags;

	if (!sd) {
		SD_INFO("%s: bad subdomain\n", __FUNCTION__);
		return;
	}

	write_lock_irqsave(&subdomain_lock, flags);
	list_add(&sd->list, &subdomain_list);
	write_unlock_irqrestore(&subdomain_lock, flags);
}

/**
 * sd_subdomainlist_remove
 * @sd: subdomain to be removed
 *
 * Remove subdomain from subdomain_list
 */
int sd_subdomainlist_remove(struct subdomain *sd)
{
	struct subdomain *node, *tmp;
	int error = -ENOENT;
	unsigned long flags;

	if (!sd)
		goto out;

	write_lock_irqsave(&subdomain_lock, flags);
	list_for_each_entry_safe(node, tmp, &subdomain_list, list) {
		if (node == sd) {
			list_del_init(&node->list);
			error = 0;
			break;
		}
	}
	write_unlock_irqrestore(&subdomain_lock, flags);

out:
	return error;
}

/**
 * sd_subdomainlist_iterate
 * @func: method to be called for each element
 * @cookie: user passed data
 *
 * Iterate over subdomain list, stop when sd_iter func returns non zero
 */
void sd_subdomainlist_iterate(sd_iter func, void *cookie)
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
 * sd_subdomainlist_iterateremove
 * @func: method to be called for each element
 * @cookie: user passed data
 *
 * Iterate over subdomain_list, remove element when sd_iter func returns
 * non zero
 */
void sd_subdomainlist_iterateremove(sd_iter func, void *cookie)
{
	struct subdomain *node, *tmp;
	int ret = 0;
	unsigned long flags;

	write_lock_irqsave(&subdomain_lock, flags);
	list_for_each_entry_safe(node, tmp, &subdomain_list, list) {
		ret = (*func) (node, cookie);
		if (ret != 0)
			list_del_init(&node->list);
	}
	write_unlock_irqrestore(&subdomain_lock, flags);
}

/**
 * sd_subdomainlist_release
 *
 * Remove all subdomains from subdomain_list
 */
void sd_subdomainlist_release()
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
 * Used by subdomainfs.c to iterate over profile_list
 */
static void *p_start(struct seq_file *f, loff_t *pos)
{
	struct sdprofile *node;
	loff_t l = *pos;

	read_lock(&profile_lock);
	list_for_each_entry(node, &profile_list, list)
		if (!l--)
			return node;
	return NULL;
}

static void *p_next(struct seq_file *f, void *p, loff_t *pos)
{
	struct list_head *lh = ((struct sdprofile *)p)->list.next;
	(*pos)++;
	return lh == &profile_list ?
			NULL : list_entry(lh, struct sdprofile, list);
}

static void p_stop(struct seq_file *f, void *v)
{
	read_unlock(&profile_lock);
}

static int seq_show_profile(struct seq_file *f, void *v)
{
	struct sdprofile *profile = (struct sdprofile *)v;
	seq_printf(f, "%s (%s)\n", profile->name,
		   PROFILE_COMPLAIN(profile) ? "complain" : "enforce");
	return 0;
}

struct seq_operations subdomainfs_profiles_op = {
	.start =	p_start,
	.next =		p_next,
	.stop =		p_stop,
	.show =		seq_show_profile,
};
