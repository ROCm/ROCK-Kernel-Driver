/*
 * eth1394.c -- Ethernet driver for Linux IEEE-1394 Subsystem
 * 
 * Copyright (C) 2001 Ben Collins <bcollins@debian.org>
 *               2000 Bonin Franck <boninf@free.fr>
 *
 * Mainly based on work by Emanuel Pirker and Andreas E. Bombe
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* State of this driver:
 *
 * This driver intends to support RFC 2734, which describes a method for
 * transporting IPv4 datagrams over IEEE-1394 serial busses. This driver
 * will ultimately support that method, but currently falls short in
 * several areas. A few issues are:
 *
 *   - Does not support send/recv over Async streams using GASP
 *     packet formats, as per the RFC for ARP requests.
 *   - Does not yet support fragmented packets.
 *   - Relies on hardware address being equal to the nodeid for some things.
 *   - Does not support multicast
 *   - Hardcoded address for sending packets, instead of using discovery
 *     (ARP, see first item)
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <asm/delay.h>
#include <asm/semaphore.h>
#include <asm/bitops.h>
#include <net/arp.h>

#include "ieee1394_types.h"
#include "ieee1394_core.h"
#include "ieee1394_transactions.h"
#include "ieee1394.h"
#include "highlevel.h"
#include "eth1394.h"

#define ETH1394_PRINT_G(level, fmt, args...) \
	printk(level ETHER1394_DRIVER_NAME": "fmt, ## args)

#define ETH1394_PRINT(level, dev_name, fmt, args...) \
	printk(level ETHER1394_DRIVER_NAME": %s: " fmt, dev_name, ## args)

#define DEBUG(fmt, args...) \
	printk(KERN_ERR fmt, ## args)

static char version[] __devinitdata =
	"$Rev: 546 $ Ben Collins <bcollins@debian.org>";

/* Our ieee1394 highlevel driver */
#define ETHER1394_DRIVER_NAME "ether1394"

static kmem_cache_t *packet_task_cache;
static struct hpsb_highlevel *hl_handle = NULL;

/* Card handling */
static LIST_HEAD (host_info_list);
static spinlock_t host_info_lock = SPIN_LOCK_UNLOCKED;

/* Use common.lf to determine header len */
static int hdr_type_len[] = {
	sizeof (struct eth1394_uf_hdr),
	sizeof (struct eth1394_ff_hdr),
	sizeof (struct eth1394_sf_hdr),
	sizeof (struct eth1394_sf_hdr)
};

MODULE_AUTHOR("Ben Collins (bcollins@debian.org)");
MODULE_DESCRIPTION("IEEE 1394 IPv4 Driver (IPv4-over-1394 as per RFC 2734)");
MODULE_LICENSE("GPL");

/* Find our host_info struct for a given host pointer. Must be called
 * under spinlock.  */
static inline struct host_info *find_host_info (struct hpsb_host *host)
{
	struct list_head *lh;
	struct host_info *hi;

	lh = host_info_list.next;
	while (lh != &host_info_list) {
		hi = list_entry (lh, struct host_info, list);

		if (hi->host == host)
			return hi;

		lh = lh->next;
	}
	return NULL;
}

/* Find the network device for our host */
static inline struct net_device *ether1394_find_dev (struct hpsb_host *host)
{
	struct host_info *hi;

	spin_lock_irq (&host_info_lock);
	hi = find_host_info (host);
	spin_unlock_irq (&host_info_lock);

	if (hi == NULL)
		return NULL;

	return hi->dev;
}

/* This is called after an "ifup" */
static int ether1394_open (struct net_device *dev)
{
	struct eth1394_priv *priv = (struct eth1394_priv *)dev->priv;

	/* Set the spinlock before grabbing IRQ! */
	priv->lock = SPIN_LOCK_UNLOCKED;

	netif_start_queue (dev);
	return 0;
}

/* This is called after an "ifdown" */
static int ether1394_stop (struct net_device *dev)
{
	netif_stop_queue (dev);
	return 0;
}

/* Return statistics to the caller */
static struct net_device_stats *ether1394_stats (struct net_device *dev)
{
	return &(((struct eth1394_priv *)dev->priv)->stats);
}

/* What to do if we timeout. I think a host reset is probably in order, so
 * that's what we do. Should we increment the stat counters too?  */
static void ether1394_tx_timeout (struct net_device *dev)
{
	ETH1394_PRINT (KERN_ERR, dev->name, "Timeout, resetting host %s\n",
		       ((struct eth1394_priv *)(dev->priv))->host->driver->name);

	highlevel_host_reset (((struct eth1394_priv *)(dev->priv))->host);

	netif_wake_queue (dev);
}

/* We need to encapsulate the standard header with our own. We use the
 * ethernet header's proto for our own.
 *
 * XXX: This is where we need to create a list of skb's for fragmented
 * packets.  */
static inline void ether1394_encapsulate (struct sk_buff *skb, struct net_device *dev,
			    int proto)
{
	union eth1394_hdr *hdr =
		(union eth1394_hdr *)skb_push (skb, hdr_type_len[ETH1394_HDR_LF_UF]);

	hdr->common.lf = ETH1394_HDR_LF_UF;
	hdr->words.word1 = htons(hdr->words.word1);
	hdr->uf.ether_type = proto;

	return;
}

/* Convert a standard ARP packet to 1394 ARP. The first 8 bytes (the
 * entire arphdr) is the same format as the ip1394 header, so they
 * overlap. The rest needs to be munged a bit. The remainder of the
 * arphdr is formatted based on hwaddr len and ipaddr len. We know what
 * they'll be, so it's easy to judge.  */
static inline void ether1394_arp_to_1394arp (struct sk_buff *skb, struct net_device *dev)
{
	struct eth1394_priv *priv =
		(struct eth1394_priv *)(dev->priv);
	u16 phy_id = priv->host->node_id & NODE_MASK;

	unsigned char *arp_ptr = (unsigned char *)skb->data;
	struct eth1394_arp *arp1394 = (struct eth1394_arp *)skb->data;
	unsigned char arp_data[2*(dev->addr_len+4)];

	/* Copy the main data that we need */
	arp_ptr = memcpy (arp_data, arp_ptr + sizeof(struct arphdr), sizeof (arp_data));

	/* Extend the buffer enough for our new header */
	skb_put (skb, sizeof (struct eth1394_arp) -
		 (sizeof (arp_data) + sizeof (struct arphdr)));

#define PROCESS_MEMBER(ptr,val,len) \
  memcpy (val, ptr, len); ptr += len
	arp_ptr += arp1394->hw_addr_len;
	PROCESS_MEMBER (arp_ptr, &arp1394->sip, arp1394->ip_addr_len);
	arp_ptr += arp1394->hw_addr_len;
	PROCESS_MEMBER (arp_ptr, &arp1394->tip, arp1394->ip_addr_len);
#undef PROCESS_MEMBER

	/* Now add our own flavor of arp header fields to the orig one */
	arp1394->hw_addr_len	= IP1394_HW_ADDR_LEN;
	arp1394->hw_type	= __constant_htons (ARPHRD_IEEE1394);
	arp1394->s_uniq_id	= cpu_to_le64 (priv->eui[phy_id]);
	arp1394->max_rec	= priv->max_rec[phy_id];
	arp1394->sspd		= priv->sspd[phy_id];
	arp1394->fifo_hi	= htons (priv->fifo_hi[phy_id]);
	arp1394->fifo_lo	= htonl (priv->fifo_lo[phy_id]);

	return;
}

static int ether1394_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > ETHER1394_REGION_ADDR_LEN))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static inline void ether1394_register_limits (int nodeid, unsigned char max_rec,
				       unsigned char sspd, u64 eui, u16 fifo_hi,
				       u32 fifo_lo, struct eth1394_priv *priv)
{
	int i;

	if (nodeid < 0 || nodeid >= ALL_NODES) {
		ETH1394_PRINT_G (KERN_ERR, "Cannot register invalid nodeid %d\n", nodeid);
		return;
	}

	priv->max_rec[nodeid]	= max_rec;
	priv->sspd[nodeid]	= sspd;
	priv->fifo_hi[nodeid]	= fifo_hi;
	priv->fifo_lo[nodeid]	= fifo_lo;
	priv->eui[nodeid]	= eui;

	/* 63 is used for broadcasts to all hosts. It is equal to the
	 * minimum of all registered nodes. A registered node is one with
	 * a nonzero offset. Set the values rediculously high to start. We
	 * know we have atleast one to change the default to.  */
	sspd = 0xff;
	max_rec = 0xff;
	for (i = 0; i < ALL_NODES; i++) {
		if (!priv->fifo_hi && !priv->fifo_lo) continue; /* Unregistered */
		if (priv->max_rec[i] < max_rec) max_rec = priv->max_rec[i];
		if (priv->sspd[i] < sspd) sspd = priv->sspd[i];
	}

	priv->max_rec[ALL_NODES] = max_rec;
	priv->sspd[ALL_NODES] = sspd;

	return;
}

static void ether1394_reset_priv (struct net_device *dev, int set_mtu)
{
	unsigned long flags;
	struct eth1394_priv *priv = (struct eth1394_priv *)dev->priv;
	int phy_id = priv->host->node_id & NODE_MASK;

	spin_lock_irqsave (&priv->lock, flags);

	/* Clear the speed/payload/offset tables */
	memset (priv->max_rec, 0, sizeof (priv->max_rec));
	memset (priv->sspd, 0, sizeof (priv->sspd));
	memset (priv->fifo_hi, 0, sizeof (priv->fifo_hi));
	memset (priv->fifo_lo, 0, sizeof (priv->fifo_lo));

	/* Register our limits now */
	ether1394_register_limits (phy_id, (be32_to_cpu(priv->host->csr.rom[2]) >> 12) & 0xf,
				   priv->host->speed_map[(phy_id << 6) + phy_id],
				   (u64)(((u64)be32_to_cpu(priv->host->csr.rom[3]) << 32) |
				   be32_to_cpu(priv->host->csr.rom[4])),
				   ETHER1394_REGION_ADDR >> 32,
				   ETHER1394_REGION_ADDR & 0xffffffff, priv);

	/* We'll use our max_rec as the default mtu */
	if (set_mtu)
		dev->mtu = (1 << (priv->max_rec[phy_id] + 1)) - sizeof (union eth1394_hdr);

	/* Set our hardware address while we're at it */
	*(nodeid_t *)dev->dev_addr = htons (priv->host->node_id);

	spin_unlock_irqrestore (&priv->lock, flags);
}

static int ether1394_tx (struct sk_buff *skb, struct net_device *dev);

/* This function is called by register_netdev */
static int ether1394_init_dev (struct net_device *dev)
{
	/* Our functions */
	dev->open		= ether1394_open;
	dev->stop		= ether1394_stop;
	dev->hard_start_xmit	= ether1394_tx;
	dev->get_stats		= ether1394_stats;
	dev->tx_timeout		= ether1394_tx_timeout;
	dev->change_mtu		= ether1394_change_mtu;

	/* Some constants */
	dev->watchdog_timeo	= ETHER1394_TIMEOUT;
	dev->flags		= IFF_BROADCAST; /* TODO: Support MCAP */
	dev->features		= NETIF_F_NO_CSUM|NETIF_F_SG|NETIF_F_HIGHDMA|NETIF_F_FRAGLIST;
	dev->addr_len		= 2;

	ether1394_reset_priv (dev, 1);

	return 0;
}

/*
 * This function is called every time a card is found. It is generally called
 * when the module is installed. This is where we add all of our ethernet
 * devices. One for each host.
 */
static void ether1394_add_host (struct hpsb_host *host)
{
	struct host_info *hi = NULL;
	struct net_device *dev = NULL;
	struct eth1394_priv *priv;
	static int version_printed = 0;

	if (version_printed++ == 0)
		ETH1394_PRINT_G (KERN_INFO, "%s\n", version);

	dev = alloc_etherdev(sizeof (struct eth1394_priv));

	if (dev == NULL)
		goto out;

	SET_MODULE_OWNER(dev);

	dev->init = ether1394_init_dev;

	priv = (struct eth1394_priv *)dev->priv;

	priv->host = host;

	hi = (struct host_info *)kmalloc (sizeof (struct host_info),
					  GFP_KERNEL);

	if (hi == NULL)
		goto out;

	if (register_netdev (dev)) {
		ETH1394_PRINT (KERN_ERR, dev->name, "Error registering network driver\n");
		kfree (dev);
		return;
	}

	ETH1394_PRINT (KERN_ERR, dev->name, "IEEE-1394 IPv4 over 1394 Ethernet (%s)\n",
		       host->driver->name);

	INIT_LIST_HEAD (&hi->list);
	hi->host = host;
	hi->dev = dev;

	spin_lock_irq (&host_info_lock);
	list_add_tail (&hi->list, &host_info_list);
	spin_unlock_irq (&host_info_lock);

	return;

out:
	if (dev != NULL)
		kfree (dev);
	ETH1394_PRINT_G (KERN_ERR, "Out of memory\n");

	return;
}

/* Remove a card from our list */
static void ether1394_remove_host (struct hpsb_host *host)
{
	struct host_info *hi;

	spin_lock_irq (&host_info_lock);
	hi = find_host_info (host);
	if (hi != NULL) {
		unregister_netdev (hi->dev);
		kfree (hi->dev);
		list_del (&hi->list);
		kfree (hi);
	}
	spin_unlock_irq (&host_info_lock);

	return;
}

/* A reset has just arisen */
static void ether1394_host_reset (struct hpsb_host *host)
{
	struct net_device *dev = ether1394_find_dev(host);

	/* This can happen for hosts that we don't use */
	if (dev == NULL)
		return;

	/* Reset our private host data, but not our mtu */
	netif_stop_queue (dev);
	ether1394_reset_priv (dev, 0);
	netif_wake_queue (dev);
}

/* Copied from net/ethernet/eth.c */
static inline unsigned short ether1394_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct ethhdr *eth;
	unsigned char *rawp;

	skb->mac.raw = skb->data;
	skb_pull (skb, ETH_HLEN);
	eth = skb->mac.ethernet;
#if 0
	if(*eth->h_dest & 1) {
		if(memcmp(eth->h_dest, dev->broadcast, dev->addr_len)==0)
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
	} else {
		if(memcmp(eth->h_dest, dev->dev_addr, dev->addr_len))
			skb->pkt_type = PACKET_OTHERHOST;
        }
#endif
	if (ntohs (eth->h_proto) >= 1536)
		return eth->h_proto;

	rawp = skb->data;

        if (*(unsigned short *)rawp == 0xFFFF)
		return htons (ETH_P_802_3);

        return htons (ETH_P_802_2);
}

/* Parse an encapsulated IP1394 header into an ethernet frame packet.
 * We also perform ARP translation here, if need be.  */
static inline unsigned short ether1394_parse_encap (struct sk_buff *skb, struct net_device *dev,
					     nodeid_t srcid, nodeid_t destid)
{
	union eth1394_hdr *hdr = (union eth1394_hdr *)skb->data;
	unsigned char src_hw[ETH_ALEN], dest_hw[ETH_ALEN];
	unsigned short ret = 0;

	/* Setup our hw addresses. We use these to build the
	 * ethernet header.  */
	*(u16 *)dest_hw = htons(destid);
	*(u16 *)src_hw = htons(srcid);

	/* Remove the encapsulation header */
	hdr->words.word1 = ntohs(hdr->words.word1);
	skb_pull (skb, hdr_type_len[hdr->common.lf]);

	/* If this is an ARP packet, convert it. First, we want to make
	 * use of some of the fields, since they tell us a little bit
	 * about the sending machine.  */
	if (hdr->uf.ether_type == __constant_htons (ETH_P_ARP)) {
		unsigned long flags;
		u16 phy_id = srcid & NODE_MASK;
		struct eth1394_priv *priv =
			(struct eth1394_priv *)dev->priv;
		struct eth1394_arp arp1394;
		struct arphdr *arp = (struct arphdr *)skb->data;
		unsigned char *arp_ptr = (unsigned char *)(arp + 1);

		memcpy (&arp1394, arp, sizeof (struct eth1394_arp));

		/* Update our speed/payload/fifo_offset table */
		spin_lock_irqsave (&priv->lock, flags);
		ether1394_register_limits (phy_id, arp1394.max_rec, arp1394.sspd,
					   le64_to_cpu (arp1394.s_uniq_id),
					   ntohs (arp1394.fifo_hi),
					   ntohl (arp1394.fifo_lo), priv);
		spin_unlock_irqrestore (&priv->lock, flags);

#define PROCESS_MEMBER(ptr,val,len) \
  ptr = memcpy (ptr, val, len) + len
                PROCESS_MEMBER (arp_ptr, src_hw, dev->addr_len);
                PROCESS_MEMBER (arp_ptr, &arp1394.sip, 4);
                PROCESS_MEMBER (arp_ptr, dest_hw, dev->addr_len);
                PROCESS_MEMBER (arp_ptr, &arp1394.tip, 4);
#undef PROCESS_MEMBER

		arp->ar_hln = dev->addr_len;
		arp->ar_hrd = __constant_htons (ARPHRD_ETHER);

		skb_trim (skb, sizeof (struct arphdr) + 2*(dev->addr_len+4));
	}

	/* Now add the ethernet header. */
	if (dev->hard_header (skb, dev, __constant_ntohs (hdr->uf.ether_type),
			      dest_hw, src_hw, skb->len) >= 0)
		ret = ether1394_type_trans(skb, dev);

	return ret;
}

/* Packet reception. We convert the IP1394 encapsulation header to an
 * ethernet header, and fill it with some of our other fields. This is
 * an incoming packet from the 1394 bus.  */
static int ether1394_write (struct hpsb_host *host, int srcid, int destid,
			    quadlet_t *data, u64 addr, unsigned int len)
{
	struct sk_buff *skb;
	char *buf = (char *)data;
	unsigned long flags;
	struct net_device *dev = ether1394_find_dev (host);
	struct eth1394_priv *priv;

	if (dev == NULL) {
		ETH1394_PRINT_G (KERN_ERR, "Could not find net device for host %p\n",
				 host);
		return RCODE_ADDRESS_ERROR;
	}

	priv = (struct eth1394_priv *)dev->priv;

	/* A packet has been received by the ieee1394 bus. Build an skbuff
	 * around it so we can pass it to the high level network layer. */

	skb = dev_alloc_skb (len + dev->hard_header_len + 15);
	if (!skb) {
		HPSB_PRINT (KERN_ERR, "ether1394 rx: low on mem\n");
		priv->stats.rx_dropped++;
		return RCODE_ADDRESS_ERROR;
	}

	skb_reserve(skb, (dev->hard_header_len + 15) & ~15);

	memcpy (skb_put (skb, len), buf, len);

	/* Write metadata, and then pass to the receive level */
	skb->dev = dev;
	skb->ip_summed = CHECKSUM_UNNECESSARY;	/* don't check it */

	/* Parse the encapsulation header. This actually does the job of
	 * converting to an ethernet frame header, aswell as arp
	 * conversion if needed. ARP conversion is easier in this
	 * direction, since we are using ethernet as our backend.  */
	skb->protocol = ether1394_parse_encap (skb, dev, srcid, destid);

	spin_lock_irqsave (&priv->lock, flags);
	if (!skb->protocol) {
		priv->stats.rx_errors++;
		priv->stats.rx_dropped++;
		dev_kfree_skb_any(skb);
		goto bad_proto;
	}

	netif_stop_queue(dev);
	if (netif_rx (skb) == NET_RX_DROP) {
		priv->stats.rx_errors++;
		priv->stats.rx_dropped++;
		goto bad_proto;
	}

	/* Statistics */
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += skb->len;

bad_proto:
	netif_start_queue(dev);
	spin_unlock_irqrestore (&priv->lock, flags);

	dev->last_rx = jiffies;

	return RCODE_COMPLETE;
}

/* This function is our scheduled write */
static void hpsb_write_sched (void *__ptask)
{
	struct packet_task *ptask = (struct packet_task *)__ptask;
	struct sk_buff *skb = ptask->skb;
	struct net_device *dev = ptask->skb->dev;
	struct eth1394_priv *priv = (struct eth1394_priv *)dev->priv;
        unsigned long flags;

	/* Statistics */
	spin_lock_irqsave (&priv->lock, flags);
	if (!hpsb_write(priv->host, ptask->dest_node,
			get_hpsb_generation(priv->host),
			ptask->addr, (quadlet_t *)skb->data, skb->len)) {
		priv->stats.tx_bytes += skb->len;
		priv->stats.tx_packets++;
	} else {
		//printk("Failed in hpsb_write_sched\n");
		priv->stats.tx_dropped++;
		priv->stats.tx_errors++;
		if (netif_queue_stopped (dev))
			netif_wake_queue (dev);
	}
	spin_unlock_irqrestore (&priv->lock, flags);

	dev->trans_start = jiffies;
	dev_kfree_skb(skb);
	kmem_cache_free(packet_task_cache, ptask);

	return;
}

/* Transmit a packet (called by kernel) */
static int ether1394_tx (struct sk_buff *skb, struct net_device *dev)
{
	int kmflags = in_interrupt () ? GFP_ATOMIC : GFP_KERNEL;
	struct ethhdr *eth;
	struct eth1394_priv *priv = (struct eth1394_priv *)dev->priv;
	int proto;
	unsigned long flags;
	nodeid_t dest_node;
	u64 addr;
	struct packet_task *ptask = NULL;
	int ret = 0;

	if ((skb = skb_share_check (skb, kmflags)) == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	/* Get rid of the ethernet header, but save a pointer */
	eth = (struct ethhdr *)skb->data;
	skb_pull (skb, ETH_HLEN);

	/* Save the destination id, and proto for our encapsulation, then
	 * toss the ethernet header aside like the cheap whore it is.  */
	dest_node = ntohs (*(nodeid_t *)(eth->h_dest));
	proto = eth->h_proto;

	/* If this is an ARP packet, convert it */
	if (proto == __constant_htons (ETH_P_ARP))
		ether1394_arp_to_1394arp (skb, dev);

	/* Now add our encapsulation header */
	ether1394_encapsulate (skb, dev, proto);

	/* TODO: The above encapsulate function needs to recognize when a
	 * packet needs to be split for a specified node. It should create
	 * a list of skb's that we could then iterate over for the below
	 * call to schedule our writes.  */

	/* XXX: Right now we accept that we don't exactly follow RFC. When
	 * we do, we will send ARP requests via GASP format, and so we wont
	 * need this hack.  */

	spin_lock_irqsave (&priv->lock, flags);
	addr = (u64)priv->fifo_hi[dest_node & NODE_MASK] << 32 |
		priv->fifo_lo[dest_node & NODE_MASK];
	spin_unlock_irqrestore (&priv->lock, flags);

	if (!addr)
		addr = ETHER1394_REGION_ADDR;

	ptask = kmem_cache_alloc(packet_task_cache, kmflags);
	if (ptask == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	ptask->skb = skb;
	ptask->addr = addr;
	ptask->dest_node = dest_node;
	INIT_TQUEUE(&ptask->tq, hpsb_write_sched, ptask);
	schedule_task(&ptask->tq);

	return 0;
fail:
	printk("Failed in ether1394_tx\n");

	if (skb != NULL)
		dev_kfree_skb (skb);

	spin_lock_irqsave (&priv->lock, flags);
	priv->stats.tx_dropped++;
	priv->stats.tx_errors++;
	if (netif_queue_stopped (dev))
		netif_wake_queue (dev);
	spin_unlock_irqrestore (&priv->lock, flags);

	return ret;
}

/* Function for incoming 1394 packets */
static struct hpsb_address_ops addr_ops = {
	.write =	ether1394_write,
};

/* Ieee1394 highlevel driver functions */
static struct hpsb_highlevel_ops hl_ops = {
	.add_host =	ether1394_add_host,
	.remove_host =	ether1394_remove_host,
	.host_reset =	ether1394_host_reset,
};

static int __init ether1394_init_module (void)
{
	packet_task_cache = kmem_cache_create("packet_task", sizeof(struct packet_task),
					      0, 0, NULL, NULL);

	/* Register ourselves as a highlevel driver */
	hl_handle = hpsb_register_highlevel (ETHER1394_DRIVER_NAME, &hl_ops);

	if (hl_handle == NULL) {
		ETH1394_PRINT_G (KERN_ERR, "No more memory for driver\n");
		return -ENOMEM;
	}

	hpsb_register_addrspace (hl_handle, &addr_ops, ETHER1394_REGION_ADDR,
				 ETHER1394_REGION_ADDR_END);

	return 0;
}

static void __exit ether1394_exit_module (void)
{
	hpsb_unregister_highlevel (hl_handle);
	kmem_cache_destroy(packet_task_cache);
}

module_init(ether1394_init_module);
module_exit(ether1394_exit_module);
