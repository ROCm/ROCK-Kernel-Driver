/* SCTP kernel reference Implementation
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 * Copyright (c) 2002 International Business Machines, Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * SCTP over IPv6.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    Le Yanqun             <yanqun.le@nokia.com>
 *    Hui Huang		    <hui.huang@nokia.com>
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *
 * Based on:
 *      linux/net/ipv6/tcp_ipv6.c
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/ipsec.h>

#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/random.h>

#include <net/protocol.h>
#include <net/tcp.h>
#include <net/ndisc.h>
#include <net/ipv6.h>
#include <net/transp_v6.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/inet_common.h>
#include <net/inet_ecn.h>
#include <net/sctp/sctp.h>

#include <asm/uaccess.h>

/* FIXME: Cleanup so we don't need TEST_FRAME here. */
#ifndef TEST_FRAME
/* FIXME: Comments. */
static inline void sctp_v6_err(struct sk_buff *skb,
			       struct inet6_skb_parm *opt,
			       int type, int code, int offset, __u32 info)
{
	/* BUG.  WRITE ME.  */
}

/* Based on tcp_v6_xmit() in tcp_ipv6.c. */
static inline int sctp_v6_xmit(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct flowi fl;
	struct dst_entry *dst;
	struct in6_addr saddr;
	int err = 0;

	fl.proto = sk->protocol;
	fl.fl6_dst = &np->daddr;
	fl.fl6_src = NULL;

	fl.fl6_flowlabel = np->flow_label;
	IP6_ECN_flow_xmit(sk, fl.fl6_flowlabel);
	fl.oif = sk->bound_dev_if;
	fl.uli_u.ports.sport = inet_sk(sk)->sport;
	fl.uli_u.ports.dport = inet_sk(sk)->dport;

	if (np->opt && np->opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *) np->opt->srcrt;
		fl.nl_u.ip6_u.daddr = rt0->addr;
	}

	dst = __sk_dst_check(sk, np->dst_cookie);

	if (dst == NULL) {
		dst = ip6_route_output(sk, &fl);

		if (dst->error) {
			sk->err_soft = -dst->error;
			dst_release(dst);
			return -sk->err_soft;
		}
		ip6_dst_store(sk, dst, NULL);
	}

	skb->dst = dst_clone(dst);

	/* FIXME: This is all temporary until real source address
	 * selection is done.
	 */
	if (ipv6_addr_any(&np->saddr)) {
		err = ipv6_get_saddr(dst, fl.fl6_dst, &saddr);

		if (err)
			printk(KERN_ERR "sctp_v6_xmit: no saddr available\n");

		/* FIXME: This is a workaround until we get
		 * real source address selection done.  This is here
		 * to disallow loopback when the scoping rules have
		 * not bound loopback to the endpoint.
		 */
		if (sctp_ipv6_addr_type(&saddr) & IPV6_ADDR_LOOPBACK) {
			if (!(sctp_ipv6_addr_type(&np->daddr) &
			      IPV6_ADDR_LOOPBACK)) {
				ipv6_addr_copy(&saddr, &np->daddr);
			}
		}
		fl.fl6_src = &saddr;
	} else {
		fl.fl6_src = &np->saddr;
	}

	/* Restore final destination back after routing done */
	fl.nl_u.ip6_u.daddr = &np->daddr;

	return ip6_xmit(sk, skb, &fl, np->opt);
}
#endif /* TEST_FRAME */

/* Returns the mtu for the given v6 destination address. */
int sctp_v6_get_dst_mtu(const sockaddr_storage_t *address)
{
	struct dst_entry *dst;
	struct flowi fl;
	int dst_mtu = SCTP_DEFAULT_MAXSEGMENT;

	fl.proto = 0;
	fl.fl6_dst = (struct in6_addr *)&address->v6.sin6_addr;
	fl.fl6_src = NULL;
	fl.fl6_flowlabel = 0;
	fl.oif = 0;
	fl.uli_u.ports.sport = 0;
	fl.uli_u.ports.dport = 0;

	dst = ip6_route_output(NULL, &fl);
	if (dst) {
		dst_mtu = dst->pmtu;
		SCTP_DEBUG_PRINTK("sctp_v6_get_dst_mtu: "
				  "ip6_route_output: dev:%s pmtu:%d\n",
				  dst->dev->name, dst_mtu);
		dst_release(dst);
	} else {
		SCTP_DEBUG_PRINTK("sctp_v6_get_dst_mtu: "
				  "ip6_route_output failed, returning "
				  "%d as dst_mtu\n", dst_mtu);
	}

	return dst_mtu;
}

/* Initialize a PF_INET6 socket msg_name. */
static void sctp_inet6_msgname(char *msgname, int *addr_len)
{
	struct sockaddr_in6 *sin6;

	sin6 = (struct sockaddr_in6 *)msgname;
	sin6->sin6_family = AF_INET6;
	sin6->sin6_flowinfo = 0;
	sin6->sin6_scope_id = 0;
	*addr_len = sizeof(struct sockaddr_in6);
}

/* Initialize a PF_INET msgname from a ulpevent. */
static void sctp_inet6_event_msgname(sctp_ulpevent_t *event, char *msgname, int *addrlen)
{
	struct sockaddr_in6 *sin6, *sin6from;

	if (msgname) {
		sockaddr_storage_t *addr;

		sctp_inet6_msgname(msgname, addrlen);
		sin6 = (struct sockaddr_in6 *)msgname;
		sin6->sin6_port = htons(event->asoc->peer.port);
		addr = &event->asoc->peer.primary_addr;

		/* Note: If we go to a common v6 format, this code
		 * will change.
		 */

		/* Map ipv4 address into v4-mapped-on-v6 address.  */
		if (AF_INET == addr->sa.sa_family) {
			/* FIXME: Easy, but there was no way to test this
			 * yet.
			 */
			return;
		}

		sin6from = &event->asoc->peer.primary_addr.v6;
		ipv6_addr_copy(&sin6->sin6_addr, &sin6from->sin6_addr);
	}
}

/* Initialize a msg_name from an inbound skb. */
static void sctp_inet6_skb_msgname(struct sk_buff *skb, char *msgname,
				   int *addr_len)
{
	struct sctphdr *sh;
	struct sockaddr_in6 *sin6;

	if (msgname) {
		sctp_inet6_msgname(msgname, addr_len);
		sin6 = (struct sockaddr_in6 *)msgname;
		sh = (struct sctphdr *)skb->h.raw;
		sin6->sin6_port = sh->source;

		/* FIXME: Map ipv4 address into v4-mapped-on-v6 address. */
		if (__constant_htons(ETH_P_IP) == skb->protocol) {
			/* FIXME: Easy, but there was no way to test this
			 * yet.
			 */
			return;
		}

		/* Otherwise, just copy the v6 address. */

		ipv6_addr_copy(&sin6->sin6_addr, &skb->nh.ipv6h->saddr);
		if (ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL) {
			struct inet6_skb_parm *opt =
				(struct inet6_skb_parm *) skb->cb;
			sin6->sin6_scope_id = opt->iif;
		}
	}
}

static struct proto_ops inet6_seqpacket_ops = {
	.family     = PF_INET6,
	.release    = inet6_release,
	.bind       = inet6_bind,
	.connect    = inet_dgram_connect,
	.socketpair = sock_no_socketpair,
	.accept     = inet_accept,
	.getname    = inet6_getname,
	.poll       = sctp_poll,
	.ioctl      = inet6_ioctl,
	.listen     = sctp_inet_listen,
	.shutdown   = inet_shutdown,
	.setsockopt = inet_setsockopt,
	.getsockopt = inet_getsockopt,
	.sendmsg    = inet_sendmsg,
	.recvmsg    = inet_recvmsg,
	.mmap       = sock_no_mmap,
};

static struct inet_protosw sctpv6_protosw = {
	.type          = SOCK_SEQPACKET,
	.protocol      = IPPROTO_SCTP,
	.prot 	       = &sctp_prot,
	.ops           = &inet6_seqpacket_ops,
	.capability    = -1,
	.no_check      = 0,
	.flags         = SCTP_PROTOSW_FLAG
};

static struct inet6_protocol sctpv6_protocol = {
	.handler      = sctp_rcv,
	.err_handler  = sctp_v6_err,
	.next         = NULL,
	.protocol     = IPPROTO_SCTP,
	.copy         = 0,
	.data         = NULL,
	.name         = "SCTPv6"
};

static sctp_func_t sctp_ipv6_specific = {
	.queue_xmit      = sctp_v6_xmit,
	.setsockopt      = ipv6_setsockopt,
	.getsockopt      = ipv6_getsockopt,
	.get_dst_mtu     = sctp_v6_get_dst_mtu,
	.net_header_len  = sizeof(struct ipv6hdr),
	.sockaddr_len    = sizeof(struct sockaddr_in6),
	.sa_family       = AF_INET6,
};

static sctp_pf_t sctp_pf_inet6_specific = {
	.event_msgname = sctp_inet6_event_msgname,
	.skb_msgname   = sctp_inet6_skb_msgname,
};

/* Initialize IPv6 support and register with inet6 stack.  */
int sctp_v6_init(void)
{
	/* Add SCTPv6 to inetsw6 linked list. */
	inet6_register_protosw(&sctpv6_protosw);
	/* Register inet6 protocol. */
	inet6_add_protocol(&sctpv6_protocol);

	/* Register the SCTP specfic PF_INET6 functions. */
	sctp_set_pf_specific(PF_INET6, &sctp_pf_inet6_specific);

	/* Fill in address family info.  */
	INIT_LIST_HEAD(&sctp_ipv6_specific.list);
	list_add_tail(&sctp_ipv6_specific.list, &sctp_proto.address_families);

	return 0;
}

/* IPv6 specific exit support. */
void sctp_v6_exit(void)
{
	list_del(&sctp_ipv6_specific.list);
	inet6_del_protocol(&sctpv6_protocol);
	inet6_unregister_protosw(&sctpv6_protosw);
}
