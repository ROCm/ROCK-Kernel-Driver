#ifndef __LINUX_IP_NETFILTER_H
#define __LINUX_IP_NETFILTER_H

/* IPv4-specific defines for netfilter. 
 * (C)1998 Rusty Russell -- This code is GPL.
 */

#include <linux/config.h>
#include <linux/netfilter.h>

/* IP Cache bits. */
/* Src IP address. */
#define NFC_IP_SRC		0x0001
/* Dest IP address. */
#define NFC_IP_DST		0x0002
/* Input device. */
#define NFC_IP_IF_IN		0x0004
/* Output device. */
#define NFC_IP_IF_OUT		0x0008
/* TOS. */
#define NFC_IP_TOS		0x0010
/* Protocol. */
#define NFC_IP_PROTO		0x0020
/* IP options. */
#define NFC_IP_OPTIONS		0x0040
/* Frag & flags. */
#define NFC_IP_FRAG		0x0080

/* Per-protocol information: only matters if proto match. */
/* TCP flags. */
#define NFC_IP_TCPFLAGS		0x0100
/* Source port. */
#define NFC_IP_SRC_PT		0x0200
/* Dest port. */
#define NFC_IP_DST_PT		0x0400
/* Something else about the proto */
#define NFC_IP_PROTO_UNKNOWN	0x2000

/* IP Hooks */
/* After promisc drops, checksum checks. */
#define NF_IP_PRE_ROUTING	0
/* If the packet is destined for this box. */
#define NF_IP_LOCAL_IN		1
/* If the packet is destined for another interface. */
#define NF_IP_FORWARD		2
/* Packets coming from a local process. */
#define NF_IP_LOCAL_OUT		3
/* Packets about to hit the wire. */
#define NF_IP_POST_ROUTING	4
#define NF_IP_NUMHOOKS		5

enum nf_ip_hook_priorities {
	NF_IP_PRI_FIRST = INT_MIN,
	NF_IP_PRI_CONNTRACK = -200,
	NF_IP_PRI_MANGLE = -150,
	NF_IP_PRI_NAT_DST = -100,
	NF_IP_PRI_FILTER = 0,
	NF_IP_PRI_NAT_SRC = 100,
	NF_IP_PRI_LAST = INT_MAX,
};

#ifdef CONFIG_NETFILTER_DEBUG
#ifdef __KERNEL__
void nf_debug_ip_local_deliver(struct sk_buff *skb);
void nf_debug_ip_loopback_xmit(struct sk_buff *newskb);
void nf_debug_ip_finish_output2(struct sk_buff *skb);
#endif /*__KERNEL__*/
#endif /*CONFIG_NETFILTER_DEBUG*/

/* Arguments for setsockopt SOL_IP: */
/* 2.0 firewalling went from 64 through 71 (and +256, +512, etc). */
/* 2.2 firewalling (+ masq) went from 64 through 76 */
/* 2.4 firewalling went 64 through 67. */
#define SO_ORIGINAL_DST 80


/* route_me_harder function, used by iptable_nat, iptable_mangle + ip_queue 
 *
 * Ideally this would be ins some netfilter_utility module, but creating this
 * module for just one function doesn't make sense. -HW */

#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/route.h>
#include <linux/ip.h>

static inline int route_me_harder(struct sk_buff **pskb)
{
	struct iphdr *iph = (*pskb)->nh.iph;
	struct rtable *rt;
	struct rt_key key = { dst:iph->daddr,
			      src:iph->saddr,
			      oif:(*pskb)->sk ? (*pskb)->sk->bound_dev_if : 0,
			      tos:RT_TOS(iph->tos)|RTO_CONN,
#ifdef CONFIG_IP_ROUTE_FWMARK
			      fwmark:(*pskb)->nfmark
#endif
			    };
	struct net_device *dev_src = NULL;
	int err;

	/* accomodate ip_route_output_slow(), which expects the key src to be
	   0 or a local address; however some non-standard hacks like
	   ipt_REJECT.c:send_reset() can cause packets with foreign
           saddr to be appear on the NF_IP_LOCAL_OUT hook -MB */
	if(key.src && !(dev_src = ip_dev_find(key.src)))
		key.src = 0;

	if ((err=ip_route_output_key(&rt, &key)) != 0) {
		printk("route_me_harder: ip_route_output_key(dst=%u.%u.%u.%u, src=%u.%u.%u.%u, oif=%d, tos=0x%x, fwmark=0x%lx) error %d\n",
			NIPQUAD(iph->daddr), NIPQUAD(iph->saddr),
			(*pskb)->sk ? (*pskb)->sk->bound_dev_if : 0,
			RT_TOS(iph->tos)|RTO_CONN,
#ifdef CONFIG_IP_ROUTE_FWMARK
			(*pskb)->nfmark,
#else
			0UL,
#endif
			err);
		goto out;
	}

	/* Drop old route. */
	dst_release((*pskb)->dst);

	(*pskb)->dst = &rt->u.dst;

	/* Change in oif may mean change in hh_len. */
	if (skb_headroom(*pskb) < (*pskb)->dst->dev->hard_header_len) {
		struct sk_buff *nskb;

		nskb = skb_realloc_headroom(*pskb,
					    (*pskb)->dst->dev->hard_header_len);
		if (!nskb) {
			err = -ENOMEM;
			goto out;
		}
		if ((*pskb)->sk)
			skb_set_owner_w(nskb, (*pskb)->sk);
		kfree_skb(*pskb);
		*pskb = nskb;
	}

out:
	if (dev_src)
		dev_put(dev_src);

	return err;
}

#endif /*__LINUX_IP_NETFILTER_H*/
