/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the UDP protocol.
 *
 * Version:	@(#)udp.h	1.0.2	04/28/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_UDP_H
#define _LINUX_UDP_H

#include <linux/types.h>

struct udphdr {
	__u16	source;
	__u16	dest;
	__u16	len;
	__u16	check;
};

/* UDP socket options */
#define UDP_CORK	1	/* Never send partially complete segments */
#define UDP_ENCAP	100	/* Set the socket to accept encapsulated packets */

/* UDP encapsulation types */
#define UDP_ENCAP_ESPINUDP_NON_IKE	1 /* draft-ietf-ipsec-nat-t-ike-00/01 */
#define UDP_ENCAP_ESPINUDP	2 /* draft-ietf-ipsec-udp-encaps-06 */

#ifdef __KERNEL__

#include <linux/config.h>
#include <net/sock.h>
#include <linux/ip.h>

struct udp_opt {
	int		pending;	/* Any pending frames ? */
	unsigned int	corkflag;	/* Cork is required */
  	__u16		encap_type;	/* Is this an Encapsulation socket? */
	/*
	 * Following member retains the infomation to create a UDP header
	 * when the socket is uncorked.
	 */
	__u16		len;		/* total length of pending frames */
};

/* WARNING: don't change the layout of the members in udp_sock! */
struct udp_sock {
	struct sock	  sk;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	struct ipv6_pinfo *pinet6;
#endif
	struct inet_opt	  inet;
	struct udp_opt	  udp;
};

static inline struct udp_opt * udp_sk(const struct sock *__sk)
{
	return &((struct udp_sock *)__sk)->udp;
}

#endif

#endif	/* _LINUX_UDP_H */
