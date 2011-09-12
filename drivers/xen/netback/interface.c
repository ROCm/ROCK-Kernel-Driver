/******************************************************************************
 * arch/xen/drivers/netif/backend/interface.c
 * 
 * Network-device interface management.
 * 
 * Copyright (c) 2004-2005, Keir Fraser
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "common.h"
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <xen/evtchn.h>

/*
 * Module parameter 'queue_length':
 * 
 * Enables queuing in the network stack when a client has run out of receive
 * descriptors. Although this feature can improve receive bandwidth by avoiding
 * packet loss, it can also result in packets sitting in the 'tx_queue' for
 * unbounded time. This is bad if those packets hold onto foreign resources.
 * For example, consider a packet that holds onto resources belonging to the
 * guest for which it is queued (e.g., packet received on vif1.0, destined for
 * vif1.1 which is not activated in the guest): in this situation the guest
 * will never be destroyed, unless vif1.1 is taken down. To avoid this, we
 * run a timer (tx_queue_timeout) to drain the queue when the interface is
 * blocked.
 */
static unsigned long netbk_queue_length = 32;
module_param_named(queue_length, netbk_queue_length, ulong, 0644);

static void __netif_up(netif_t *netif)
{
	unsigned int group = 0;
	unsigned int min_groups = atomic_read(&xen_netbk[0].nr_groups);
	unsigned int i;

	/* Find the list which contains least number of domains. */
	for (i = 1; i < netbk_nr_groups; i++) {
		unsigned int nr_groups = atomic_read(&xen_netbk[i].nr_groups);

		if (nr_groups < min_groups) {
			group = i;
			min_groups = nr_groups;
		}
	}

	atomic_inc(&xen_netbk[group].nr_groups);
	netif->group = group;

	enable_irq(netif->irq);
	netif_schedule_work(netif);
}

static void __netif_down(netif_t *netif)
{
	struct xen_netbk *netbk = xen_netbk + netif->group;

	disable_irq(netif->irq);
	netif_deschedule_work(netif);

	netif->group = UINT_MAX;
	atomic_dec(&netbk->nr_groups);
}

static int net_open(struct net_device *dev)
{
	netif_t *netif = netdev_priv(dev);
	if (netback_carrier_ok(netif)) {
		__netif_up(netif);
		netif_start_queue(dev);
	}
	return 0;
}

static int net_close(struct net_device *dev)
{
	netif_t *netif = netdev_priv(dev);
	if (netback_carrier_ok(netif))
		__netif_down(netif);
	netif_stop_queue(dev);
	return 0;
}

static int netbk_change_mtu(struct net_device *dev, int mtu)
{
	int max = netbk_can_sg(dev) ? 65535 - ETH_HLEN : ETH_DATA_LEN;

	if (mtu > max)
		return -EINVAL;
	dev->mtu = mtu;
	return 0;
}

static u32 netbk_fix_features(struct net_device *dev, u32 features)
{
	netif_t *netif = netdev_priv(dev);

	if (!netif->can_sg)
		features &= ~NETIF_F_SG;
	if (!netif->gso)
		features &= ~NETIF_F_TSO;
	if (!netif->csum)
		features &= ~NETIF_F_IP_CSUM;

	return features;
}

static void netbk_get_drvinfo(struct net_device *dev,
			      struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "netbk");
	strcpy(info->bus_info, dev_name(dev->dev.parent));
}

static const struct netif_stat {
	char name[ETH_GSTRING_LEN];
	u16 offset;
} netbk_stats[] = {
	{ "copied_skbs", offsetof(netif_t, nr_copied_skbs) / sizeof(long) },
	{ "rx_gso_csum_fixups", offsetof(netif_t, rx_gso_csum_fixups) / sizeof(long) },
};

static int netbk_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(netbk_stats);
	}
	return -EOPNOTSUPP;
}

static void netbk_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *stats, u64 * data)
{
	unsigned long *np = netdev_priv(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(netbk_stats); i++)
		data[i] = np[netbk_stats[i].offset];
}

static void netbk_get_strings(struct net_device *dev, u32 stringset, u8 * data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(netbk_stats); i++)
			memcpy(data + i * ETH_GSTRING_LEN,
			       netbk_stats[i].name, ETH_GSTRING_LEN);
		break;
	}
}

static const struct ethtool_ops network_ethtool_ops =
{
	.get_drvinfo = netbk_get_drvinfo,
	.get_link = ethtool_op_get_link,

	.get_sset_count = netbk_get_sset_count,
	.get_ethtool_stats = netbk_get_ethtool_stats,
	.get_strings = netbk_get_strings,
};

static const struct net_device_ops netif_be_netdev_ops = {
	.ndo_open               = net_open,
	.ndo_stop               = net_close,
	.ndo_start_xmit         = netif_be_start_xmit,
	.ndo_change_mtu	        = netbk_change_mtu,
	.ndo_fix_features       = netbk_fix_features,
	.ndo_set_mac_address    = eth_mac_addr,
	.ndo_validate_addr      = eth_validate_addr,
};

netif_t *netif_alloc(struct device *parent, domid_t domid, unsigned int handle)
{
	int err = 0;
	struct net_device *dev;
	netif_t *netif;
	char name[IFNAMSIZ] = {};

	snprintf(name, IFNAMSIZ - 1, "vif%u.%u", domid, handle);
	dev = alloc_netdev(sizeof(netif_t), name, ether_setup);
	if (dev == NULL) {
		DPRINTK("Could not create netif: out of memory\n");
		return ERR_PTR(-ENOMEM);
	}

	SET_NETDEV_DEV(dev, parent);

	netif = netdev_priv(dev);
	netif->domid  = domid;
	netif->group = UINT_MAX;
	netif->handle = handle;
	netif->can_sg = 1;
	netif->csum = 1;
	atomic_set(&netif->refcnt, 1);
	init_waitqueue_head(&netif->waiting_to_free);
	netif->dev = dev;

	netback_carrier_off(netif);

	netif->credit_bytes = netif->remaining_credit = ~0UL;
	netif->credit_usec  = 0UL;
	init_timer(&netif->credit_timeout);
	/* Initialize 'expires' now: it's used to track the credit window. */
	netif->credit_timeout.expires = jiffies;

	init_timer(&netif->tx_queue_timeout);

	dev->netdev_ops = &netif_be_netdev_ops;

	dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO;
	dev->features = dev->hw_features;

	SET_ETHTOOL_OPS(dev, &network_ethtool_ops);

	dev->tx_queue_len = netbk_queue_length;

	/*
	 * Initialise a dummy MAC address. We choose the numerically
	 * largest non-broadcast address to prevent the address getting
	 * stolen by an Ethernet bridge for STP purposes.
	 * (FE:FF:FF:FF:FF:FF)
	 */ 
	memset(dev->dev_addr, 0xFF, ETH_ALEN);
	dev->dev_addr[0] &= ~0x01;

	rtnl_lock();
	err = register_netdevice(dev);
	rtnl_unlock();
	if (err) {
		DPRINTK("Could not register new net device %s: err=%d\n",
			dev->name, err);
		free_netdev(dev);
		return ERR_PTR(err);
	}

	DPRINTK("Successfully created netif\n");
	return netif;
}

int netif_map(struct backend_info *be, grant_ref_t tx_ring_ref,
	      grant_ref_t rx_ring_ref, evtchn_port_t evtchn)
{
	netif_t *netif = be->netif;
	struct vm_struct *area;
	int err = -ENOMEM;
	netif_tx_sring_t *txs;
	netif_rx_sring_t *rxs;

	/* Already connected through? */
	if (netif->irq)
		return 0;

	area = xenbus_map_ring_valloc(be->dev, tx_ring_ref);
	if (IS_ERR(area))
		return PTR_ERR(area);
	netif->tx_comms_area = area;
	area = xenbus_map_ring_valloc(be->dev, rx_ring_ref);
	if (IS_ERR(area)) {
		err = PTR_ERR(area);
		goto err_rx;
	}
	netif->rx_comms_area = area;

	err = bind_interdomain_evtchn_to_irqhandler(
		netif->domid, evtchn, netif_be_int, 0,
		netif->dev->name, netif);
	if (err < 0)
		goto err_hypervisor;
	BUG_ON(err < DYNIRQ_BASE || err >= DYNIRQ_BASE + NR_DYNIRQS);
	netif->irq = err;
	disable_irq(netif->irq);

	txs = (netif_tx_sring_t *)netif->tx_comms_area->addr;
	BACK_RING_INIT(&netif->tx, txs, PAGE_SIZE);

	rxs = (netif_rx_sring_t *)
		((char *)netif->rx_comms_area->addr);
	BACK_RING_INIT(&netif->rx, rxs, PAGE_SIZE);

	netif->rx_req_cons_peek = 0;

	netif_get(netif);

	rtnl_lock();
	if (netif_running(netif->dev))
		__netif_up(netif);
	if (!netif->can_sg && netif->dev->mtu > ETH_DATA_LEN)
		dev_set_mtu(netif->dev, ETH_DATA_LEN);
	netdev_update_features(netif->dev);
	netback_carrier_on(netif);
	rtnl_unlock();

	return 0;
err_hypervisor:
	xenbus_unmap_ring_vfree(be->dev, netif->rx_comms_area);
err_rx:
	xenbus_unmap_ring_vfree(be->dev, netif->tx_comms_area);
	return err;
}

void netif_disconnect(struct backend_info *be)
{
	netif_t *netif = be->netif;

	if (netback_carrier_ok(netif)) {
		rtnl_lock();
		netback_carrier_off(netif);
		netif_carrier_off(netif->dev); /* discard queued packets */
		if (netif_running(netif->dev))
			__netif_down(netif);
		rtnl_unlock();
		netif_put(netif);
	}

	atomic_dec(&netif->refcnt);
	wait_event(netif->waiting_to_free, atomic_read(&netif->refcnt) == 0);

	del_timer_sync(&netif->credit_timeout);
	del_timer_sync(&netif->tx_queue_timeout);

	if (netif->irq)
		unbind_from_irqhandler(netif->irq, netif);
	
	unregister_netdev(netif->dev);

	if (netif->tx.sring) {
		xenbus_unmap_ring_vfree(be->dev, netif->tx_comms_area);
		xenbus_unmap_ring_vfree(be->dev, netif->rx_comms_area);
	}

	free_netdev(netif->dev);
}
