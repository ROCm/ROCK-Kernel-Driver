/*
 * AppArmor security module
 *
 * This file contains AppArmor contexts used to associate "labels" to objects.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_CONTEXT_H
#define __AA_CONTEXT_H

#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include "policy.h"


/* struct aa_file_cxt - the AppArmor context the file was opened in
 * @profile: the profile the file was opened under
 * @perms: the permission the file was opened with
 */
struct aa_file_cxt {
	struct aa_profile *profile;
	u16 allowed;
};

static inline struct aa_file_cxt *aa_alloc_file_context(gfp_t gfp)
{
	return kzalloc(sizeof(struct aa_file_cxt), gfp);
}

static inline void aa_free_file_context(struct aa_file_cxt *cxt)
{
	aa_put_profile(cxt->profile);
	memset(cxt, 0, sizeof(struct aa_file_cxt));
	kfree(cxt);
}





/* struct aa_task_cxt_group - a grouping label data for confined tasks
 * @profile: the current profile
 * @exec: profile to transition to on next exec
 * @previous: profile the task may return to
 * @token: magic value the task must know for returning to @previous_profile
 *
 * Contains the task's current profile (which could change due to
 * change_hat).  Plus the hat_magic needed during change_hat.
 */
struct aa_task_cxt_group {
	struct aa_profile *profile;
	struct aa_profile *onexec;
	struct aa_profile *previous;
	u64 token;
};

/**
 * struct aa_task_context - primary label for confined tasks
 * @sys: the system labeling for the task
 *
 * A task is confined by the intersection of its system and user profiles
 */
struct aa_task_context {
	struct aa_task_cxt_group sys;
};

struct aa_task_context *aa_alloc_task_context(gfp_t flags);
void aa_free_task_context(struct aa_task_context *cxt);
struct aa_task_context *aa_dup_task_context(struct aa_task_context *old_cxt,
					    gfp_t gfp);
void aa_cred_policy(const struct cred *cred, struct aa_profile **sys);
struct cred *aa_get_task_policy(const struct task_struct *task,
				struct aa_profile **sys);
int aa_replace_current_profiles(struct aa_profile *sys);
void aa_put_task_policy(struct cred *cred);
int aa_set_current_onexec(struct aa_profile *sys);
int aa_set_current_hat(struct aa_profile *profile, u64 token);
int aa_restore_previous_profile(u64 cookie);


static inline struct aa_task_context *__aa_task_cxt(struct task_struct *task)
{
	return __task_cred(task)->security;
}

/**
 * __aa_task_is_confined - determine if @task has any confinement
 * @task: task to check confinement of
 *
 * If @task != current needs to be in RCU safe critical section
 */
static inline int __aa_task_is_confined(struct task_struct *task)
{
	struct aa_task_context *cxt;
	int rc = 1;

	cxt = __aa_task_cxt(task);
	if (!cxt || (cxt->sys.profile->flags & PFLAG_UNCONFINED))
		rc = 0;

	return rc;
}

static inline const struct cred *aa_current_policy(struct aa_profile **sys)
{
	const struct cred *cred = current_cred();
	struct aa_task_context *cxt = cred->security;
	BUG_ON(!cxt);
	*sys = aa_filtered_profile(aa_profile_newest(cxt->sys.profile));

	return cred;
}

static inline const struct cred *aa_current_policy_wupd(struct aa_profile **sys)
{
	const struct cred *cred = current_cred();
	struct aa_task_context *cxt = cred->security;
	BUG_ON(!cxt);

	*sys = aa_profile_newest(cxt->sys.profile);
	if (unlikely((cxt->sys.profile != *sys)))
		aa_replace_current_profiles(*sys);
	*sys = aa_filtered_profile(*sys);

	return cred;
}

static inline struct aa_profile *aa_current_profile(void)
{
	const struct cred *cred = current_cred();
	struct aa_task_context *cxt = cred->security;
	BUG_ON(!cxt);
	return aa_filtered_profile(aa_profile_newest(cxt->sys.profile));
}

static inline struct aa_profile *aa_current_profile_wupd(void)
{
	struct aa_profile *p;
	aa_current_policy_wupd(&p);
	return p;
}


#endif	/* __AA_CONTEXT_H */
