/*
 *	Copyright (C) 1998-2007 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 */

#ifndef __INLINE_H
#define __INLINE_H

#include <linux/sched.h>

#include "match.h"

static inline int mediated_filesystem(struct inode *inode)
{
	return !(inode->i_sb->s_flags & MS_NOUSER);
}

static inline struct aa_task_context *aa_task_context(struct task_struct *task)
{
	return (struct aa_task_context *) rcu_dereference(task->security);
}

static inline struct aa_namespace *aa_get_namespace(struct aa_namespace *ns)
{
	if (ns)
		kref_get(&(ns->count));

	return ns;
}

static inline void aa_put_namespace(struct aa_namespace *ns)
{
	if (ns)
		kref_put(&ns->count, free_aa_namespace_kref);
}


static inline struct aa_namespace *aa_find_namespace(const char *name)
{
	struct aa_namespace *ns = NULL;

	read_lock(&profile_ns_list_lock);
	ns = aa_get_namespace(__aa_find_namespace(name, &profile_ns_list));
	read_unlock(&profile_ns_list_lock);

	return ns;
}

/**
 * aa_dup_profile - increment refcount on profile @p
 * @p: profile
 */
static inline struct aa_profile *aa_dup_profile(struct aa_profile *p)
{
	if (p)
		kref_get(&(p->count));

	return p;
}

/**
 * aa_put_profile - decrement refcount on profile @p
 * @p: profile
 */
static inline void aa_put_profile(struct aa_profile *p)
{
	if (p)
		kref_put(&p->count, free_aa_profile_kref);
}

static inline struct aa_profile *aa_get_profile(struct task_struct *task)
{
	struct aa_task_context *cxt;
	struct aa_profile *profile = NULL;

	rcu_read_lock();
	cxt = aa_task_context(task);
	if (cxt) {
		profile = cxt->profile;
		aa_dup_profile(profile);
	}
	rcu_read_unlock();

	return profile;
}

static inline struct aa_profile *aa_find_profile(struct aa_namespace *ns,
						 const char *name)
{
	struct aa_profile *profile = NULL;

	read_lock(&ns->lock);
	profile = aa_dup_profile(__aa_find_profile(name, &ns->profiles));
	read_unlock(&ns->lock);

	return profile;
}

static inline struct aa_task_context *aa_alloc_task_context(gfp_t flags)
{
	struct aa_task_context *cxt;

	cxt = kzalloc(sizeof(*cxt), flags);
	if (cxt) {
		INIT_LIST_HEAD(&cxt->list);
		INIT_RCU_HEAD(&cxt->rcu);
	}

	return cxt;
}

static inline void aa_free_task_context(struct aa_task_context *cxt)
{
	if (cxt) {
		aa_put_profile(cxt->profile);
		aa_put_profile(cxt->previous_profile);
		kfree(cxt);
	}
}

/**
 * lock_profile - lock a profile
 * @profile: the profile to lock
 *
 * While the profile is locked, local interrupts are disabled. This also
 * gives us RCU reader safety.
 */
static inline void lock_profile_nested(struct aa_profile *profile,
				       enum aa_lock_class lock_class)
{
	/*
	 * Lock the profile.
	 *
	 * Need to disable interrupts here because this lock is used in
	 * the task_free_security hook, which may run in RCU context.
	 */
	if (profile)
		spin_lock_irqsave_nested(&profile->lock, profile->int_flags,
					 lock_class);
}

static inline void lock_profile(struct aa_profile *profile)
{
	lock_profile_nested(profile, aa_lock_normal);
}

/**
 * unlock_profile - unlock a profile
 * @profile: the profile to unlock
 */
static inline void unlock_profile(struct aa_profile *profile)
{
	/* Unlock the profile. */
	if (profile)
		spin_unlock_irqrestore(&profile->lock, profile->int_flags);
}

/**
 * lock_both_profiles  -  lock two profiles in a deadlock-free way
 * @profile1:	profile to lock (may be NULL)
 * @profile2:	profile to lock (may be NULL)
 *
 * The order in which profiles are passed into lock_both_profiles() /
 * unlock_both_profiles() does not matter.
 * While the profile is locked, local interrupts are disabled. This also
 * gives us RCU reader safety.
 */
static inline void lock_both_profiles(struct aa_profile *profile1,
				      struct aa_profile *profile2)
{
	/*
	 * Lock the two profiles.
	 *
	 * We need to disable interrupts because the profile locks are
	 * used in the task_free_security hook, which may run in RCU
	 * context.
	 *
	 * Do not nest spin_lock_irqsave()/spin_unlock_irqresore():
	 * interrupts only need to be turned off once.
	 */
	if (!profile1 || profile1 == profile2) {
		if (profile2)
			spin_lock_irqsave_nested(&profile2->lock,
						 profile2->int_flags,
						 aa_lock_normal);
	} else if (profile1 > profile2) {
		/* profile1 cannot be NULL here. */
		spin_lock_irqsave_nested(&profile1->lock, profile1->int_flags,
					 aa_lock_normal);
		if (profile2)
			spin_lock_nested(&profile2->lock, aa_lock_nested);

	} else {
		/* profile2 cannot be NULL here. */
		spin_lock_irqsave_nested(&profile2->lock, profile2->int_flags,
					 aa_lock_normal);
		spin_lock_nested(&profile1->lock, aa_lock_nested);
	}
}

/**
 * unlock_both_profiles  -  unlock two profiles in a deadlock-free way
 * @profile1:	profile to unlock (may be NULL)
 * @profile2:	profile to unlock (may be NULL)
 *
 * The order in which profiles are passed into lock_both_profiles() /
 * unlock_both_profiles() does not matter.
 * While the profile is locked, local interrupts are disabled. This also
 * gives us RCU reader safety.
 */
static inline void unlock_both_profiles(struct aa_profile *profile1,
				        struct aa_profile *profile2)
{
	/* Unlock the two profiles. */
	if (!profile1 || profile1 == profile2) {
		if (profile2)
			spin_unlock_irqrestore(&profile2->lock,
					       profile2->int_flags);
	} else if (profile1 > profile2) {
		/* profile1 cannot be NULL here. */
		if (profile2)
			spin_unlock(&profile2->lock);
		spin_unlock_irqrestore(&profile1->lock, profile1->int_flags);
	} else {
		/* profile2 cannot be NULL here. */
		spin_unlock(&profile1->lock);
		spin_unlock_irqrestore(&profile2->lock, profile2->int_flags);
	}
}

static inline unsigned int aa_match(struct aa_dfa *dfa, const char *pathname,
				    int *audit_mask)
{
	if (dfa)
		return aa_dfa_match(dfa, pathname, audit_mask);
	if (audit_mask)
		*audit_mask = 0;
	return 0;
}

static inline int dfa_audit_mask(struct aa_dfa *dfa, unsigned int state)
{
	return 	ACCEPT_TABLE2(dfa)[state];
}

#endif /* __INLINE_H__ */
