/*
 * llc_output.c - LLC output path
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License version 2 for more details.
 */

#include <linux/if_arp.h>
#include <linux/if_tr.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

/**
 *	llc_mac_hdr_init - fills MAC header fields
 *	@skb: Address of the frame to initialize its MAC header
 *	@sa: The MAC source address
 *	@da: The MAC destination address
 *
 *	Fills MAC header fields, depending on MAC type. Returns 0, If MAC type
 *	is a valid type and initialization completes correctly 1, otherwise.
 */
int llc_mac_hdr_init(struct sk_buff *skb, unsigned char *sa, unsigned char *da)
{
	int rc = 0;

	switch (skb->dev->type) {
#ifdef CONFIG_TR
	case ARPHRD_IEEE802_TR: {
		struct net_device *dev = skb->dev;
		struct trh_hdr *trh;
		
		trh = (struct trh_hdr *)skb_push(skb, sizeof(*trh));
		trh->ac = AC;
		trh->fc = LLC_FRAME;
		if (sa)
			memcpy(trh->saddr, sa, dev->addr_len);
		else
			memset(trh->saddr, 0, dev->addr_len);
		if (da) {
			memcpy(trh->daddr, da, dev->addr_len);
			tr_source_route(skb, trh, dev);
		}
		skb->mac.raw = skb->data;
		break;
	}
#endif
	case ARPHRD_ETHER:
	case ARPHRD_LOOPBACK: {
		unsigned short len = skb->len;
		struct ethhdr *eth;

		skb->mac.raw = skb_push(skb, sizeof(*eth));
		eth = (struct ethhdr *)skb->mac.raw;
		eth->h_proto = htons(len);
		memcpy(eth->h_dest, da, ETH_ALEN);
		memcpy(eth->h_source, sa, ETH_ALEN);
		break;
	}
	default:
		printk(KERN_WARNING "device type not supported: %d\n",
		       skb->dev->type);
		rc = -EINVAL;
	}
	return rc;
}

EXPORT_SYMBOL(llc_mac_hdr_init);
