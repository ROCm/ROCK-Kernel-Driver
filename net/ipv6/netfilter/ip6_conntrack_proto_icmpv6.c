/*
 * ICMPv6 extension for IPv6 connection tracking
 * Linux INET6 implementation
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: net/ipv4/netfilter/ip_conntrack_proto_icmp.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/netfilter.h>
#include <linux/in.h>
#include <linux/icmpv6.h>
#include <linux/netfilter_ipv6/ip6_conntrack_protocol.h>

#define ICMPV6_TIMEOUT (30*HZ)

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static int icmpv6_pkt_to_tuple(const struct sk_buff *skb,
			       unsigned int dataoff,
			       struct ip6_conntrack_tuple *tuple)
{
	struct icmp6hdr hdr;

	if (skb_copy_bits(skb, dataoff, &hdr, sizeof(hdr)) != 0)
		return 0;
	tuple->dst.u.icmpv6.type = hdr.icmp6_type;
	tuple->src.u.icmpv6.id = hdr.icmp6_identifier;
	tuple->dst.u.icmpv6.code = hdr.icmp6_code;

	return 1;
}

static int icmpv6_invert_tuple(struct ip6_conntrack_tuple *tuple,
			       const struct ip6_conntrack_tuple *orig)
{
	/* Add 1; spaces filled with 0. */
	static u_int8_t invmap[] = {
		[ICMPV6_ECHO_REQUEST]	= ICMPV6_ECHO_REPLY + 1,
		[ICMPV6_ECHO_REPLY]	= ICMPV6_ECHO_REQUEST + 1,
		[ICMPV6_NI_QUERY]	= ICMPV6_NI_QUERY + 1,
		[ICMPV6_NI_REPLY]	= ICMPV6_NI_REPLY +1
	};

	if (orig->dst.u.icmpv6.type >= sizeof(invmap)
	    || !invmap[orig->dst.u.icmpv6.type])
		return 0;

	tuple->src.u.icmpv6.id   = orig->src.u.icmpv6.id;
	tuple->dst.u.icmpv6.type = invmap[orig->dst.u.icmpv6.type] - 1;
	tuple->dst.u.icmpv6.code = orig->dst.u.icmpv6.code;
	return 1;
}

/* Print out the per-protocol part of the tuple. */
static unsigned int icmpv6_print_tuple(char *buffer,
				     const struct ip6_conntrack_tuple *tuple)
{
	return sprintf(buffer, "type=%u code=%u id=%u ",
		       tuple->dst.u.icmpv6.type,
		       tuple->dst.u.icmpv6.code,
		       ntohs(tuple->src.u.icmpv6.id));
}

/* Print out the private part of the conntrack. */
static unsigned int icmpv6_print_conntrack(char *buffer,
				     const struct ip6_conntrack *conntrack)
{
	return sprintf(buffer, "count=%u ",
		       atomic_read(&conntrack->proto.icmpv6.count));
}

/* Returns verdict for packet, or -1 for invalid. */
static int icmpv6_packet(struct ip6_conntrack *ct,
			 const struct sk_buff *skb,
			 unsigned int dataoff,
			 enum ip6_conntrack_info ctinfo)
{
	/* Try to delete connection immediately after all replies:
           won't actually vanish as we still have skb, and del_timer
           means this will only run once even if count hits zero twice
           (theoretically possible with SMP) */
	if (CTINFO2DIR(ctinfo) == IP6_CT_DIR_REPLY) {
		if (atomic_dec_and_test(&ct->proto.icmpv6.count)
		    && del_timer(&ct->timeout))
			ct->timeout.function((unsigned long)ct);
	} else {
		atomic_inc(&ct->proto.icmpv6.count);
		ip6_ct_refresh(ct, ICMPV6_TIMEOUT);
	}

	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int icmpv6_new(struct ip6_conntrack *conntrack,
		      const struct sk_buff *skb,
		      unsigned int dataoff)
{
	static u_int8_t valid_new[] = {
		[ICMPV6_ECHO_REQUEST] = 1,
		[ICMPV6_NI_QUERY] = 1
	};

	if (conntrack->tuplehash[0].tuple.dst.u.icmpv6.type >= sizeof(valid_new)
	    || !valid_new[conntrack->tuplehash[0].tuple.dst.u.icmpv6.type]) {
		/* Can't create a new ICMPV6 `conn' with this. */
		DEBUGP("icmpv6: can't create new conn with type %u\n",
		       conntrack->tuplehash[0].tuple.dst.u.icmpv6.type);
		DUMP_TUPLE(&conntrack->tuplehash[0].tuple);
		return 0;
	}
	atomic_set(&conntrack->proto.icmpv6.count, 0);
	return 1;
}

struct ip6_conntrack_protocol ip6_conntrack_protocol_icmpv6
= { { NULL, NULL }, IPPROTO_ICMPV6, "icmpv6",
    icmpv6_pkt_to_tuple, icmpv6_invert_tuple, icmpv6_print_tuple,
    icmpv6_print_conntrack, icmpv6_packet, icmpv6_new, NULL, NULL, NULL };
