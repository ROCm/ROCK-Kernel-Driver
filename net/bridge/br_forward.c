/*
 *	Forwarding decision
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_forward.c,v 1.2 2000/02/21 15:51:33 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/skbuff.h>
#include <linux/if_bridge.h>
#include "br_private.h"

static inline int should_forward(struct net_bridge_port *p, struct sk_buff *skb)
{
	if (skb->dev == p->dev ||
	    p->state != BR_STATE_FORWARDING)
		return 0;

	return 1;
}

static void __br_forward(struct net_bridge_port *to, struct sk_buff *skb)
{
	skb->dev = to->dev;
	dev_queue_xmit(skb);
}

/* called under bridge lock */
void br_forward(struct net_bridge_port *to, struct sk_buff *skb)
{
	if (should_forward(to, skb)) {
		__br_forward(to, skb);
		return;
	}

	kfree_skb(skb);
}

/* called under bridge lock */
void br_flood(struct net_bridge *br, struct sk_buff *skb, int clone)
{
	struct net_bridge_port *p;
	struct net_bridge_port *prev;

	if (clone) {
		struct sk_buff *skb2;

		if ((skb2 = skb_clone(skb, GFP_ATOMIC)) == NULL) {
			br->statistics.tx_dropped++;
			return;
		}

		skb = skb2;
	}

	prev = NULL;

	p = br->port_list;
	while (p != NULL) {
		if (should_forward(p, skb)) {
			if (prev != NULL) {
				struct sk_buff *skb2;

				if ((skb2 = skb_clone(skb, GFP_ATOMIC)) == NULL) {
					br->statistics.tx_dropped++;
					kfree_skb(skb);
					return;
				}

				__br_forward(prev, skb2);
			}

			prev = p;
		}

		p = p->next;
	}

	if (prev != NULL) {
		__br_forward(prev, skb);
		return;
	}

	kfree_skb(skb);
}
