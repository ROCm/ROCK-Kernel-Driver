/*
 * AppArmor security module
 *
 * This file contains AppArmor capability mediation definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_CAPABILITY_H
#define __AA_CAPABILITY_H

#include <linux/sched.h>

struct aa_profile;

/* aa_caps - confinement data for capabilities
 * @set_caps: capabilities that are being set
 * @capabilities: capabilities mask
 * @audit_caps: caps that are to be audited
 * @quiet_caps: caps that should not be audited
 */
struct aa_caps {
	kernel_cap_t set;
	kernel_cap_t allowed;
	kernel_cap_t audit;
	kernel_cap_t quiet;
	kernel_cap_t kill;
};

int aa_profile_capable(struct aa_profile *profile, int cap);
int aa_capable(struct task_struct *task, struct aa_profile *profile, int cap,
	       int audit);

static inline void aa_free_cap_rules(struct aa_caps *caps)
{
	/* NOP */
}

#endif	/* __AA_CAPBILITY_H */
