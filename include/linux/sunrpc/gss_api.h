/*
 * linux/include/linux/gss_api.h
 *
 * Somewhat simplified version of the gss api.
 *
 * Dug Song <dugsong@monkey.org>
 * Andy Adamson <andros@umich.edu>
 * Bruce Fields <bfields@umich.edu>
 * Copyright (c) 2000 The Regents of the University of Michigan
 *
 * $Id$
 */

#ifndef _LINUX_SUNRPC_GSS_API_H
#define _LINUX_SUNRPC_GSS_API_H

#ifdef __KERNEL__
#include <linux/sunrpc/xdr.h>

/* The mechanism-independent gss-api context: */
struct gss_ctx {
	struct gss_api_mech	*mech_type;
	void			*internal_ctx_id;
};

#define GSS_C_NO_BUFFER		((struct xdr_netobj) 0)
#define GSS_C_NO_CONTEXT	((struct gss_ctx *) 0)
#define GSS_C_NULL_OID		((struct xdr_netobj) 0)

/*XXX  arbitrary length - is this set somewhere? */
#define GSS_OID_MAX_LEN 32

/* gss-api prototypes; note that these are somewhat simplified versions of
 * the prototypes specified in RFC 2744. */
u32 gss_import_sec_context(
		struct xdr_netobj	*input_token,
		struct gss_api_mech	*mech,
		struct gss_ctx		**ctx_id);
u32 gss_get_mic(
		struct gss_ctx		*ctx_id,
		u32			qop,
		struct xdr_netobj	*message,
		struct xdr_netobj	*mic_token);
u32 gss_verify_mic(
		struct gss_ctx		*ctx_id,
		struct xdr_netobj	*message,
		struct xdr_netobj	*mic_token,
		u32			*qstate);
u32 gss_delete_sec_context(
		struct gss_ctx		**ctx_id);

/* We maintain a list of the pseudoflavors (equivalently, mechanism-qop-service
 * triples) that we currently support: */

struct sup_sec_triple {
	struct list_head	triples;
	u32			pseudoflavor;
	struct gss_api_mech	*mech;
	u32			qop;
	u32			service;
};

int gss_register_triple(u32 pseudoflavor, struct gss_api_mech *mech, u32 qop,
			u32 service);
int gss_unregister_triple(u32 pseudoflavor);
int gss_pseudoflavor_supported(u32 pseudoflavor);
u32 gss_cmp_triples(u32 oid_len, char *oid_data, u32 qop, u32 service);
u32 gss_get_pseudoflavor(struct gss_ctx *ctx_id, u32 qop, u32 service);
u32 gss_pseudoflavor_to_service(u32 pseudoflavor);
/* Both return NULL on failure: */
struct gss_api_mech * gss_pseudoflavor_to_mech(u32 pseudoflavor);
int gss_pseudoflavor_to_mechOID(u32 pseudoflavor, struct xdr_netobj *mech);

/* Different mechanisms (e.g., krb5 or spkm3) may implement gss-api, and
 * mechanisms may be dynamically registered or unregistered by modules.
 * Our only built-in mechanism is a trivial debugging mechanism that provides
 * no actual security; the following function registers that mechanism: */

void gss_mech_register_debug(void);

/* Each mechanism is described by the following struct: */
struct gss_api_mech {
	struct xdr_netobj	gm_oid;
	struct list_head	gm_list;
	atomic_t		gm_count;
	struct gss_api_ops	*gm_ops;
};

/* and must provide the following operations: */
struct gss_api_ops {
	char *name;
	u32 (*gss_import_sec_context)(
			struct xdr_netobj	*input_token,
			struct gss_ctx		*ctx_id);
	u32 (*gss_get_mic)(
			struct gss_ctx		*ctx_id,
			u32			qop, 
			struct xdr_netobj	*message,
			struct xdr_netobj	*mic_token);
	u32 (*gss_verify_mic)(
			struct gss_ctx		*ctx_id,
			struct xdr_netobj	*message,
			struct xdr_netobj	*mic_token,
			u32			*qstate);
	void (*gss_delete_sec_context)(
			void			*internal_ctx_id);
};

/* Returns nonzero on failure. */
int gss_mech_register(struct xdr_netobj *, struct gss_api_ops *);

/* Returns nonzero iff someone still has a reference to this mech. */
int gss_mech_unregister(struct gss_api_mech *);

/* Returns nonzer iff someone still has a reference to some mech. */
int gss_mech_unregister_all(void);

/* returns a mechanism descriptor given an OID, an increments the mechanism's
 * reference count. */
struct gss_api_mech * gss_mech_get_by_OID(struct xdr_netobj *);

/* Just increments the mechanism's reference count and returns its input: */
struct gss_api_mech * gss_mech_get(struct gss_api_mech *);

/* Returns nonzero iff you've released the last reference to this mech.
 * Note that for every succesful gss_get_mech call there must be exactly
 * one corresponding call to gss_mech_put.*/
int gss_mech_put(struct gss_api_mech *);

#endif /* __KERNEL__ */
#endif /* _LINUX_SUNRPC_GSS_API_H */

