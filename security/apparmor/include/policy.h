/*
 * AppArmor security module
 *
 * This file contains AppArmor policy definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_POLICY_H
#define __AA_POLICY_H

#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/kref.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/socket.h>

#include "apparmor.h"
#include "audit.h"
#include "capability.h"
#include "domain.h"
#include "file.h"
#include "net.h"
#include "resource.h"

extern const char *profile_mode_names[];
#define APPARMOR_NAMES_MAX_INDEX 3

#define PROFILE_COMPLAIN(_profile)				\
	((g_profile_mode == APPARMOR_COMPLAIN) || ((_profile) &&	\
					(_profile)->mode == APPARMOR_COMPLAIN))

#define PROFILE_KILL(_profile)					\
	((g_profile_mode == APPARMOR_KILL) || ((_profile) &&	\
					(_profile)->mode == APPARMOR_KILL))

#define PROFILE_IS_HAT(_profile) \
	((_profile) && (_profile)->flags & PFLAG_HAT)


/*
 * FIXME: currently need a clean way to replace and remove profiles as a
 * set.  It should be done at the namespace level.
 * Either, with a set of profiles loaded at the namespace level or via
 * a mark and remove marked interface.
 */
enum profile_mode {
	APPARMOR_ENFORCE,		/* enforce access rules */
	APPARMOR_COMPLAIN,		/* allow and log access violations */
	APPARMOR_KILL,			/* kill task on access violation */
};

enum profile_flags {
	PFLAG_HAT = 1,			/* profile is a hat */
	PFLAG_UNCONFINED = 2,		/* profile is the unconfined profile */
	PFLAG_NULL = 4,			/* profile is null learning profile */
	PFLAG_IX_ON_NAME_ERROR = 8,	/* fallback to ix on name lookup fail */
	PFLAG_IMMUTABLE = 0x10,		/* don't allow changes/replacement */
	PFLAG_USER_DEFINED = 0x20,	/* user based profile */
	PFLAG_NO_LIST_REF = 0x40,	/* list doesn't keep profile ref */
};

#define AA_NEW_SID 0

struct aa_profile;

/* struct aa_policy_common - common part of both namespaces and profiles
 * @name: name of the object
 * @count: reference count of the obj
 * lock: lock for modifying the object
 * @list: list object is on
 * @profiles: head of the profiles list contained in the object
 */
struct aa_policy_common {
	char *name;
	struct kref count;
	rwlock_t lock;
	struct list_head list;
	struct list_head profiles;
};

/* struct aa_ns_acct - accounting of profiles in namespace
 * @max_size: maximum space allowed for all profiles in namespace
 * @max_count: maximum number of profiles that can be in this namespace
 * @size: current size of profiles
 * @count: current count of profiles (includes null profiles)
 */
struct aa_ns_acct {
	int max_size;
	int max_count;
	int size;
	int count;
};

/* struct aa_namespace - namespace for a set of profiles
 * @name: the name of the namespace
 * @list: list the namespace is on
 * @profiles: list of profile in the namespace
 * @acct: accounting for the namespace
 * @profile_count: count of profiles on @profiles list
 * @size: accounting of how much memory is consumed by the contained profiles
 * @unconfined: special unconfined profile for the namespace
 * @count: reference count on the namespace
 * @lock: lock for adding/removing profile to the namespace
 *
 * An aa_namespace defines the set profiles that are searched to determine
 * which profile to attach to a task.  Profiles can not be shared between
 * aa_namespaces and profile names within a namespace are guarenteed to be
 * unique.  When profiles in seperate namespaces have the same name they
 * are NOT considered to be equivalent.
 *
 * Namespace names must be unique and can not contain the characters :/\0
 *
 * FIXME TODO: add vserver support so a vserer gets a default namespace
 */
struct aa_namespace {
	struct aa_policy_common base;
	struct aa_ns_acct acct;
	int is_stale;
	struct aa_profile *unconfined;
};


/* struct aa_profile - basic confinement data
 * @base - base componets of the profile (name, refcount, lists, lock ...)
 * @fqname - The fully qualified profile name, less the namespace name
 * @ns: namespace the profile is in
 * @parent: parent profile of this profile, if one exists
 * @replacedby: is set profile that replaced this profile
 * @xmatch: optional extended matching for unconfined executables names
 * @xmatch_plen: xmatch prefix len, used to determine xmatch priority
 * @sid: the unique security id number of this profile
 * @audit: the auditing mode of the profile
 * @mode: the enforcement mode of the profile
 * @flags: flags controlling profile behavior
 * @size: the memory consumed by this profiles rules
 * @file: The set of rules governing basic file access and domain transitions
 * @caps: capabilities for the profile
 * @net: network controls for the profile
 * @rlimits: rlimits for the profile
 *
 * The AppArmor profile contains the basic confinement data.  Each profile
 * has a name, and exist in a namespace.  The @name and @exec_match are
 * used to determine profile attachment against unconfined tasks.  All other
 * attachments are determined by in profile X transition rules.
 *
 * The @replacedby field is write protected by the profile lock.  Reads
 * are assumed to be atomic, and are done without locking.
 *
 * Profiles have a hierachy where hats and children profiles keep
 * a reference to their parent.
 *
 * Profile names can not begin with a : and can not contain the \0
 * character.  If a profile name begins with / it will be considered when
 * determining profile attachment on "unconfined" tasks.
 */
struct aa_profile {
	struct aa_policy_common base;
	char *fqname;

	struct aa_namespace *ns;
	struct aa_profile *parent;
	struct aa_profile *replacedby;

	struct aa_dfa *xmatch;
	int xmatch_len;
	u32 sid;
	enum audit_mode audit;
	enum profile_mode mode;
	u32 flags;
	int size;

	struct aa_file_rules file;
	struct aa_caps caps;
	struct aa_net net;
	struct aa_rlimit rlimits;
};


extern struct list_head ns_list;
extern rwlock_t ns_list_lock;

extern struct aa_namespace *default_namespace;
extern enum profile_mode g_profile_mode;


void aa_add_profile(struct aa_policy_common *common,
		    struct aa_profile *profile);

int alloc_default_namespace(void);
void free_default_namespace(void);
struct aa_namespace *alloc_aa_namespace(const char *name);
void free_aa_namespace_kref(struct kref *kref);
void free_aa_namespace(struct aa_namespace *ns);
struct aa_namespace *__aa_find_namespace(struct list_head *head,
					 const char *name);

struct aa_namespace *aa_find_namespace(const char *name);
struct aa_namespace *aa_prepare_namespace(const char *name);
void aa_remove_namespace(struct aa_namespace *ns);
struct aa_namespace *aa_prepare_namespace(const char *name);
void aa_profile_list_release(struct list_head *head);
void aa_profile_ns_list_release(void);
void __aa_remove_namespace(struct aa_namespace *ns);


static inline struct aa_policy_common *aa_get_common(struct aa_policy_common *c)
{
	if (c)
		kref_get(&c->count);

	return c;
}

static inline struct aa_namespace *aa_get_namespace(struct aa_namespace *ns)
{
	if (ns)
		kref_get(&(ns->base.count));

	return ns;
}

static inline void aa_put_namespace(struct aa_namespace *ns)
{
	if (ns)
		kref_put(&ns->base.count, free_aa_namespace_kref);
}



struct aa_profile *alloc_aa_profile(const char *name);
struct aa_profile *aa_alloc_null_profile(struct aa_profile *parent, int hat);
void free_aa_profile_kref(struct kref *kref);
void free_aa_profile(struct aa_profile *profile);
struct aa_profile *__aa_find_profile(struct list_head *head, const char *name);
struct aa_profile *aa_find_child(struct aa_profile *parent, const char *name);
struct aa_policy_common *__aa_find_parent_by_fqname(struct aa_namespace *ns,
						    const char *fqname);
struct aa_profile *__aa_find_profile_by_fqname(struct aa_namespace *ns,
					       const char *fqname);
struct aa_profile *aa_find_profile_by_fqname(struct aa_namespace *ns,
					     const char *name);
struct aa_profile *aa_match_profile(struct aa_namespace *ns, const char *name);
struct aa_profile *aa_profile_newest(struct aa_profile *profile);
struct aa_profile *aa_sys_find_attach(struct aa_policy_common *base,
				      const char *name);
void __aa_add_profile(struct aa_policy_common *common,
		      struct aa_profile *profile);
void __aa_remove_profile(struct aa_profile *profile,
			 struct aa_profile *replacement);
void __aa_replace_profile(struct aa_profile *profile,
			  struct aa_profile *replacement);
void __aa_profile_list_release(struct list_head *head);

static inline struct aa_profile *aa_filtered_profile(struct aa_profile *profile)
{
	if (profile->flags & PFLAG_UNCONFINED)
		return NULL;
	return profile;
}

/**
 * aa_get_profile - increment refcount on profile @p
 * @p: profile
 */
static inline struct aa_profile *aa_get_profile(struct aa_profile *p)
{
	if (p)
		kref_get(&(p->base.count));

	return p;
}

/**
 * aa_put_profile - decrement refcount on profile @p
 * @p: profile
 */
static inline void aa_put_profile(struct aa_profile *p)
{
	if (p)
		kref_put(&p->base.count, free_aa_profile_kref);
}

static inline int PROFILE_AUDIT_MODE(struct aa_profile *profile)
{
	if (g_apparmor_audit != AUDIT_NORMAL)
		return g_apparmor_audit;
	if (profile)
		return profile->audit;
	return AUDIT_NORMAL;
}

#endif	/* __AA_POLICY_H */

