/*
 *	Mobile Node specific ICMP routines
 *
 *	Authors:
 *	Antti Tuominen	<ajtuomin@tml.hut.fi>
 *	Jaakko Laine	<medved@iki.fi>
 *
 *      $Id: s.mipv6_icmp_mn.c 1.15 03/09/26 14:48:43+03:00 henkku@mart10.hut.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/mipv6.h>

#include "mn.h"
#include "bul.h"
#include "mdetect.h"
#include "debug.h"
#include "mipv6_icmp.h"
#include "util.h"
#include "prefix.h"

#define INFINITY 0xffffffff

/* BUL callback */

static int bul_entry_expired(struct mipv6_bul_entry *bulentry)
{
	DEBUG(DBG_INFO, "bul entry 0x%x lifetime expired, deleting entry",
	      (int) bulentry);
	return 1;
}

/**
 * mipv6_icmpv6_paramprob - Parameter Problem ICMP error message handler
 * @skb: buffer containing ICMP error message
 *
 * Special Mobile IPv6 ICMP handling.  If Mobile Node receives ICMP
 * Parameter Problem message when using a Home Address Option,
 * offending node should be logged and error message dropped.  If
 * error is received because of a Binding Update, offending node
 * should be recorded in Binding Update List and no more Binding
 * Updates should be sent to this destination.  See draft section
 * 10.15.
 **/
int mipv6_icmpv6_rcv_paramprob(struct sk_buff *skb)
{
	struct icmp6hdr *phdr = (struct icmp6hdr *) skb->h.raw;
	struct in6_addr *saddr = skb ? &skb->nh.ipv6h->saddr : NULL;
	struct in6_addr *daddr = skb ? &skb->nh.ipv6h->daddr : NULL;
	struct ipv6hdr *hdr = (struct ipv6hdr *) (phdr + 1);
	int ulen = (skb->tail - (unsigned char *) (phdr + 1));

	int errptr;
	__u8 *off_octet;

	DEBUG_FUNC();

	/* We only handle code 1 & 2 messages. */
	if (phdr->icmp6_code != ICMPV6_UNK_NEXTHDR &&
		phdr->icmp6_code != ICMPV6_UNK_OPTION)
		return 0;

	/* Find offending octet in the original packet. */
	errptr = ntohl(phdr->icmp6_pointer);

	/* There is not enough of the original packet left to figure
	 * out what went wrong. Bail out. */
	if (ulen <= errptr)
		return 0;

	off_octet = ((__u8 *) hdr + errptr);
	DEBUG(DBG_INFO, "Parameter problem: offending octet %d [0x%2x]",
	      errptr, *off_octet);

	/* If CN did not understand Mobility Header, set BUL entry to
	 * ACK_ERROR so no further BUs are sumbitted to this CN. */
	if (phdr->icmp6_code == ICMPV6_UNK_NEXTHDR) {
		struct mipv6_bul_entry *bulentry = NULL;
		if (*off_octet != IPPROTO_MOBILITY)
			return 0;

		write_lock_bh(&bul_lock);
		bulentry = mipv6_bul_get(saddr, daddr);
		if (bulentry) {
			bulentry->state = ACK_ERROR;
			bulentry->callback = bul_entry_expired;
			bulentry->callback_time = jiffies +
				DUMB_CN_BU_LIFETIME * HZ;
			bulentry->expire = bulentry->callback_time;
			DEBUG(DBG_INFO, "BUL entry set to ACK_ERROR");
			mipv6_bul_reschedule(bulentry);
		}
		write_unlock_bh(&bul_lock);
	}

	/* If CN did not understand Home Address Option, we log an
	 * error and discard the error message. */
	if (phdr->icmp6_code == ICMPV6_UNK_OPTION &&
	    *off_octet == MIPV6_TLV_HOMEADDR) {
		DEBUG(DBG_WARNING, "Correspondent node does not "
		      "implement Home Address Option receipt.");
		return 1;
	}
	return 0;
}

/**
 * mipv6_mn_dhaad_send_req - Send DHAAD Request to home network
 * @home_addr: address to do DHAAD for
 * @plen: prefix length for @home_addr
 *
 * Send Dynamic Home Agent Address Discovery Request to the Home
 * Agents anycast address in the nodes home network.
 **/
void 
mipv6_icmpv6_send_dhaad_req(struct in6_addr *home_addr, int plen, __u16 dhaad_id)
{
	struct in6_addr ha_anycast;
	struct in6_addr careofaddr;
	
	if (mipv6_get_care_of_address(home_addr, &careofaddr) < 0) {
		DEBUG(DBG_WARNING, "Could not get node's Care-of Address");
		return;
	}

	if (mipv6_ha_anycast(&ha_anycast, home_addr, plen) < 0) {
		DEBUG(DBG_WARNING, 
		      "Could not get Home Agent Anycast address for home address %x:%x.%x:%x:%x:%x:%x:%x/%d",
		      NIPV6ADDR(home_addr), plen);
		return;
	}

	mipv6_icmpv6_send(&ha_anycast, &careofaddr, ICMPV6_DHAAD_REQUEST, 0, 
			  &dhaad_id, 0, NULL, 0);

}

/** 
 * mipv6_icmpv6_dhaad_rep - Home Agent Address Discovery Reply ICMP handler
 * @skb: buffer containing ICMP information message
 *
 * Special Mobile IPv6 ICMP message.  Handles Dynamic Home Agent
 * Address Discovery Reply messages.
 **/
int mipv6_icmpv6_rcv_dhaad_rep(struct sk_buff *skb)
{
	struct icmp6hdr *phdr = (struct icmp6hdr *) skb->h.raw;
	struct in6_addr *address;
	struct in6_addr *saddr = &skb->nh.ipv6h->saddr;
	__u16 identifier;
	int ulen = (skb->tail - (unsigned char *) ((__u32 *) phdr + 2));
	int i;
	struct in6_addr home_addr, coa;
	struct in6_addr *first_ha = NULL;
	struct mn_info *minfo;
	int sender_on_list = 0;
	int n_addr = ulen / sizeof(struct in6_addr);

	DEBUG_FUNC();

	/* Invalid packet checks. */
	if (ulen % sizeof(struct in6_addr) != 0)
		return 0;

	if (phdr->icmp6_code != 0)
		return 0;

	identifier = ntohs(phdr->icmp6_identifier);
	if (ulen > 0) {
		address = (struct in6_addr *) ((__u32 *) phdr + 2);
	} else {
		address = saddr;
		n_addr = 1;
	}

	/* receive list of home agent addresses
	 * add to home agents list
	 */
	DEBUG(DBG_INFO, "DHAAD: got %d home agents", n_addr);

	first_ha = address;

	/* lookup H@ with identifier */
	read_lock(&mn_info_lock);
	minfo = mipv6_mninfo_get_by_id(identifier);
	if (!minfo) {
		read_unlock(&mn_info_lock);
		DEBUG(DBG_INFO, "no mninfo with id %d", 
		      identifier);
		return 0;
	}
	spin_lock(&minfo->lock);

	/* Logic:
	 * 1. if old HA on list, prefer it
	 * 2. if reply sender not on list, prefer it
	 * 3. otherwise first HA on list prefered
	 */
	for (i = 0; i < n_addr; i++) {
		DEBUG(DBG_INFO, "HA[%d] %x:%x:%x:%x:%x:%x:%x:%x",
		      i, NIPV6ADDR(address));
		if (ipv6_addr_cmp(saddr, address) == 0)
			sender_on_list = 1;
		if (ipv6_addr_cmp(&minfo->ha, address) == 0) {
			spin_unlock(&minfo->lock);
			read_unlock(&mn_info_lock);
			return 0;
		}
		address++;
	}
	if (!sender_on_list)
		ipv6_addr_copy(&minfo->ha, saddr);
	else
		ipv6_addr_copy(&minfo->ha, first_ha);
	spin_unlock(&minfo->lock);
	ipv6_addr_copy(&home_addr, &minfo->home_addr);
	read_unlock(&mn_info_lock);

	mipv6_get_care_of_address(&home_addr, &coa);
	init_home_registration(&home_addr, &coa);

	return 1;
}
#if 1
/**
 * mipv6_icmpv6_handle_pfx_adv - handle prefix advertisements
 * @skb: sk_buff including the icmp6 message
 */
int mipv6_icmpv6_rcv_pfx_adv(struct sk_buff *skb)
{
	struct icmp6hdr *hdr = (struct icmp6hdr *) skb->h.raw;
	struct in6_addr *saddr = &skb->nh.ipv6h->saddr;
	struct in6_addr *daddr = &skb->nh.ipv6h->daddr;
	__u8 *opt = (__u8 *) (hdr + 1);
	int optlen = (skb->tail - opt);
	unsigned long min_expire = INFINITY;
	struct inet6_skb_parm *parm = (struct inet6_skb_parm *) skb->cb;

	DEBUG_FUNC();

	while (optlen > 0) {
		int len = opt[1] << 3;
		if (len == 0)
			goto set_timer;

		if (opt[0] == ND_OPT_PREFIX_INFO) {
			int ifindex;
			unsigned long expire;
			struct prefix_info *pinfo =
				(struct prefix_info *) opt;
			struct net_device *dev;
			struct mn_info *mninfo;

			read_lock(&mn_info_lock);
			mninfo = mipv6_mninfo_get_by_ha(saddr);
			if (mninfo == NULL) {
				ifindex = 0;
			} else {
				spin_lock(&mninfo->lock);
				ifindex = mninfo->ifindex;
				spin_unlock(&mninfo->lock);
				mninfo = NULL;
			}
			read_unlock(&mn_info_lock);

			if (!(dev = dev_get_by_index(ifindex))) {
				DEBUG(DBG_WARNING, "Cannot find device by index %d", parm->iif);
				goto nextopt;
			}

			expire = ntohl(pinfo->valid);
			expire = expire == 0 ? INFINITY : expire;

			min_expire = expire < min_expire ? expire : min_expire;

			dev_put(dev);
		}

nextopt:
		optlen -= len;
		opt += len;
	}

set_timer:

	mipv6_pfx_add_home(parm->iif, saddr, daddr, min_expire);
	return 0;
}
#endif
