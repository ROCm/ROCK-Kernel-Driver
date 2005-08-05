/*
 * Immunix SubDomain Profile List Management
 *
 *  Original 2.2 work
 *  Copyright 1998, 1999, 2000, 2001 Wirex Communications &
 *			Oregon Graduate Institute
 * 
 * 	Written by Steve Beattie <steve@wirex.net>
 *
 *  Updated 2.4/5 work
 *  Copyright (C) 2002 WireX Communications, Inc.
 *
 *  Ported from 2.2 by Chris Wright <chris@wirex.com>
 *
 *  Ported to 2.6 by Tony Jones <tony@immunix.com>
 *  Copyright (C) 2003-2004 Immunix, Inc
 */

#include <linux/config.h>
#ifdef SDLISTLOCK_SEM
#include <linux/rwsem.h>
#else
#include <linux/spinlock.h>
#endif
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include "subdomain.h"
#include "inline.h"

/* list of all profiles */
static LIST_HEAD(profile_list);
// profile list spin lock
static rwlock_t profile_lock = RW_LOCK_UNLOCKED; 
#define LIST_RLOCK	read_lock(&profile_lock)
#define LIST_RUNLOCK	read_unlock(&profile_lock)
#define LIST_WLOCK	write_lock(&profile_lock)
#define LIST_WUNLOCK	write_unlock(&profile_lock)

/* list of all subdomains */
static LIST_HEAD(subdomain_list);
// subdomain list spin lock
static rwlock_t subdomain_lock = RW_LOCK_UNLOCKED; 
#define SDLIST_RLOCK	read_lock(&subdomain_lock)
#define SDLIST_RUNLOCK	read_unlock(&subdomain_lock)
#define SDLIST_WLOCK	write_lock(&subdomain_lock)
#define SDLIST_WUNLOCK	write_unlock(&subdomain_lock)

/**
 * sd_profilelist_find - search for subdomain profile by name
 * @name: profile name (program name)
 *
 * Search the profile list for profile @name.  Return refcounted profile on
 * success, NULL on failure.
 */
struct sdprofile * sd_profilelist_find(const char *name)
{
	struct sdprofile *p = NULL;
	if (name) {
		LIST_RLOCK;
		p = __sd_find_profile(name, &profile_list);
		LIST_RUNLOCK;
	}
	return p;
}

/**
 * sd_profilelist_add - add new profile to profile_list
 * @profile: new profile to add to list
 *
 * Add new profile to list.  Reference count on profile is
 * incremented.
 */
void sd_profilelist_add(struct sdprofile *profile)
{
	if (!profile) {
		SD_INFO("%s: bad profile\n", __FUNCTION__);
		return;
	}

	profile = get_sdprofile(profile);
	LIST_WLOCK;
	list_add(&profile->list, &profile_list);
	LIST_WUNLOCK;
}

/**
 * sd_profilelist_remove - remove profile from profile_list
 * @name: name of profile to be removed
 *
 * Remove profile from list.  Reference count on profile is
 * decremented.
 */
int sd_profilelist_remove(const char *name)
{
	struct sdprofile *profile = NULL;
	struct list_head *lh, *tmp;
	int error = -ENOENT;

	if (!name)
		goto out;

	LIST_WLOCK;
	list_for_each_safe(lh, tmp, &profile_list) {
		struct sdprofile *p = list_entry(lh, struct sdprofile, list);
		if (!strcmp(p->name, name)) {
			list_del_init(&p->list);
			profile = p;
			break;
		}
	}
	LIST_WUNLOCK;
	if (!profile)
		goto out;

	put_sdprofile(profile);
	error = 0;
out:
	return error;
}

/**
 * sd_profilelist_replace - replace profile by name
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

	LIST_WLOCK;
	oldprofile = __sd_find_profile(profile->name, &profile_list);
	if (oldprofile) {
		list_del_init(&oldprofile->list);

		/* __sd_find_profile incremented count, so adjust down */
		put_sdprofile(oldprofile);
	}
	profile = get_sdprofile(profile);
	list_add(&profile->list, &profile_list);
	LIST_WUNLOCK;

	return oldprofile;
}

/**
 * sd_profilelist_release - remove all profiles from profile_list
 */
void sd_profilelist_release(void) 
{
	struct list_head *lh, *tmp;

	LIST_WLOCK;
	list_for_each_safe(lh, tmp, &profile_list) {
		struct sdprofile *profile=list_entry(lh,struct sdprofile,list);
		list_del_init(&profile->list);	
		put_sdprofile(profile);
	}
	LIST_WUNLOCK;
}

/**
 * sd_profile_dump - dump a profile
 * @profile: profile to dump
 */
static __INLINE__ void sd_profile_dump_lvl(struct sdprofile *profile, int sub)
{
	struct sd_entry *sdent;

	SD_DEBUG("dumping profile: %s%s(%d) %s\n", 
		sub ? "^" : "", 
		profile->name,
		atomic_read(&profile->count),
		profile->sub_name ?
		profile->sub_name : "");
	for (sdent = profile->file_entry; sdent; sdent = sdent->next) {
		SD_DEBUG("\t%d\t%s\n", sdent->mode, sdent->filename);
	}
}

void sd_profile_dump(struct sdprofile *profile)
{
	struct list_head *slh;

	sd_profile_dump_lvl(profile, 0);

	list_for_each(slh, &profile->sub) {
		struct sdprofile *sub;
		sub = list_entry(slh, struct sdprofile, list);
		sd_profile_dump_lvl(sub, 1);
	}
}

/**
 * sd_profilelist_dump - dump profile list for debugging
 */
void sd_profilelist_dump(void)
{
	struct list_head *lh;

	SD_DEBUG("%s\n", __FUNCTION__);
	LIST_RLOCK;
	list_for_each(lh, &profile_list) {
		struct sdprofile *profile=list_entry(lh,struct sdprofile,list);
		sd_profile_dump(profile);
	}
	LIST_RUNLOCK;
}

/**
 * sd_subdomainlist_add - add new subdomain to subdomain_list
 * @sd: new subdomain to add to list
 */
void sd_subdomainlist_add(struct subdomain *sd)
{
	if (!sd) {
		SD_INFO("%s: bad subdomain\n", __FUNCTION__);
		return;
	}

	SDLIST_WLOCK;
	list_add(&sd->list, &subdomain_list);
	SDLIST_WUNLOCK;
}

/**
 * sd_subdomainlist_remove - remove subdomain from subdomain_list
 * @sd: subdomain to be removed
 */
int sd_subdomainlist_remove(struct subdomain *sd)
{
	struct list_head *lh, *tmp;
	int error = -ENOENT;

	if (!sd)
		goto out;

	SDLIST_WLOCK;
	list_for_each_safe(lh, tmp, &subdomain_list) {
		struct subdomain *node = list_entry(lh, struct subdomain, list);
		if (node == sd){
			list_del_init(&node->list);
			error = 0;
			break;
		}
	}
	SDLIST_WUNLOCK;

out:
	return error;
}

/**
 * sd_subdomainlist_iterate - Iterate over subdomain_list.
 * stop when sd_iter func returns non zero
 * @func: method to be called for each element
 */
void sd_subdomainlist_iterate(sd_iter func, void *cookie)
{
	struct list_head *lh;
	int ret=0;

	SDLIST_RLOCK;
	list_for_each(lh, &subdomain_list) {
		struct subdomain *node = list_entry(lh, struct subdomain, list);
		ret=(*func)(node, cookie);
		if (ret != 0){
			break;
		}
	}
	SDLIST_RUNLOCK;
}

/**
 * sd_subdomainlist_iterateremove - Iterate over subdomain_list.
 * remove element when sd_iter func returns non zero
 * @func: method to be called for each element
 */
void sd_subdomainlist_iterateremove(sd_iter func, void *cookie)
{
	struct list_head *lh, *tmp;
	int ret=0;

	SDLIST_WLOCK;
	list_for_each_safe(lh, tmp, &subdomain_list) {
		struct subdomain *node = list_entry(lh, struct subdomain, list);
		ret=(*func)(node, cookie);
		if (ret != 0){
			list_del_init(lh);
		}
	}
	SDLIST_WUNLOCK;
}

/**
 * sd_profilelist_release - remove all subdomains from subdomain_list
 */
void sd_subdomainlist_release()
{
	struct list_head *lh, *tmp;

	SDLIST_WLOCK;
	list_for_each_safe(lh, tmp, &subdomain_list) {
		list_del_init(lh);
	}
	SDLIST_WUNLOCK;
}

static void * p_start(struct seq_file *f, loff_t *pos)
{
	struct list_head *lh;
	loff_t l = *pos;

	LIST_RLOCK;
	list_for_each(lh, &profile_list)
		if (!l--)
			return list_entry(lh, struct sdprofile, list);
	return NULL;
}

static void * p_next(struct seq_file *f, void *p, loff_t *pos)
{
	struct list_head *lh = ((struct sdprofile *)p)->list.next;
	(*pos)++;
	return lh==&profile_list ? NULL : list_entry(lh, struct sdprofile, list);
}

static void p_stop(struct seq_file *f, void *v)
{
	LIST_RUNLOCK;
}

static int seq_show_profile(struct seq_file *f, void *v)
{
	struct sdprofile *profile = (struct sdprofile *)v;
	seq_printf(f, "%s (%s)\n", profile->name, PROFILE_COMPLAIN(profile) ? "complain" : "enforce");
	return 0;
}

struct seq_operations subdomainfs_profiles_op = {
	.start =	p_start,
	.next =		p_next,
	.stop =		p_stop,
	.show =		seq_show_profile,
};
