/*
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: include/linux/netfilter_ipv4/ip_conntrack_icmp.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _IP6_CONNTRACK_ICMPV6_H
#define _IP6_CONNTRACK_ICMPV6_H
/* ICMPv6 tracking. */
#include <asm/atomic.h>

struct ip6_ct_icmpv6
{
	/* Optimization: when number in == number out, forget immediately. */
	atomic_t count;
};
#endif /* _IP6_CONNTRACK_ICMPv6_H */
