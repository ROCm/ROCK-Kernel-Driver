/*
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: include/linux/netfilter_ipv4/ip_conntrack_core.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _IP6_CONNTRACK_CORE_H
#define _IP6_CONNTRACK_CORE_H
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4/lockhelp.h>

/* This header is used to share core functionality between the
   standalone connection tracking module, and the compatibility layer's use
   of connection tracking. */
extern unsigned int ip6_conntrack_in(unsigned int hooknum,
				    struct sk_buff **pskb,
				    const struct net_device *in,
				    const struct net_device *out,
				    int (*okfn)(struct sk_buff *));

extern int ip6_conntrack_init(void);
extern void ip6_conntrack_cleanup(void);

struct ip6_conntrack_protocol;
extern struct ip6_conntrack_protocol *ip6_ct_find_proto(u_int8_t protocol);
/* Like above, but you already have conntrack read lock. */
extern struct ip6_conntrack_protocol *__ip6_ct_find_proto(u_int8_t protocol);
extern struct list_head ip6_protocol_list;

/* Returns conntrack if it dealt with ICMP, and filled in skb->nfct */
extern struct ip6_conntrack *icmp6_error_track(struct sk_buff *skb,
					       unsigned int icmp6off,
					       enum ip6_conntrack_info *ctinfo,
					       unsigned int hooknum);
extern int ip6_get_tuple(const struct ipv6hdr *ipv6h,
			 const struct sk_buff *skb,
			 unsigned int protoff,
			 u_int8_t protonum,
			 struct ip6_conntrack_tuple *tuple,
			 const struct ip6_conntrack_protocol *protocol);

/* Find a connection corresponding to a tuple. */
struct ip6_conntrack_tuple_hash *
ip6_conntrack_find_get(const struct ip6_conntrack_tuple *tuple,
		      const struct ip6_conntrack *ignored_conntrack);

extern int __ip6_conntrack_confirm(struct nf_ct_info *nfct);

/* Confirm a connection: returns NF_DROP if packet must be dropped. */
static inline int ip6_conntrack_confirm(struct sk_buff *skb)
{
	if (skb->nfct
	    && !is_confirmed((struct ip6_conntrack *)skb->nfct->master))
		return __ip6_conntrack_confirm(skb->nfct);
	return NF_ACCEPT;
}

extern struct list_head *ip6_conntrack_hash;
extern struct list_head ip6_conntrack_expect_list;
DECLARE_RWLOCK_EXTERN(ip6_conntrack_lock);
#endif /* _IP6_CONNTRACK_CORE_H */

