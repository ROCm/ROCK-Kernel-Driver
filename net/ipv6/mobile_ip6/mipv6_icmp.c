/**
 * Generic icmp routines
 *
 * Authors:
 * Jaakko Laine <medved@iki.fi>,
 * Ville Nuorvala <vnuorval@tcs.hut.fi> 
 *
 * $Id: s.mipv6_icmp.c 1.35 03/08/26 13:56:40+03:00 henkku@tcs-pc-5.tcs.hut.fi $
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/icmpv6.h>
#include <net/checksum.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/mipv6.h>
#include <net/mipglue.h>
#include <linux/module.h>

#include "debug.h"
#include "bcache.h"
#include "mipv6_icmp.h"
#include "config.h"

struct mipv6_icmpv6_msg {
	struct icmp6hdr icmph;
	__u8 *data;
	struct in6_addr *saddr;
	struct in6_addr *daddr;
	int len;
	__u32 csum;
};

#define MIPV6_ICMP_HOP_LIMIT 64

static struct socket *mipv6_icmpv6_socket = NULL;
static __u16 identifier = 0;

int mipv6_icmpv6_no_rcv(struct sk_buff *skb)
{
	return 0;
}

static int mipv6_icmpv6_xmit_holder = -1;

static int mipv6_icmpv6_xmit_lock_bh(void)
{
	if (!spin_trylock(&mipv6_icmpv6_socket->sk->sk_lock.slock)) {
		if (mipv6_icmpv6_xmit_holder == smp_processor_id())
			return -EAGAIN;
		spin_lock(&mipv6_icmpv6_socket->sk->sk_lock.slock);
	}
	mipv6_icmpv6_xmit_holder = smp_processor_id();
	return 0;
}

static __inline__ int mipv6_icmpv6_xmit_lock(void)
{
	int ret;
	local_bh_disable();
	ret = mipv6_icmpv6_xmit_lock_bh();
	if (ret)
		local_bh_enable();
	return ret;
}

static void mipv6_icmpv6_xmit_unlock_bh(void)
{
	mipv6_icmpv6_xmit_holder = -1;
	spin_unlock(&mipv6_icmpv6_socket->sk->sk_lock.slock);
}

static __inline__ void mipv6_icmpv6_xmit_unlock(void)
{
	mipv6_icmpv6_xmit_unlock_bh();
	local_bh_enable();
}


/**
 * mipv6_icmpv6_dest_unreach - Destination Unreachable ICMP error message handler
 * @skb: buffer containing ICMP error message
 *
 * Special Mobile IPv6 ICMP handling.  If Correspondent Node receives
 * persistent ICMP Destination Unreachable messages for a destination
 * in its Binding Cache, the binding should be deleted.  See draft
 * section 8.8.
 **/
static int mipv6_icmpv6_rcv_dest_unreach(struct sk_buff *skb)
{
	struct icmp6hdr *icmph = (struct icmp6hdr *) skb->h.raw;
	struct ipv6hdr *ipv6h = (struct ipv6hdr *) (icmph + 1);
	int left = (skb->tail - skb->h.raw) - sizeof(*icmph)- sizeof(ipv6h);
	struct ipv6_opt_hdr *eh;
	struct rt2_hdr *rt2h = NULL;
	struct in6_addr *daddr = &ipv6h->daddr;
	struct in6_addr *saddr = &ipv6h->saddr;
	int hdrlen, nexthdr = ipv6h->nexthdr;
	struct mipv6_bce bce;
	DEBUG_FUNC();

	eh = (struct ipv6_opt_hdr *) (ipv6h + 1);

	while (left > 0) {
		if (nexthdr != NEXTHDR_HOP && nexthdr != NEXTHDR_DEST && 
		    nexthdr != NEXTHDR_ROUTING)
			return 0;

		hdrlen = ipv6_optlen(eh);
		if (hdrlen > left)
			return 0;

		if (nexthdr == NEXTHDR_ROUTING) {
			struct ipv6_rt_hdr *rth = (struct ipv6_rt_hdr *) eh;

			if (rth->type == IPV6_SRCRT_TYPE_2) {
				if (hdrlen != sizeof(struct rt2_hdr))
					return 0;

				rt2h = (struct rt2_hdr *) rth;

				if (rt2h->rt_hdr.segments_left > 0)
					daddr = &rt2h->addr;
				break;
			}
		}
		/* check for home address option in case this node is a MN */
		if (nexthdr == NEXTHDR_DEST) {
			__u8 *raw = (__u8 *) eh;
			__u16 i = 2;
			while (1) {
				struct mipv6_dstopt_homeaddr *hao;
				
				if (i + sizeof (*hao) > hdrlen)
					break;
				
				hao = (struct mipv6_dstopt_homeaddr *) &raw[i];
				
				if (hao->type == MIPV6_TLV_HOMEADDR &&
				    hao->length == sizeof(struct in6_addr)) {
					saddr = &hao->addr;
					break;
				}
				if (hao->type)
					i += hao->length + 2;
				else
					i++;
			}
			
		}
		nexthdr = eh->nexthdr;
		eh = (struct ipv6_opt_hdr *) ((u8 *) eh + hdrlen);
		left -= hdrlen;
	}
	if (rt2h == NULL) return 0;

	if (mipv6_bcache_get(daddr, saddr, &bce) == 0 && !(bce.flags&HOME_REGISTRATION)) {
		/* A primitive algorithm for detecting persistent ICMP destination unreachable messages */
		if (bce.destunr_count) {
			if (time_before(jiffies, bce.last_destunr + MIPV6_DEST_UNR_IVAL*HZ)) 
				bce.destunr_count++;
			else bce.destunr_count = 0;
		}
		else
			bce.destunr_count = 1;
		bce.last_destunr = jiffies;

		if (bce.destunr_count > MIPV6_MAX_DESTUNREACH && mipv6_bcache_delete(daddr, saddr, CACHE_ENTRY) == 0) {
			DEBUG(DBG_INFO, "Deleted bcache entry "
			      "%x:%x:%x:%x:%x:%x:%x:%x "
			      "%x:%x:%x:%x:%x:%x:%x:%x (reason: "
			      "%d dest unreachables) ",
			      NIPV6ADDR(daddr), NIPV6ADDR(saddr), bce.destunr_count);
		}
	}
	return 0;
}

static int mipv6_icmpv6_getfrag(const void *data, struct in6_addr *saddr, 
				char *buff, unsigned int offset, 
				unsigned int len)
{
	struct mipv6_icmpv6_msg *msg = (struct mipv6_icmpv6_msg *) data;
	struct icmp6hdr *icmph;
	__u32 csum;

	if (offset) {
		msg->csum = csum_partial_copy_nocheck(msg->data + offset -
						      sizeof(*icmph), buff,
						      len, msg->csum);
		return 0;
	}
	
	csum = csum_partial_copy_nocheck((__u8 *) &msg->icmph, buff,
					 sizeof(*icmph), msg->csum);
	
	csum = csum_partial_copy_nocheck(msg->data, buff + sizeof(*icmph),
					 len - sizeof(*icmph), csum);
	
	icmph = (struct icmp6hdr *) buff;
	
	icmph->icmp6_cksum = csum_ipv6_magic(saddr, msg->daddr, msg->len,
					     IPPROTO_ICMPV6, csum);
	return 0; 
}

extern int ip6_build_xmit(struct sock *sk, inet_getfrag_t getfrag,
		const void *data, struct flowi *fl, unsigned length,
		struct ipv6_txoptions *opt, int hlimit, int flags);

/**
 * mipv6_icmpv6_send - generic icmpv6 message send
 * @daddr: destination address
 * @saddr: source address
 * @type: icmp type
 * @code: icmp code
 * @id: packet identifier. If null, uses internal counter to get new id
 * @data: packet data
 * @datalen: length of data in bytes
 */
void mipv6_icmpv6_send(struct in6_addr *daddr, struct in6_addr *saddr, int type,
		       int code, __u16 *id, __u16 flags, void *data, int datalen)
{
	struct inet6_dev *idev = NULL;
	struct sock *sk = mipv6_icmpv6_socket->sk;
	struct flowi fl;
	struct mipv6_icmpv6_msg msg;
	struct icmp6hdr tmp_hdr;

	DEBUG_FUNC();

	fl.proto = IPPROTO_ICMPV6;
	ipv6_addr_copy(&fl.fl6_dst, daddr);
	ipv6_addr_copy(&fl.fl6_src, saddr);
	fl.fl6_flowlabel = 0;
	fl.uli_u.icmpt.type = type;
	fl.uli_u.icmpt.code = code;

	msg.icmph.icmp6_type = type;
	msg.icmph.icmp6_code = code;
	msg.icmph.icmp6_cksum = 0;

	if (id)
		msg.icmph.icmp6_identifier = htons(*id);
	else
		msg.icmph.icmp6_identifier = htons(identifier++);

	msg.icmph.icmp6_sequence = htons(flags);
	msg.data = data;
	msg.csum = 0;
	msg.len = datalen + sizeof(struct icmp6hdr);
	msg.daddr = daddr;
	msg.saddr = saddr;

	/* Duplicate fields */
	tmp_hdr.icmp6_type = type;
	tmp_hdr.icmp6_code = code;
	tmp_hdr.icmp6_cksum = 0;
	tmp_hdr.icmp6_pointer = htonl(msg.icmph.icmp6_identifier);

	if (mipv6_icmpv6_xmit_lock())
		return;

	ip6_build_xmit(sk, mipv6_icmpv6_getfrag, &msg, &fl, msg.len, NULL, -1,
			MSG_DONTWAIT);
        if (likely(idev != NULL))
                in6_dev_put(idev);

	// KK ICMP6_INC_STATS_BH(Icmp6OutMsgs);

	mipv6_icmpv6_xmit_unlock();
}

/**
 * icmp6_rcv - ICMPv6 receive and multiplex
 * @skb: buffer containing ICMP message
 *
 * Generic ICMPv6 receive function to multiplex messages to approriate
 * handlers.  Only used for ICMP messages with special handling in
 * Mobile IPv6.
 **/
static void icmp6_rcv(struct sk_buff *skb)
{
	struct icmp6hdr *hdr;

	if (skb_is_nonlinear(skb) &&
	    skb_linearize(skb, GFP_ATOMIC) != 0) {
		kfree_skb(skb);
		return;
	}
	__skb_push(skb, skb->data-skb->h.raw);

	hdr = (struct icmp6hdr *) skb->h.raw;

	switch (hdr->icmp6_type) {
	case ICMPV6_DEST_UNREACH:
		mipv6_icmpv6_rcv_dest_unreach(skb);
		break;

	case ICMPV6_PARAMPROB:
		mip6_fn.icmpv6_paramprob_rcv(skb);
		break;

	case ICMPV6_DHAAD_REPLY:
		mip6_fn.icmpv6_dhaad_rep_rcv(skb);
		break;

	case ICMPV6_MOBILE_PREFIX_ADV:
		mip6_fn.icmpv6_pfxadv_rcv(skb);
		break;

	case ICMPV6_DHAAD_REQUEST:
		mip6_fn.icmpv6_dhaad_req_rcv(skb);
		break;

	case ICMPV6_MOBILE_PREFIX_SOL:
		mip6_fn.icmpv6_pfxsol_rcv(skb);
		break;
	}
}

int mipv6_icmpv6_init(void)
{
	struct sock *sk;
	int err;

	if ((mipv6_icmpv6_socket = sock_alloc()) == NULL) {
		DEBUG(DBG_ERROR, "Cannot allocate mipv6_icmpv6_socket");
		return -1;
	}
	mipv6_icmpv6_socket->type = SOCK_RAW;

	if ((err = sock_create(PF_INET6, SOCK_RAW, IPPROTO_ICMP, 
			       &mipv6_icmpv6_socket)) < 0) {
		DEBUG(DBG_ERROR, "Cannot initialize mipv6_icmpv6_socket");
		sock_release(mipv6_icmpv6_socket);
		mipv6_icmpv6_socket = NULL; /* For safety */
		return err;
	}
	sk = mipv6_icmpv6_socket->sk;
	sk->sk_allocation = GFP_ATOMIC;
	sk->sk_prot->unhash(sk);

	/* Register our ICMP handler */
	MIPV6_SETCALL(mipv6_icmp_rcv, icmp6_rcv);
	return 0;
}

void mipv6_icmpv6_exit(void)
{
	MIPV6_RESETCALL(mipv6_icmp_rcv);
	if (mipv6_icmpv6_socket)
		sock_release(mipv6_icmpv6_socket);
	mipv6_icmpv6_socket = NULL; /* For safety */
}

EXPORT_SYMBOL(mipv6_icmpv6_no_rcv);
EXPORT_SYMBOL(mipv6_icmpv6_send);
