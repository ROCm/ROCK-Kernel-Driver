/*
 * linux/include/linux/svcauth_gss.h
 *
 * Bruce Fields <bfields@umich.edu>
 * Copyright (c) 2002 The Regents of the Unviersity of Michigan
 *
 * $Id$
 *
 */

#ifndef _LINUX_SUNRPC_SVCAUTH_GSS_H
#define _LINUX_SUNRPC_SVCAUTH_GSS_H

#ifdef __KERNEL__
#include <linux/sched.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/auth_gss.h>

int gss_svc_init(void);
int svcauth_gss_register_pseudoflavor(u32 pseudoflavor, char * name);


struct gss_svc_data {
	/* decoded gss client cred: */
	struct rpc_gss_wire_cred	clcred;
	/* pointer to the beginning of the procedure-specific results, which
	 * may be encrypted/checksummed in svcauth_gss_release: */
	u32				*body_start;
};

#endif /* __KERNEL__ */
#endif /* _LINUX_SUNRPC_SVCAUTH_GSS_H */
