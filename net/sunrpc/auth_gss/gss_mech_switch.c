/*
 *  linux/net/sunrpc/gss_mech_switch.c
 *
 *  Copyright (c) 2001 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  J. Bruce Fields   <bfields@umich.edu>
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

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/sunrpc/msg_prot.h>
#include <linux/sunrpc/gss_asn1.h>
#include <linux/sunrpc/auth_gss.h>
#include <linux/sunrpc/gss_err.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/gss_api.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/name_lookup.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

static LIST_HEAD(registered_mechs);
static spinlock_t registered_mechs_lock = SPIN_LOCK_UNLOCKED;

/* Reference counting: The reference count includes the reference in the
 * global registered_mechs list.  That reference will never diseappear
 * (so the reference count will never go below 1) until after the mech
 * is removed from the list.  Nothing can be removed from the list without
 * first getting the registered_mechs_lock, so a gss_api_mech won't diseappear
 * from underneath us while we hold the registered_mech_lock.  */

int
gss_mech_register(struct xdr_netobj * mech_type, struct gss_api_ops * ops)
{
	struct gss_api_mech *gm;

	if (!(gm = kmalloc(sizeof(*gm), GFP_KERNEL))) {
		printk("Failed to allocate memory in gss_mech_register");
		return -1;
	}
	gm->gm_oid.len = mech_type->len;
	if (!(gm->gm_oid.data = kmalloc(mech_type->len, GFP_KERNEL))) {
		printk("Failed to allocate memory in gss_mech_register");
		return -1;
	}
	memcpy(gm->gm_oid.data, mech_type->data, mech_type->len);
	/* We're counting the reference in the registered_mechs list: */
	atomic_set(&gm->gm_count, 1);
	gm->gm_ops = ops;
	
	spin_lock(&registered_mechs_lock);
	list_add(&gm->gm_list, &registered_mechs);
	spin_unlock(&registered_mechs_lock);
	dprintk("RPC: gss_mech_register: registered mechanism with oid:\n");
	print_hexl((u32 *)mech_type->data, mech_type->len, 0);
	return 0;
}

/* The following must be called with spinlock held: */
int
do_gss_mech_unregister(struct gss_api_mech *gm)
{

	list_del(&gm->gm_list);

	dprintk("RPC: unregistered mechanism with oid:\n");
	print_hexl((u32 *)gm->gm_oid.data, gm->gm_oid.len, 0);
	if (!gss_mech_put(gm)) {
		dprintk("RPC: We just unregistered a gss_mechanism which"
				" someone is still using.\n");
		return -1;
	} else {
		return 0;
	}
}

int
gss_mech_unregister(struct gss_api_mech *gm)
{
	int status;

	spin_lock(&registered_mechs_lock);
	status = do_gss_mech_unregister(gm);
	spin_unlock(&registered_mechs_lock);
	return status;
}

int
gss_mech_unregister_all(void)
{
	struct list_head	*pos;
	struct gss_api_mech	*gm;
	int			status = 0;

	spin_lock(&registered_mechs_lock);
	while (!list_empty(&registered_mechs)) {
		pos = registered_mechs.next;
		gm = list_entry(pos, struct gss_api_mech, gm_list);
		if (do_gss_mech_unregister(gm))
			status = -1;
	}
	spin_unlock(&registered_mechs_lock);
	return status;
}

struct gss_api_mech *
gss_mech_get(struct gss_api_mech *gm)
{
	atomic_inc(&gm->gm_count);
	return gm;
}

struct gss_api_mech *
gss_mech_get_by_OID(struct xdr_netobj *mech_type)
{
	struct gss_api_mech 	*pos, *gm = NULL;

	dprintk("RPC: gss_mech_get_by_OID searching for mechanism with OID:\n");
	print_hexl((u32 *)mech_type->data, mech_type->len, 0);
	spin_lock(&registered_mechs_lock);
	list_for_each_entry(pos, &registered_mechs, gm_list) {
		if ((pos->gm_oid.len == mech_type->len)
			&& !memcmp(pos->gm_oid.data, mech_type->data,
							mech_type->len)) {
			gm = gss_mech_get(pos);
			break;
		}
	}
	spin_unlock(&registered_mechs_lock);
	dprintk("RPC: gss_mech_get_by_OID %s it\n", gm ? "found" : "didn't find");
	return gm;
}

int
gss_mech_put(struct gss_api_mech * gm)
{
	if (atomic_dec_and_test(&gm->gm_count)) {
		if (gm->gm_oid.len >0)
			kfree(gm->gm_oid.data);
		kfree(gm);
		return 1;
	} else {
		return 0;
	}
}

/* The mech could probably be determined from the token instead, but it's just
 * as easy for now to pass it in. */
u32
gss_import_sec_context(struct xdr_netobj	*input_token,
		       struct gss_api_mech	*mech,
		       struct gss_ctx		**ctx_id)
{
	if (!(*ctx_id = kmalloc(sizeof(**ctx_id), GFP_KERNEL)))
		return GSS_S_FAILURE;
	memset(*ctx_id, 0, sizeof(**ctx_id));
	(*ctx_id)->mech_type = gss_mech_get(mech);

	return mech->gm_ops
		->gss_import_sec_context(input_token, *ctx_id);
}

/* gss_get_mic: compute a mic over message and return mic_token. */

u32
gss_get_mic(struct gss_ctx	*context_handle,
	    u32			qop,
	    struct xdr_netobj	*message,
	    struct xdr_netobj	*mic_token)
{
	 return context_handle->mech_type->gm_ops
		->gss_get_mic(context_handle,
			      qop,
			      message,
			      mic_token);
}

/* gss_verify_mic: check whether the provided mic_token verifies message. */

u32
gss_verify_mic(struct gss_ctx		*context_handle,
	       struct xdr_netobj	*message,
	       struct xdr_netobj	*mic_token,
	       u32			*qstate)
{
	return context_handle->mech_type->gm_ops
		->gss_verify_mic(context_handle,
				 message,
				 mic_token,
				 qstate);
}

/* gss_delete_sec_context: free all resources associated with context_handle.
 * Note this differs from the RFC 2744-specified prototype in that we don't
 * bother returning an output token, since it would never be used anyway. */

u32
gss_delete_sec_context(struct gss_ctx	**context_handle)
{
	dprintk("gss_delete_sec_context deleting %p\n",*context_handle);

	if (!*context_handle)
		return(GSS_S_NO_CONTEXT);
	if ((*context_handle)->internal_ctx_id != 0)
		(*context_handle)->mech_type->gm_ops
			->gss_delete_sec_context((*context_handle)
							->internal_ctx_id);
	if ((*context_handle)->mech_type)
		gss_mech_put((*context_handle)->mech_type);
	kfree(*context_handle);
	*context_handle=NULL;
	return GSS_S_COMPLETE;
}
