/******************************************************************************
 * netback/loopback.c
 * 
 * A two-interface loopback device to emulate a local netfront-netback
 * connection. This ensures that local packet delivery looks identical
 * to inter-domain delivery. Most importantly, packets delivered locally
 * originating from other domains will get *copied* when they traverse this
 * driver. This prevents unbounded delays in socket-buffer queues from
 * causing the netback driver to "seize up".
 * 
 * This driver creates a symmetric pair of loopback interfaces with names
 * vif0.0 and veth0. The intention is that 'vif0.0' is bound to an Ethernet
 * bridge, just like a proper netback interface, while a local IP interface
 * is configured on 'veth0'.
 * 
 * As with a real netback interface, vif0.0 is configured with a suitable
 * dummy MAC address. No default is provided for veth0: a reasonable strategy
 * is to transfer eth0's MAC address to veth0, and give eth0 a dummy address
 * (to avoid confusing the Etherbridge).
 * 
 * Copyright (c) 2005 K A Fraser
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

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <net/dst.h>
#include <net/xfrm.h>		/* secpath_reset() */
#include <asm/hypervisor.h>	/* is_initial_xendomain() */
#include <../net/core/kmap_skb.h> /* k{,un}map_skb_frag() */

static int nloopbacks = -1;
module_param(nloopbacks, int, 0);
MODULE_PARM_DESC(nloopbacks, "Number of netback-loopback devices to create");

struct net_private {
	struct net_device *loopback_dev;
	int loop_idx;
};

static inline struct net_private *loopback_priv(struct net_device *dev)
{
	return netdev_priv(dev);
}

static int loopback_open(struct net_device *dev)
{
	memset(&dev->stats, 0, sizeof(dev->stats));
	netif_start_queue(dev);
	return 0;
}

static int loopback_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

#ifdef CONFIG_X86
static int is_foreign(unsigned long pfn)
{
	/* NB. Play it safe for auto-translation mode. */
	return (xen_feature(XENFEAT_auto_translated_physmap) ||
		(phys_to_machine_mapping[pfn] & FOREIGN_FRAME_BIT));
}
#else
/* How to detect a foreign mapping? Play it safe. */
#define is_foreign(pfn)	(1)
#endif

static int skb_remove_foreign_references(struct sk_buff *skb)
{
	struct page *page;
	unsigned long pfn;
	int i, off;
	char *vaddr;

	BUG_ON(skb_shinfo(skb)->frag_list);

	if (skb_cloned(skb) &&
	    unlikely(pskb_expand_head(skb, 0, 0, GFP_ATOMIC)))
		return 0;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		pfn = page_to_pfn(skb_shinfo(skb)->frags[i].page);
		if (!is_foreign(pfn))
			continue;
		
		page = alloc_page(GFP_ATOMIC | __GFP_NOWARN);
		if (unlikely(!page))
			return 0;

		vaddr = kmap_skb_frag(&skb_shinfo(skb)->frags[i]);
		off = skb_shinfo(skb)->frags[i].page_offset;
		memcpy(page_address(page) + off,
		       vaddr + off,
		       skb_shinfo(skb)->frags[i].size);
		kunmap_skb_frag(vaddr);

		put_page(skb_shinfo(skb)->frags[i].page);
		skb_shinfo(skb)->frags[i].page = page;
	}

	return 1;
}

static int loopback_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	if (!skb_remove_foreign_references(skb)) {
		dev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	dst_release(skb_dst(skb));
	skb_dst_set(skb, NULL);

	skb_orphan(skb);

	dev->stats.tx_bytes += skb->len;
	dev->stats.tx_packets++;

	/* Switch to loopback context. */
	dev = loopback_priv(dev)->loopback_dev;

	dev->stats.rx_bytes += skb->len;
	dev->stats.rx_packets++;

	skb->pkt_type = PACKET_HOST; /* overridden by eth_type_trans() */
	skb->protocol = eth_type_trans(skb, dev);

	/* Flush netfilter context: rx'ed skbuffs not expected to have any. */
	nf_reset(skb);
	secpath_reset(skb);

	netif_rx(skb);

	return NETDEV_TX_OK;
}

static void get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "netloop");
	snprintf(info->bus_info, ETHTOOL_BUSINFO_LEN, "vif-0-%d",
		 loopback_priv(dev)->loop_idx);
}

static const struct ethtool_ops network_ethtool_ops =
{
	.get_drvinfo = get_drvinfo,

	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = ethtool_op_set_tx_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_tso = ethtool_op_get_tso,
	.set_tso = ethtool_op_set_tso,
	.get_link = ethtool_op_get_link,
};

/*
 * Nothing to do here. Virtual interface is point-to-point and the
 * physical interface is probably promiscuous anyway.
 */
static void loopback_set_multicast_list(struct net_device *dev)
{
}

static const struct net_device_ops loopback_netdev_ops = {
	.ndo_open               = loopback_open,
	.ndo_stop               = loopback_close,
	.ndo_start_xmit         = loopback_start_xmit,
	.ndo_set_multicast_list = loopback_set_multicast_list,
	.ndo_change_mtu	        = NULL, /* allow arbitrary mtu */
};

static void loopback_construct(struct net_device *dev, struct net_device *lo,
			       int loop_idx)
{
	struct net_private *np = loopback_priv(dev);

	np->loopback_dev     = lo;
	np->loop_idx         = loop_idx;

	dev->netdev_ops      = &loopback_netdev_ops;
	dev->tx_queue_len    = 0;

	dev->features        = (NETIF_F_HIGHDMA |
				NETIF_F_LLTX |
				NETIF_F_TSO |
				NETIF_F_SG |
				NETIF_F_IP_CSUM);

	SET_ETHTOOL_OPS(dev, &network_ethtool_ops);

	/*
	 * We do not set a jumbo MTU on the interface. Otherwise the network
	 * stack will try to send large packets that will get dropped by the
	 * Ethernet bridge (unless the physical Ethernet interface is
	 * configured to transfer jumbo packets). If a larger MTU is desired
	 * then the system administrator can specify it using the 'ifconfig'
	 * command.
	 */
	/*dev->mtu             = 16*1024;*/
}

static int __init make_loopback(int i)
{
	struct net_device *dev1, *dev2;
	char dev_name[IFNAMSIZ];
	int err = -ENOMEM;

	sprintf(dev_name, "vif0.%d", i);
	dev1 = alloc_netdev(sizeof(struct net_private), dev_name, ether_setup);
	if (!dev1)
		return err;

	sprintf(dev_name, "veth%d", i);
	dev2 = alloc_netdev(sizeof(struct net_private), dev_name, ether_setup);
	if (!dev2)
		goto fail_netdev2;

	loopback_construct(dev1, dev2, i);
	loopback_construct(dev2, dev1, i);

	/*
	 * Initialise a dummy MAC address for the 'dummy backend' interface. We
	 * choose the numerically largest non-broadcast address to prevent the
	 * address getting stolen by an Ethernet bridge for STP purposes.
	 */
	memset(dev1->dev_addr, 0xFF, ETH_ALEN);
	dev1->dev_addr[0] &= ~0x01;

	if ((err = register_netdev(dev1)) != 0)
		goto fail;

	if ((err = register_netdev(dev2)) != 0) {
		unregister_netdev(dev1);
		goto fail;
	}

	return 0;

 fail:
	free_netdev(dev2);
 fail_netdev2:
	free_netdev(dev1);
	return err;
}

static int __init loopback_init(void)
{
	int i, err = 0;

	if (nloopbacks == -1)
		nloopbacks = is_initial_xendomain() ? 4 : 0;

	for (i = 0; i < nloopbacks; i++)
		if ((err = make_loopback(i)) != 0)
			break;

	return err;
}

module_init(loopback_init);

MODULE_LICENSE("Dual BSD/GPL");
