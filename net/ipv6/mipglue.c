/*
 *	Glue for Mobility support integration to IPv6
 *
 *	Authors:
 *	Antti Tuominen		<ajtuomin@cc.hut.fi>	
 *
 *	$Id: s.mipglue.c 1.7 03/09/18 15:59:41+03:00 vnuorval@amber.hut.mediapoli.com $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#include <linux/sched.h>

#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/neighbour.h>
#include <net/mipglue.h>

extern int ip6_tlvopt_unknown(struct sk_buff *skb, int optoff);

/*  Initialize all zero  */
struct mipv6_callable_functions mipv6_functions = { NULL };

/* Sets mipv6_functions struct to zero to invalidate all successive
 * calls to mipv6 functions. Used on module unload. */

void mipv6_invalidate_calls(void)
{
	memset(&mipv6_functions, 0, sizeof(mipv6_functions));
}


/* Selects correct handler for tlv encoded destination option. Called
 * by ip6_parse_tlv. Checks if mipv6 calls are valid before calling. */

int mipv6_handle_dstopt(struct sk_buff *skb, int optoff)
{
	int ret;

        switch (skb->nh.raw[optoff]) {
	case MIPV6_TLV_HOMEADDR: 
		ret = MIPV6_CALLFUNC(mipv6_handle_homeaddr, 0)(skb, optoff);
		break;
	default:
		/* Should never happen */
		printk(KERN_ERR __FILE__ ": Invalid destination option code (%d)\n",
		       skb->nh.raw[optoff]);
		ret = 1;
		break;
	}

	/* If mipv6 handlers are not valid, pass the packet to
         * ip6_tlvopt_unknown() for correct handling. */
	if (!ret)
		return ip6_tlvopt_unknown(skb, optoff);

	return ret;
}

EXPORT_SYMBOL(mipv6_functions);
EXPORT_SYMBOL(mipv6_invalidate_calls);
