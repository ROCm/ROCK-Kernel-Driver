/*
 * linux/net/sunrpc/svcauth.c
 *
 * The generic interface for RPC authentication on the server side.
 * 
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 *
 * CHANGES
 * 19-Apr-2000 Chris Evans      - Security fix
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/err.h>

#define RPCDBG_FACILITY	RPCDBG_AUTH

/*
 * Builtin auth flavors
 */
static int	svcauth_null_accept(struct svc_rqst *rqstp, u32 *authp, int proc);
static int	svcauth_null_release(struct svc_rqst *rqstp);
static int	svcauth_unix_accept(struct svc_rqst *rqstp, u32 *authp, int proc);
static int	svcauth_unix_release(struct svc_rqst *rqstp);

struct auth_ops svcauth_null = {
	.name		= "null",
	.flavour	= RPC_AUTH_NULL,
	.accept 	= svcauth_null_accept,
	.release	= svcauth_null_release,
};

struct auth_ops svcauth_unix = {
	.name		= "unix",
	.flavour	= RPC_AUTH_UNIX,
	.accept 	= svcauth_unix_accept,
	.release	= svcauth_unix_release,
};

/*
 * Table of authenticators
 */
static struct auth_ops	*authtab[RPC_AUTH_MAXFLAVOR] = {
	[0] = &svcauth_null,
	[1] = &svcauth_unix,
};

int
svc_authenticate(struct svc_rqst *rqstp, u32 *statp, u32 *authp, int proc)
{
	rpc_authflavor_t	flavor;
	struct auth_ops		*aops;

	*statp = rpc_success;
	*authp = rpc_auth_ok;

	svc_getu32(&rqstp->rq_argbuf, flavor);
	flavor = ntohl(flavor);

	dprintk("svc: svc_authenticate (%d)\n", flavor);
	if (flavor >= RPC_AUTH_MAXFLAVOR || !(aops = authtab[flavor])) {
		*authp = rpc_autherr_badcred;
		return 0;
	}

	rqstp->rq_authop = aops;
	switch (aops->accept(rqstp, authp, proc)) {
	case SVC_OK:
		return 0;
	case SVC_GARBAGE:
		*statp = rpc_garbage_args;
		return 0;
	case SVC_SYSERR:
		*statp = rpc_system_err;
		return 0;
	case SVC_DENIED:
		return 0;
	case SVC_DROP:
		break;
	}
	return 1; /* drop the request */
}

/* A reqeust, which was authenticated, has now executed.
 * Time to finalise the the credentials and verifier
 * and release and resources
 */
int svc_authorise(struct svc_rqst *rqstp)
{
	struct auth_ops *aops = rqstp->rq_authop;
	int rv = 0;

	rqstp->rq_authop = NULL;
	
	if (aops) 
		rv = aops->release(rqstp);

	/* FIXME should I count and release authops */
	return rv;
}

int
svc_auth_register(rpc_authflavor_t flavor, struct auth_ops *aops)
{
	if (flavor >= RPC_AUTH_MAXFLAVOR || authtab[flavor])
		return -EINVAL;
	authtab[flavor] = aops;
	return 0;
}

void
svc_auth_unregister(rpc_authflavor_t flavor)
{
	if (flavor < RPC_AUTH_MAXFLAVOR)
		authtab[flavor] = NULL;
}

static int
svcauth_null_accept(struct svc_rqst *rqstp, u32 *authp, int proc)
{
	struct svc_buf	*argp = &rqstp->rq_argbuf;
	struct svc_buf	*resp = &rqstp->rq_resbuf;

	if ((argp->len -= 3) < 0) {
		return SVC_GARBAGE;
	}
	if (*(argp->buf)++ != 0) {	/* we already skipped the flavor */
		dprintk("svc: bad null cred\n");
		*authp = rpc_autherr_badcred;
		return SVC_DENIED;
	}
	if (*(argp->buf)++ != RPC_AUTH_NULL || *(argp->buf)++ != 0) {
		dprintk("svc: bad null verf\n");
		*authp = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	/* Signal that mapping to nobody uid/gid is required */
	rqstp->rq_cred.cr_uid = (uid_t) -1;
	rqstp->rq_cred.cr_gid = (gid_t) -1;
	rqstp->rq_cred.cr_groups[0] = NOGROUP;

	/* Put NULL verifier */
	svc_putu32(resp, RPC_AUTH_NULL);
	svc_putu32(resp, 0);
	return SVC_OK;
}

static int
svcauth_null_release(struct svc_rqst *rqstp)
{
	return 0; /* don't drop */
}

static int
svcauth_unix_accept(struct svc_rqst *rqstp, u32 *authp, int proc)
{
	struct svc_buf	*argp = &rqstp->rq_argbuf;
	struct svc_buf	*resp = &rqstp->rq_resbuf;
	struct svc_cred	*cred = &rqstp->rq_cred;
	u32		*bufp = argp->buf, slen, i;
	int		len   = argp->len;

	if ((len -= 3) < 0)
		return SVC_GARBAGE;

	bufp++;					/* length */
	bufp++;					/* time stamp */
	slen = XDR_QUADLEN(ntohl(*bufp++));	/* machname length */
	if (slen > 64 || (len -= slen + 3) < 0)
		goto badcred;
	bufp += slen;				/* skip machname */

	cred->cr_uid = ntohl(*bufp++);		/* uid */
	cred->cr_gid = ntohl(*bufp++);		/* gid */

	slen = ntohl(*bufp++);			/* gids length */
	if (slen > 16 || (len -= slen + 2) < 0)
		goto badcred;
	for (i = 0; i < NGROUPS && i < slen; i++)
		cred->cr_groups[i] = ntohl(*bufp++);
	if (i < NGROUPS)
		cred->cr_groups[i] = NOGROUP;
	bufp += (slen - i);

	if (*bufp++ != RPC_AUTH_NULL || *bufp++ != 0) {
		*authp = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	argp->buf = bufp;
	argp->len = len;

	/* Put NULL verifier */
	svc_putu32(resp, RPC_AUTH_NULL);
	svc_putu32(resp, 0);

	return SVC_OK;

badcred:
	*authp = rpc_autherr_badcred;
	return SVC_DENIED;
}

static int
svcauth_unix_release(struct svc_rqst *rqstp)
{
	/* Verifier (such as it is) is already in place.
	 */
	return 0;
}
