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

#include <linux/sched.h>

/**
 * aa_dup_profile - increment refcount on profile @p
 * @p: profile
 */
static inline struct aa_profile *aa_dup_profile(struct aa_profile *p)
{
	if (p)
		kref_get(&(p->parent->count));

	return p;
}

/**
 * aa_put_profile - decrement refcount on profile @p
 * @p: profile
 */
static inline void aa_put_profile(struct aa_profile *p)
{
	if (p)
		kref_put(&p->parent->count, free_aa_profile_kref);
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

static inline struct aa_profile *aa_find_profile(const char *name)
{
	struct aa_profile *profile = NULL;

	read_lock(&profile_list_lock);
	profile = aa_dup_profile(__aa_find_profile(name, &profile_list));
	read_unlock(&profile_list_lock);

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
		kfree(cxt);
	}
}

/**
 * alloc_aa_profile - Allocate, initialize and return a new zeroed profile.
 * Returns NULL on failure.
 */
static inline struct aa_profile *alloc_aa_profile(void)
{
	struct aa_profile *profile;

	profile = kzalloc(sizeof(*profile), GFP_KERNEL);
	AA_DEBUG("%s(%p)\n", __FUNCTION__, profile);
	if (profile) {
		profile->parent = profile;
		INIT_LIST_HEAD(&profile->list);
		INIT_LIST_HEAD(&profile->sub);
		kref_init(&profile->count);
		INIT_LIST_HEAD(&profile->task_contexts);
		spin_lock_init(&profile->lock);
	}
	return profile;
}

/**
 * lock_profile - lock a profile
 * @profile: the profile to lock
 *
 * While the profile is locked, local interrupts are disabled. This also
 * gives us RCU reader safety.
 */
static inline void lock_profile(struct aa_profile *profile)
{
	/* We always lock top-level profiles instead of children. */
	if (profile)
		profile = profile->parent;

	/*
	 * Lock the profile.
	 *
	 * Need to disable interrupts here because this lock is used in
	 * the task_free_security hook, which may run in RCU context.
	 */
	if (profile)
		spin_lock_irqsave(&profile->lock, profile->int_flags);
}

/**
 * unlock_profile - unlock a profile
 * @profile: the profile to unlock
 */
static inline void unlock_profile(struct aa_profile *profile)
{
	/* We always lock top-level profiles instead of children. */
	if (profile)
		profile = profile->parent;

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
	/* We always lock top-level profiles instead of children. */
	if (profile1)
		profile1 = profile1->parent;
	if (profile2)
		profile2 = profile2->parent;

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
			spin_lock_irqsave(&profile2->lock, profile2->int_flags);
	} else if (profile1 > profile2) {
		/* profile1 cannot be NULL here. */
		spin_lock_irqsave(&profile1->lock, profile1->int_flags);
		if (profile2)
			spin_lock(&profile2->lock);

	} else {
		/* profile2 cannot be NULL here. */
		spin_lock_irqsave(&profile2->lock, profile2->int_flags);
		spin_lock(&profile1->lock);
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
	/* We always lock top-level profiles instead of children. */
	if (profile1)
		profile1 = profile1->parent;
	if (profile2)
		profile2 = profile2->parent;

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

#endif /* __INLINE_H__ */
