/*
 *	Device handling code
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_device.c,v 1.6 2001/12/24 00:59:55 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/if_bridge.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include "br_private.h"

static int br_dev_do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	unsigned long args[4];
	unsigned long *data;

	if (cmd != SIOCDEVPRIVATE)
		return -EOPNOTSUPP;

	data = (unsigned long *)rq->ifr_data;
	if (copy_from_user(args, data, 4*sizeof(unsigned long)))
		return -EFAULT;

	return br_ioctl(dev->priv, args[0], args[1], args[2], args[3]);
}

static struct net_device_stats *br_dev_get_stats(struct net_device *dev)
{
	struct net_bridge *br;

	br = dev->priv;

	return &br->statistics;
}

static int __br_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_bridge *br;
	unsigned char *dest;
	struct net_bridge_fdb_entry *dst;

	br = dev->priv;
	br->statistics.tx_packets++;
	br->statistics.tx_bytes += skb->len;

	dest = skb->mac.raw = skb->data;
	skb_pull(skb, ETH_HLEN);

	if (dest[0] & 1) {
		br_flood_deliver(br, skb, 0);
		return 0;
	}

	if ((dst = br_fdb_get(br, dest)) != NULL) {
		br_deliver(dst->dst, skb);
		br_fdb_put(dst);
		return 0;
	}

	br_flood_deliver(br, skb, 0);
	return 0;
}

int br_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int ret;

	rcu_read_lock();
	ret = __br_dev_xmit(skb, dev);
	rcu_read_unlock();

	return ret;
}

static int br_dev_open(struct net_device *dev)
{
	netif_start_queue(dev);

	br_stp_enable_bridge(dev->priv);

	return 0;
}

static void br_dev_set_multicast_list(struct net_device *dev)
{
}

static int br_dev_stop(struct net_device *dev)
{
	br_stp_disable_bridge(dev->priv);

	netif_stop_queue(dev);

	return 0;
}

static int br_dev_accept_fastpath(struct net_device *dev, struct dst_entry *dst)
{
	return -1;
}

void br_dev_setup(struct net_device *dev)
{
	memset(dev->dev_addr, 0, ETH_ALEN);

	ether_setup(dev);

	dev->do_ioctl = br_dev_do_ioctl;
	dev->get_stats = br_dev_get_stats;
	dev->hard_start_xmit = br_dev_xmit;
	dev->open = br_dev_open;
	dev->set_multicast_list = br_dev_set_multicast_list;
	dev->destructor = free_netdev;
	SET_MODULE_OWNER(dev);
	dev->stop = br_dev_stop;
	dev->accept_fastpath = br_dev_accept_fastpath;
	dev->tx_queue_len = 0;
	dev->set_mac_address = NULL;
	dev->priv_flags = IFF_EBRIDGE;
}
