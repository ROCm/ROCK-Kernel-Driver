/*
 *      MIPL Mobile IPv6 ICMP send and receive prototypes
 *
 *      $Id: s.mipv6_icmp.h 1.12 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _MIPV6_ICMP
#define _MIPV6_ICMP

#include <linux/config.h>
#include <linux/in6.h>

void mipv6_icmpv6_send(struct in6_addr *daddr, struct in6_addr *saddr,
		       int type, int code, __u16 *id, __u16 flags,
		       void *data, int datalen);

void mipv6_icmpv6_send_dhaad_req(struct in6_addr *home_addr, int plen, __u16 dhaad_id);

void mipv6_icmpv6_send_dhaad_rep(int ifindex, __u16 id, struct in6_addr *daddr);
/* No handling */
int mipv6_icmpv6_no_rcv(struct sk_buff *skb);

/* Receive DHAAD Reply message */
int mipv6_icmpv6_rcv_dhaad_rep(struct sk_buff *skb);
/* Receive Parameter Problem message */
int mipv6_icmpv6_rcv_paramprob(struct sk_buff *skb);
/* Receive prefix advertisements */
int mipv6_icmpv6_rcv_pfx_adv(struct sk_buff *skb);

/* Receive DHAAD Request message */
int mipv6_icmpv6_rcv_dhaad_req(struct sk_buff *skb);
/* Receive prefix solicitations */
int mipv6_icmpv6_rcv_pfx_sol(struct sk_buff *skb);

int mipv6_icmpv6_init(void);
void mipv6_icmpv6_exit(void);

#endif
