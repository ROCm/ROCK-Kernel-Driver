/*
 *	MIPL Mobile IPv6 Extension Headers header file
 *
 *	$Id: s.exthdrs.h 1.27 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _MIPV6_EXTHDRS_H
#define _MIPV6_EXTHDRS_H

/*
 * Home Address Destination Option function prototypes
 */
int mipv6_append_home_addr(__u8 *opt, int offset, struct in6_addr *addr);

int mipv6_handle_homeaddr(struct sk_buff *skb, int optoff);

void mipv6_icmp_handle_homeaddr(struct sk_buff *skb);

/*
 * Creates a routing header of type 2.
 */
void mipv6_append_rt2hdr(struct ipv6_rt_hdr *srcrt, struct in6_addr *addr);

/* Function to add the first destination option header, which may
 * include a home address option.  
 */
void mipv6_append_dst1opts(struct ipv6_opt_hdr *dst1opt, struct in6_addr *saddr,
			   struct ipv6_opt_hdr *old_dst1opt, int len);

struct ipv6_txoptions *mipv6_modify_txoptions(
	struct sock *sk, struct sk_buff *skb,
	struct ipv6_txoptions *old_opt, struct flowi *fl,
	struct dst_entry **dst);

#endif /* _MIPV6_EXTHDRS_H */
