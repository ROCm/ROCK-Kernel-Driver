/*
 * AppArmor security module
 *
 * This file contains AppArmor functions used to manipulate object security
 * contexts.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include "include/context.h"
#include "include/policy.h"



struct aa_task_context *aa_alloc_task_context(gfp_t flags)
{
	return kzalloc(sizeof(struct aa_task_context), flags);
}

void aa_free_task_context(struct aa_task_context *cxt)
{
	if (cxt) {
		aa_put_profile(cxt->sys.profile);
		aa_put_profile(cxt->sys.previous);
		aa_put_profile(cxt->sys.onexec);

		memset(cxt, 0, sizeof(*cxt));
		kfree(cxt);
	}
}

/*
 * duplicate a task context, incrementing reference counts
 */
struct aa_task_context *aa_dup_task_context(struct aa_task_context *old_cxt,
					    gfp_t gfp)
{
	struct aa_task_context *cxt;

	cxt = kmemdup(old_cxt, sizeof(*cxt), gfp);
	if (!cxt)
		return NULL;

	aa_get_profile(cxt->sys.profile);
	aa_get_profile(cxt->sys.previous);
	aa_get_profile(cxt->sys.onexec);

	return cxt;
}

/**
 * aa_cred_policy - obtain cred's profiles
 * @cred: cred to obtain profiles from
 * @sys: return system profile
 * does NOT increment reference count
 */
void aa_cred_policy(const struct cred *cred, struct aa_profile **sys)
{
	struct aa_task_context *cxt = cred->security;
	BUG_ON(!cxt);
	*sys = aa_filtered_profile(aa_profile_newest(cxt->sys.profile));
}

/**
 * aa_get_task_policy - get the cred with the task policy, and current profiles
 * @task: task to get policy of
 * @sys: return - pointer to system profile
 *
 * Only gets the cred ref count which has ref counts on the profiles returned
 */
struct cred *aa_get_task_policy(const struct task_struct *task,
				struct aa_profile **sys)
{
	struct cred *cred = get_task_cred(task);
	aa_cred_policy(cred, sys);
	return cred;
}

void aa_put_task_policy(struct cred *cred)
{
	put_cred(cred);
}

static void replace_group(struct aa_task_cxt_group *cgrp,
			  struct aa_profile *profile)
{
	if (cgrp->profile == profile)
		return;

	if (!profile || (profile->flags & PFLAG_UNCONFINED) ||
	    (cgrp->profile && cgrp->profile->ns != profile->ns)) {
		aa_put_profile(cgrp->previous);
		aa_put_profile(cgrp->onexec);
		cgrp->previous = NULL;
		cgrp->onexec = NULL;
		cgrp->token = 0;
	}
	aa_put_profile(cgrp->profile);
	cgrp->profile = aa_get_profile(profile);
}

/**
 * aa_replace_current_profiles - replace the current tasks profiles
 * @sys: new system profile
 *
 * Returns: error on failure
 */
int aa_replace_current_profiles(struct aa_profile *sys)
{
	struct aa_task_context *cxt;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;

	cxt = new->security;
	replace_group(&cxt->sys, sys);

	commit_creds(new);
	return 0;
}

int aa_set_current_onexec(struct aa_profile *sys)
{
	struct aa_task_context *cxt;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;

	cxt = new->security;
	aa_put_profile(cxt->sys.onexec);
	cxt->sys.onexec = aa_get_profile(sys);

	commit_creds(new);
	return 0;
}

/*
 * Do the actual cred switching of a changehat
 * profile must be valid
 */
int aa_set_current_hat(struct aa_profile *profile, u64 token)
{
	struct aa_task_context *cxt;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;

	cxt = new->security;
	if (!cxt->sys.previous) {
		cxt->sys.previous = cxt->sys.profile;
		cxt->sys.token = token;
	} else if (cxt->sys.token == token) {
		aa_put_profile(cxt->sys.profile);
	} else {
		/* previous_profile && cxt->token != token */
		abort_creds(new);
		return -EACCES;
	}
	cxt->sys.profile = aa_get_profile(profile);
	/* clear exec on switching context */
	aa_put_profile(cxt->sys.onexec);
	cxt->sys.onexec = NULL;

	commit_creds(new);
	return 0;
}

/*
 * Attempt to return out of a hat to the previous profile
 */
int aa_restore_previous_profile(u64 token)
{
	struct aa_task_context *cxt;
	struct cred *new = prepare_creds();
	if (!new)
		return -ENOMEM;

	cxt = new->security;
	if (cxt->sys.token != token) {
		abort_creds(new);
		return -EACCES;
	}
	/* ignore restores when there is no saved profile */
	if (!cxt->sys.previous) {
		abort_creds(new);
		return 0;
	}

	aa_put_profile(cxt->sys.profile);
	cxt->sys.profile = aa_profile_newest(cxt->sys.previous);
	if (unlikely(cxt->sys.profile != cxt->sys.previous)) {
		aa_get_profile(cxt->sys.profile);
		aa_put_profile(cxt->sys.previous);
	}
	/* clear exec && prev information when restoring to previous context */
	cxt->sys.previous = NULL;
	cxt->sys.token = 0;
	aa_put_profile(cxt->sys.onexec);
	cxt->sys.onexec = NULL;

	commit_creds(new);
	return 0;
}
