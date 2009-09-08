/*
 * AppArmor security module
 *
 * This file contains AppArmor policy manipulation functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 *
 * AppArmor policy is based around profiles, which contain the rules a
 * task is confined by.  Every task in the sytem has a profile attached
 * to it determined either by matching "unconfined" tasks against the
 * visible set of profiles or by following a profiles attachment rules.
 *
 * Each profile exists in an AppArmor profile namespace which is a
 * container of related profiles.  Each namespace contains a special
 * "unconfined" profile, which doesn't efforce any confinement on
 * a task beyond DAC.
 *
 * Namespace and profile names can be written together in either
 * of two syntaxes.
 *	:namespace:profile - used by kernel interfaces for easy detection
 *	namespace://profile - used by policy
 *
 * Profile names name not start with : or @ and may not contain \0
 * a // in a profile name indicates a compound name with the name before
 * the // being the parent profile and the name after the child
 *
 * Reserved profile names
 *	unconfined - special automatically generated unconfined profile
 *	inherit - special name to indicate profile inheritance
 *	null-XXXX-YYYY - special automically generated learning profiles
 *
 * Namespace names may not start with / or @ and may not contain \0 or //
 * it is recommend that they do not contain any '/' characters
 * Reserved namespace namespace
 *	default - the default namespace setup by AppArmor
 *	user-XXXX - user defined profiles
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include "include/security/apparmor.h"
#include "include/capability.h"
#include "include/file.h"
#include "include/ipc.h"
#include "include/match.h"
#include "include/policy.h"
#include "include/resource.h"
#include "include/sid.h"

/* list of profile namespaces and lock */
LIST_HEAD(ns_list);
DEFINE_RWLOCK(ns_list_lock);

struct aa_namespace *default_namespace;

const char *profile_mode_names[] = {
	"enforce",
	"complain",
	"kill",
};

#define AA_SYS_SID 0
#define AA_USR_SID 1


static int common_init(struct aa_policy_common *common, const char *name)
{
	common->name = kstrdup(name, GFP_KERNEL);
	if (!common->name)
		return 0;
	INIT_LIST_HEAD(&common->list);
	INIT_LIST_HEAD(&common->profiles);
	kref_init(&common->count);
	rwlock_init(&common->lock);

	return 1;
}

static void common_free(struct aa_policy_common *common)
{
	/* still contains profiles -- invalid */
	if (!list_empty(&common->profiles)) {
		AA_ERROR("%s: internal error, "
			 "policy '%s' still contains profiles\n",
			 __func__, common->name);
		BUG();
	}
	if (!list_empty(&common->list)) {
		AA_ERROR("%s: internal error, policy '%s' still on list\n",
			 __func__, common->name);
		BUG();
	}

	kfree(common->name);
}

static struct aa_policy_common *__common_find(struct list_head *head,
					      const char *name)

{
	struct aa_policy_common *common;

	list_for_each_entry(common, head, list) {
		if (!strcmp(common->name, name))
			return common;
	}
	return NULL;
}

static struct aa_policy_common *__common_find_strn(struct list_head *head,
						   const char *str, int len)
{
	struct aa_policy_common *common;

	list_for_each_entry(common, head, list) {
		if (aa_strneq(common->name, str, len))
			return common;
	}

	return NULL;
}

/*
 * Routines for AppArmor namespaces
 */

int alloc_default_namespace(void)
{
	struct aa_namespace *ns;
	ns = alloc_aa_namespace("default");
	if (!ns)
		return -ENOMEM;

	default_namespace = aa_get_namespace(ns);
	write_lock(&ns_list_lock);
	list_add(&ns->base.list, &ns_list);
	write_unlock(&ns_list_lock);

	return 0;
}

void free_default_namespace(void)
{
	write_lock(&ns_list_lock);
	list_del_init(&default_namespace->base.list);
	aa_put_namespace(default_namespace);
	write_unlock(&ns_list_lock);
	aa_put_namespace(default_namespace);
	default_namespace = NULL;
}

/**
 * alloc_aa_namespace - allocate, initialize and return a new namespace
 * @name: a preallocated name
 * Returns NULL on failure.
 */
struct aa_namespace *alloc_aa_namespace(const char *name)
{
	struct aa_namespace *ns;

	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	AA_DEBUG("%s(%p)\n", __func__, ns);
	if (!ns)
		return NULL;

	if (!common_init(&ns->base, name))
		goto fail_ns;

	/* null profile is not added to the profile list */
	ns->unconfined = alloc_aa_profile("unconfined");
	if (!ns->unconfined)
		goto fail_unconfined;

	ns->unconfined->sid = aa_alloc_sid(AA_ALLOC_SYS_SID);
	ns->unconfined->flags = PFLAG_UNCONFINED | PFLAG_IX_ON_NAME_ERROR |
		PFLAG_IMMUTABLE;
	ns->unconfined->ns = aa_get_namespace(ns);

	return ns;

fail_unconfined:
	if (ns->base.name)
		kfree(ns->base.name);
fail_ns:
	kfree(ns);
	return NULL;
}

/**
 * free_aa_namespace_kref - free aa_namespace by kref (see aa_put_namespace)
 * @kr: kref callback for freeing of a namespace
 */
void free_aa_namespace_kref(struct kref *kref)
{
	free_aa_namespace(container_of(kref, struct aa_namespace, base.count));
}

/**
 * free_aa_namespace - free a profile namespace
 * @namespace: the namespace to free
 *
 * Free a namespace.  All references to the namespace must have been put.
 * If the namespace was referenced by a profile confining a task,
 */
void free_aa_namespace(struct aa_namespace *ns)
{
	if (!ns)
		return;

	common_free(&ns->base);

	if (ns->unconfined && ns->unconfined->ns == ns)
		ns->unconfined->ns = NULL;

	aa_put_profile(ns->unconfined);
	memset(ns, 0, sizeof(*ns));
	kfree(ns);
}

struct aa_namespace *__aa_find_namespace(struct list_head *head,
					 const char *name)

{
	return (struct aa_namespace *) __common_find(head, name);
}

/**
 * aa_find_namespace  -  look up a profile namespace on the namespace list
 * @name: name of namespace to find
 *
 * Returns a pointer to the namespace on the list, or NULL if no namespace
 * called @name exists.
 */
struct aa_namespace *aa_find_namespace(const char *name)
{
	struct aa_namespace *ns = NULL;

	read_lock(&ns_list_lock);
	ns = aa_get_namespace(__aa_find_namespace(&ns_list, name));
	read_unlock(&ns_list_lock);

	return ns;
}

static struct aa_namespace *__aa_find_namespace_by_strn(struct list_head *head,
							const char *name,
							int len)
{
	return (struct aa_namespace *) __common_find_strn(head, name, len);
}

struct aa_namespace *aa_find_namespace_by_strn(const char *name, int len)
{
	struct aa_namespace *ns = NULL;

	read_lock(&ns_list_lock);
	ns = aa_get_namespace(__aa_find_namespace_by_strn(&ns_list, name, len));
	read_unlock(&ns_list_lock);

	return ns;
}

/**
 * aa_prepare_namespace - find an existing or create a new namespace of @name
 * @name: the namespace to find or add
 */
struct aa_namespace *aa_prepare_namespace(const char *name)
{
	struct aa_namespace *ns;

	write_lock(&ns_list_lock);
	if (name)
		ns = aa_get_namespace(__aa_find_namespace(&ns_list, name));
	else
		ns = aa_get_namespace(default_namespace);
	if (!ns) {
		struct aa_namespace *new_ns;
		write_unlock(&ns_list_lock);
		new_ns = alloc_aa_namespace(name);
		if (!new_ns)
			return NULL;
		write_lock(&ns_list_lock);
		ns = __aa_find_namespace(&ns_list, name);
		if (!ns) {
			list_add(&new_ns->base.list, &ns_list);
			ns = new_ns;
		} else {
			/* raced so free the new one */
			free_aa_namespace(new_ns);
			aa_get_namespace(ns);
		}
	}
	write_unlock(&ns_list_lock);

	return ns;
}

/*
 * requires profile->ns set first, takes profiles refcount
 * TODO: add accounting
 */
void __aa_add_profile(struct aa_policy_common *common,
		      struct aa_profile *profile)
{
	list_add(&profile->base.list, &common->profiles);
	if (!(profile->flags & PFLAG_NO_LIST_REF))
		aa_get_profile(profile);
}

void __aa_remove_profile(struct aa_profile *profile,
			 struct aa_profile *replacement)
{
	if (replacement)
		profile->replacedby = aa_get_profile(replacement);
	else
		profile->replacedby = ERR_PTR(-EINVAL);
	list_del_init(&profile->base.list);
	if (!(profile->flags & PFLAG_NO_LIST_REF))
		aa_put_profile(profile);
}

/* TODO: add accounting */
void __aa_replace_profile(struct aa_profile *profile,
			  struct aa_profile *replacement)
{
	if (replacement) {
		struct aa_policy_common *common;

		if (profile->parent)
			common = &profile->parent->base;
		else
			common = &profile->ns->base;

		__aa_remove_profile(profile, replacement);
		__aa_add_profile(common, replacement);
	} else
		__aa_remove_profile(profile, NULL);
}

/**
 * __aa_profile_list_release - remove all profiles on the list and put refs
 * @head: list of profiles
 */
void __aa_profile_list_release(struct list_head *head)
{
	struct aa_profile *profile, *tmp;
	list_for_each_entry_safe(profile, tmp, head, base.list) {
		__aa_profile_list_release(&profile->base.profiles);
		__aa_remove_profile(profile, NULL);
	}
}

void __aa_remove_namespace(struct aa_namespace *ns)
{
	struct aa_profile *unconfined = ns->unconfined;
	list_del_init(&ns->base.list);

	/*
	 * break the ns, unconfined profile cyclic reference and forward
	 * all new unconfined profiles requests to the default namespace
	 */
	ns->unconfined = aa_get_profile(default_namespace->unconfined);
	__aa_profile_list_release(&ns->base.profiles);
	aa_put_profile(unconfined);
	aa_put_namespace(ns);
}

/**
 * aa_remove_namespace = Remove namespace from the list
 * @ns: namespace to remove
 */
void aa_remove_namespace(struct aa_namespace *ns)
{
	write_lock(&ns_list_lock);
	write_lock(&ns->base.lock);
	__aa_remove_namespace(ns);
	write_unlock(&ns->base.lock);
	write_unlock(&ns_list_lock);
}

/**
 * aa_profilelist_release - remove all namespaces and all associated profiles
 */
void aa_profile_ns_list_release(void)
{
	struct aa_namespace *ns, *tmp;

	/* Remove and release all the profiles on namespace profile lists. */
	write_lock(&ns_list_lock);
	list_for_each_entry_safe(ns, tmp, &ns_list, base.list) {
		write_lock(&ns->base.lock);
		__aa_remove_namespace(ns);
		write_unlock(&ns->base.lock);
	}
	write_unlock(&ns_list_lock);
}

/**
 * alloc_aa_profile - allocate, initialize and return a new profile
 * @fqname: name of the profile
 *
 * Returns NULL on failure.
 */
struct aa_profile *alloc_aa_profile(const char *fqname)
{
	struct aa_profile *profile;

	profile = kzalloc(sizeof(*profile), GFP_KERNEL);
	if (!profile)
		return NULL;

	if (!common_init(&profile->base, fqname)) {
		kfree(profile);
		return NULL;
	}

	profile->fqname = profile->base.name;
	profile->base.name = (char *) fqname_subname((const char *) profile->fqname);
	return profile;
}

/**
 * aa_new_null_profile - create a new null-X learning profile
 * @parent: profile that caused this profile to be created
 * @hat: true if the null- learning profile is a hat
 *
 * Create a null- complain mode profile used in learning mode.  The name of
 * the profile is unique and follows the format of parent//null-sid.
 *
 * null profiles are added to the profile list but the list does not
 * hold a count on them so that they are automatically released when
 * not in use.
 */
struct aa_profile *aa_alloc_null_profile(struct aa_profile *parent, int hat)
{
	struct aa_profile *profile = NULL;
	char *name;
	u32 sid = aa_alloc_sid(AA_ALLOC_SYS_SID);

	name = kmalloc(strlen(parent->fqname) + 2 + 7 + 8, GFP_KERNEL);
	if (!name)
		goto fail;
	sprintf(name, "%s//null-%x", parent->fqname, sid);

	profile = alloc_aa_profile(name);
	kfree(name);
	if (!profile)
		goto fail;

	profile->sid = aa_alloc_sid(AA_ALLOC_SYS_SID);
	profile->mode = APPARMOR_COMPLAIN;
	profile->flags = PFLAG_NULL | PFLAG_NO_LIST_REF;
	if (hat)
	  profile->flags |= PFLAG_HAT;

	profile->parent = aa_get_profile(parent);
	profile->ns = aa_get_namespace(parent->ns);

	write_lock(&profile->ns->base.lock);
	__aa_add_profile(&parent->base, profile);
	write_unlock(&profile->ns->base.lock);

	return profile;

fail:
	aa_free_sid(sid);
	return NULL;
}

/**
 * free_aa_profile_kref - free aa_profile by kref (called by aa_put_profile)
 * @kr: kref callback for freeing of a profile
 */
void free_aa_profile_kref(struct kref *kref)
{
	struct aa_profile *p = container_of(kref, struct aa_profile,
					    base.count);

	free_aa_profile(p);
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
	AA_DEBUG("%s(%p)\n", __func__, profile);

	if (!profile)
		return;

	/*
	 * profile can still be on the list if the list doesn't hold a
	 * reference.  There is no race as NULL profiles can't be attached
	 */
	if (!list_empty(&profile->base.list)) {
		if ((profile->flags & PFLAG_NULL) && profile->ns) {
			write_lock(&profile->ns->base.lock);
			list_del_init(&profile->base.list);
			write_unlock(&profile->ns->base.lock);
		} else {
			AA_ERROR("%s: internal error, "
				 "profile '%s' still on ns list\n",
				 __func__, profile->base.name);
			BUG();
		}
	}

	/* profile->name is a substring of fqname */
	profile->base.name = NULL;
	common_free(&profile->base);

	BUG_ON(!list_empty(&profile->base.profiles));

	kfree(profile->fqname);

	aa_put_namespace(profile->ns);
	aa_put_profile(profile->parent);

	aa_free_file_rules(&profile->file);
	aa_free_cap_rules(&profile->caps);
	aa_free_net_rules(&profile->net);
	aa_free_rlimit_rules(&profile->rlimits);

	aa_free_sid(profile->sid);
	aa_match_free(profile->xmatch);

	if (profile->replacedby && !PTR_ERR(profile->replacedby))
		aa_put_profile(profile->replacedby);

	memset(profile, 0, sizeof(profile));
	kfree(profile);
}


/* TODO: profile count accounting - setup in remove */


struct aa_profile *__aa_find_profile(struct list_head *head, const char *name)
{
	return (struct aa_profile *) __common_find(head, name);
}

struct aa_profile *__aa_find_profile_by_strn(struct list_head *head,
					     const char *name, int len)
{
	return (struct aa_profile *) __common_find_strn(head, name, len);
}


/**
 * aa_find_child - find a profile by @name in @parent
 * @parent: profile to search
 * @name: profile name to search for
 *
 * Returns a ref counted profile or NULL if not found
 */
struct aa_profile *aa_find_child(struct aa_profile *parent, const char *name)
{
	struct aa_profile *profile;

	read_lock(&parent->ns->base.lock);
	profile = aa_get_profile(__aa_find_profile(&parent->base.profiles,
						   name));
	read_unlock(&parent->ns->base.lock);

	return profile;
}


struct aa_policy_common *__aa_find_parent_by_fqname(struct aa_namespace *ns,
						    const char *fqname)
{
	struct aa_policy_common *common;
	struct aa_profile *profile = NULL;
	char *split;

	common = &ns->base;


	for (split = strstr(fqname, "//"); split; ) {
		profile = __aa_find_profile_by_strn(&common->profiles, fqname,
						    split - fqname);
		if (!profile)
			return NULL;
		common = &profile->base;
		fqname = split + 2;
		split = strstr(fqname, "//");
	}
	if (!profile)
		return &ns->base;
	return &profile->base;
}

struct aa_profile *__aa_find_profile_by_fqname(struct aa_namespace *ns,
					       const char *fqname)
{
	struct aa_policy_common *common;
	struct aa_profile *profile = NULL;
	char *split;

	common = &ns->base;
	for (split = strstr(fqname, "//"); split; ) {
		profile = __aa_find_profile_by_strn(&common->profiles, fqname,
						    split - fqname);
		if (!profile)
			return NULL;

		common = &profile->base;
		fqname = split + 2;
		split = strstr(fqname, "//");
	}

	profile = __aa_find_profile(&common->profiles, fqname);

	return profile;
}

/**
 * aa_find_profile_by_name - find a profile by its full or partial name
 * @ns: the namespace to start from
 * @fqname: name to do lookup on.  Does not contain namespace prefix
 */
struct aa_profile *aa_find_profile_by_fqname(struct aa_namespace *ns,
					     const char *fqname)
{
	struct aa_profile *profile;

	read_lock(&ns->base.lock);
	profile = aa_get_profile(__aa_find_profile_by_fqname(ns, fqname));
	read_unlock(&ns->base.lock);
	return profile;
}


/* __aa_attach_match_ - find an attachment match
 * @name - to match against
 * @head - profile list to walk
 *
 * Do a linear search on the profiles in the list.  There is a matching
 * preference where an exact match is prefered over a name which uses
 * expressions to match, and matching expressions with the greatest
 * xmatch_len are prefered.
 */
static struct aa_profile *__aa_attach_match(const char *name,
					    struct list_head *head)
{
	int len = 0;
	struct aa_profile *profile, *candidate = NULL;

	list_for_each_entry(profile, head, base.list) {
		if (profile->flags & PFLAG_NULL)
			continue;
		if (profile->xmatch && profile->xmatch_len > len) {
			unsigned int state = aa_dfa_match(profile->xmatch,
							  DFA_START, name);
			u16 perm = dfa_user_allow(profile->xmatch, state);
			/* any accepting state means a valid match. */
			if (perm & MAY_EXEC) {
				candidate = profile;
				len = profile->xmatch_len;
			}
		} else if (!strcmp(profile->base.name, name))
			/* exact non-re match, no more searching required */
			return profile;
	}

	return candidate;
}

/**
 * aa_sys_find_attach - do attachment search for sys unconfined processes
 * @base: the base to search
 * name: the executable name to match against
 */
struct aa_profile *aa_sys_find_attach(struct aa_policy_common *base,
				      const char *name)
{
	struct aa_profile *profile;

	read_lock(&base->lock);
	profile = aa_get_profile(__aa_attach_match(name, &base->profiles));
	read_unlock(&base->lock);

	return profile;
}

/**
 * aa_profile_newest - find the newest version of @profile
 * @profile: the profile to check for newer versions of
 *
 * Find the newest version of @profile, if @profile is the newest version
 * return @profile.  If @profile has been removed return NULL.
 *
 * NOTE: the profile returned is not refcounted, The refcount on @profile
 * must be held until the caller decides what to do with the returned newest
 * version.
 */
struct aa_profile *aa_profile_newest(struct aa_profile *profile)
{
	if (unlikely(profile && profile->replacedby)) {
		for (;profile->replacedby; profile = profile->replacedby) {
			if (IS_ERR(profile->replacedby)) {
				/* profile has been removed */
				profile = NULL;
				break;
			}
		}
	}

	return profile;
}

