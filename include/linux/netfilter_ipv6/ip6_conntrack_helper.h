/*
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: include/linux/netfilter_ipv4/ip_conntrack_helper.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
/* IP6 connection tracking helpers. */
#ifndef _IP6_CONNTRACK_HELPER_H
#define _IP6_CONNTRACK_HELPER_H
#include <linux/netfilter_ipv6/ip6_conntrack.h>

struct module;

/* Reuse expectation when max_expected reached */
#define IP6_CT_HELPER_F_REUSE_EXPECT	0x01

struct ip6_conntrack_helper
{	
	struct list_head list;		/* Internal use. */

	const char *name;		/* name of the module */
	unsigned char flags;		/* Flags (see above) */
	struct module *me;		/* pointer to self */
	unsigned int max_expected;	/* Maximum number of concurrent
					 * expected connections */
	unsigned int timeout;		/* timeout for expecteds */

	/* Mask of things we will help (compared against server response) */
	struct ip6_conntrack_tuple tuple;
	struct ip6_conntrack_tuple mask;
	
	/* Function to call when data passes; return verdict, or -1 to
           invalidate. */
	int (*help)(const struct sk_buff *skb,
		    unsigned int protoff,
		    struct ip6_conntrack *ct,
		    enum ip6_conntrack_info conntrackinfo);
};

extern int ip6_conntrack_helper_register(struct ip6_conntrack_helper *);
extern void ip6_conntrack_helper_unregister(struct ip6_conntrack_helper *);

extern struct ip6_conntrack_helper *ip6_ct_find_helper(const struct ip6_conntrack_tuple *tuple);

/* Add an expected connection: can have more than one per connection */
extern int ip6_conntrack_expect_related(struct ip6_conntrack *related_to,
					struct ip6_conntrack_expect *exp);
extern void ip6_conntrack_unexpect_related(struct ip6_conntrack_expect *exp);

#endif /*_IP6_CONNTRACK_HELPER_H*/
