/*
 *  linux/net/sunrpc/gss_union.c
 *
 *  Adapted from MIT Kerberos 5-1.2.1 lib/gssapi/generic code
 *
 *  Copyright (c) 2001 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson   <andros@umich.edu>
 *
 */

/*
 * Copyright 1993 by OpenVision Technologies, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */ 

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/sunrpc/gss_asn1.h>
#include <linux/sunrpc/auth_gss.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY        RPCDBG_AUTH
#endif

static LIST_HEAD(registered_triples);
static spinlock_t registered_triples_lock = SPIN_LOCK_UNLOCKED;

/* The following must be called with spinlock held: */
static struct sup_sec_triple *
do_lookup_triple_by_pseudoflavor(u32 pseudoflavor)
{
	struct sup_sec_triple *pos, *triple = NULL;

	list_for_each_entry(pos, &registered_triples, triples) {
		if (pos->pseudoflavor == pseudoflavor) {
			triple = pos;
			break;
		}
	}
	return triple;
}

/* XXX Need to think about reference counting of triples and of mechs.
 * Currently we do no reference counting of triples, and I think that's
 * probably OK given the reference counting on mechs, but there's probably
 * a better way to do all this. */

int
gss_register_triple(u32 pseudoflavor, struct gss_api_mech *mech,
			  u32 qop, u32 service)
{
	struct sup_sec_triple *triple;

	if (!(triple = kmalloc(sizeof(*triple), GFP_KERNEL))) {
		printk("Alloc failed in gss_register_triple");
		goto err;
	}
	triple->pseudoflavor = pseudoflavor;
	triple->mech = gss_mech_get_by_OID(&mech->gm_oid);
	triple->qop = qop;
	triple->service = service;

	spin_lock(&registered_triples_lock);
	if (do_lookup_triple_by_pseudoflavor(pseudoflavor)) {
		printk("Registered pseudoflavor %d again\n", pseudoflavor);
		goto err_unlock;
	}
	list_add(&triple->triples, &registered_triples);
	spin_unlock(&registered_triples_lock);
	dprintk("RPC: registered pseudoflavor %d\n", pseudoflavor);

	return 0;

err_unlock:
	spin_unlock(&registered_triples_lock);
err:
	return -1;
}

int
gss_unregister_triple(u32 pseudoflavor)
{
	struct sup_sec_triple *triple;

	spin_lock(&registered_triples_lock);
	if (!(triple = do_lookup_triple_by_pseudoflavor(pseudoflavor))) {
		spin_unlock(&registered_triples_lock);
		printk("Can't unregister unregistered pseudoflavor %d\n",
		       pseudoflavor);
		return -1;
	}
	list_del(&triple->triples);
	spin_unlock(&registered_triples_lock);
	gss_mech_put(triple->mech);
	kfree(triple);
	return 0;

}

void
print_sec_triple(struct xdr_netobj *oid,u32 qop,u32 service)
{
	dprintk("RPC: print_sec_triple:\n");
	dprintk("                     oid_len %d\n  oid :\n",oid->len);
	print_hexl((u32 *)oid->data,oid->len,0);
	dprintk("                     qop %d\n",qop);
	dprintk("                     service %d\n",service);
}

/* Function: gss_get_cmp_triples
 *
 * Description: search sec_triples for a matching security triple
 * return pseudoflavor if match, else 0
 * (Note that 0 is a valid pseudoflavor, but not for any gss pseudoflavor
 * (0 means auth_null), so this shouldn't cause confusion.)
 */
u32
gss_cmp_triples(u32 oid_len, char *oid_data, u32 qop, u32 service)
{
	struct sup_sec_triple *triple;
	u32 pseudoflavor = 0;
	struct xdr_netobj oid;

	oid.len = oid_len;
	oid.data = oid_data;

	dprintk("RPC: gss_cmp_triples \n");
	print_sec_triple(&oid,qop,service);

	spin_lock(&registered_triples_lock);
	list_for_each_entry(triple, &registered_triples, triples) {
		if((g_OID_equal(&oid, &triple->mech->gm_oid))
		    && (qop == triple->qop)
		    && (service == triple->service)) {
			pseudoflavor = triple->pseudoflavor;
			break;
		}
	}
	spin_unlock(&registered_triples_lock);
	dprintk("RPC: gss_cmp_triples return %d\n", pseudoflavor);
	return pseudoflavor;
}

u32
gss_get_pseudoflavor(struct gss_ctx *ctx, u32 qop, u32 service)
{
	return gss_cmp_triples(ctx->mech_type->gm_oid.len,
			       ctx->mech_type->gm_oid.data,
			       qop, service);
}

/* Returns nonzero iff the given pseudoflavor is in the supported list.
 * (Note that without incrementing a reference count or anything, this
 * doesn't give any guarantees.) */
int
gss_pseudoflavor_supported(u32 pseudoflavor)
{
	struct sup_sec_triple *triple;

	spin_lock(&registered_triples_lock);
	triple = do_lookup_triple_by_pseudoflavor(pseudoflavor);
	spin_unlock(&registered_triples_lock);
	return (triple ? 1 : 0);
}

u32
gss_pseudoflavor_to_service(u32 pseudoflavor)
{
	struct sup_sec_triple *triple;

	spin_lock(&registered_triples_lock);
	triple = do_lookup_triple_by_pseudoflavor(pseudoflavor);
	spin_unlock(&registered_triples_lock);
	if (!triple) {
		dprintk("RPC: gss_pseudoflavor_to_service called with"
			" unsupported pseudoflavor %d\n", pseudoflavor);
		return 0;
	}
	return triple->service;
}

struct gss_api_mech *
gss_pseudoflavor_to_mech(u32 pseudoflavor) {
	struct sup_sec_triple *triple;
	struct gss_api_mech *mech = NULL;

	spin_lock(&registered_triples_lock);
	triple = do_lookup_triple_by_pseudoflavor(pseudoflavor);
	spin_unlock(&registered_triples_lock);
	if (triple)
		mech = gss_mech_get(triple->mech);
	else
		dprintk("RPC: gss_pseudoflavor_to_mech called with"
			" unsupported pseudoflavor %d\n", pseudoflavor);
	return mech;
}

int
gss_pseudoflavor_to_mechOID(u32 pseudoflavor, struct xdr_netobj * oid)
{
	struct gss_api_mech *mech;

	mech = gss_pseudoflavor_to_mech(pseudoflavor);
	if (!mech)  {
		dprintk("RPC: gss_pseudoflavor_to_mechOID called with"
			" unsupported pseudoflavor %d\n", pseudoflavor);
		        return -1;
	}
	oid->len = mech->gm_oid.len;
	if (!(oid->data = kmalloc(oid->len, GFP_KERNEL)))
		return -1;
	memcpy(oid->data, mech->gm_oid.data, oid->len);
	gss_mech_put(mech);
	return 0;
}
