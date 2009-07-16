/*
 * AppArmor security module
 *
 * This file contains AppArmor auditing function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_AUDIT_H
#define __AA_AUDIT_H

#include <linux/audit.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>

struct aa_profile;


extern const char *audit_mode_names[];
#define AUDIT_MAX_INDEX 5

#define AUDIT_APPARMOR_AUTO 0	/* auto choose audit message type */

enum audit_mode {
	AUDIT_NORMAL,		/* follow normal auditing of accesses */
	AUDIT_QUIET_DENIED,	/* quiet all denied access messages */
	AUDIT_QUIET,		/* quiet all messages */
	AUDIT_NOQUIET,		/* do not quiet audit messages */
	AUDIT_ALL		/* audit all accesses */
};

/*
 * aa_audit - AppArmor auditing structure
 * Structure is populated by access control code and passed to aa_audit which
 * provides for a single point of logging.
 */
struct aa_audit {
	struct task_struct *task;
	gfp_t gfp_mask;
	int error;
	const char *operation;
	const char *info;
};

int aa_audit(int type, struct aa_profile *profile, struct aa_audit *sa,
	     void(*cb)(struct audit_buffer *, void *));

int aa_audit_syscallreject(struct aa_profile *profile, gfp_t gfp, const char *,
			   void(*cb)(struct audit_buffer *, void *));


#endif	/* __AA_AUDIT_H */
