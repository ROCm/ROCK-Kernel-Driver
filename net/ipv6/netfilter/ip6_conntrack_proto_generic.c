/*
 * IPv6 generic protocol extension for IPv6 connection tracking
 * Linux INET6 implementation
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: net/ipv4/netfilter/ip_conntrack_proto_generic.c
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
#include <linux/netfilter_ipv6/ip6_conntrack_protocol.h>

#define GENERIC_TIMEOUT (600*HZ)

static int generic_pkt_to_tuple(const struct sk_buff *skb,
				unsigned int dataoff,
				struct ip6_conntrack_tuple *tuple)
{
	tuple->src.u.all = 0;
	tuple->dst.u.all = 0;

	return 1;
}

static int generic_invert_tuple(struct ip6_conntrack_tuple *tuple,
				const struct ip6_conntrack_tuple *orig)
{
	tuple->src.u.all = 0;
	tuple->dst.u.all = 0;

	return 1;
}

/* Print out the per-protocol part of the tuple. */
static unsigned int generic_print_tuple(char *buffer,
					const struct ip6_conntrack_tuple *tuple)
{
	return 0;
}

/* Print out the private part of the conntrack. */
static unsigned int generic_print_conntrack(char *buffer,
					    const struct ip6_conntrack *state)
{
	return 0;
}

/* Returns verdict for packet, or -1 for invalid. */
static int established(struct ip6_conntrack *conntrack,
		       const struct sk_buff *skb,
		       unsigned int dataoff,
		       enum ip6_conntrack_info conntrackinfo)
{
	ip6_ct_refresh(conntrack, GENERIC_TIMEOUT);
	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int
new(struct ip6_conntrack *conntrack,
    const struct sk_buff *skb,
    unsigned int dataoff)
{
	return 1;
}

struct ip6_conntrack_protocol ip6_conntrack_generic_protocol
= { { NULL, NULL }, 0, "unknown",
    generic_pkt_to_tuple, generic_invert_tuple, generic_print_tuple,
    generic_print_conntrack, established, new, NULL, NULL, NULL };

