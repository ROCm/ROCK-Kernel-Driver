/*
 * Matching connection tracking information
 * Linux INET6 implementation
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: net/ipv4/netfilter/ip6t_state.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
/* Kernel module to match connection tracking information.
 * GPL (C) 1999  Rusty Russell (rusty@rustcorp.com.au).
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv6/ip6_conntrack.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_state.h>

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      const void *hdr,
      uint16_t datalen,
      int *hotdrop)
{
	const struct ip6t_state_info *sinfo = matchinfo;
	enum ip6_conntrack_info ctinfo;
	unsigned int statebit;

	if (!ip6_conntrack_get((struct sk_buff *)skb, &ctinfo))
		statebit = IP6T_STATE_INVALID;
	else
		statebit = IP6T_STATE_BIT(ctinfo);

	return (sinfo->statemask & statebit);
}

static int check(const char *tablename,
		 const struct ip6t_ip6 *ip,
		 void *matchinfo,
		 unsigned int matchsize,
		 unsigned int hook_mask)
{
	if (matchsize != IP6T_ALIGN(sizeof(struct ip6t_state_info)))
		return 0;

	return 1;
}

static struct ip6t_match state_match = {
	.name		= "state",
	.match		= &match,
	.checkentry	= &check,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	need_ip6_conntrack();
	return ip6t_register_match(&state_match);
}

static void __exit fini(void)
{
	ip6t_unregister_match(&state_match);
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
