/*
 *	Extension Header handling and adding code
 *
 *	Authors:
 *	Sami Kivisaari		<skivisaa@cc.hut.fi>	
 *
 *	$Id: s.exthdrs.c 1.73 03/10/02 14:20:42+03:00 henkku@mart10.hut.mediapoli.com $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/slab.h>

#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/mipv6.h>

#include "debug.h"
#include "stats.h"
#include "mobhdr.h"
#include "bcache.h"
#include "config.h"

/**
 * mipv6_append_home_addr - Add Home Address Option
 * @opt: buffer for Home Address Option
 * @offset: offset from beginning of @opt
 * @addr: address for HAO
 *
 * Adds a Home Address Option to a packet.  Option is stored in
 * @offset from beginning of @opt.  The option is created but the
 * original source address in IPv6 header is left intact.  The source
 * address will be changed from home address to CoA after the checksum
 * has been calculated in getfrag.  Padding is done automatically, and
 * @opt must have allocated space for both actual option and pad.
 * Returns offset from @opt to end of options.
 **/
int mipv6_append_home_addr(__u8 *opt, int offset, struct in6_addr *addr)
{
	int pad;
	struct mipv6_dstopt_homeaddr *ho;

	DEBUG(DBG_DATADUMP, "HAO: %x:%x:%x:%x:%x:%x:%x:%x",
	      NIPV6ADDR(addr));

	pad = (6 - offset) & 7;
	mipv6_add_pad(opt + offset, pad);

	ho = (struct mipv6_dstopt_homeaddr *)(opt + offset + pad);
	ho->type = MIPV6_TLV_HOMEADDR;
	ho->length = sizeof(*ho) - 2;
	ipv6_addr_copy(&ho->addr, addr); 

	return offset + pad + sizeof(*ho);
}

/**
 * mipv6_handle_homeaddr - Home Address Destination Option handler
 * @skb: packet buffer
 * @optoff: offset to where option begins
 *
 * Handles Home Address Option in IPv6 Destination Option header.
 * Packet and offset to option are passed.  If HAO is used without
 * binding, sends a Binding Error code 1.  When sending BE, notify bit
 * is cleared to prevent IPv6 error handling from sending ICMP
 * Parameter Problem.  Returns 1 on success, otherwise zero.
 **/
int mipv6_handle_homeaddr(struct sk_buff *skb, int optoff)
{
	struct in6_addr *saddr = &(skb->nh.ipv6h->saddr);
	struct in6_addr coaddr;
	struct inet6_skb_parm *opt = (struct inet6_skb_parm *) skb->cb;
	struct mipv6_dstopt_homeaddr *haopt =
	    (struct mipv6_dstopt_homeaddr *) &skb->nh.raw[optoff];
	struct mipv6_bce bc_entry;
	u8 *dst1;

	DEBUG_FUNC();

	if (haopt->length != sizeof(*haopt) - 2) {
		DEBUG(DBG_WARNING, "HAO has invalid length");
		MIPV6_INC_STATS(n_ha_drop.invalid);
		return 0;
	}
	ipv6_addr_copy(&coaddr, saddr);
	ipv6_addr_copy(saddr, &haopt->addr);
	ipv6_addr_copy(&haopt->addr, &coaddr);
	dst1 = (u8 *)skb->h.raw;
	if (dst1[0] != IPPROTO_MOBILITY && 
	    mipv6_bcache_get(saddr, &skb->nh.ipv6h->daddr, &bc_entry) != 0) {
		DEBUG(DBG_INFO, "HAO without binding, sending BE code 1: "
		      "home address %x:%x:%x:%x:%x:%x:%x:%x",
		      NIPV6ADDR(saddr));
		haopt->type &= ~(0x80); /* clear notify bit */
		mipv6_send_be(&skb->nh.ipv6h->daddr, &coaddr, saddr,
			      MIPV6_BE_HAO_WO_BINDING);
		MIPV6_INC_STATS(n_ha_drop.misc);
		return 0;
	}
	opt->hao = optoff + 2;
	if (mip6_fn.mn_check_tunneled_packet != NULL)
		mip6_fn.mn_check_tunneled_packet(skb);

	MIPV6_INC_STATS(n_ha_rcvd);
	return 1;
}

/**
 * mipv6_icmp_handle_homeaddr - Switch HAO and source for ICMP errors
 * @skb: packet buffer
 *
 * Reset the source address and the Home Address option in skb before
 * appending it to an ICMP error message, so original packet appears
 * in the error message rather than mangled.
 **/
void mipv6_icmp_handle_homeaddr(struct sk_buff *skb)
{
	struct inet6_skb_parm *opt = (struct inet6_skb_parm *)skb->cb;

	DEBUG_FUNC();

	if (opt->hao) {
		struct in6_addr tmp;
		struct in6_addr *ha = 
			(struct in6_addr *)(skb->nh.raw + opt->hao);

		ipv6_addr_copy(&tmp, ha);
		ipv6_addr_copy(ha, &skb->nh.ipv6h->saddr);
		ipv6_addr_copy(&skb->nh.ipv6h->saddr, &tmp);
	}
}

/**
 * mipv6_append_rt2hdr - Add Type 2 Routing Header
 * @rt: buffer for new routing header
 * @addr: intermediate hop address
 *
 * Adds a Routing Header Type 2 in a packet.  Stores newly created
 * routing header in buffer @rt.  Type 2 RT only carries one address,
 * so there is no need to process old routing header.  @rt must have
 * allocated space for 24 bytes.
 **/
void mipv6_append_rt2hdr(struct ipv6_rt_hdr *rt, struct in6_addr *addr)
{
	struct rt2_hdr *rt2 = (struct rt2_hdr *)rt;

        DEBUG(DBG_DATADUMP, "RT2: %x:%x:%x:%x:%x:%x:%x:%x",
	      NIPV6ADDR(addr));

	if (ipv6_addr_type(addr) == IPV6_ADDR_MULTICAST) {
		DEBUG(DBG_ERROR, "destination address not unicast");
		return;
	}

	memset(rt2, 0, sizeof(*rt2));
	rt2->rt_hdr.type = 2;
	rt2->rt_hdr.hdrlen = 2;
	rt2->rt_hdr.segments_left = 1;
	ipv6_addr_copy(&rt2->addr, addr);
}

/**
 * mipv6_append_dst1opts - Add Destination Option (1) Headers
 * @dst1opt: buffer for new destination options
 * @saddr: address for Home Address Option
 * @old_dst1opt: old destination options
 * @len: length of options
 *
 * Adds Destination Option (1) Header to a packet.  New options are
 * stored in @dst1opt.  If old destination options exist, they are
 * copied from @old_dst1opt.  Only Home Address Option is destination
 * option.  @dstopt must have allocated space for @len bytes.  @len
 * includes Destination Option Header (2 bytes), Home Address Option
 * (18 bytes) and possible HAO pad (8n+6).
 **/
/*
 * ISSUE: Home Address Destination Option should really be added to a
 * new destination option header specified in Mobile IPv6 spec which
 * should be placed after routing header(s), but before fragmentation
 * header.  Putting HAO in DO1 works for now, but support for the new
 * placement should be added to the IPv6 stack.
 */
void 
mipv6_append_dst1opts(struct ipv6_opt_hdr *dst1opt, struct in6_addr *saddr,
		      struct ipv6_opt_hdr *old_dst1opt, int len)
{
	int offset;

	if (old_dst1opt) {
		memcpy(dst1opt, old_dst1opt, ipv6_optlen(old_dst1opt));
		offset = ipv6_optlen(old_dst1opt);
	} else {
		offset = sizeof (*dst1opt);
	}
	dst1opt->hdrlen = (len >> 3) - 1;
	mipv6_append_home_addr((__u8 *) dst1opt, offset, saddr);
}

/**
 * mipv6_modify_txoptions - Modify outgoing packets
 * @sk: socket
 * @skb: packet buffer for outgoing packet
 * @old_opt: transmit options
 * @fl: packet flow structure
 * @dst: pointer to destination cache entry
 *
 * Adds Home Address Option (for MN packets, when not at home) and
 * Routing Header Type 2 (for CN packets when sending to an MN) to
 * data packets.  Old extension headers are copied from @old_opt (if
 * any).  Extension headers are _explicitly_ added for packets with
 * Mobility Header.  Returns the new header structure, or old if no
 * changes.
 **/
struct ipv6_txoptions *
mipv6_modify_txoptions(struct sock *sk, struct sk_buff *skb, 
		       struct ipv6_txoptions *old_opt, struct flowi *fl, 
		       struct dst_entry **dst)
{	
	struct ipv6_opt_hdr *old_hopopt = NULL;
	struct ipv6_opt_hdr *old_dst1opt = NULL;
	struct ipv6_rt_hdr *old_srcrt = NULL;
	struct ipv6_opt_hdr *old_dst0opt = NULL;
	/* XXX: What about auth opt ? */

	int optlen;
	int srcrtlen = 0, dst1len = 0;
	int tot_len, use_hao = 0;
	struct ipv6_txoptions *opt;
	struct mipv6_bce bc_entry;
	struct in6_addr tmpaddr, *saddr, *daddr, coaddr;
	__u8 *opt_ptr;

	DEBUG_FUNC();

	if (fl->proto == IPPROTO_MOBILITY) return old_opt;
	/*
	 * we have to be prepared to the fact that saddr might not be present,
	 * if that is the case, we acquire saddr just as kernel does.
	 */
	saddr = fl ? &fl->fl6_src : NULL;
	daddr = fl ? &fl->fl6_dst : NULL;

	if (daddr == NULL)
		return old_opt;
	if (saddr == NULL) {
		int err = ipv6_get_saddr(NULL, daddr, &tmpaddr);
		if (err)
			return old_opt;
		else
			saddr = &tmpaddr;
	}

	DEBUG(DBG_DATADUMP,
	      "dest. address of packet: %x:%x:%x:%x:%x:%x:%x:%x",
	      NIPV6ADDR(daddr));
 	DEBUG(DBG_DATADUMP, " and src. address: %x:%x:%x:%x:%x:%x:%x:%x", 
	      NIPV6ADDR(saddr));

	if (old_opt) {
		old_hopopt = old_opt->hopopt;
		old_dst1opt = old_opt->dst1opt;
		old_srcrt = old_opt->srcrt;
		old_dst0opt = old_opt->dst0opt;
	} 

	if (mip6_fn.mn_use_hao != NULL)
		use_hao = mip6_fn.mn_use_hao(daddr, saddr);

	if (use_hao) {
		if (old_dst1opt)
			dst1len = ipv6_optlen(old_dst1opt);
		dst1len += sizeof(struct mipv6_dstopt_homeaddr) +
			((6 - dst1len) & 7); /* padding */
	}

	if (mipv6_bcache_get(daddr, saddr, &bc_entry) == 0)
		srcrtlen = sizeof(struct rt2_hdr);

	if (old_opt) {
		tot_len = old_opt->tot_len;
		if (use_hao && old_dst1opt) {
			/* already accounted for old_dst1opt in check above */
			tot_len -= ipv6_optlen(old_dst1opt);
		}
	} else
		tot_len = sizeof(*opt);

	if ((tot_len += (srcrtlen + dst1len)) == 0) { 
		return old_opt;
	}

	/* tot_len += sizeof(*opt); XXX : Already included in old_opt->len */

	if (!(opt = kmalloc(tot_len, GFP_ATOMIC))) {
		return old_opt;
	}
	memset(opt, 0, tot_len);
	opt->tot_len = tot_len;
	opt_ptr = (__u8 *) (opt + 1);
	
	if (old_srcrt) {
		opt->srcrt = (struct ipv6_rt_hdr *) opt_ptr;
		opt_ptr += ipv6_optlen(old_srcrt);
		opt->opt_nflen += ipv6_optlen(old_srcrt);
		memcpy(opt->srcrt, old_srcrt, ipv6_optlen(old_srcrt));
	}

	if (srcrtlen) {
		DEBUG(DBG_DATADUMP, "Binding exists. Adding routing header");

		opt->srcrt2 = (struct ipv6_rt_hdr *) opt_ptr;
		opt->opt_nflen += srcrtlen;
		opt_ptr += srcrtlen;
		
		/*
		 * Append care-of-address to routing header (original
		 * destination address is home address, the first
		 * source route segment gets put to the destination
		 * address and the home address gets to the last
		 * segment of source route (just as it should)) 
		 */

		ipv6_addr_copy(&coaddr, &bc_entry.coa);

		mipv6_append_rt2hdr(opt->srcrt2, &coaddr);

		/*
		 * reroute output (we have to do this in case of TCP
                 * segment) unless a routing header of type 0 is also added
		 */
		if (dst && !opt->srcrt) {
			struct in6_addr tmp;

			ipv6_addr_copy(&tmp, &fl->fl6_dst);
			ipv6_addr_copy(&fl->fl6_dst, &coaddr);

			dst_release(*dst);
			*dst = ip6_route_output(sk, fl);
			if (skb)
				skb->dst = *dst;
			ipv6_addr_copy(&fl->fl6_dst, &tmp);

			DEBUG(DBG_DATADUMP, "Rerouted outgoing packet");
		}
	}

	/* Only home address option is inserted to first dst opt header */
	if (dst1len) {
		opt->dst1opt = (struct ipv6_opt_hdr *) opt_ptr;
		opt_ptr += dst1len;
		opt->opt_flen += dst1len;
		mipv6_append_dst1opts(opt->dst1opt, saddr, old_dst1opt,
					dst1len);
		opt->mipv6_flags = MIPV6_SND_HAO;
	} else if (old_dst1opt) {
		optlen = ipv6_optlen(old_dst1opt);
		opt->dst1opt = (struct ipv6_opt_hdr *) opt_ptr;
		opt_ptr += optlen;
		opt->opt_flen += optlen;
		memcpy(opt->dst1opt, old_dst1opt, optlen);
	}
	if (old_hopopt) {
		optlen = ipv6_optlen(old_hopopt);
		opt->hopopt = (struct ipv6_opt_hdr *) opt_ptr;
		opt_ptr += optlen;
		opt->opt_nflen += optlen;
		memcpy(opt_ptr, old_hopopt, optlen);
	}	
	if (old_dst0opt) {
		optlen = ipv6_optlen(old_dst0opt);
		opt->dst0opt = (struct ipv6_opt_hdr *) opt_ptr;
		opt_ptr += optlen;
		opt->opt_nflen += optlen;
		memcpy(opt_ptr, old_dst0opt, optlen);
	}	
	return opt;
}
