/*
 * linux/include/linux/sunrpc/svcauth.h
 *
 * RPC server-side authentication stuff.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_SVCAUTH_H_
#define _LINUX_SUNRPC_SVCAUTH_H_

#ifdef __KERNEL__

#include <linux/sunrpc/msg_prot.h>

struct svc_cred {
	uid_t			cr_uid;
	gid_t			cr_gid;
	gid_t			cr_groups[NGROUPS];
};

struct svc_rqst;		/* forward decl */
/*
 * Each authentication flavour registers an auth_ops
 * structure.
 * name is simply the name.
 * flavour gives the auth flavour. It determines where the flavour is registered
 * accept() is given a request and should verify it.
 *   It should inspect the authenticator and verifier, and possibly the data.
 *    If there is a problem with the authentication *authp should be set.
 *    The return value of accept() can indicate:
 *      OK - authorised. client and credential are set in rqstp.
 *           reqbuf points to arguments
 *           resbuf points to good place for results.  verfier
 *             is (probably) already in place.  Certainly space is
 *	       reserved for it.
 *      DROP - simply drop the request. It may have been deferred
 *      GARBAGE - rpc garbage_args error
 *      SYSERR - rpc system_err error
 *      DENIED - authp holds reason for denial.
 *
 *   accept is passed the proc number so that it can accept NULL rpc requests
 *   even if it cannot authenticate the client (as is sometimes appropriate).
 *
 * release() is given a request after the procedure has been run.
 *  It should sign/encrypt the results if needed
 * It should return:
 *    OK - the resbuf is ready to be sent
 *    DROP - the reply should be quitely dropped
 *    DENIED - authp holds a reason for MSG_DENIED
 *    SYSERR - rpc system_err
 */
struct auth_ops {
	char *	name;
	int	flavour;
	int	(*accept)(struct svc_rqst *rq, u32 *authp, int proc);
	int	(*release)(struct svc_rqst *rq);
};
extern struct auth_ops	*authtab[RPC_AUTH_MAXFLAVOR];

#define	SVC_GARBAGE	1
#define	SVC_SYSERR	2
#define	SVC_VALID	3
#define	SVC_NEGATIVE	4
#define	SVC_OK		5
#define	SVC_DROP	6
#define	SVC_DENIED	7
#define	SVC_PENDING	8


extern int	svc_authenticate(struct svc_rqst *rqstp, u32 *statp, u32 *authp, int proc);
extern int	svc_authorise(struct svc_rqst *rqstp);
extern int	svc_auth_register(rpc_authflavor_t flavor, struct auth_ops *aops);
extern void	svc_auth_unregister(rpc_authflavor_t flavor);


#endif /* __KERNEL__ */

#endif /* _LINUX_SUNRPC_SVCAUTH_H_ */
