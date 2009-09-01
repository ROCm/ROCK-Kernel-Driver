/*
 * AppArmor security module
 *
 * This file contains AppArmor network mediation
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include "include/security/apparmor.h"
#include "include/audit.h"
#include "include/context.h"
#include "include/net.h"
#include "include/policy.h"

#include "af_names.h"

static const char *sock_type_names[] = {
	"unknown(0)",
	"stream",
	"dgram",
	"raw",
	"rdm",
	"seqpacket",
	"dccp",
	"unknown(7)",
	"unknown(8)",
	"unknown(9)",
	"packet",
};

struct aa_audit_net {
	struct aa_audit base;

	int family, type, protocol;

};

static void audit_cb(struct audit_buffer *ab, void *va)
{
	struct aa_audit_net *sa = va;

	if (sa->family || sa->type) {
		if (address_family_names[sa->family])
			audit_log_format(ab, " family=\"%s\"",
					 address_family_names[sa->family]);
		else
			audit_log_format(ab, " family=\"unknown(%d)\"",
					 sa->family);

		if (sock_type_names[sa->type])
			audit_log_format(ab, " sock_type=\"%s\"",
					 sock_type_names[sa->type]);
		else
			audit_log_format(ab, " sock_type=\"unknown(%d)\"",
					 sa->type);

		audit_log_format(ab, " protocol=%d", sa->protocol);
	}
	
}

static int aa_audit_net(struct aa_profile *profile, struct aa_audit_net *sa)
{
	int type = AUDIT_APPARMOR_AUTO;

	if (likely(!sa->base.error)) {
		u16 audit_mask = profile->net.audit[sa->family];
		if (likely((PROFILE_AUDIT_MODE(profile) != AUDIT_ALL) &&
			   !(1 << sa->type & audit_mask)))
			return 0;
		type = AUDIT_APPARMOR_AUDIT;
	} else {
		u16 quiet_mask = profile->net.quiet[sa->family];
		u16 kill_mask = 0;
		u16 denied = (1 << sa->type) & ~quiet_mask;

		if (denied & kill_mask)
			type = AUDIT_APPARMOR_KILL;

		if ((denied & quiet_mask) &&
		    PROFILE_AUDIT_MODE(profile) != AUDIT_NOQUIET &&
		    PROFILE_AUDIT_MODE(profile) != AUDIT_ALL)
			return PROFILE_COMPLAIN(profile) ? 0 : sa->base.error;
	}

	return aa_audit(type, profile, (struct aa_audit *)sa, audit_cb);
}

int aa_net_perm(struct aa_profile *profile, char *operation,
		int family, int type, int protocol)
{
	struct aa_audit_net sa;
	u16 family_mask;

	if ((family < 0) || (family >= AF_MAX))
		return -EINVAL;

	if ((type < 0) || (type >= SOCK_MAX))
		return -EINVAL;

	/* unix domain and netlink sockets are handled by ipc */
	if (family == AF_UNIX || family == AF_NETLINK)
		return 0;

	family_mask = profile->net.allowed[family];

	sa.base.error = (family_mask & (1 << type)) ? 0 : -EACCES;

	memset(&sa, 0, sizeof(sa));
	sa.base.operation = operation;
	sa.base.gfp_mask = GFP_KERNEL;
	sa.family = family;
	sa.type = type;
	sa.protocol = protocol;

	return  aa_audit_net(profile, &sa);
}

int aa_revalidate_sk(struct sock *sk, char *operation)
{
	struct aa_profile *profile;
	struct cred *cred;
	int error = 0;

	/* this is some debugging code to flush out the network hooks that
	   that are called in interrupt context */
	if (in_interrupt()) {
		printk(KERN_WARNING "AppArmor Debug: Hook being called from interrupt context\n");
		dump_stack();
		return 0;
	}

	cred = aa_get_task_policy(current, &profile);
	if (profile)
		error = aa_net_perm(profile, operation,
				    sk->sk_family, sk->sk_type,
				    sk->sk_protocol);
	put_cred(cred);

	return error;
}
