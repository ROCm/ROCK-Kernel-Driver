/*
 * UDP extension for IPv6 Connection Tracking
 * Linux INET6 implementation
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: net/ipv4/netfilter/ip_conntrack_proto_udp.c
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
#include <linux/udp.h>
#include <linux/netfilter_ipv6/ip6_conntrack_protocol.h>

#define UDP_TIMEOUT (30*HZ)
#define UDP_STREAM_TIMEOUT (180*HZ)

static int udp_pkt_to_tuple(const struct sk_buff *skb,
			     unsigned int dataoff,
			     struct ip6_conntrack_tuple *tuple)
{
	struct udphdr hdr;

	/* Actually only need first 8 bytes. */
	if (skb_copy_bits(skb, dataoff, &hdr, 8) != 0)
		return 0;

	tuple->src.u.udp.port = hdr.source;
	tuple->dst.u.udp.port = hdr.dest;

	return 1;
}

static int udp_invert_tuple(struct ip6_conntrack_tuple *tuple,
			    const struct ip6_conntrack_tuple *orig)
{
	tuple->src.u.udp.port = orig->dst.u.udp.port;
	tuple->dst.u.udp.port = orig->src.u.udp.port;
	return 1;
}

/* Print out the per-protocol part of the tuple. */
static unsigned int udp_print_tuple(char *buffer,
				    const struct ip6_conntrack_tuple *tuple)
{
	return sprintf(buffer, "sport=%hu dport=%hu ",
		       ntohs(tuple->src.u.udp.port),
		       ntohs(tuple->dst.u.udp.port));
}

/* Print out the private part of the conntrack. */
static unsigned int udp_print_conntrack(char *buffer,
					const struct ip6_conntrack *conntrack)
{
	return 0;
}

/* Returns verdict for packet, and may modify conntracktype */
static int udp_packet(struct ip6_conntrack *conntrack,
		      const struct sk_buff *skb,
		      unsigned int dataoff,
		      enum ip6_conntrack_info conntrackinfo)
{
	/* If we've seen traffic both ways, this is some kind of UDP
	   stream.  Extend timeout. */
	if (test_bit(IP6S_SEEN_REPLY_BIT, &conntrack->status)) {
		ip6_ct_refresh(conntrack, UDP_STREAM_TIMEOUT);
		/* Also, more likely to be important, and not a probe */
		set_bit(IP6S_ASSURED_BIT, &conntrack->status);
	} else
		ip6_ct_refresh(conntrack, UDP_TIMEOUT);

	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int udp_new(struct ip6_conntrack *conntrack, const struct sk_buff *skb,
		   unsigned int dataoff)
{
	return 1;
}

struct ip6_conntrack_protocol ip6_conntrack_protocol_udp
= { { NULL, NULL }, IPPROTO_UDP, "udp",
    udp_pkt_to_tuple, udp_invert_tuple, udp_print_tuple, udp_print_conntrack,
    udp_packet, udp_new, NULL, NULL, NULL };
