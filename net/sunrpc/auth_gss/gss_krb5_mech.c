/*
 *  linux/net/sunrpc/gss_krb5_mech.c
 *
 *  Copyright (c) 2001 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson <andros@umich.edu>
 *  J. Bruce Fields <bfields@umich.edu>
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
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/gss_krb5.h>
#include <linux/sunrpc/xdr.h>
#include <linux/crypto.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

struct xdr_netobj gss_mech_krb5_oid =
   {9, "\052\206\110\206\367\022\001\002\002"};

static inline int
get_bytes(char **ptr, const char *end, void *res, int len)
{
	char *p, *q;
	p = *ptr;
	q = p + len;
	if (q > end || q < p)
		return -1;
	memcpy(res, p, len);
	*ptr = q;
	return 0;
}

static inline int
get_netobj(char **ptr, const char *end, struct xdr_netobj *res)
{
	char *p, *q;
	p = *ptr;
	if (get_bytes(&p, end, &res->len, sizeof(res->len)))
		return -1;
	q = p + res->len;
	if (q > end || q < p)
		return -1;
	if (!(res->data = kmalloc(res->len, GFP_KERNEL)))
		return -1;
	memcpy(res->data, p, res->len);
	*ptr = q;
	return 0;
}

static inline int
get_key(char **p, char *end, struct crypto_tfm **res)
{
	struct xdr_netobj	key;
	int			alg, alg_mode;
	char			*alg_name;

	if (get_bytes(p, end, &alg, sizeof(alg)))
		goto out_err;
	if ((get_netobj(p, end, &key)))
		goto out_err;

	switch (alg) {
		case ENCTYPE_DES_CBC_RAW:
			alg_name = "des";
			alg_mode = CRYPTO_TFM_MODE_CBC;
			break;
		default:
			dprintk("RPC: get_key: unsupported algorithm %d", alg);
			goto out_err_free_key;
	}
	if (!(*res = crypto_alloc_tfm(alg_name, alg_mode)))
		goto out_err_free_key;
	if (crypto_cipher_setkey(*res, key.data, key.len))
		goto out_err_free_tfm;

	kfree(key.data);
	return 0;

out_err_free_tfm:
	crypto_free_tfm(*res);
out_err_free_key:
	kfree(key.data);
out_err:
	return -1;
}

static u32
gss_import_sec_context_kerberos(struct xdr_netobj *inbuf,
				struct gss_ctx *ctx_id)
{
	char	*p = inbuf->data;
	char	*end = inbuf->data + inbuf->len;
	struct	krb5_ctx *ctx;

	if (!(ctx = kmalloc(sizeof(*ctx), GFP_KERNEL)))
		goto out_err;
	memset(ctx, 0, sizeof(*ctx));

	if (get_bytes(&p, end, &ctx->initiate, sizeof(ctx->initiate)))
		goto out_err_free_ctx;
	if (get_bytes(&p, end, &ctx->seed_init, sizeof(ctx->seed_init)))
		goto out_err_free_ctx;
	if (get_bytes(&p, end, ctx->seed, sizeof(ctx->seed)))
		goto out_err_free_ctx;
	if (get_bytes(&p, end, &ctx->signalg, sizeof(ctx->signalg)))
		goto out_err_free_ctx;
	if (get_bytes(&p, end, &ctx->sealalg, sizeof(ctx->sealalg)))
		goto out_err_free_ctx;
	if (get_bytes(&p, end, &ctx->endtime, sizeof(ctx->endtime)))
		goto out_err_free_ctx;
	if (get_bytes(&p, end, &ctx->seq_send, sizeof(ctx->seq_send)))
		goto out_err_free_ctx;
	if (get_netobj(&p, end, &ctx->mech_used))
		goto out_err_free_ctx;
	if (get_key(&p, end, &ctx->enc))
		goto out_err_free_mech;
	if (get_key(&p, end, &ctx->seq))
		goto out_err_free_key1;
	if (p != end)
		goto out_err_free_key2;

	ctx_id->internal_ctx_id = ctx;
	dprintk("Succesfully imported new context.\n");
	return 0;

out_err_free_key2:
	crypto_free_tfm(ctx->seq);
out_err_free_key1:
	crypto_free_tfm(ctx->enc);
out_err_free_mech:
	kfree(ctx->mech_used.data);
out_err_free_ctx:
	kfree(ctx);
out_err:
	return GSS_S_FAILURE;
}

void
gss_delete_sec_context_kerberos(void *internal_ctx) {
	struct krb5_ctx *kctx = internal_ctx;

	if (kctx->seq)
		crypto_free_tfm(kctx->seq);
	if (kctx->enc)
		crypto_free_tfm(kctx->enc);
	if (kctx->mech_used.data)
		kfree(kctx->mech_used.data);
	kfree(kctx);
}

u32
gss_verify_mic_kerberos(struct gss_ctx		*ctx,
			struct xdr_netobj	*signbuf,
			struct xdr_netobj	*checksum,
			u32		*qstate) {
	u32 maj_stat = 0;
	int qop_state;
	struct krb5_ctx *kctx = ctx->internal_ctx_id;

	maj_stat = krb5_read_token(kctx, checksum, signbuf, &qop_state,
				   KG_TOK_MIC_MSG);
	if (!maj_stat && qop_state)
	    *qstate = qop_state;

	dprintk("RPC: gss_verify_mic_kerberos returning %d\n", maj_stat);
	return maj_stat;
}

u32
gss_get_mic_kerberos(struct gss_ctx	*ctx,
		     u32		qop,
		     struct xdr_netobj	*message_buffer,
		     struct xdr_netobj	*message_token) {
	u32 err = 0;
	struct krb5_ctx *kctx = ctx->internal_ctx_id;

	if (!message_buffer->data) return GSS_S_FAILURE;

	dprintk("RPC: gss_get_mic_kerberos:"
		" message_buffer->len %d\n",message_buffer->len);

	err = krb5_make_token(kctx, qop, message_buffer,
			      message_token, KG_TOK_MIC_MSG);

	dprintk("RPC: gss_get_mic_kerberos returning %d\n",err);

	return err;
}

static struct gss_api_ops gss_kerberos_ops = {
	.name			= "krb5",
	.gss_import_sec_context	= gss_import_sec_context_kerberos,
	.gss_get_mic		= gss_get_mic_kerberos,
	.gss_verify_mic		= gss_verify_mic_kerberos,
	.gss_delete_sec_context	= gss_delete_sec_context_kerberos,
};

/* XXX error checking? reference counting? */
static int __init init_kerberos_module(void)
{
	struct gss_api_mech *gm;

	if (gss_mech_register(&gss_mech_krb5_oid, &gss_kerberos_ops))
		printk("Failed to register kerberos gss mechanism!\n");
	gm = gss_mech_get_by_OID(&gss_mech_krb5_oid);
	gss_register_triple(RPC_AUTH_GSS_KRB5 , gm, 0, RPC_GSS_SVC_NONE);
	gss_mech_put(gm);
	return 0;
}

static void __exit cleanup_kerberos_module(void)
{
	gss_unregister_triple(RPC_AUTH_GSS_KRB5);
}

MODULE_LICENSE("GPL");
module_init(init_kerberos_module);
module_exit(cleanup_kerberos_module);
