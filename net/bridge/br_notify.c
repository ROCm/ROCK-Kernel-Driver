/*
 *	Device event handling
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_notify.c,v 1.2 2000/02/21 15:51:34 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>

#include "br_private.h"

static int br_device_event(struct notifier_block *unused, unsigned long event, void *ptr);

struct notifier_block br_device_notifier = {
	.notifier_call = br_device_event
};

static int br_device_event(struct notifier_block *unused, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct net_bridge_port *p = dev->br_port;
	struct net_bridge *br;

	if (p == NULL)
		return NOTIFY_DONE;

	br = p->br;
	if ( !(br->dev->flags & IFF_UP))
		return NOTIFY_DONE;

	if (event == NETDEV_CHANGEMTU) {
		dev_set_mtu(br->dev, br_min_mtu(br));
		return NOTIFY_DONE;
	}

	spin_lock_bh(&br->lock);
	switch (event) {
	case NETDEV_CHANGEADDR:
		br_fdb_changeaddr(p, dev->dev_addr);
		br_stp_recalculate_bridge_id(br);
		break;

	case NETDEV_CHANGE:	/* device is up but carrier changed */
		if (netif_carrier_ok(dev)) {
			if (p->state == BR_STATE_DISABLED)
				br_stp_enable_port(p);
		} else {
			if (p->state != BR_STATE_DISABLED)
				br_stp_disable_port(p);
		}
		break;

	case NETDEV_DOWN:
		br_stp_disable_port(p);
		break;

	case NETDEV_UP:
		if (netif_carrier_ok(dev)) 
			br_stp_enable_port(p);
		break;

	case NETDEV_UNREGISTER:
		br_del_if(br, dev);
		break;
	} 
	spin_unlock_bh(&br->lock);

	return NOTIFY_DONE;
}
