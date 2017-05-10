/*
 * AppArmor security module
 *
 * This file contains AppArmor network mediation
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2012 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include "include/apparmor.h"
#include "include/audit.h"
#include "include/context.h"
#include "include/net.h"
#include "include/policy.h"

#include "net_names.h"

struct aa_fs_entry aa_fs_entry_network[] = {
	AA_FS_FILE_STRING("af_mask", AA_FS_AF_MASK),
	{ }
};

/* audit callback for net specific fields */
static void audit_cb(struct audit_buffer *ab, void *va)
{
	struct common_audit_data *sa = va;

	audit_log_format(ab, " family=");
	if (address_family_names[sa->u.net->family]) {
		audit_log_string(ab, address_family_names[sa->u.net->family]);
	} else {
		audit_log_format(ab, "\"unknown(%d)\"", sa->u.net->family);
	}
	audit_log_format(ab, " sock_type=");
	if (sock_type_names[aad(sa)->net.type]) {
		audit_log_string(ab, sock_type_names[aad(sa)->net.type]);
	} else {
		audit_log_format(ab, "\"unknown(%d)\"", aad(sa)->net.type);
	}
	audit_log_format(ab, " protocol=%d", aad(sa)->net.protocol);
}

/**
 * audit_net - audit network access
 * @profile: profile being enforced  (NOT NULL)
 * @op: operation being checked
 * @family: network family
 * @type:   network type
 * @protocol: network protocol
 * @sk: socket auditing is being applied to
 * @error: error code for failure else 0
 *
 * Returns: %0 or sa->error else other errorcode on failure
 */
static int audit_net(struct aa_profile *profile, const char *op,
		     u16 family, int type, int protocol,
		     struct sock *sk, int error)
{
	int audit_type = AUDIT_APPARMOR_AUTO;
	struct common_audit_data sa;
	struct apparmor_audit_data aad = { };
	struct lsm_network_audit net = { };
	if (sk) {
		sa.type = LSM_AUDIT_DATA_NET;
	} else {
		sa.type = LSM_AUDIT_DATA_NONE;
	}
	/* todo fill in socket addr info */
	aad(&sa) = &aad;
	sa.u.net = &net;
	aad(&sa)->op = op,
	sa.u.net->family = family;
	sa.u.net->sk = sk;
	aad(&sa)->net.type = type;
	aad(&sa)->net.protocol = protocol;
	aad(&sa)->error = error;

	if (likely(!aad(&sa)->error)) {
		u16 audit_mask = profile->net.audit[sa.u.net->family];
		if (likely((AUDIT_MODE(profile) != AUDIT_ALL) &&
			   !(1 << aad(&sa)->net.type & audit_mask)))
			return 0;
		audit_type = AUDIT_APPARMOR_AUDIT;
	} else {
		u16 quiet_mask = profile->net.quiet[sa.u.net->family];
		u16 kill_mask = 0;
		u16 denied = (1 << aad(&sa)->net.type);

		if (denied & kill_mask)
			audit_type = AUDIT_APPARMOR_KILL;

		if ((denied & quiet_mask) &&
		    AUDIT_MODE(profile) != AUDIT_NOQUIET &&
		    AUDIT_MODE(profile) != AUDIT_ALL)
			return COMPLAIN_MODE(profile) ? 0 : aad(&sa)->error;
	}

	return aa_audit(audit_type, profile, &sa, audit_cb);
}

/**
 * aa_net_perm - very course network access check
 * @op: operation being checked
 * @profile: profile being enforced  (NOT NULL)
 * @family: network family
 * @type:   network type
 * @protocol: network protocol
 *
 * Returns: %0 else error if permission denied
 */
int aa_net_perm(const char *op, struct aa_profile *profile, u16 family,
		int type, int protocol, struct sock *sk)
{
	u16 family_mask;
	int error;

	if ((family < 0) || (family >= AF_MAX))
		return -EINVAL;

	if ((type < 0) || (type >= SOCK_MAX))
		return -EINVAL;

	/* unix domain and netlink sockets are handled by ipc */
	if (family == AF_UNIX || family == AF_NETLINK)
		return 0;

	family_mask = profile->net.allow[family];

	error = (family_mask & (1 << type)) ? 0 : -EACCES;

	return audit_net(profile, op, family, type, protocol, sk, error);
}

/**
 * aa_revalidate_sk - Revalidate access to a sock
 * @op: operation being checked
 * @sk: sock being revalidated  (NOT NULL)
 *
 * Returns: %0 else error if permission denied
 */
int aa_revalidate_sk(const char *op, struct sock *sk)
{
	struct aa_profile *profile;
	int error = 0;

	/* aa_revalidate_sk should not be called from interrupt context
	 * don't mediate these calls as they are not task related
	 */
	if (in_interrupt())
		return 0;

	profile = __aa_current_profile();
	if (!unconfined(profile))
		error = aa_net_perm(op, profile, sk->sk_family, sk->sk_type,
				    sk->sk_protocol, sk);

	return error;
}
