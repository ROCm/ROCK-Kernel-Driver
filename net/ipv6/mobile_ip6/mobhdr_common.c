/*
 *	Mobile IPv6 Mobility Header Common Functions
 *
 *	Authors:
 *	Antti Tuominen <ajtuomin@tml.hut.fi>
 *
 *      $Id: s.mh_recv.c 1.159 02/10/16 15:01:29+03:00 antti@traci.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#include <linux/autoconf.h>
#include <linux/types.h>
#include <linux/in6.h>
#include <linux/skbuff.h>
#include <linux/ipsec.h>
#include <linux/init.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/ip6_checksum.h>
#include <net/addrconf.h>
#include <net/mipv6.h>
#include <net/checksum.h>
#include <net/protocol.h>
#include <linux/module.h>

#include "stats.h"
#include "debug.h"
#include "mobhdr.h"
#include "bcache.h"
#include "rr_crypto.h"
#include "exthdrs.h"
#include "config.h"

#define MIPV6_MH_MAX MIPV6_MH_BE
struct mh_proto {
	int	(*func) (struct in6_addr *, struct in6_addr *, 
			 struct in6_addr *, struct in6_addr *, 
			 struct mipv6_mh *);
};

static struct mh_proto mh_rcv[MIPV6_MH_MAX];

int mipv6_mh_register(int type, int (*func)(
	struct in6_addr *, struct in6_addr *, 
	struct in6_addr *, struct in6_addr *, struct mipv6_mh *))
{
	if (mh_rcv[type].func != NULL)
		return -1;

	mh_rcv[type].func = func;

	return 0;
}

void mipv6_mh_unregister(int type)
{
	if (type < 0 || type > MIPV6_MH_MAX)
		return;

	mh_rcv[type].func = NULL;
}

struct socket *mipv6_mh_socket = NULL;

#if 1
/* TODO: Fix fragmentation */
static int dstopts_getfrag(
	const void *data, struct in6_addr *addr,
	char *buff, unsigned int offset, unsigned int len)
{
	memcpy(buff, data + offset, len);
	return 0;
}
#else
static int dstopts_getfrag(
	void *from, char *to, int offset, int len, int odd, struct sk_buff *skb)
{
}
#endif

struct mipv6_mh_opt *alloc_mh_opts(int totlen)
{
	struct mipv6_mh_opt *ops;

	ops = kmalloc(sizeof(*ops) + totlen, GFP_ATOMIC);
	if (ops == NULL)
		return NULL;

	memset(ops, 0, sizeof(*ops));
	ops->next_free = ops->data;
	ops->freelen = totlen;

	return ops;
}

int append_mh_opt(struct mipv6_mh_opt *ops, u8 type, u8 len, void *data)
{
	struct mipv6_mo *mo;

	if (ops->next_free == NULL) {
		DEBUG(DBG_ERROR, "No free room for option");
		return -ENOMEM;
	}
	if (ops->freelen < len + 2) {
		DEBUG(DBG_ERROR, "No free room for option");
		return -ENOMEM;
	}
	else {
		ops->freelen -= (len + 2);
		ops->totlen += (len + 2);
	}

	mo = (struct mipv6_mo *)ops->next_free;
	mo->type = type;
	mo->length = len;

	switch (type) {
	case MIPV6_OPT_ALTERNATE_COA:
		ops->alt_coa = (struct mipv6_mo_alt_coa *)mo;
		ipv6_addr_copy(&ops->alt_coa->addr, (struct in6_addr *)data);
		break;
	case MIPV6_OPT_NONCE_INDICES:
		DEBUG(DBG_INFO, "Added nonce indices pointer");
		ops->nonce_indices = (struct mipv6_mo_nonce_indices *)mo;
		ops->nonce_indices->home_nonce_i = *(__u16 *)data;
		ops->nonce_indices->careof_nonce_i = *((__u16 *)data + 1);
		break;
	case MIPV6_OPT_AUTH_DATA:
		DEBUG(DBG_INFO, "Added opt auth_data pointer");
		ops->auth_data = (struct mipv6_mo_bauth_data *)mo;
		break;
	case MIPV6_OPT_BIND_REFRESH_ADVICE:
		ops->br_advice = (struct mipv6_mo_br_advice *)mo;
		ops->br_advice->refresh_interval = htons(*(u16 *)data);
		break;
	default:
		DEBUG(DBG_ERROR, "Unknow option type");
		break;
	}

	if (ops->freelen == 0)
		ops->next_free = NULL;
	else
		ops->next_free += (len + 2);

	return 0;
}

/*
 * Calculates required padding with xn + y requirement with offset
 */
static inline int optpad(int xn, int y, int offset)
{
	return ((y - offset) & (xn - 1));
}

static int option_pad(int type, int offset)
{
	if (type == MIPV6_OPT_ALTERNATE_COA)
		return optpad(8, 6, offset); /* 8n + 6 */
	if (type == MIPV6_OPT_BIND_REFRESH_ADVICE ||
	    type == MIPV6_OPT_NONCE_INDICES)
		return optpad(2, 0, offset); /* 2n */
	return 0;
}

/*
 * Add Pad1 or PadN option to data
 */
int mipv6_add_pad(u8 *data, int n)
{
	struct mipv6_mo_padn *padn;

	if (n <= 0) return 0;
	if (n == 1) {
		*data = MIPV6_OPT_PAD1;
		return 1;
	}
	padn = (struct mipv6_mo_padn *)data;
	padn->type = MIPV6_OPT_PADN;
	padn->length = n - 2;
	memset(padn->data, 0, n - 2);
	return n;
}

/*
 * Write options to mobility header buffer
 */
static int prepare_mh_opts(u8 *optdata, int off, struct mipv6_mh_opt *ops)
{
	u8 *nextopt = optdata;
	int offset = off, pad = 0;

	if (ops == NULL) {
		nextopt = NULL;
		return -1;
	}

	if (ops->alt_coa) {
		pad = option_pad(MIPV6_OPT_ALTERNATE_COA, offset);
		nextopt += mipv6_add_pad(nextopt, pad);
		memcpy(nextopt, ops->alt_coa, sizeof(struct mipv6_mo_alt_coa));
		nextopt += sizeof(struct mipv6_mo_alt_coa);
		offset += pad + sizeof(struct mipv6_mo_alt_coa);
	}

	if (ops->br_advice) {
		pad = option_pad(MIPV6_OPT_BIND_REFRESH_ADVICE, offset);
		nextopt += mipv6_add_pad(nextopt, pad);
		memcpy(nextopt, ops->br_advice, sizeof(struct mipv6_mo_br_advice));
		nextopt += sizeof(struct mipv6_mo_br_advice);
		offset += pad + sizeof(struct mipv6_mo_br_advice);
	}

	if (ops->nonce_indices) {
		pad = option_pad(MIPV6_OPT_NONCE_INDICES, offset);
		nextopt += mipv6_add_pad(nextopt, pad);
		memcpy(nextopt, ops->nonce_indices, sizeof(struct mipv6_mo_nonce_indices));
		nextopt += sizeof(struct mipv6_mo_nonce_indices);
		offset += pad + sizeof(struct mipv6_mo_nonce_indices);
	}

	if (ops->auth_data) {
		/* This option should always be the last.  Header
		 * length must be a multiple of 8 octects, so we pad
		 * if necessary. */
		pad = optpad(8, 0, offset + ops->auth_data->length + 2);
		nextopt += mipv6_add_pad(nextopt, pad);
		memcpy(nextopt, ops->auth_data, ops->auth_data->length + 2);
		nextopt += ops->auth_data->length + 2;
	}
	nextopt = NULL;

	return 0;
}

static int calculate_mh_opts(struct mipv6_mh_opt *ops, int mh_len)
{
	int offset = mh_len;

	if (ops == NULL)
		return 0;

	if (ops->alt_coa)
		offset += sizeof(struct mipv6_mo_alt_coa)
			+ option_pad(MIPV6_OPT_ALTERNATE_COA, offset);

	if (ops->br_advice)
		offset += sizeof(struct mipv6_mo_br_advice)
			+ option_pad(MIPV6_OPT_BIND_REFRESH_ADVICE, offset);

	if (ops->nonce_indices)
		offset += sizeof(struct mipv6_mo_nonce_indices)
			+ option_pad(MIPV6_OPT_NONCE_INDICES, offset);

	if (ops->auth_data) /* no alignment */
		offset += ops->auth_data->length + 2;

	return offset - mh_len;
}

extern int ip6_build_xmit(struct sock *sk, inet_getfrag_t getfrag,
		const void *data, struct flowi *fl, unsigned length,
		struct ipv6_txoptions *opt, int hlimit, int flags);

/*
 *
 * Mobility Header Message send functions
 *
 */

/**
 * send_mh - builds and sends a MH msg
 *
 * @daddr: destination address for packet
 * @saddr: source address for packet
 * @msg_type: type of MH
 * @msg_len: message length
 * @msg: MH type specific data
 * @hao_addr: home address for home address option
 * @rth_addr: routing header address
 * @ops: mobility options
 * @parm: auth data
 *
 * Builds MH, appends the type specific msg data to the header and
 * sends the packet with a home address option, if a home address was
 * given. Returns 0, if everything succeeded and a negative error code
 * otherwise.
 **/
int send_mh(struct in6_addr *daddr, 
	    struct in6_addr *saddr, 
	    u8 msg_type, u8 msg_len, u8 *msg,
	    struct in6_addr *hao_addr,
	    struct in6_addr *rth_addr,
	    struct mipv6_mh_opt *ops,
	    struct mipv6_auth_parm *parm)
{
	struct flowi fl;
	struct mipv6_mh *mh; 
	struct sock *sk = mipv6_mh_socket->sk;
	struct ipv6_txoptions *txopt = NULL;
	int tot_len = sizeof(struct mipv6_mh) + msg_len;
	int padded_len = 0, txopt_len = 0;

	DEBUG_FUNC();
	/* Add length of options */
	tot_len += calculate_mh_opts(ops, tot_len);
	/* Needs to be a multiple of 8 octets */
	padded_len = tot_len + optpad(8, 0, tot_len);

	mh = sock_kmalloc(sk, padded_len, GFP_ATOMIC);
	if (!mh) {
		DEBUG(DBG_ERROR, "memory allocation failed");
		return -ENOMEM;
	}

	memset(&fl, 0, sizeof(fl)); 
	fl.proto = IPPROTO_MOBILITY;
	ipv6_addr_copy(&fl.fl6_dst, daddr);
	ipv6_addr_copy(&fl.fl6_src, saddr);
	fl.fl6_flowlabel = 0;
	fl.oif = sk->sk_bound_dev_if;

	if (hao_addr || rth_addr) {
		__u8 *opt_ptr;

		if (hao_addr)
			txopt_len += sizeof(struct mipv6_dstopt_homeaddr) + 6;
		if (rth_addr)
			txopt_len += sizeof(struct rt2_hdr);

		txopt_len += sizeof(*txopt);
		txopt = sock_kmalloc(sk, txopt_len, GFP_ATOMIC);
		if (txopt == NULL) {
			DEBUG(DBG_ERROR, "No socket space left");
			sock_kfree_s(sk, mh, padded_len);
			return -ENOMEM;
		}
		memset(txopt, 0, txopt_len);
		txopt->tot_len = txopt_len;
		opt_ptr = (__u8 *) (txopt + 1);
		if (hao_addr) {
			int holen = sizeof(struct mipv6_dstopt_homeaddr) + 6;
			txopt->dst1opt = (struct ipv6_opt_hdr *) opt_ptr;
			txopt->opt_flen += holen;
			opt_ptr += holen;
			mipv6_append_dst1opts(txopt->dst1opt, saddr, 
					      NULL, holen);
			txopt->mipv6_flags = MIPV6_SND_HAO;
		}
		if (rth_addr) {
			int rtlen = sizeof(struct rt2_hdr);
			txopt->srcrt2 = (struct ipv6_rt_hdr *) opt_ptr;
			txopt->opt_nflen += rtlen;
			opt_ptr += rtlen;
			mipv6_append_rt2hdr(txopt->srcrt2, rth_addr);
		}
	}

	/* Fill in the fields of MH */
	mh->payload = NEXTHDR_NONE;
	mh->length = (padded_len >> 3) - 1;	/* Units of 8 octets - 1 */
	mh->type = msg_type;
	mh->reserved = 0;
	mh->checksum = 0;

	memcpy(mh->data, msg, msg_len);
	prepare_mh_opts(mh->data + msg_len, msg_len + sizeof(*mh), ops);
	/* If BAD is present, this is already done. */
	mipv6_add_pad((u8 *)mh + tot_len, padded_len - tot_len);
	
	if (parm && parm->k_bu && ops && ops->auth_data) {
		/* Calculate the position of the authorization data before adding checksum*/
		mipv6_auth_build(parm->cn_addr, parm->coa, (__u8 *)mh, 
				 (__u8 *)mh + padded_len - MIPV6_RR_MAC_LENGTH, parm->k_bu);
	}
	/* Calculate the MH checksum */
	mh->checksum = csum_ipv6_magic(&fl.fl6_src, &fl.fl6_dst, 
				       padded_len, IPPROTO_MOBILITY,
				       csum_partial((char *)mh, padded_len, 0));

	ip6_build_xmit(sk, dstopts_getfrag, mh, &fl, padded_len, txopt, 255,
			MSG_DONTWAIT);
	/* dst cache must be cleared so RR messages can be routed through
	   different interfaces */
	sk_dst_reset(sk);

	if (txopt_len)
		sock_kfree_s(sk, txopt, txopt_len);
	sock_kfree_s(sk, mh, padded_len);
	return 0;
}

/**
 * mipv6_send_brr - send a Binding Refresh Request 
 * @saddr: source address for BRR
 * @daddr: destination address for BRR
 * @ops: mobility options
 *
 * Sends a binding request.  On a mobile node, use the mobile node's
 * home address for @saddr.  Returns 0 on success, negative on
 * failure.
 **/
int mipv6_send_brr(struct in6_addr *saddr, struct in6_addr *daddr,
		   struct mipv6_mh_opt *ops)
{
	struct mipv6_mh_brr br;

	memset(&br, 0, sizeof(br));
	/* We don't need to explicitly add a RH to brr, since it will be 
	 * included automatically, if a BCE exists 
	 */
	MIPV6_INC_STATS(n_brr_sent);
	return send_mh(daddr, saddr, MIPV6_MH_BRR, sizeof(br), (u8 *)&br,
		       NULL, NULL, ops, NULL);
}

/**
 * mipv6_send_ba - send a Binding Acknowledgement 
 * @saddr: source address for BA
 * @daddr: destination address for BA 
 * @coaddr: care-of address of MN
 * @status: status field value
 * @sequence: sequence number from BU
 * @lifetime: granted lifetime for binding in seconds
 * @ops: mobility options
 *
 * Send a binding acknowledgement.  On a mobile node, use the mobile
 * node's home address for saddr.  Returns 0 on success, non-zero on
 * failure.
 **/
int mipv6_send_ba(struct in6_addr *saddr, struct in6_addr *daddr, 
		  struct in6_addr *coaddr, u8 status, u16 sequence, 
		  u32 lifetime, u8 *k_bu)
{
	struct mipv6_mh_ba ba;
	struct mipv6_auth_parm parm;
	struct mipv6_mh_opt *ops = NULL; 
	int ops_len = 0, ret = 0;

	memset(&ba, 0, sizeof(ba));
	
	ba.status = status;
	ba.sequence = htons(sequence);
	ba.lifetime = htons(lifetime >> 2);
	
	DEBUG(DBG_INFO, "sending a status %d BA %s authenticator to MN \n"
	      "%x:%x:%x:%x:%x:%x:%x:%x  at care of address \n" 
	      "%x:%x:%x:%x:%x:%x:%x:%x : with lifetime %d and \n" 
	      " sequence number %d",
	      status, k_bu ? "with" : "without", 
	      NIPV6ADDR(daddr), NIPV6ADDR(coaddr), lifetime, sequence);

	memset(&parm, 0, sizeof(parm));
	parm.coa = coaddr;
	parm.cn_addr = saddr;

	if (k_bu) {
		ops_len += sizeof(struct mipv6_mo_bauth_data) + 
			MIPV6_RR_MAC_LENGTH;
		parm.k_bu = k_bu;
	}

	if (mip6node_cnf.binding_refresh_advice) {
		ops_len += sizeof(struct mipv6_mo_br_advice);
	}
	if (ops_len) {
		ops = alloc_mh_opts(ops_len);
		if (ops == NULL) {
			DEBUG(DBG_WARNING, "Out of memory");
			return -ENOMEM;
		}
		if (mip6node_cnf.binding_refresh_advice > 0) {
			if (append_mh_opt(ops, MIPV6_OPT_BIND_REFRESH_ADVICE, 2,
					  &mip6node_cnf.binding_refresh_advice) < 0) {
				DEBUG(DBG_WARNING, "Adding BRA failed");
				if (ops)
					kfree(ops);
				return -ENOMEM;
			}
		}
		if (k_bu) {
			if (append_mh_opt(ops, MIPV6_OPT_AUTH_DATA,
					  MIPV6_RR_MAC_LENGTH, NULL) < 0) {
				DEBUG(DBG_WARNING, "Adding BAD failed");
				if (ops)
					kfree(ops);
				return -ENOMEM;
			}
		}
	}

	if (!ipv6_addr_cmp(coaddr, daddr))
		ret = send_mh(daddr, saddr, MIPV6_MH_BA, sizeof(ba), (u8 *)&ba,
			      NULL, NULL, ops, &parm);
	else
		ret = send_mh(daddr, saddr, MIPV6_MH_BA, sizeof(ba), (u8 *)&ba,
			      NULL, coaddr, ops, &parm);

	if (ret == 0) {
		if (status < 128) {
			MIPV6_INC_STATS(n_ba_sent);
		} else {
			MIPV6_INC_STATS(n_ban_sent);
		}
	}

	if (ops)
		kfree(ops);

	return 0;
}

/**
 * mipv6_send_be - send a Binding Error message
 * @saddr: source address for BE
 * @daddr: destination address for BE
 * @home: Home Address in offending packet (if any)
 *
 * Sends a binding error.  On a mobile node, use the mobile node's
 * home address for @saddr.  Returns 0 on success, negative on
 * failure.
 **/
int mipv6_send_be(struct in6_addr *saddr, struct in6_addr *daddr, 
		  struct in6_addr *home, __u8 status)
{
	struct mipv6_mh_be be;
	int ret = 0;

	memset(&be, 0, sizeof(be));
	be.status = status;
	if (home)
		ipv6_addr_copy(&be.home_addr, home);

	ret = send_mh(daddr, saddr, MIPV6_MH_BE, sizeof(be), (u8 *)&be,
		      NULL, NULL, NULL, NULL);
	if (ret == 0)
		MIPV6_INC_STATS(n_be_sent);

	return ret;
}

/**
 * mipv6_send_addr_test - send a HoT or CoT message
 * @saddr: source address
 * @daddr: destination address
 * @msg_type: HoT or CoT message
 * @init: HoTI or CoTI message
 *
 * Send a reply to HoTI or CoTI message. 
 **/
static int mipv6_send_addr_test(struct in6_addr *saddr,
				struct in6_addr *daddr,
				int msg_type,
				struct mipv6_mh_addr_ti *init)
{
	u_int8_t			*kgen_token = NULL;
	struct mipv6_mh_addr_test       addr_test;      
	struct mipv6_rr_nonce		*nonce;
	struct mipv6_mh_opt *ops = NULL;
	int ret = 0;

	DEBUG_FUNC();

	if ((nonce = mipv6_rr_get_new_nonce())== NULL) {
		DEBUG(DBG_WARNING, "Nonce creation failed");
		return 0;
	} 
	if (mipv6_rr_cookie_create(daddr, &kgen_token, nonce->index)) {
		DEBUG(DBG_WARNING, "No cookie");
		return 0;
	}

	addr_test.nonce_index = nonce->index;
	memcpy(addr_test.init_cookie, init->init_cookie,
			MIPV6_RR_COOKIE_LENGTH);
	memcpy(addr_test.kgen_token, kgen_token,
			MIPV6_RR_COOKIE_LENGTH);

	/* No options defined */
	ret = send_mh(daddr, saddr, msg_type, sizeof(addr_test),
		      (u8 *)&addr_test, NULL, NULL, ops, NULL);

	if (ret == 0) {
		if (msg_type == MIPV6_MH_HOT) {
			MIPV6_INC_STATS(n_hot_sent);
		} else {
			MIPV6_INC_STATS(n_cot_sent);
		}
	}

	return 0;
}

static void bc_cache_add(int ifindex, struct in6_addr *saddr,
			 struct in6_addr *daddr, struct in6_addr *haddr, 
			 struct in6_addr *coa, __u32 lifetime, __u16 sequence,
			 __u8 flags, __u8 *k_bu)
{
	__u8 ba_status = SUCCESS;

	if (lifetime >  MAX_RR_BINDING_LIFE)
		lifetime = MAX_RR_BINDING_LIFE;

	if (mipv6_bcache_add(ifindex, daddr, haddr, coa, lifetime,
			     sequence, flags, CACHE_ENTRY) != 0) {
		DEBUG(DBG_ERROR, "binding failed.");
		ba_status = INSUFFICIENT_RESOURCES;
	} 

	if (flags & MIPV6_BU_F_ACK) {
		DEBUG(DBG_INFO, "sending ack (code=%d)", ba_status);
		mipv6_send_ba(daddr, haddr, coa, ba_status, sequence,
			      lifetime, k_bu);
	}
}

static void bc_cn_home_add(
	int ifindex, struct in6_addr *saddr, struct in6_addr *daddr, 
	struct in6_addr *haddr, struct in6_addr *coa, __u32 lifetime, 
	__u16 sequence, __u8 flags, __u8 *k_bu)
{
#if 0
	mipv6_send_ba(daddr, haddr, coa, HOME_REGISTRATION_NOT_SUPPORTED,
		      sequence, lifetime, k_bu);
#endif
}

static void bc_cache_delete(struct in6_addr *daddr, struct in6_addr *haddr, 
			    struct in6_addr *coa, __u16 sequence, __u8 flags,
			    __u8 *k_bu)
{
	__u8 status = SUCCESS;

	/* Cached Care-of Address Deregistration */
	if (mipv6_bcache_exists(haddr, daddr) == CACHE_ENTRY) {
		mipv6_bcache_delete(haddr, daddr, CACHE_ENTRY);
	} else {
		DEBUG(DBG_INFO, "entry is not in cache");
		status = REASON_UNSPECIFIED;
	}
	if (flags & MIPV6_BU_F_ACK) {
		mipv6_send_ba(daddr, haddr, coa, status, sequence, 
			      0, k_bu);
	}
}

static void bc_cn_home_delete(struct in6_addr *daddr, struct in6_addr *haddr, 
			      struct in6_addr *coa, __u16 sequence, __u8 flags,
			      __u8 *k_bu)
{
#if 0
	mipv6_send_ba(daddr, haddr, coa, HOME_REGISTRATION_NOT_SUPPORTED, 
		      sequence, 0, k_bu);
#endif
}

/**
 * parse_mo_tlv - Parse TLV-encoded Mobility Options
 * @mos: pointer to Mobility Options
 * @len: total length of options
 * @opts: structure to store option pointers
 *
 * Parses Mobility Options passed in @mos.  Stores pointers in @opts
 * to all valid mobility options found in @mos.  Unknown options and
 * padding (%MIPV6_OPT_PAD1 and %MIPV6_OPT_PADN) is ignored and
 * skipped.
 **/
int parse_mo_tlv(void *mos, int len, struct mobopt *opts)
{
	struct mipv6_mo *curr = (struct mipv6_mo *)mos;
	int left = len;

	while (left > 0) {
		int optlen = 0;
		if (curr->type == MIPV6_OPT_PAD1)
			optlen = 1;
		else
			optlen = 2 + curr->length;

		if (optlen > left)
			goto bad;

		switch (curr->type) {
		case MIPV6_OPT_PAD1:
			DEBUG(DBG_DATADUMP, "MIPV6_OPT_PAD1 at %x", curr);
			break;
		case MIPV6_OPT_PADN:
			DEBUG(DBG_DATADUMP, "MIPV6_OPT_PADN at %x", curr);
			break;
		case MIPV6_OPT_ALTERNATE_COA:
			DEBUG(DBG_DATADUMP, "MIPV6_OPT_ACOA at %x", curr);
			opts->alt_coa = (struct mipv6_mo_alt_coa *)curr;
			break;
		case MIPV6_OPT_NONCE_INDICES:
			DEBUG(DBG_DATADUMP, "MIPV6_OPT_NONCE_INDICES at %x", curr);
			opts->nonce_indices = 
				(struct mipv6_mo_nonce_indices *)curr;
			break;
		case MIPV6_OPT_AUTH_DATA:
			DEBUG(DBG_DATADUMP, "MIPV6_OPT_AUTH_DATA at %x", curr);
			opts->auth_data = (struct mipv6_mo_bauth_data *)curr;
			break;
		case MIPV6_OPT_BIND_REFRESH_ADVICE:
			DEBUG(DBG_DATADUMP, "MIPV6_OPT_BIND_REFRESH_ADVICE at %x", curr);
			opts->br_advice = (struct mipv6_mo_br_advice *)curr;
			break;
		default:
			DEBUG(DBG_INFO, "MO Unknown option type %d at %x, ignoring.",
			       curr->type, curr);
			/* unknown mobility option, ignore and skip */
		}

		(u8 *)curr += optlen;
		left -= optlen;
	}

	if (left == 0)
		return 0;
 bad:
	return -1;
}

/*
 *
 * Mobility Header Message handlers
 *
 */

static int mipv6_handle_mh_testinit(struct in6_addr *cn,
				    struct in6_addr *unused,
				    struct in6_addr *saddr,
				    struct in6_addr *hao,
				    struct mipv6_mh *mh)
{
	struct mipv6_mh_addr_ti *ti = (struct mipv6_mh_addr_ti *)mh->data;
	
	DEBUG_FUNC();
	if (!mip6node_cnf.accept_ret_rout) {
		DEBUG(DBG_INFO, "Return routability administratively disabled");
		return -1;
	}
	if (mh->length < 1) {
		DEBUG(DBG_INFO, "Mobility Header length less than H/C TestInit");
		return -1;
	}

	if (hao) {
		DEBUG(DBG_INFO, "H/C TestInit has HAO, dropped.");
		return -1;
	}

	if (mh->type == MIPV6_MH_HOTI) {
		MIPV6_INC_STATS(n_hoti_rcvd);
		return mipv6_send_addr_test(cn, saddr, MIPV6_MH_HOT, ti);
	} else if (mh->type == MIPV6_MH_COTI) {
		MIPV6_INC_STATS(n_coti_rcvd);
		return mipv6_send_addr_test(cn, saddr, MIPV6_MH_COT, ti);
	} else 
		return -1; /* Impossible to get here */
}

/**
 * mipv6_handle_mh_bu - Binding Update handler
 * @src: care-of address of sender
 * @dst: our address
 * @haddr: home address of sender
 * @mh: pointer to the beginning of the Mobility Header
 *
 * Handles Binding Update. Packet and offset to option are passed.
 * Returns 0 on success, otherwise negative.
 **/
static int mipv6_handle_mh_bu(struct in6_addr *dst,
			      struct in6_addr *unused,
			      struct in6_addr *haddr, 
			      struct in6_addr *coaddr,
			      struct mipv6_mh *mh)
{
	struct mipv6_mh_bu *bu = (struct mipv6_mh_bu *)mh->data;
	int msg_len = (mh->length << 3) + 2;
	int auth = 0;
	int dereg; /* Is this deregistration? */ 

	struct mipv6_bce bc_entry;
	struct in6_addr *coa;
	__u8 *key_bu = NULL; /* RR BU authentication key */
	__u8 flags = bu->flags;
	__u16 sequence;
	__u32 lifetime;
	
	if (msg_len < (sizeof(*bu))) {
		DEBUG(DBG_INFO, "Mobility Header length less than BU");
		MIPV6_INC_STATS(n_bu_drop.invalid);
		return -1;
	}

	/* If HAO not present, CoA == HAddr */
	if (coaddr == NULL)
		coa = haddr;
	else
		coa = coaddr;

	sequence = ntohs(bu->sequence);
	if (bu->lifetime == 0xffff)
		lifetime = 0xffffffff;
	else
		lifetime = ntohs(bu->lifetime) << 2;

	dereg = (ipv6_addr_cmp(haddr, coa) == 0 || lifetime == 0);

	if (msg_len > sizeof(*bu)) {
		struct mobopt opts;
		memset(&opts, 0, sizeof(opts));
		if (parse_mo_tlv(bu + 1, msg_len - sizeof(*bu), &opts) < 0) {
			MIPV6_INC_STATS(n_bu_drop.invalid);
			return -1;
		}
		/*
		 * MIPV6_OPT_AUTH_DATA, MIPV6_OPT_NONCE_INDICES, 
		 * MIPV6_OPT_ALT_COA
		 */
		if (opts.alt_coa) {
			coa = &opts.alt_coa->addr;
			dereg = (ipv6_addr_cmp(haddr, coa) == 0 || lifetime == 0);
		}
		if (!(flags & MIPV6_BU_F_HOME)) { 
			u8 ba_status = 0;
			u8 *h_ckie  = NULL, *c_ckie = NULL; /* Home and care-of cookies */

			/* BUs to CN MUST include authorization data and nonce indices options */
			if (!opts.auth_data || !opts.nonce_indices) {
				DEBUG(DBG_WARNING,
				      "Route optimization BU without authorization material, aborting processing");
				return MH_AUTH_FAILED;
			}
			if (mipv6_rr_cookie_create(
				    haddr, &h_ckie, opts.nonce_indices->home_nonce_i) < 0) {
				DEBUG(DBG_WARNING,
				      "mipv6_rr_cookie_create failed for home cookie");
				ba_status = EXPIRED_HOME_NONCE_INDEX;
			}
			/* Don't create the care-of cookie, if MN deregisters */
			if (!dereg && mipv6_rr_cookie_create(
				    coa, &c_ckie,
				    opts.nonce_indices->careof_nonce_i) < 0) {
				DEBUG(DBG_WARNING,
				      "mipv6_rr_cookie_create failed for coa cookie");
				if (ba_status == 0)
					ba_status = EXPIRED_CAREOF_NONCE_INDEX;
				else
					ba_status = EXPIRED_NONCES;
			}
			if (ba_status == 0) {
				if (dereg)
					key_bu = mipv6_rr_key_calc(h_ckie, NULL);
				else
					key_bu = mipv6_rr_key_calc(h_ckie, c_ckie);	       
				mh->checksum = 0;/* TODO: Don't mangle the packet */
				if (key_bu && mipv6_auth_check(
					dst, coa, (__u8 *)mh,  msg_len + sizeof(*mh), opts.auth_data, key_bu) == 0) {
					DEBUG(DBG_INFO, "mipv6_auth_check OK for BU");
					auth = 1;
				} else {
					DEBUG(DBG_WARNING, 
					      "BU Authentication failed");
				}
			}
			if (h_ckie)
				kfree(h_ckie);
			if (c_ckie)
				kfree(c_ckie);
			if (ba_status != 0) {
				MIPV6_INC_STATS(n_bu_drop.auth);
				mipv6_send_ba(dst, haddr, coa, ba_status,
					      sequence, 0, NULL);
				goto out;
			}
		}

	}
	/* Require authorization option for RO, home reg is protected by IPsec */
	if (!(flags & MIPV6_BU_F_HOME) && !auth) {
		MIPV6_INC_STATS(n_bu_drop.auth);
		if (key_bu)
			kfree(key_bu);
		return MH_AUTH_FAILED;
	}


	if (mipv6_bcache_get(haddr, dst, &bc_entry) == 0) {
			/* Avoid looping binding cache entries */
		if (!ipv6_addr_cmp(&bc_entry.coa, haddr) && !ipv6_addr_cmp(&bc_entry.home_addr, coa)) {
			DEBUG(DBG_WARNING, "Looped BU, dropping the packet");
			goto out;
		}
		if ((bc_entry.flags & MIPV6_BU_F_HOME) != (flags & MIPV6_BU_F_HOME)) {
			DEBUG(DBG_INFO,
			      "Registration type change. Sending BA REG_TYPE_CHANGE_FORBIDDEN");
			mipv6_send_ba(dst, haddr, coa, REG_TYPE_CHANGE_FORBIDDEN,
				      sequence, lifetime, key_bu);
			goto out;
		}
		if (!MIPV6_SEQ_GT(sequence, bc_entry.seq)) {
			DEBUG(DBG_INFO,
			      "Sequence number mismatch. Sending BA SEQUENCE_NUMBER_OUT_OF_WINDOW");
			mipv6_send_ba(dst, haddr, coa, SEQUENCE_NUMBER_OUT_OF_WINDOW,
				      bc_entry.seq, lifetime, key_bu);
			goto out;
		}
	}

	if (!dereg) {
		int ifindex;
		struct rt6_info *rt;

		DEBUG(DBG_INFO, "calling bu_add.");
		if ((rt = rt6_lookup(haddr, dst, 0, 0)) != NULL) {
			ifindex = rt->rt6i_dev->ifindex;
			dst_release(&rt->u.dst);
		} else {
			/*
			 * Can't process the BU since the right interface is 
			 * not found.
			 */
			DEBUG(DBG_WARNING, "No route entry found for handling "
			      "a BU request, (using 0 as index)");
			ifindex = 0;
		}
		if (flags & MIPV6_BU_F_HOME)
			mip6_fn.bce_home_add(ifindex, haddr, dst, haddr, 
					     coa, lifetime, sequence, flags, 
					     key_bu);
		else
			mip6_fn.bce_cache_add(ifindex, haddr, dst, haddr, 
					      coa, lifetime, sequence, flags, 
					      key_bu);
	} else {
		DEBUG(DBG_INFO, "calling BCE delete.");

		if (flags & MIPV6_BU_F_HOME)
			mip6_fn.bce_home_del(dst, haddr, coa, sequence, 
					     flags, key_bu);
		else
			mip6_fn.bce_cache_del(dst, haddr, coa, sequence, 
					      flags, key_bu);
	}
 out:
	MIPV6_INC_STATS(n_bu_rcvd);
	if (key_bu)
		kfree(key_bu);
	return 0;
}

int mipv6_mh_rcv(struct sk_buff **skbp, unsigned int *nhoffp)
{
	struct sk_buff *skb = *skbp;
	struct inet6_skb_parm *opt = (struct inet6_skb_parm *)skb->cb;
	struct mipv6_mh *mh;
	struct in6_addr *lhome, *fhome, *lcoa = NULL, *fcoa = NULL;
	int len = ((skb->h.raw[1] + 1)<<3);
	int ret = 0;

	fhome = &skb->nh.ipv6h->saddr;
	lhome = &skb->nh.ipv6h->daddr;

	if (opt->hao != 0) {
		fcoa = (struct in6_addr *)((u8 *)skb->nh.raw + opt->hao);
	}

	if (opt->srcrt2 != 0) {
		struct rt2_hdr *rt2;
		rt2 = (struct rt2_hdr *)((u8 *)skb->nh.raw + opt->srcrt2);
		lcoa = &rt2->addr;
	}

	/* Verify checksum is correct */
	if (skb->ip_summed == CHECKSUM_HW) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		if (csum_ipv6_magic(fhome, lhome, skb->len, IPPROTO_MOBILITY,
				    skb->csum)) {
			if (net_ratelimit())
				printk(KERN_WARNING "MIPv6 MH hw checksum failed\n");
			skb->ip_summed = CHECKSUM_NONE;
		}
	}
	if (skb->ip_summed == CHECKSUM_NONE) {
		if (csum_ipv6_magic(fhome, lhome, skb->len, IPPROTO_MOBILITY,
				    skb_checksum(skb, 0, skb->len, 0))) {
			printk(KERN_WARNING "MIPv6 MH checksum failed\n");
			goto bad;
		}
	}

	if (!pskb_may_pull(skb, (skb->h.raw-skb->data) + sizeof(*mh)) ||
	    !pskb_may_pull(skb, (skb->h.raw-skb->data) + len)) {
		DEBUG(DBG_INFO, "MIPv6 MH invalid length");
		kfree_skb(skb);
		return 0;
	}

	mh = (struct mipv6_mh *) skb->h.raw;

	/* Verify there are no more headers after the MH */
	if (mh->payload != NEXTHDR_NONE) {
		DEBUG(DBG_INFO, "MIPv6 MH error");
		goto bad;
	}

	if (mh->type > MIPV6_MH_MAX) {
		/* send binding error */
		printk("Invalid mobility header type (%d)\n", mh->type);
		mipv6_send_be(lhome, fcoa ? fcoa : fhome, fcoa ? fhome : NULL, 
			      MIPV6_BE_UNKNOWN_MH_TYPE);
		goto bad;
	}
	if (mh_rcv[mh->type].func != NULL) {
		ret = mh_rcv[mh->type].func(lhome, lcoa, fhome, fcoa, mh);
	} else {
		DEBUG(DBG_INFO, "No handler for MH Type %d", mh->type);
		goto bad;
	}

	ASSERT(len == (mh->length + 1) << 3);

	skb->h.raw += (mh->length + 1) << 3;
	*nhoffp = (u8*)mh - skb->nh.raw;
	return 1;

bad:
	MIPV6_INC_STATS(n_mh_in_error);
	kfree_skb(skb);
	return 0;

}

#if LINUX_VERSION_CODE >= 0
//0x2052a
struct inet6_protocol mipv6_mh_protocol =
{
	mipv6_mh_rcv,		/* handler		*/
	NULL,			/* error control	*/
	IPPROTO_MOBILITY,	/* protocol ID		*/
};
#else
struct inet6_protocol mipv6_mh_protocol = 
{
	mipv6_mh_rcv,		/* handler		*/
	NULL,			/* error control	*/
	IPPROTO_MOBILITY,	/* protocol ID		*/
};
#endif

/*
 *
 * Code module init/exit functions
 *
 */

int __init mipv6_mh_common_init(void)
{
	struct sock *sk;
	int err;

	mip6_fn.bce_home_add = bc_cn_home_add;
	mip6_fn.bce_cache_add = bc_cache_add;
	mip6_fn.bce_home_del = bc_cn_home_delete;
	mip6_fn.bce_cache_del = bc_cache_delete;

	mipv6_mh_socket = sock_alloc();
	if (mipv6_mh_socket == NULL) {
		printk(KERN_ERR
		       "Failed to create the MIP6 MH control socket.\n");
		return -1;
	}
	mipv6_mh_socket->type = SOCK_RAW;

	if ((err = sock_create(PF_INET6, SOCK_RAW, IPPROTO_MOBILITY, 
			       &mipv6_mh_socket)) < 0) {
		printk(KERN_ERR
		       "Failed to initialize the MIP6 MH control socket (err %d).\n",
		       err);
		sock_release(mipv6_mh_socket);
		mipv6_mh_socket = NULL; /* for safety */
		return err;
	}

	sk = mipv6_mh_socket->sk;
	sk->sk_allocation = GFP_ATOMIC;
	sk->sk_sndbuf = 65536;
	sk->sk_prot->unhash(sk);

	memset(&mh_rcv, 0, sizeof(mh_rcv));
	mh_rcv[MIPV6_MH_HOTI].func = mipv6_handle_mh_testinit;
	mh_rcv[MIPV6_MH_COTI].func = mipv6_handle_mh_testinit;
	mh_rcv[MIPV6_MH_BU].func =  mipv6_handle_mh_bu;

#if LINUX_VERSION_CODE >= 0
	if (inet6_add_protocol(&mipv6_mh_protocol, IPPROTO_MOBILITY) < 0) {
		printk(KERN_ERR "Failed to register MOBILITY protocol\n");
		sock_release(mipv6_mh_socket);
		mipv6_mh_socket = NULL;
		return -EAGAIN;
	}
#else
	inet6_add_protocol(&mipv6_mh_protocol);
#endif
	/* To disable the use of dst_cache, 
	 *  which slows down the sending of BUs ??
	 */
	sk->sk_dst_cache=NULL; 

	return 0;
}

void __exit mipv6_mh_common_exit(void)
{
	if (mipv6_mh_socket) sock_release(mipv6_mh_socket);
	mipv6_mh_socket = NULL; /* For safety. */

#if LINUX_VERSION_CODE >= 0
	inet6_del_protocol(&mipv6_mh_protocol, IPPROTO_MOBILITY);
#else
	inet6_del_protocol(&mipv6_mh_protocol);
#endif
	memset(&mh_rcv, 0, sizeof(mh_rcv));
}

EXPORT_SYMBOL(send_mh);
EXPORT_SYMBOL(parse_mo_tlv);
EXPORT_SYMBOL(mipv6_send_ba);
EXPORT_SYMBOL(mipv6_mh_register);
EXPORT_SYMBOL(mipv6_mh_unregister);
EXPORT_SYMBOL(alloc_mh_opts);
EXPORT_SYMBOL(append_mh_opt);

