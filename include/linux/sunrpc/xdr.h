/*
 * include/linux/sunrpc/xdr.h
 *
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _SUNRPC_XDR_H_
#define _SUNRPC_XDR_H_

#ifdef __KERNEL__

#include <linux/uio.h>

/*
 * Buffer adjustment
 */
#define XDR_QUADLEN(l)		(((l) + 3) >> 2)

/*
 * Generic opaque `network object.' At the kernel level, this type
 * is used only by lockd.
 */
#define XDR_MAX_NETOBJ		1024
struct xdr_netobj {
	unsigned int		len;
	u8 *			data;
};

/*
 * This is the generic XDR function. rqstp is either a rpc_rqst (client
 * side) or svc_rqst pointer (server side).
 * Encode functions always assume there's enough room in the buffer.
 */
typedef int	(*kxdrproc_t)(void *rqstp, u32 *data, void *obj);

/*
 * These variables contain pre-xdr'ed values for faster operation.
 * FIXME: should be replaced by macros for big-endian machines.
 */
extern u32	xdr_zero, xdr_one, xdr_two;

extern u32	rpc_success,
		rpc_prog_unavail,
		rpc_prog_mismatch,
		rpc_proc_unavail,
		rpc_garbage_args,
		rpc_system_err;

extern u32	rpc_auth_ok,
		rpc_autherr_badcred,
		rpc_autherr_rejectedcred,
		rpc_autherr_badverf,
		rpc_autherr_rejectedverf,
		rpc_autherr_tooweak,
		rpc_autherr_dropit;

void		xdr_init(void);

/*
 * Miscellaneous XDR helper functions
 */
u32 *	xdr_encode_array(u32 *p, const char *s, unsigned int len);
u32 *	xdr_encode_string(u32 *p, const char *s);
u32 *	xdr_decode_string(u32 *p, char **sp, int *lenp, int maxlen);
u32 *	xdr_encode_netobj(u32 *p, const struct xdr_netobj *);
u32 *	xdr_decode_netobj(u32 *p, struct xdr_netobj *);
u32 *	xdr_decode_netobj_fixed(u32 *p, void *obj, unsigned int len);

/*
 * Decode 64bit quantities (NFSv3 support)
 */
static inline u32 *
xdr_encode_hyper(u32 *p, __u64 val)
{
	*p++ = htonl(val >> 32);
	*p++ = htonl(val & 0xFFFFFFFF);
	return p;
}

static inline u32 *
xdr_decode_hyper(u32 *p, __u64 *valp)
{
	*valp  = ((__u64) ntohl(*p++)) << 32;
	*valp |= ntohl(*p++);
	return p;
}

/*
 * Adjust iovec to reflect end of xdr'ed data (RPC client XDR)
 */
static inline int
xdr_adjust_iovec(struct iovec *iov, u32 *p)
{
	return iov->iov_len = ((u8 *) p - (u8 *) iov->iov_base);
}

void xdr_shift_iovec(struct iovec *, int, size_t);
void xdr_zero_iovec(struct iovec *, int, size_t);

#endif /* __KERNEL__ */

#endif /* _SUNRPC_XDR_H_ */
