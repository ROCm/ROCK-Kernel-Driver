/*
 * linux/net/sunrpc/auth_gss.c
 *
 * RPCSEC_GSS client authentication.
 * 
 *  Copyright (c) 2000 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Dug Song       <dugsong@monkey.org>
 *  Andy Adamson   <andros@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id$
 */


#define __NO_VERSION__
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/auth_gss.h>
#include <linux/sunrpc/gss_err.h>

static struct rpc_authops authgss_ops;

static struct rpc_credops gss_credops;

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

#define NFS_NGROUPS	16

#define GSS_CRED_EXPIRE		(60 * HZ)	/* XXX: reasonable? */
#define GSS_CRED_SLACK		1024		/* XXX: unused */
#define GSS_VERF_SLACK		48		/* length of a krb5 verifier.*/

/* XXX this define must match the gssd define
* as it is passed to gssd to signal the use of
* machine creds should be part of the shared rpc interface */

#define CA_RUN_AS_MACHINE  0x00000200 

/* dump the buffer in `emacs-hexl' style */
#define isprint(c)      ((c > 0x1f) && (c < 0x7f))

void
print_hexl(u32 *p, u_int length, u_int offset)
{
	u_int i, j, jm;
	u8 c, *cp;
	
	dprintk("RPC: print_hexl: length %d\n",length);
	dprintk("\n");
	cp = (u8 *) p;
	
	for (i = 0; i < length; i += 0x10) {
		dprintk("  %04x: ", (u_int)(i + offset));
		jm = length - i;
		jm = jm > 16 ? 16 : jm;
		
		for (j = 0; j < jm; j++) {
			if ((j % 2) == 1)
				dprintk("%02x ", (u_int)cp[i+j]);
			else
				dprintk("%02x", (u_int)cp[i+j]);
		}
		for (; j < 16; j++) {
			if ((j % 2) == 1)
				dprintk("   ");
			else
				dprintk("  ");
		}
		dprintk(" ");
		
		for (j = 0; j < jm; j++) {
			c = cp[i+j];
			c = isprint(c) ? c : '.';
			dprintk("%c", c);
		}
		dprintk("\n");
	}
}


/* 
 * NOTE: we have the opportunity to use different 
 * parameters based on the input flavor (which must be a pseudoflavor)
 */
static struct rpc_auth *
gss_create(struct rpc_clnt *clnt, rpc_authflavor_t flavor)
{
	struct rpc_auth * auth;

	dprintk("RPC: creating GSS authenticator for client %p\n",clnt);
	if (!try_module_get(THIS_MODULE))
		return NULL;
	if (!(auth = kmalloc(sizeof(*auth), GFP_KERNEL)))
		goto out_dec;
	auth->au_cslack = GSS_CRED_SLACK >> 2;
	auth->au_rslack = GSS_VERF_SLACK >> 2;
	auth->au_expire = GSS_CRED_EXPIRE;
	auth->au_ops = &authgss_ops;
	auth->au_flavor = flavor;

	rpcauth_init_credcache(auth);

	return auth;
out_dec:
	module_put(THIS_MODULE);
	return NULL;
}

static void
gss_destroy(struct rpc_auth *auth)
{
	dprintk("RPC: destroying GSS authenticator %p flavor %d\n",
		auth, auth->au_flavor);

	rpcauth_free_credcache(auth);

	kfree(auth);
	module_put(THIS_MODULE);
}

/* gss_destroy_cred (and gss_destroy_ctx) are used to clean up after failure
 * to create a new cred or context, so they check that things have been
 * allocated before freeing them. */
void
gss_destroy_ctx(struct gss_cl_ctx *ctx)
{

	dprintk("RPC: gss_destroy_ctx\n");

	if (ctx->gc_gss_ctx)
		gss_delete_sec_context(&ctx->gc_gss_ctx);

	if (ctx->gc_wire_ctx.len > 0) {
		kfree(ctx->gc_wire_ctx.data);
		ctx->gc_wire_ctx.len = 0;
	}

	kfree(ctx);

}

static void
gss_destroy_cred(struct rpc_cred *rc)
{
	struct gss_cred *cred = (struct gss_cred *)rc;

	dprintk("RPC: gss_destroy_cred \n");

	if (cred->gc_ctx)
		gss_destroy_ctx(cred->gc_ctx);
	kfree(cred);
}

static struct rpc_cred *
gss_create_cred(struct rpc_auth *auth, struct auth_cred *acred, int taskflags)
{
	struct gss_cred	*cred = NULL;

	dprintk("RPC: gss_create_cred for uid %d, flavor %d\n",
		acred->uid, auth->au_flavor);

	if (!(cred = kmalloc(sizeof(*cred), GFP_KERNEL)))
		goto out_err;

	memset(cred, 0, sizeof(*cred));
	atomic_set(&cred->gc_count, 0);
	cred->gc_uid = acred->uid;
	/*
	 * Note: in order to force a call to call_refresh(), we deliberately
	 * fail to flag the credential as RPCAUTH_CRED_UPTODATE.
	 */
	cred->gc_flags = 0;
	cred->gc_base.cr_ops = &gss_credops;
	cred->gc_flavor = auth->au_flavor;

	return (struct rpc_cred *) cred;

out_err:
	dprintk("RPC: gss_create_cred failed\n");
	if (cred) gss_destroy_cred((struct rpc_cred *)cred);
	return NULL;
}

static int
gss_match(struct auth_cred *acred, struct rpc_cred *rc, int taskflags)
{
	return (rc->cr_uid == acred->uid);
}

/*
* Marshal credentials.
* Maybe we should keep a cached credential for performance reasons.
*/
static u32 *
gss_marshal(struct rpc_task *task, u32 *p, int ruid)
{
	struct gss_cred	*cred = (struct gss_cred *) task->tk_msg.rpc_cred;
	struct gss_cl_ctx	*ctx = cred->gc_ctx;
	u32		*cred_len;
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_clnt *clnt = task->tk_client;
	struct rpc_xprt *xprt = clnt->cl_xprt;
	u32             *verfbase = req->rq_svec[0].iov_base; 
	u32             maj_stat = 0;
	struct xdr_netobj bufin,bufout;
	u32		service;

	dprintk("RPC: gss_marshal\n");

	/* We compute the checksum for the verifier over the xdr-encoded bytes
	 * starting with the xid (which verfbase points to) and ending at
	 * the end of the credential. */
	if (xprt->stream)
		verfbase++; /* See clnt.c:call_header() */

	*p++ = htonl(RPC_AUTH_GSS);
	cred_len = p++;

	service = gss_pseudoflavor_to_service(cred->gc_flavor);
	if (service == 0) {
		dprintk("Bad pseudoflavor %d in gss_marshal\n",
			cred->gc_flavor);
		return NULL;
	}
	spin_lock(&ctx->gc_seq_lock);
	task->tk_gss_seqno = ctx->gc_seq++;
	spin_unlock(&ctx->gc_seq_lock);

	*p++ = htonl((u32) RPC_GSS_VERSION);
	*p++ = htonl((u32) ctx->gc_proc);
	*p++ = htonl((u32) task->tk_gss_seqno);
	*p++ = htonl((u32) service);
	p = xdr_encode_netobj(p, &ctx->gc_wire_ctx);
	*cred_len = htonl((p - (cred_len + 1)) << 2);

	/* Marshal verifier. */
	bufin.data = (u8 *)verfbase;
	bufin.len = (p - verfbase) << 2;

	/* set verifier flavor*/
	*p++ = htonl(RPC_AUTH_GSS);

	maj_stat = gss_get_mic(ctx->gc_gss_ctx,
			       GSS_C_QOP_DEFAULT, 
			       &bufin, &bufout);
	if(maj_stat != 0){
		printk("gss_marshal: gss_get_mic FAILED (%d)\n",
		       maj_stat);
		return(NULL);
	}
	p = xdr_encode_netobj(p, &bufout);
	return p;
}

/*
* Refresh credentials. XXX - finish
*/
static int
gss_refresh(struct rpc_task *task)
{
	/* Insert upcall here ! */
	task->tk_msg.rpc_cred->cr_flags |= RPCAUTH_CRED_UPTODATE;
	return task->tk_status = -EACCES;
}

static u32 *
gss_validate(struct rpc_task *task, u32 *p)
{
	struct gss_cred *cred = (struct gss_cred *)task->tk_msg.rpc_cred; 
	struct gss_cl_ctx	*ctx = cred->gc_ctx;
	u32		seq, qop_state;
	struct xdr_netobj bufin;
	struct xdr_netobj bufout;
	u32		flav,len;
	int             code = 0;

	dprintk("RPC: gss_validate\n");

	flav = ntohl(*p++);
	if ((len = ntohl(*p++)) > RPC_MAX_AUTH_SIZE) {
                printk("RPC: giant verf size: %ld\n", (unsigned long) len);
                return NULL;
	}
	dprintk("RPC: gss_validate: verifier flavor %d, len %d\n", flav, len);

	if (flav != RPC_AUTH_GSS) {
		printk("RPC: bad verf flavor: %ld\n", (unsigned long)flav);
		return NULL;
	}
	seq = htonl(task->tk_gss_seqno);
	bufin.data = (u8 *) &seq;
	bufin.len = sizeof(seq);
	bufout.data = (u8 *) p;
	bufout.len = len;

	if ((code = gss_verify_mic(ctx->gc_gss_ctx, 
				   &bufin, &bufout, &qop_state) < 0))
		return NULL;
	task->tk_auth->au_rslack = XDR_QUADLEN(len) + 2;
	dprintk("RPC: GSS gss_validate: gss_verify_mic succeeded.\n");
	return p + XDR_QUADLEN(len);
}

static struct rpc_authops authgss_ops = {
	.au_flavor	= RPC_AUTH_GSS,
#ifdef RPC_DEBUG
	.au_name	= "RPCSEC_GSS",
#endif
	.create		= gss_create,
	.destroy	= gss_destroy,
	.crcreate	= gss_create_cred
};

static struct rpc_credops gss_credops = {
	.crdestroy	= gss_destroy_cred,
	.crmatch	= gss_match,
	.crmarshal	= gss_marshal,
	.crrefresh	= gss_refresh,
	.crvalidate	= gss_validate,
};

extern void gss_svc_ctx_init(void);

/*
 * Initialize RPCSEC_GSS module
 */
static int __init init_rpcsec_gss(void)
{
	int err = 0;

	err = rpcauth_register(&authgss_ops);
	return err;
}

static void __exit exit_rpcsec_gss(void)
{
	gss_mech_unregister_all();
	rpcauth_unregister(&authgss_ops);
}

MODULE_LICENSE("GPL");
module_init(init_rpcsec_gss)
module_exit(exit_rpcsec_gss)
