/*
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: include/linux/netfilter_ipv4/ip_conntrack_tuple.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _IP6_CONNTRACK_TUPLE_H
#define _IP6_CONNTRACK_TUPLE_H

#ifdef __KERNEL__
#include <linux/in6.h>
#include <linux/kernel.h>
#endif

/* A `tuple' is a structure containing the information to uniquely
  identify a connection.  ie. if two packets have the same tuple, they
  are in the same connection; if not, they are not.

  We divide the structure along "manipulatable" and
  "non-manipulatable" lines, for the benefit of the NAT code.
*/

/* The protocol-specific manipulable parts of the tuple: always in
   network order! */
union ip6_conntrack_manip_proto
{
	/* Add other protocols here. */
	u_int16_t all;

	struct {
		u_int16_t port;
	} tcp;
	struct {
		u_int16_t port;
	} udp;
	struct {
		u_int16_t id;
	} icmpv6;
};

/* The manipulable part of the tuple. */
struct ip6_conntrack_manip
{
	struct in6_addr ip;
	union ip6_conntrack_manip_proto u;
};

/* This contains the information to distinguish a connection. */
struct ip6_conntrack_tuple
{
	struct ip6_conntrack_manip src;

	/* These are the parts of the tuple which are fixed. */
	struct {
		struct in6_addr ip;
		union {
			/* Add other protocols here. */
			u_int16_t all;

			struct {
				u_int16_t port;
			} tcp;
			struct {
				u_int16_t port;
			} udp;
			struct {
				u_int8_t type, code;
			} icmpv6;
		} u;

		/* The protocol. */
		u_int16_t protonum;
	} dst;
};

enum ip6_conntrack_dir
{
	IP6_CT_DIR_ORIGINAL,
	IP6_CT_DIR_REPLY,
	IP6_CT_DIR_MAX
};

#ifdef __KERNEL__

#define DUMP_TUPLE(tp)							\
{									\
	DEBUGP("tuple %p: %u %x:%x:%x:%x:%x:%x:%x:%x, %hu -> %x:%x:%x:%x:%x:%x:%x:%x, %hu\n",								\
		(tp), (tp)->dst.protonum,				\
		NIP6((tp)->src.ip), ntohs((tp)->src.u.all),		\
		NIP6((tp)->dst.ip), ntohs((tp)->dst.u.all));		\
}

#define CTINFO2DIR(ctinfo) ((ctinfo) >= IP6_CT_IS_REPLY ? IP6_CT_DIR_REPLY : IP6_CT_DIR_ORIGINAL)

/* If we're the first tuple, it's the original dir. */
#define DIRECTION(h) ((enum ip6_conntrack_dir)(&(h)->ctrack->tuplehash[1] == (h)))

/* Connections have two entries in the hash table: one for each way */
struct ip6_conntrack_tuple_hash
{
	struct list_head list;

	struct ip6_conntrack_tuple tuple;

	/* this == &ctrack->tuplehash[DIRECTION(this)]. */
	struct ip6_conntrack *ctrack;
};

#endif /* __KERNEL__ */

extern int ip6_ct_tuple_src_equal(const struct ip6_conntrack_tuple *t1,
				  const struct ip6_conntrack_tuple *t2);

extern int ip6_ct_tuple_dst_equal(const struct ip6_conntrack_tuple *t1,
				  const struct ip6_conntrack_tuple *t2);

extern int ip6_ct_tuple_equal(const struct ip6_conntrack_tuple *t1,
			      const struct ip6_conntrack_tuple *t2);

extern int ip6_ct_tuple_mask_cmp(const struct ip6_conntrack_tuple *t,
			       const struct ip6_conntrack_tuple *tuple,
			       const struct ip6_conntrack_tuple *mask);

#endif /* _IP6_CONNTRACK_TUPLE_H */
