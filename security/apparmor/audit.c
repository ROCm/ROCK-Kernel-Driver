/*
 * AppArmor security module
 *
 * This file contains AppArmor auditing functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/audit.h>
#include <linux/socket.h>

#include "include/security/apparmor.h"
#include "include/audit.h"
#include "include/policy.h"

const char *audit_mode_names[] = {
	"normal",
	"quiet_denied",
	"quiet"
	"noquiet",
	"all"
};

static char* aa_audit_type[] = {
	"APPARMOR_AUDIT",
	"APPARMOR_ALLOWED",
	"APPARMOR_DENIED",
	"APPARMOR_HINT",
	"APPARMOR_STATUS",
	"APPARMOR_ERROR",
	"APPARMOR_KILLED"
};

/*
 * TODO:
 * user auditing - netlink interface
 * system control of whether user audit messages go to system log
 */
static int aa_audit_base(int type, struct aa_profile *profile,
			 struct aa_audit *sa, struct audit_context *audit_cxt,
			 void(*cb)(struct audit_buffer *, void *))
{
	struct audit_buffer *ab = NULL;

	if (profile && PROFILE_KILL(profile) && type == AUDIT_APPARMOR_DENIED)
		type = AUDIT_APPARMOR_KILL;

	ab = audit_log_start(audit_cxt, sa->gfp_mask, type);

	if (!ab) {
		AA_ERROR("(%d) Unable to log event of type (%d)\n",
			 -ENOMEM, type);
		 /* don't fail operations in complain mode even if logging
		  * fails */
		return type == AUDIT_APPARMOR_ALLOWED ? 0 : -ENOMEM;
	}

	if (g_apparmor_audit_header)
		audit_log_format(ab, "type=%s ",
				 aa_audit_type[type - AUDIT_APPARMOR_AUDIT]);

	if (sa->operation)
		audit_log_format(ab, "operation=\"%s\"", sa->operation);

	if (sa->info) {
		audit_log_format(ab, " info=\"%s\"", sa->info);
		if (sa->error)
			audit_log_format(ab, " error=%d", sa->error);
	}

	audit_log_format(ab, " pid=%d", sa->task ?sa->task->pid : current->pid);

	if (profile) {
		pid_t pid = sa->task ? sa->task->real_parent->pid :
				       current->real_parent->pid;
		audit_log_format(ab, " parent=%d", pid);
		audit_log_format(ab, " profile=");
		audit_log_untrustedstring(ab, profile->fqname);

		if (profile->ns != default_namespace) {
			audit_log_format(ab, " namespace=");
			audit_log_untrustedstring(ab, profile->ns->base.name);
		}
	}

	if (cb)
		cb(ab, sa);

	audit_log_end(ab);

	if (type == AUDIT_APPARMOR_KILL)
		(void)send_sig_info(SIGKILL, NULL,
				    sa->task ? sa->task : current);

	return type == AUDIT_APPARMOR_ALLOWED ? 0 : sa->error;
}

/**
 * aa_audit - Log an audit event to the audit subsystem
 * @type: audit type for the message
 * @profile: profile to check against
 * @sa: audit event
 */
int aa_audit(int type, struct aa_profile *profile, struct aa_audit *sa,
	     void(*cb)(struct audit_buffer *, void *))
{
	struct audit_context *audit_cxt;
	audit_cxt = g_apparmor_logsyscall ? current->audit_context : NULL;

	if (type == AUDIT_APPARMOR_AUTO) {
		if (likely(!sa->error)) {
			if (PROFILE_AUDIT_MODE(profile) != AUDIT_ALL)
				return 0;
			type = AUDIT_APPARMOR_AUDIT;
		} else if (PROFILE_COMPLAIN(profile))
			type = AUDIT_APPARMOR_ALLOWED;
		else
			type = AUDIT_APPARMOR_DENIED;
	}
	if (PROFILE_AUDIT_MODE(profile) == AUDIT_QUIET ||
	    (type == AUDIT_APPARMOR_DENIED &&
	     PROFILE_AUDIT_MODE(profile) == AUDIT_QUIET))
		return sa->error;

	return aa_audit_base(type, profile, sa, audit_cxt, cb);
}

/**
 * aa_audit_syscallreject - Log a syscall rejection to the audit subsystem
 * @profile: profile to check against
 * @gfp: memory allocation flags
 * @msg: string describing syscall being rejected
 */
int aa_audit_syscallreject(struct aa_profile *profile, gfp_t gfp,
			   const char *msg,
			   void(*cb)(struct audit_buffer *, void *))
{
	struct aa_audit sa;
	memset(&sa, 0, sizeof(sa));
	sa.operation = "syscall";
	sa.info = msg;
	sa.gfp_mask = gfp;
	sa.error = -EACCES;

	return aa_audit_base(AUDIT_APPARMOR_DENIED, profile, &sa,
			     current->audit_context, NULL);
}
