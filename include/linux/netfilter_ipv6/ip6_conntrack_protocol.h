/*
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: include/linux/netfilter_ipv4/ip_conntrack_protocol.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
/* Header for use in defining a given protocol for connection tracking. */
#ifndef _IP6_CONNTRACK_PROTOCOL_H
#define _IP6_CONNTRACK_PROTOCOL_H
#include <linux/netfilter_ipv6/ip6_conntrack.h>
#include <linux/skbuff.h>

struct ip6_conntrack_protocol
{
	/* Next pointer. */
	struct list_head list;

	/* Protocol number. */
	u_int8_t proto;

	/* Protocol name */
	const char *name;

	/* Try to fill in the third arg: dataoff is offset past IPv6
	   hdr and IPv6 ext hdrs. Return true if possible. */
	int (*pkt_to_tuple)(const struct sk_buff *skb,
			   unsigned int dataoff,
			   struct ip6_conntrack_tuple *tuple);

	/* Invert the per-proto part of the tuple: ie. turn xmit into reply.
	 * Some packets can't be inverted: return 0 in that case.
	 */
	int (*invert_tuple)(struct ip6_conntrack_tuple *inverse,
			    const struct ip6_conntrack_tuple *orig);

	/* Print out the per-protocol part of the tuple. */
	unsigned int (*print_tuple)(char *buffer,
				    const struct ip6_conntrack_tuple *);

	/* Print out the private part of the conntrack. */
	unsigned int (*print_conntrack)(char *buffer,
					const struct ip6_conntrack *);

	/* Returns verdict for packet, or -1 for invalid. */
	int (*packet)(struct ip6_conntrack *conntrack,
		      const struct sk_buff *skb,
		      unsigned int dataoff,
		      enum ip6_conntrack_info ctinfo);

	/* Called when a new connection for this protocol found;
	 * returns TRUE if it's OK.  If so, packet() called next. */
	int (*new)(struct ip6_conntrack *conntrack, const struct sk_buff *skb,
		   unsigned int dataoff);

	/* Called when a conntrack entry is destroyed */
	void (*destroy)(struct ip6_conntrack *conntrack);

	/* Has to decide if a expectation matches one packet or not */
	int (*exp_matches_pkt)(struct ip6_conntrack_expect *exp,
			       const struct sk_buff *skb,
			       unsigned int dataoff);

	/* Module (if any) which this is connected to. */
	struct module *me;
};

/* Protocol registration. */
extern int ip6_conntrack_protocol_register(struct ip6_conntrack_protocol *proto);
extern void ip6_conntrack_protocol_unregister(struct ip6_conntrack_protocol *proto);

/* Existing built-in protocols */
extern struct ip6_conntrack_protocol ip6_conntrack_protocol_tcp;
extern struct ip6_conntrack_protocol ip6_conntrack_protocol_udp;
extern struct ip6_conntrack_protocol ip6_conntrack_protocol_icmpv6;
extern int ip6_conntrack_protocol_tcp_init(void);
#endif /*_IP6_CONNTRACK_PROTOCOL_H*/
