/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the ICMP module.
 *
 * Version:	@(#)icmp.h	1.0.4	05/13/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _ICMP_H
#define	_ICMP_H

#include <linux/config.h>
#include <linux/icmp.h>
#include <linux/skbuff.h>

#include <net/sock.h>
#include <net/protocol.h>
#include <net/snmp.h>
#include <linux/ip.h>

struct icmp_err {
  int		errno;
  unsigned	fatal:1;
};

extern struct icmp_err icmp_err_convert[];
DECLARE_SNMP_STAT(struct icmp_mib, icmp_statistics);
#define ICMP_INC_STATS(field)		SNMP_INC_STATS(icmp_statistics, field)
#define ICMP_INC_STATS_BH(field)	SNMP_INC_STATS_BH(icmp_statistics, field)
#define ICMP_INC_STATS_USER(field) 	SNMP_INC_STATS_USER(icmp_statistics, field)
#define ICMP_INC_STATS_FIELD(offt)					\
	(*((unsigned long *) ((void *)					\
			     per_cpu_ptr(icmp_statistics[!in_softirq()],\
					 smp_processor_id()) + offt)))++
#define ICMP_INC_STATS_BH_FIELD(offt)					\
	(*((unsigned long *) ((void *)					\
			     per_cpu_ptr(icmp_statistics[0],		\
					 smp_processor_id()) + offt)))++
#define ICMP_INC_STATS_USER_FIELD(offt)					\
	(*((unsigned long *) ((void *)					\
			     per_cpu_ptr(icmp_statistics[1],		\
					 smp_processor_id()) + offt)))++

extern void	icmp_send(struct sk_buff *skb_in,  int type, int code, u32 info);
extern int	icmp_rcv(struct sk_buff *skb);
extern int	icmp_ioctl(struct sock *sk, int cmd, unsigned long arg);
extern void	icmp_init(struct net_proto_family *ops);

/* Move into dst.h ? */
extern int 	xrlim_allow(struct dst_entry *dst, int timeout);

struct raw_opt {
	struct icmp_filter filter;
};

struct ipv6_pinfo;

/* WARNING: don't change the layout of the members in raw_sock! */
struct raw_sock {
	struct sock	  sk;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	struct ipv6_pinfo *pinet6;
#endif
	struct inet_opt	  inet;
	struct raw_opt	  raw4;
};

#define raw4_sk(__sk) (&((struct raw_sock *)__sk)->raw4)

#endif	/* _ICMP_H */
