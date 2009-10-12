/*
 * AppArmor security module
 *
 * This file contains AppArmor network mediation definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_NET_H
#define __AA_NET_H

#include <net/sock.h>

/* struct aa_net - network confinement data
 * @allowed: basic network families permissions
 * @audit_network: which network permissions to force audit
 * @quiet_network: which network permissions to quiet rejects
 */
struct aa_net {
	u16 allowed[AF_MAX];
	u16 audit[AF_MAX];
	u16 quiet[AF_MAX];
};

extern int aa_net_perm(struct aa_profile *profile, char *operation,
			int family, int type, int protocol);
extern int aa_revalidate_sk(struct sock *sk, char *operation);

static inline void aa_free_net_rules(struct aa_net *new)
{
	/* NOP */
}

#endif	/* __AA_NET_H */
