/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  NET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Ethernet handlers.
 *
 * Version:	@(#)eth.h	1.0.4	05/13/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		Relocated to include/linux where it belongs by Alan Cox 
 *							<gw4pts@gw4pts.ampr.org>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	WARNING: This move may well be temporary. This file will get merged with others RSN.
 *
 */
#ifndef _LINUX_ETHERDEVICE_H
#define _LINUX_ETHERDEVICE_H

#include <linux/if_ether.h>

#ifdef __KERNEL__
extern int		eth_header(struct sk_buff *skb, struct net_device *dev,
				   unsigned short type, void *daddr,
				   void *saddr, unsigned len);
extern int		eth_rebuild_header(struct sk_buff *skb);
extern unsigned short	eth_type_trans(struct sk_buff *skb, struct net_device *dev);
extern void		eth_header_cache_update(struct hh_cache *hh, struct net_device *dev,
						unsigned char * haddr);
extern int		eth_header_cache(struct neighbour *neigh,
					 struct hh_cache *hh);
extern int		eth_header_parse(struct sk_buff *skb,
					 unsigned char *haddr);
extern struct net_device	* init_etherdev(struct net_device *, int);

static __inline__ void eth_copy_and_sum (struct sk_buff *dest, unsigned char *src, int len, int base)
{
	memcpy (dest->data, src, len);
}

#endif

#endif	/* _LINUX_ETHERDEVICE_H */
