/*
 * linux/net/sunrpc/xdr.c
 *
 * Generic XDR support.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/in.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/msg_prot.h>

u32	rpc_success, rpc_prog_unavail, rpc_prog_mismatch, rpc_proc_unavail,
	rpc_garbage_args, rpc_system_err;
u32	rpc_auth_ok, rpc_autherr_badcred, rpc_autherr_rejectedcred,
	rpc_autherr_badverf, rpc_autherr_rejectedverf, rpc_autherr_tooweak;
u32	xdr_zero, xdr_one, xdr_two;

void
xdr_init(void)
{
	static int	inited = 0;

	if (inited)
		return;

	xdr_zero = htonl(0);
	xdr_one = htonl(1);
	xdr_two = htonl(2);

	rpc_success = htonl(RPC_SUCCESS);
	rpc_prog_unavail = htonl(RPC_PROG_UNAVAIL);
	rpc_prog_mismatch = htonl(RPC_PROG_MISMATCH);
	rpc_proc_unavail = htonl(RPC_PROC_UNAVAIL);
	rpc_garbage_args = htonl(RPC_GARBAGE_ARGS);
	rpc_system_err = htonl(RPC_SYSTEM_ERR);

	rpc_auth_ok = htonl(RPC_AUTH_OK);
	rpc_autherr_badcred = htonl(RPC_AUTH_BADCRED);
	rpc_autherr_rejectedcred = htonl(RPC_AUTH_REJECTEDCRED);
	rpc_autherr_badverf = htonl(RPC_AUTH_BADVERF);
	rpc_autherr_rejectedverf = htonl(RPC_AUTH_REJECTEDVERF);
	rpc_autherr_tooweak = htonl(RPC_AUTH_TOOWEAK);

	inited = 1;
}

/*
 * XDR functions for basic NFS types
 */
u32 *
xdr_encode_netobj(u32 *p, const struct xdr_netobj *obj)
{
	unsigned int	quadlen = XDR_QUADLEN(obj->len);

	p[quadlen] = 0;		/* zero trailing bytes */
	*p++ = htonl(obj->len);
	memcpy(p, obj->data, obj->len);
	return p + XDR_QUADLEN(obj->len);
}

u32 *
xdr_decode_netobj_fixed(u32 *p, void *obj, unsigned int len)
{
	if (ntohl(*p++) != len)
		return NULL;
	memcpy(obj, p, len);
	return p + XDR_QUADLEN(len);
}

u32 *
xdr_decode_netobj(u32 *p, struct xdr_netobj *obj)
{
	unsigned int	len;

	if ((len = ntohl(*p++)) > XDR_MAX_NETOBJ)
		return NULL;
	obj->len  = len;
	obj->data = (u8 *) p;
	return p + XDR_QUADLEN(len);
}

u32 *
xdr_encode_array(u32 *p, const char *array, unsigned int len)
{
	int quadlen = XDR_QUADLEN(len);

	p[quadlen] = 0;
	*p++ = htonl(len);
	memcpy(p, array, len);
	return p + quadlen;
}

u32 *
xdr_encode_string(u32 *p, const char *string)
{
	return xdr_encode_array(p, string, strlen(string));
}

u32 *
xdr_decode_string(u32 *p, char **sp, int *lenp, int maxlen)
{
	unsigned int	len;
	char		*string;

	if ((len = ntohl(*p++)) > maxlen)
		return NULL;
	if (lenp)
		*lenp = len;
	if ((len % 4) != 0) {
		string = (char *) p;
	} else {
		string = (char *) (p - 1);
		memmove(string, p, len);
	}
	string[len] = '\0';
	*sp = string;
	return p + XDR_QUADLEN(len);
}

/*
 * Realign the iovec if the server missed out some reply elements
 * (such as post-op attributes,...)
 * Note: This is a simple implementation that assumes that
 *            len <= iov->iov_len !!!
 *       The RPC header (assumed to be the 1st element in the iov array)
 *            is not shifted.
 */
void xdr_shift_iovec(struct iovec *iov, int nr, size_t len)
{
	struct iovec *pvec;

	for (pvec = iov + nr - 1; nr > 1; nr--, pvec--) {
		struct iovec *svec = pvec - 1;

		if (len > pvec->iov_len) {
			printk(KERN_DEBUG "RPC: Urk! Large shift of short iovec.\n");
			return;
		}
		memmove((char *)pvec->iov_base + len, pvec->iov_base,
			pvec->iov_len - len);

		if (len > svec->iov_len) {
			printk(KERN_DEBUG "RPC: Urk! Large shift of short iovec.\n");
			return;
		}
		memcpy(pvec->iov_base,
		       (char *)svec->iov_base + svec->iov_len - len, len);
	}
}

/*
 * Zero the last n bytes in an iovec array of 'nr' elements
 */
void xdr_zero_iovec(struct iovec *iov, int nr, size_t n)
{
	struct iovec *pvec;

	for (pvec = iov + nr - 1; n && nr > 0; nr--, pvec--) {
		if (n < pvec->iov_len) {
			memset((char *)pvec->iov_base + pvec->iov_len - n, 0, n);
			n = 0;
		} else {
			memset(pvec->iov_base, 0, pvec->iov_len);
			n -= pvec->iov_len;
		}
	}
}
