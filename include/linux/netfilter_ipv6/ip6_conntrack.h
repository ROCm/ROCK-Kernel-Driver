/*
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: include/linux/netfilter_ipv4/ip_conntrack.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _IP6_CONNTRACK_H
#define _IP6_CONNTRACK_H
/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */

#include <linux/config.h>
#include <linux/netfilter_ipv6/ip6_conntrack_tuple.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <asm/atomic.h>

enum ip6_conntrack_info
{
	/* Part of an established connection (either direction). */
	IP6_CT_ESTABLISHED,

	/* Like NEW, but related to an existing connection, or ICMP error
	   (in either direction). */
	IP6_CT_RELATED,

	/* Started a new connection to track (only
           IP6_CT_DIR_ORIGINAL); may be a retransmission. */
	IP6_CT_NEW,

	/* >= this indicates reply direction */
	IP6_CT_IS_REPLY,

	/* Number of distinct IP6_CT types (no NEW in reply dirn). */
	IP6_CT_NUMBER = IP6_CT_IS_REPLY * 2 - 1
};

/* Bitset representing status of connection. */
enum ip6_conntrack_status {
	/* It's an expected connection: bit 0 set.  This bit never changed */
	IP6S_EXPECTED_BIT = 0,
	IP6S_EXPECTED = (1 << IP6S_EXPECTED_BIT),

	/* We've seen packets both ways: bit 1 set.  Can be set, not unset. */
	IP6S_SEEN_REPLY_BIT = 1,
	IP6S_SEEN_REPLY = (1 << IP6S_SEEN_REPLY_BIT),

	/* Conntrack should never be early-expired. */
	IP6S_ASSURED_BIT = 2,
	IP6S_ASSURED = (1 << IP6S_ASSURED_BIT),

	/* Connection is confirmed: originating packet has left box */
	IP6S_CONFIRMED_BIT = 3,
	IP6S_CONFIRMED = (1 << IP6S_CONFIRMED_BIT),
};

#include <linux/netfilter_ipv6/ip6_conntrack_tcp.h>
#include <linux/netfilter_ipv6/ip6_conntrack_icmpv6.h>

/* per conntrack: protocol private data */
union ip6_conntrack_proto {
	/* insert conntrack proto private data here */
	struct ip6_ct_tcp tcp;
	struct ip6_ct_icmpv6 icmpv6;
};

union ip6_conntrack_expect_proto {
	/* insert expect proto private data here */
};

/* Add protocol helper include file here */
#include <linux/netfilter_ipv6/ip6_conntrack_ftp.h>

/* per expectation: application helper private data */
union ip6_conntrack_expect_help {
	/* insert conntrack helper private data (expect) here */
	struct ip6_ct_ftp_expect exp_ftp_info;
};

/* per conntrack: application helper private data */
union ip6_conntrack_help {
	/* insert conntrack helper private data (master) here */
	struct ip6_ct_ftp_master ct_ftp_info;
};

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/skbuff.h>

#ifdef CONFIG_NF_DEBUG
#define IP6_NF_ASSERT(x)							\
do {									\
	if (!(x))							\
		/* Wooah!  I'm tripping my conntrack in a frenzy of	\
		   netplay... */					\
		printk("NF_IP6_ASSERT: %s:%i(%s)\n",			\
		       __FILE__, __LINE__, __FUNCTION__);		\
} while(0)
#else
#define IP6_NF_ASSERT(x)
#endif

struct ip6_conntrack_expect
{
	/* Internal linked list (global expectation list) */
	struct list_head list;

	/* reference count */
	atomic_t use;

	/* expectation list for this master */
	struct list_head expected_list;

	/* The conntrack of the master connection */
	struct ip6_conntrack *expectant;

	/* The conntrack of the sibling connection, set after
	 * expectation arrived */
	struct ip6_conntrack *sibling;

	/* IPv6 packet is never NATed */
	/* Tuple saved for conntrack */
/*
	struct ip6_conntrack_tuple ct_tuple;
*/

	/* Timer function; deletes the expectation. */
	struct timer_list timeout;

	/* Data filled out by the conntrack helpers follow: */

	/* We expect this tuple, with the following mask */
	struct ip6_conntrack_tuple tuple, mask;

	/* Function to call after setup and insertion */
	int (*expectfn)(struct ip6_conntrack *new);

	/* At which sequence number did this expectation occur */
	u_int32_t seq;
  
	union ip6_conntrack_expect_proto proto;

	union ip6_conntrack_expect_help help;
};

#include <linux/netfilter_ipv6/ip6_conntrack_helper.h>
struct ip6_conntrack
{
	/* Usage count in here is 1 for hash table/destruct timer, 1 per skb,
           plus 1 for any connection(s) we are `master' for */
	struct nf_conntrack ct_general;

	/* These are my tuples; original and reply */
	struct ip6_conntrack_tuple_hash tuplehash[IP6_CT_DIR_MAX];

	/* Have we seen traffic both ways yet? (bitset) */
	unsigned long status;

	/* Timer function; drops refcnt when it goes off. */
	struct timer_list timeout;

	/* If we're expecting another related connection, this will be
           in expected linked list */
	struct list_head sibling_list;
	
	/* Current number of expected connections */
	unsigned int expecting;

	/* If we were expected by an expectation, this will be it */
	struct ip6_conntrack_expect *master;

	/* Helper, if any. */
	struct ip6_conntrack_helper *helper;

	/* Our various nf_ct_info structs specify *what* relation this
           packet has to the conntrack */
	struct nf_ct_info infos[IP6_CT_NUMBER];

	/* Storage reserved for other modules: */

	union ip6_conntrack_proto proto;

	union ip6_conntrack_help help;
};

/* get master conntrack via master expectation */
#define master_ct6(conntr) (conntr->master ? conntr->master->expectant : NULL)

/* Alter reply tuple (maybe alter helper).  If it's already taken,
   return 0 and don't do alteration. */
extern int
ip6_conntrack_alter_reply(struct ip6_conntrack *conntrack,
			 const struct ip6_conntrack_tuple *newreply);

/* Is this tuple taken? (ignoring any belonging to the given
   conntrack). */
extern int
ip6_conntrack_tuple_taken(const struct ip6_conntrack_tuple *tuple,
			 const struct ip6_conntrack *ignored_conntrack);

/* Return conntrack_info and tuple hash for given skb. */
extern struct ip6_conntrack *
ip6_conntrack_get(struct sk_buff *skb, enum ip6_conntrack_info *ctinfo);

/* decrement reference count on a conntrack */
extern inline void ip6_conntrack_put(struct ip6_conntrack *ct);

/* find unconfirmed expectation based on tuple */
struct ip6_conntrack_expect *
ip6_conntrack_expect_find_get(const struct ip6_conntrack_tuple *tuple);

/* decrement reference count on an expectation */
void ip6_conntrack_expect_put(struct ip6_conntrack_expect *exp);

/* call to create an explicit dependency on ip6_conntrack. */
extern void need_ip6_conntrack(void);

extern int ip6_invert_tuplepr(struct ip6_conntrack_tuple *inverse,
			  const struct ip6_conntrack_tuple *orig);

/* Refresh conntrack for this many jiffies */
extern void ip6_ct_refresh(struct ip6_conntrack *ct,
			  unsigned long extra_jiffies);

/* Call me when a conntrack is destroyed. */
extern void (*ip6_conntrack_destroyed)(struct ip6_conntrack *conntrack);

/* Returns new sk_buff, or NULL */
struct sk_buff *
ip6_ct_gather_frags(struct sk_buff *skb);

/* Delete all conntracks which match. */
extern void
ip6_ct_selective_cleanup(int (*kill)(const struct ip6_conntrack *i, void *data),
			void *data);

/* It's confirmed if it is, or has been in the hash table. */
static inline int is_confirmed(struct ip6_conntrack *ct)
{
	return test_bit(IP6S_CONFIRMED_BIT, &ct->status);
}

extern unsigned int ip6_conntrack_htable_size;

/* eg. PROVIDES_CONNTRACK6(ftp); */
#define PROVIDES_CONNTRACK6(name)                        \
        int needs_ip6_conntrack_##name;                  \
        EXPORT_SYMBOL(needs_ip6_conntrack_##name)

/*. eg. NEEDS_CONNTRACK6(ftp); */
#define NEEDS_CONNTRACK6(name)                                           \
        extern int needs_ip6_conntrack_##name;                           \
        static int *need_ip6_conntrack_##name __attribute_used__ = &needs_ip6_conntrack_##name

#endif /* __KERNEL__ */
#endif /* _IP6_CONNTRACK_H */
