/*
 * AppArmor security module
 *
 * This file contains AppArmor ipc mediation
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/gfp.h>
#include <linux/ptrace.h>

#include "include/audit.h"
#include "include/capability.h"
#include "include/context.h"
#include "include/policy.h"


struct aa_audit_ptrace {
	struct aa_audit base;

	pid_t tracer, tracee;
};

/* call back to audit ptrace fields */
static void audit_cb(struct audit_buffer *ab, void *va)
{
	struct aa_audit_ptrace *sa = va;
	audit_log_format(ab, " tracer=%d tracee=%d", sa->tracer, sa->tracee);
}

static int aa_audit_ptrace(struct aa_profile *profile,
			   struct aa_audit_ptrace *sa)
{
	return aa_audit(AUDIT_APPARMOR_AUTO, profile, (struct aa_audit *)sa,
			audit_cb);
}

int aa_may_ptrace(struct task_struct *tracer_task, struct aa_profile *tracer,
		  struct aa_profile *tracee, unsigned int mode)
{
	/* TODO: currently only based on capability, not extended ptrace
	 *       rules,
	 *       Test mode for PTRACE_MODE_READ || PTRACE_MODE_ATTACH
	 */

	if (!tracer || tracer == tracee)
		return 0;
	/* log this capability request */
	return aa_capable(tracer_task, tracer, CAP_SYS_PTRACE, 1);
}

int aa_ptrace(struct task_struct *tracer, struct task_struct *tracee,
	      unsigned int mode)
{
	/*
	 * tracer can ptrace tracee when
	 * - tracer is unconfined ||
	 * - tracer & tracee are in the same namespace &&
	 *   - tracer is in complain mode
	 *   - tracer has rules allowing it to trace tracee currently this is:
	 *       - confined by the same profile ||
	 *       - tracer profile has CAP_SYS_PTRACE
	 */

	struct aa_profile *tracer_p;
	const struct cred *cred = aa_get_task_policy(tracer, &tracer_p);
	int error = 0;

	if (tracer_p) {
		struct aa_audit_ptrace sa;
		memset(&sa, 0, sizeof(sa));
		sa.base.operation = "ptrace";
		sa.base.gfp_mask = GFP_ATOMIC;
		sa.tracer = tracer->pid;
		sa.tracee = tracee->pid;
		/* FIXME: different namespace restriction can be lifted
		 * if, namespace are matched to AppArmor namespaces
		 */
		if (tracer->nsproxy != tracee->nsproxy) {
			sa.base.info = "different namespaces";
			sa.base.error = -EPERM;
			aa_audit(AUDIT_APPARMOR_DENIED, tracer_p, &sa.base,
				 audit_cb);
		} else {
			struct aa_profile *tracee_p;
			struct cred *lcred = aa_get_task_policy(tracee,
								&tracee_p);

			sa.base.error = aa_may_ptrace(tracer, tracer_p,
						      tracee_p, mode);
			sa.base.error = aa_audit_ptrace(tracer_p, &sa);

			put_cred(lcred);
		}
		error = sa.base.error;
	}
	put_cred(cred);

	return error;
}
