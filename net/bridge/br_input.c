/*
 *	Handle incoming frames
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_input.c,v 1.7 2000/12/13 16:44:14 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include "br_private.h"

unsigned char bridge_ula[6] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };

static void br_pass_frame_up(struct net_bridge *br, struct sk_buff *skb)
{
	br->statistics.rx_packets++;
	br->statistics.rx_bytes += skb->len;

	skb->dev = &br->dev;
	skb->pkt_type = PACKET_HOST;
	skb_pull(skb, skb->mac.raw - skb->data);
	skb->protocol = eth_type_trans(skb, &br->dev);
	netif_rx(skb);
}

static void __br_handle_frame(struct sk_buff *skb)
{
	struct net_bridge *br;
	unsigned char *dest;
	struct net_bridge_fdb_entry *dst;
	struct net_bridge_port *p;
	int passedup;

	dest = skb->mac.ethernet->h_dest;

	p = skb->dev->br_port;
	br = p->br;
	passedup = 0;

	if (!(br->dev.flags & IFF_UP) ||
	    p->state == BR_STATE_DISABLED)
		goto freeandout;

	skb_push(skb, skb->data - skb->mac.raw);

	if (br->dev.flags & IFF_PROMISC) {
		struct sk_buff *skb2;

		skb2 = skb_clone(skb, GFP_ATOMIC);
		if (skb2) {
			passedup = 1;
			br_pass_frame_up(br, skb2);
		}
	}

	if (skb->mac.ethernet->h_source[0] & 1)
		goto freeandout;

	if (!passedup &&
	    (dest[0] & 1) &&
	    (br->dev.flags & IFF_ALLMULTI || br->dev.mc_list != NULL)) {
		struct sk_buff *skb2;

		skb2 = skb_clone(skb, GFP_ATOMIC);
		if (skb2) {
			passedup = 1;
			br_pass_frame_up(br, skb2);
		}
	}

	if (br->stp_enabled &&
	    !memcmp(dest, bridge_ula, 5) &&
	    !(dest[5] & 0xF0))
		goto handle_special_frame;

	if (p->state == BR_STATE_LEARNING ||
	    p->state == BR_STATE_FORWARDING)
		br_fdb_insert(br, p, skb->mac.ethernet->h_source, 0);

	if (p->state != BR_STATE_FORWARDING)
		goto freeandout;

	if (dest[0] & 1) {
		br_flood(br, skb, 1);
		if (!passedup)
			br_pass_frame_up(br, skb);
		else
			kfree_skb(skb);
		return;
	}

	dst = br_fdb_get(br, dest);

	if (dst != NULL && dst->is_local) {
		if (!passedup)
			br_pass_frame_up(br, skb);
		else
			kfree_skb(skb);
		br_fdb_put(dst);
		return;
	}

	if (dst != NULL) {
		br_forward(dst->dst, skb);
		br_fdb_put(dst);
		return;
	}

	br_flood(br, skb, 0);
	return;

 handle_special_frame:
	if (!dest[5]) {
		br_stp_handle_bpdu(skb);
		return;
	}

 freeandout:
	kfree_skb(skb);
}

void br_handle_frame(struct sk_buff *skb)
{
	struct net_bridge *br;

	br = skb->dev->br_port->br;
	read_lock(&br->lock);
	__br_handle_frame(skb);
	read_unlock(&br->lock);
}
