/*
 *	Home Agent specific ICMP routines
 *
 *	Authors:
 *	Antti Tuominen	<ajtuomin@tml.hut.fi>
 *	Jaakko Laine	<medved@iki.fi>
 *
 *      $Id: s.mipv6_icmp_ha.c 1.10 03/09/30 16:13:37+03:00 henkku@ron.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/autoconf.h>
#include <linux/sched.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/mipv6.h>

#include "halist.h"
#include "debug.h"
#include "mipv6_icmp.h"
#include "prefix.h"

/* Is this the easiest way of checking on 
 *  which interface an anycast address is ?
 */
static int find_ac_dev(struct in6_addr *addr)
{
	int ifindex = 0;
	struct net_device *dev;
	read_lock(&dev_base_lock);
	for (dev=dev_base; dev; dev=dev->next) {
		if (ipv6_chk_acast_addr(dev, addr)) {
			ifindex = dev->ifindex;
			break;
		}
	}
	read_unlock(&dev_base_lock);
	return ifindex;
}

/**
 * mipv6_icmpv6_send_dhaad_rep - Reply to DHAAD Request
 * @ifindex: index of interface request was received from
 * @id: request's identification number
 * @daddr: requester's IPv6 address
 *
 * When Home Agent receives Dynamic Home Agent Address Discovery
 * request, it replies with a list of home agents available on the
 * home link.
 */
void mipv6_icmpv6_send_dhaad_rep(int ifindex, __u16 id, struct in6_addr *daddr)
{
	__u8 *data = NULL;
	struct in6_addr home, *ha_addrs = NULL;
	int addr_count, max_addrs, size = 0;

	if (daddr == NULL)
		return;

	if (mipv6_ha_get_addr(ifindex, &home) < 0) {
		DEBUG(DBG_INFO, "Not Home Agent in this interface");
		return;
	}

	/* We send all available HA addresses, not exceeding a maximum
	 * number we can fit in a packet with minimum IPv6 MTU (to
	 * avoid fragmentation).
	 */
	max_addrs = 76;
	addr_count = mipv6_ha_get_pref_list(ifindex, &ha_addrs, max_addrs);

	if (addr_count < 0) return;

	if (addr_count != 0 && ha_addrs == NULL) {
		DEBUG(DBG_ERROR, "addr_count = %d but return no addresses", 
		      addr_count);
		return;
	}

	if (addr_count > 1 && (ipv6_addr_cmp(ha_addrs, &home) == 0)) {
		/* If multiple home agents and we are the prefered,
		 * remove from list. */
		addr_count--;
		data = (u8 *)(ha_addrs + 1);
	} else {
		data = (u8 *)ha_addrs;
	}
	size = addr_count * sizeof(struct in6_addr);

	mipv6_icmpv6_send(daddr, &home, ICMPV6_DHAAD_REPLY, 
			  0, &id, 0, data, size);
	if (ha_addrs) {
		data = NULL;
		kfree(ha_addrs);
	}
}

/** 
 * mipv6_icmpv6_dhaad_req - Home Agent Address Discovery Request ICMP handler
 * @skb: buffer containing ICMP information message
 *
 * Special Mobile IPv6 ICMP message.  Handles Dynamic Home Agent
 * Address Discovery Request messages.
 **/
int mipv6_icmpv6_rcv_dhaad_req(struct sk_buff *skb)
{
	struct icmp6hdr *phdr = (struct icmp6hdr *) skb->h.raw;
	struct in6_addr *saddr = &skb->nh.ipv6h->saddr;
	struct in6_addr *daddr = &skb->nh.ipv6h->daddr;
	__u16 identifier;
	int ifindex = 0;

	DEBUG_FUNC();

	/* Invalid packet checks. */
	if (phdr->icmp6_code != 0)
		return 0;

	identifier = ntohs(phdr->icmp6_identifier);

	/* 
	 * Make sure we have the right ifindex (if the
	 * req came through another interface. 
	 */
	ifindex = find_ac_dev(daddr);
	if (ifindex == 0) { 
		DEBUG(DBG_WARNING, "received dhaad request to anycast address %x:%x:%x:%x:%x:%x:%x:%x"
		      " on which prefix we are not HA",
		      NIPV6ADDR(daddr));
		return 0;
	}

	/*
	 * send reply with list
	 */
	mipv6_icmpv6_send_dhaad_rep(ifindex, identifier, saddr);
	return 1;
}
#if 1
/**
 * mipv6_icmpv6_handle_pfx_sol - handle prefix solicitations
 * @skb: sk_buff including the icmp6 message
 */
int mipv6_icmpv6_rcv_pfx_sol(struct sk_buff *skb)
{
	struct in6_addr *saddr = &skb->nh.ipv6h->saddr;
	struct in6_addr *daddr = &skb->nh.ipv6h->daddr;
	struct inet6_ifaddr *ifp;

	DEBUG_FUNC();

	if (!(ifp = ipv6_get_ifaddr(daddr, NULL, 0)))
		return -1;

	in6_ifa_put(ifp);
	mipv6_pfx_cancel_send(saddr, -1);

	return 0;
}
EXPORT_SYMBOL(mipv6_icmpv6_rcv_pfx_sol);
#endif
