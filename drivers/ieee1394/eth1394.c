/*
 * eth1394.c -- Ethernet driver for Linux IEEE-1394 Subsystem
 * 
 * Copyright (C) 2001 Ben Collins <bcollins@debian.org>
 *               2000 Bonin Franck <boninf@free.fr>
 *               2003 Steve Kinneberg <kinnebergsteve@acmsystems.com>
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

/* This driver intends to support RFC 2734, which describes a method for
 * transporting IPv4 datagrams over IEEE-1394 serial busses. This driver
 * will ultimately support that method, but currently falls short in
 * several areas.
 *
 * TODO:
 * RFC 2734 related:
 * - Add support for broadcast messages
 * - Use EUI instead of node id in internal ARP tables
 * - Add Config ROM entry
 * - Add MCAP and multicast
 *
 * Non-RFC 2734 related:
 * - Move generic GASP reception to core 1394 code
 * - Convert kmalloc/kfree for link fragments to use kmem_cache_* instead
 * - Stability improvements
 * - Performance enhancements
 * - Change hardcoded 1394 bus address region to a dynamic memory space allocation
 * - Consider garbage collecting old partial datagrams after X amount of time
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
#include <linux/bitops.h>
#include <asm/delay.h>
#include <asm/semaphore.h>
#include <net/arp.h>

#include "ieee1394_types.h"
#include "ieee1394_core.h"
#include "ieee1394_transactions.h"
#include "ieee1394.h"
#include "highlevel.h"
#include "iso.h"
#include "eth1394.h"

#define ETH1394_PRINT_G(level, fmt, args...) \
	printk(level ETHER1394_DRIVER_NAME": "fmt, ## args)

#define ETH1394_PRINT(level, dev_name, fmt, args...) \
	printk(level ETHER1394_DRIVER_NAME": %s: " fmt, dev_name, ## args)

#define DEBUG(fmt, args...) \
	printk(KERN_ERR "eth1394:%s[%d]: "fmt"\n", __FUNCTION__, __LINE__, ## args)
#define TRACE() printk(KERN_ERR "eth1394:%s[%d] ---- TRACE\n", __FUNCTION__, __LINE__)

static char version[] __devinitdata =
	"$Rev: 938 $ Ben Collins <bcollins@debian.org>";

struct fragment_info {
	struct list_head list;
	int offset;
	int len;
};

struct partial_datagram {
	struct list_head list;
	u16 dgl;
	u16 dg_size;
	u16 ether_type;
	struct sk_buff *skb;
	char *pbuf;
	struct list_head frag_info;
};

/* Our ieee1394 highlevel driver */
#define ETHER1394_DRIVER_NAME "ether1394"

static kmem_cache_t *packet_task_cache;

static struct hpsb_highlevel eth1394_highlevel;

/* Use common.lf to determine header len */
static const int hdr_type_len[] = {
	sizeof (struct eth1394_uf_hdr),
	sizeof (struct eth1394_ff_hdr),
	sizeof (struct eth1394_sf_hdr),
	sizeof (struct eth1394_sf_hdr)
};

MODULE_AUTHOR("Ben Collins (bcollins@debian.org)");
MODULE_DESCRIPTION("IEEE 1394 IPv4 Driver (IPv4-over-1394 as per RFC 2734)");
MODULE_LICENSE("GPL");

/* The max_partial_datagrams parameter is the maximum number of fragmented datagrams
 * per node that eth1394 will keep in memory.  Providing an upper bound allows us to
 * limit the amount of memory that partial datagrams consume in the event that some
 * partial datagrams are never completed.  This should probably change to a sysctl
 * item or the like if possible.
 */
MODULE_PARM(max_partial_datagrams, "i");
MODULE_PARM_DESC(max_partial_datagrams,
                 "Maximum number of partially received fragmented datagrams (default = 25).");
static int max_partial_datagrams = 25;


static inline void purge_partial_datagram(struct list_head *old);
static int ether1394_tx(struct sk_buff *skb, struct net_device *dev);
static void ether1394_iso(struct hpsb_iso *iso);


static int ether1394_init_bc(struct net_device *dev)
{
	int ret = 0;
	struct eth1394_priv *priv = (struct eth1394_priv *)dev->priv;

	/* First time sending?  Need a broadcast channel for ARP and for
	 * listening on */
	if(priv->bc_state == ETHER1394_BC_CHECK) {
		quadlet_t bc;

		/* Get the local copy of the broadcast channel and check its
		 * validity (the IRM should validate it for us) */

		bc = priv->host->csr.broadcast_channel;

		if((bc & 0xc0000000) != 0xc0000000) {
			/* broadcast channel not validated yet */
			ETH1394_PRINT(KERN_WARNING, dev->name,
				      "Error BROADCAST_CHANNEL register valid "
				      "bit not set, can't send IP traffic\n");
			if(!in_interrupt()) {
				hpsb_iso_shutdown(priv->iso);
				priv->bc_state = ETHER1394_BC_CLOSED;
			}
			ret = -EAGAIN;
			goto fail;
		}
		if(priv->broadcast_channel != (bc & 0x3f)) {
			/* This really shouldn't be possible, but just in case
			 * the IEEE 1394 spec changes regarding broadcast
			 * channels in the future. */

			if(in_interrupt()) {
				ret = -EAGAIN;
				goto fail;

			}

			hpsb_iso_shutdown(priv->iso);

			priv->broadcast_channel = bc & 0x3f;
			ETH1394_PRINT(KERN_INFO, dev->name,
				      "Changing to broadcast channel %d...\n",
				      priv->broadcast_channel);

			priv->iso = hpsb_iso_recv_init(priv->host, 16 * 4096,
						       16, priv->broadcast_channel,
						       1, ether1394_iso);
			if(priv->iso == NULL) {
				ETH1394_PRINT(KERN_ERR, dev->name,
					      "failed to change broadcast "
					      "channel\n");
				ret = -EAGAIN;
				goto fail;
			}
		}
		if(hpsb_iso_recv_start(priv->iso, -1, (1 << 3), -1) < 0) {
			ETH1394_PRINT(KERN_ERR, dev->name,
				      "Could not start data stream reception\n");
			if(!in_interrupt()) {
				hpsb_iso_shutdown(priv->iso);
				priv->bc_state = ETHER1394_BC_CLOSED;
			}
			ret = -EAGAIN;
			goto fail;
		}
		priv->bc_state = ETHER1394_BC_OPENED;
	}
    
fail:
	return ret;
}

/* This is called after an "ifup" */
static int ether1394_open (struct net_device *dev)
{
	struct eth1394_priv *priv = (struct eth1394_priv *)dev->priv;
	unsigned long flags;
	int ret;

	/* Set the spinlock before grabbing IRQ! */
	priv->lock = SPIN_LOCK_UNLOCKED;
	spin_lock_irqsave(&priv->lock, flags);
	ret = ether1394_init_bc(dev);
	spin_unlock_irqrestore(&priv->lock, flags);

	if(ret)
		return ret;

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


/* Convert a standard ARP packet to 1394 ARP. The first 8 bytes (the
 * entire arphdr) is the same format as the ip1394 header, so they
 * overlap. The rest needs to be munged a bit. The remainder of the
 * arphdr is formatted based on hwaddr len and ipaddr len. We know what
 * they'll be, so it's easy to judge.  */
static inline void ether1394_arp_to_1394arp (struct sk_buff *skb, struct net_device *dev)
{
	struct eth1394_priv *priv =
		(struct eth1394_priv *)(dev->priv);
	u16 phy_id = NODEID_TO_NODE(priv->host->node_id);

	unsigned char *arp_ptr = (unsigned char *)skb->data;
	struct eth1394_arp *arp1394 = (struct eth1394_arp *)skb->data;
	unsigned char arp_data[2*(dev->addr_len+4)];

	/* Copy the main data that we need */
	memcpy (arp_data, arp_ptr + sizeof(struct arphdr), sizeof (arp_data));

	/* Extend the buffer enough for our new header */
	skb_put (skb, sizeof (struct eth1394_arp) -
		 (sizeof (arp_data) + sizeof (struct arphdr)));

#define PROCESS_MEMBER(ptr,val,len) \
  memcpy (val, ptr, len); ptr += len
	arp_ptr = arp_data + arp1394->hw_addr_len;
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
	if (nodeid < 0 || nodeid >= ALL_NODES) {
		ETH1394_PRINT_G (KERN_ERR, "Cannot register invalid nodeid %d\n", nodeid);
		return;
	}

	priv->max_rec[nodeid]	= max_rec;
	priv->sspd[nodeid]	= sspd;
	priv->fifo_hi[nodeid]	= fifo_hi;
	priv->fifo_lo[nodeid]	= fifo_lo;
	priv->eui[nodeid]	= eui;

	priv->max_rec[ALL_NODES] = min(priv->max_rec[ALL_NODES], max_rec);
	priv->sspd[ALL_NODES] = min(priv->sspd[ALL_NODES], sspd);

	return;
}

static void ether1394_reset_priv (struct net_device *dev, int set_mtu)
{
	unsigned long flags;
	int i;
	struct eth1394_priv *priv = (struct eth1394_priv *)dev->priv;
	int phy_id = NODEID_TO_NODE(priv->host->node_id);
	struct hpsb_host *host = priv->host;

	spin_lock_irqsave (&priv->lock, flags);

	/* Clear the speed/payload/offset tables */
	memset (priv->max_rec, 8, sizeof (priv->max_rec));
	memset (priv->sspd, 0, sizeof (priv->sspd));
	memset (priv->fifo_hi, 0, sizeof (priv->fifo_hi));
	memset (priv->fifo_lo, 0, sizeof (priv->fifo_lo));

	priv->bc_state = ETHER1394_BC_CHECK;

	/* Register our limits now */
	ether1394_register_limits (phy_id, (be32_to_cpu(host->csr.rom[2]) >> 12) & 0xf,
				   host->speed_map[(phy_id << 6) + phy_id],
				   (u64)(((u64)be32_to_cpu(host->csr.rom[3]) << 32) |
				   be32_to_cpu(host->csr.rom[4])),
				   ETHER1394_REGION_ADDR >> 32,
				   ETHER1394_REGION_ADDR & 0xffffffff, priv);

	/* We'll use our max_rec as the default mtu */
	if (set_mtu)
		dev->mtu = (1 << (priv->max_rec[phy_id] + 1)) -
			(sizeof (union eth1394_hdr) + ETHER1394_OVERHEAD);

	/* Set our hardware address while we're at it */
	*(nodeid_t *)dev->dev_addr = htons (host->node_id);

	spin_unlock_irqrestore (&priv->lock, flags);

	for(i = 0; i < ALL_NODES; i++) {
		struct list_head *lh, *n;

		spin_lock_irqsave(&priv->pdg[i].lock, flags);
		if(!set_mtu) {
			list_for_each_safe(lh, n, &priv->pdg[i].list) {
				purge_partial_datagram(lh);
			}
		}
		INIT_LIST_HEAD(&(priv->pdg[i].list));
		priv->pdg[i].sz = 0;
		spin_unlock_irqrestore(&priv->pdg[i].lock, flags);
	}
}

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
	int i;
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
	spin_lock_init(&priv->lock);

	for(i = 0; i < ALL_NODES; i++) {
                spin_lock_init(&priv->pdg[i].lock);
		INIT_LIST_HEAD(&priv->pdg[i].list);
		priv->pdg[i].sz = 0;
	}

	hi = hpsb_create_hostinfo(&eth1394_highlevel, host, sizeof(*hi));

	if (hi == NULL)
		goto out;

	if (register_netdev (dev)) {
		ETH1394_PRINT (KERN_ERR, dev->name, "Error registering network driver\n");
		goto out;
	}

	ETH1394_PRINT (KERN_ERR, dev->name, "IEEE-1394 IPv4 over 1394 Ethernet (%s)\n",
		       host->driver->name);

	hi->host = host;
	hi->dev = dev;

	/* Ignore validity in hopes that it will be set in the future.  It'll
	 * be checked when the eth device is opened. */
	priv->broadcast_channel = host->csr.broadcast_channel & 0x3f;

	priv->iso = hpsb_iso_recv_init(host, 16 * 4096, 16, priv->broadcast_channel,
				       1, ether1394_iso);
	if (priv->iso == NULL) {
		priv->bc_state = ETHER1394_BC_CLOSED;
	}
	return;

out:
	if (dev != NULL)
		kfree (dev); dev = NULL;
	if (hi)
		hpsb_destroy_hostinfo(&eth1394_highlevel, host);

	ETH1394_PRINT_G (KERN_ERR, "Out of memory\n");

	return;
}

/* Remove a card from our list */
static void ether1394_remove_host (struct hpsb_host *host)
{
	struct host_info *hi = hpsb_get_hostinfo(&eth1394_highlevel, host);

	if (hi != NULL) {
		struct eth1394_priv *priv = (struct eth1394_priv *)hi->dev->priv;

		priv->bc_state = ETHER1394_BC_CLOSED;
		unregister_netdev (hi->dev);
		hpsb_iso_shutdown(priv->iso);

		kfree (hi->dev); hi->dev = NULL;
	}

	return;
}

/* A reset has just arisen */
static void ether1394_host_reset (struct hpsb_host *host)
{
	struct host_info *hi = hpsb_get_hostinfo(&eth1394_highlevel, host);
	struct net_device *dev;

	/* This can happen for hosts that we don't use */
	if (hi == NULL)
		return;

	dev = hi->dev;

	/* Reset our private host data, but not our mtu */
	netif_stop_queue (dev);
	ether1394_reset_priv (dev, 0);
	netif_wake_queue (dev);
}


/******************************************
 * Datagram reception code
 ******************************************/

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
						    nodeid_t srcid, nodeid_t destid, u16 ether_type)
{
	unsigned char src_hw[ETH_ALEN], dest_hw[ETH_ALEN];
	unsigned short ret = 0;

	/* Setup our hw addresses. We use these to build the
	 * ethernet header.  */
	*(u16 *)dest_hw = htons(destid);
	*(u16 *)src_hw = htons(srcid);

	/* If this is an ARP packet, convert it. First, we want to make
	 * use of some of the fields, since they tell us a little bit
	 * about the sending machine.  */
	if (ether_type == __constant_htons (ETH_P_ARP)) {
		unsigned long flags;
		u16 phy_id = NODEID_TO_NODE(srcid);
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
  memcpy (ptr, val, len); ptr += len
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
	if (dev->hard_header (skb, dev, __constant_ntohs (ether_type),
			      dest_hw, src_hw, skb->len) >= 0)
		ret = ether1394_type_trans(skb, dev);

	return ret;
}

static inline int fragment_overlap(struct list_head *frag_list, int offset, int len)
{
	struct list_head *lh;
	struct fragment_info *fi;

	list_for_each(lh, frag_list) {
		fi = list_entry(lh, struct fragment_info, list);

		if( ! ((offset > (fi->offset + fi->len - 1)) ||
		       ((offset + len - 1) < fi->offset)))
			return 1;
	}
	return 0;
}

static inline struct list_head *find_partial_datagram(struct list_head *pdgl, int dgl)
{
	struct list_head *lh;
	struct partial_datagram *pd;

	list_for_each(lh, pdgl) {
		pd = list_entry(lh, struct partial_datagram, list);
		if(pd->dgl == dgl)
			return lh;
	}
	return NULL;
}

/* Assumes that new fragment does not overlap any existing fragments */
static inline int new_fragment(struct list_head *frag_info, int offset, int len)
{
	struct list_head *lh;
	struct fragment_info *fi, *fi2, *new;

	list_for_each(lh, frag_info) {
		fi = list_entry(lh, struct fragment_info, list);
		if((fi->offset + fi->len) == offset) {
			/* The new fragment can be tacked on to the end */
			fi->len += len;
			/* Did the new fragment plug a hole? */
			fi2 = list_entry(lh->next, struct fragment_info, list);
			if((fi->offset + fi->len) == fi2->offset) {
				/* glue fragments together */
				fi->len += fi2->len;
				list_del(lh->next);
				kfree(fi2); fi2 = NULL;
			}
			return 0;
		} else if((offset + len) == fi->offset) {
			/* The new fragment can be tacked on to the beginning */
			fi->offset = offset;
			fi->len += len;
			/* Did the new fragment plug a hole? */
			fi2 = list_entry(lh->prev, struct fragment_info, list);
			if((fi2->offset + fi2->len) == fi->offset) {
				/* glue fragments together */
				fi2->len += fi->len;
				list_del(lh);
				kfree(fi); fi = NULL;
			}
			return 0;
		} else if(offset > (fi->offset + fi->len)) {
			break;
		} else if ((offset + len) < fi->offset) {
			lh = lh->prev;
			break;
		}
	}

	new = kmalloc(sizeof(struct fragment_info), GFP_ATOMIC);
	if(!new) 
		return -ENOMEM;

	new->offset = offset;
	new->len = len;

	list_add(&new->list, lh);

	return 0;
}

static inline int new_partial_datagram(struct net_device *dev,
				       struct list_head *pdgl, int dgl,
				       int dg_size, char *frag_buf,
				       int frag_off, int frag_len)
{
	struct partial_datagram *new;

	new = kmalloc(sizeof(struct partial_datagram), GFP_ATOMIC);
	if(!new)
		return -ENOMEM;

	INIT_LIST_HEAD(&new->frag_info);

	if(new_fragment(&new->frag_info, frag_off, frag_len) < 0) {
		kfree(new); new = NULL;
		return -ENOMEM;
	}

	new->dgl = dgl;
	new->dg_size = dg_size;

	new->skb = dev_alloc_skb(dg_size + dev->hard_header_len + 15);
	if(!new->skb) {
		struct fragment_info *fi = list_entry(new->frag_info.next,
						      struct fragment_info,
						      list);
		kfree(fi); fi = NULL;
		kfree(new); new = NULL;
		return -ENOMEM;
	}

	skb_reserve(new->skb, (dev->hard_header_len + 15) & ~15);
	new->pbuf = skb_put(new->skb, dg_size);
	memcpy(new->pbuf + frag_off, frag_buf, frag_len);

	list_add(&new->list, pdgl);

	return 0;
}

static inline int update_partial_datagram(struct list_head *pdgl, struct list_head *lh,
					  char *frag_buf, int frag_off, int frag_len)
{
	struct partial_datagram *pd = list_entry(lh, struct partial_datagram, list);

	if(new_fragment(&pd->frag_info, frag_off, frag_len) < 0) {
		return -ENOMEM;
	}

	memcpy(pd->pbuf + frag_off, frag_buf, frag_len);

	/* Move list entry to beginnig of list so that oldest partial
	 * datagrams percolate to the end of the list */
	list_del(lh);
	list_add(lh, pdgl);

	return 0;
}

static inline void purge_partial_datagram(struct list_head *old)
{
	struct partial_datagram *pd = list_entry(old, struct partial_datagram, list);
	struct list_head *lh, *n;

	list_for_each_safe(lh, n, &pd->frag_info) {
		struct fragment_info *fi = list_entry(lh, struct fragment_info, list);
		list_del(lh);
		kfree(fi); fi = NULL;
	}
	list_del(old);
	kfree_skb(pd->skb); pd->skb = NULL;
	kfree(pd); pd = NULL;
}

static inline int is_datagram_complete(struct list_head *lh, int dg_size)
{
	struct partial_datagram *pd = list_entry(lh, struct partial_datagram, list);
	struct fragment_info *fi = list_entry(pd->frag_info.next,
					      struct fragment_info, list);

	return (fi->len == dg_size);
}

/* Packet reception. We convert the IP1394 encapsulation header to an
 * ethernet header, and fill it with some of our other fields. This is
 * an incoming packet from the 1394 bus.  */
static int ether1394_data_handler(struct net_device *dev, int srcid, int destid,
				  char *buf, int len)
{
	struct sk_buff *skb;
	unsigned long flags;
	struct eth1394_priv *priv;
	union eth1394_hdr *hdr = (union eth1394_hdr *)buf;
	u16 ether_type = 0;  /* initialized to clear warning */
	int hdr_len;

	priv = (struct eth1394_priv *)dev->priv;

	/* First, did we receive a fragmented or unfragmented datagram? */
	hdr->words.word1 = ntohs(hdr->words.word1);

	hdr_len = hdr_type_len[hdr->common.lf];

	if(hdr->common.lf == ETH1394_HDR_LF_UF) {
		/* An unfragmented datagram has been received by the ieee1394
		 * bus. Build an skbuff around it so we can pass it to the
		 * high level network layer. */

		skb = dev_alloc_skb(len + dev->hard_header_len + 15);
		if (!skb) {
			HPSB_PRINT (KERN_ERR, "ether1394 rx: low on mem\n");
			priv->stats.rx_dropped++;
			return -1;
		}
		skb_reserve(skb, (dev->hard_header_len + 15) & ~15);
		memcpy(skb_put(skb, len - hdr_len), buf + hdr_len, len - hdr_len);
		ether_type = hdr->uf.ether_type;
	} else {
#if 0
		return 0;
	}
	if(0) {
#endif
		/* A datagram fragment has been received, now the fun begins. */

		struct list_head *pdgl, *lh;
		struct partial_datagram *pd;
		int fg_off;
		int fg_len = len - hdr_len;
		int dg_size;
		int dgl;
		int retval;
		int sid = NODEID_TO_NODE(srcid);
                struct pdg_list *pdg = &(priv->pdg[sid]);

		hdr->words.word3 = ntohs(hdr->words.word3);
		/* The 4th header word is reserved so no need to do ntohs() */

		if(hdr->common.lf == ETH1394_HDR_LF_FF) {
			ether_type = hdr->ff.ether_type;
			dgl = hdr->ff.dgl;
			dg_size = hdr->ff.dg_size;
			fg_off = 0;
		} else {
			hdr->words.word2 = ntohs(hdr->words.word2);
			dgl = hdr->sf.dgl;
			dg_size = hdr->sf.dg_size;
			fg_off = hdr->sf.fg_off;
		}

		spin_lock_irqsave(&pdg->lock, flags);

		pdgl = &(pdg->list);
		lh = find_partial_datagram(pdgl, dgl);

		if(lh == NULL) {
			if(pdg->sz == max_partial_datagrams) {
				/* remove the oldest */
				purge_partial_datagram(pdgl->prev);
				pdg->sz--;
			}
            
			retval = new_partial_datagram(dev, pdgl, dgl, dg_size,
						      buf + hdr_len, fg_off,
						      fg_len);
			if(retval < 0) {
				spin_unlock_irqrestore(&pdg->lock, flags);
				goto bad_proto;
			}
			pdg->sz++;
			lh = find_partial_datagram(pdgl, dgl);
		} else {
			struct partial_datagram *pd;

			pd = list_entry(lh, struct partial_datagram, list);

			if(fragment_overlap(&pd->frag_info, fg_off, fg_len)) {
				/* Overlapping fragments, obliterate old
				 * datagram and start new one. */
				purge_partial_datagram(lh);
				retval = new_partial_datagram(dev, pdgl, dgl,
							      dg_size,
							      buf + hdr_len,
							      fg_off, fg_len);
				if(retval < 0) {
					pdg->sz--;
					spin_unlock_irqrestore(&pdg->lock, flags);
					goto bad_proto;
				}
			} else {
				retval = update_partial_datagram(pdgl, lh,
								 buf + hdr_len,
								 fg_off, fg_len);
				if(retval < 0) {
					/* Couldn't save off fragment anyway
					 * so might as well obliterate the
					 * datagram now. */
					purge_partial_datagram(lh);
					pdg->sz--;
					spin_unlock_irqrestore(&pdg->lock, flags);
					goto bad_proto;
				}
			} /* fragment overlap */
		} /* new datagram or add to existing one */

		pd = list_entry(lh, struct partial_datagram, list);

		if(hdr->common.lf == ETH1394_HDR_LF_FF) {
			pd->ether_type = ether_type;
		}

		if(is_datagram_complete(lh, dg_size)) {
			ether_type = pd->ether_type;
			pdg->sz--;
			skb = skb_get(pd->skb);
			purge_partial_datagram(lh);
			spin_unlock_irqrestore(&pdg->lock, flags);
		} else {
			/* Datagram is not complete, we're done for the
			 * moment. */
			spin_unlock_irqrestore(&pdg->lock, flags);
			return 0;
		}
	} /* unframgented datagram or fragmented one */

	/* Write metadata, and then pass to the receive level */
	skb->dev = dev;
	skb->ip_summed = CHECKSUM_UNNECESSARY;	/* don't check it */

	/* Parse the encapsulation header. This actually does the job of
	 * converting to an ethernet frame header, aswell as arp
	 * conversion if needed. ARP conversion is easier in this
	 * direction, since we are using ethernet as our backend.  */
	skb->protocol = ether1394_parse_encap(skb, dev, srcid, destid,
					      ether_type);


	spin_lock_irqsave(&priv->lock, flags);
	if(!skb->protocol) {
		priv->stats.rx_errors++;
		priv->stats.rx_dropped++;
		dev_kfree_skb_any(skb);
		goto bad_proto;
	}

	if(netif_rx(skb) == NET_RX_DROP) {
		priv->stats.rx_errors++;
		priv->stats.rx_dropped++;
		goto bad_proto;
	}

	/* Statistics */
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += skb->len;

bad_proto:
	if(netif_queue_stopped(dev))
		netif_wake_queue(dev);
	spin_unlock_irqrestore(&priv->lock, flags);

	dev->last_rx = jiffies;

	return 0;
}

static int ether1394_write(struct hpsb_host *host, int srcid, int destid,
			   quadlet_t *data, u64 addr, unsigned int len, u16 flags)
{
	struct host_info *hi = hpsb_get_hostinfo(&eth1394_highlevel, host);

	if(hi == NULL) {
		ETH1394_PRINT_G(KERN_ERR, "Could not find net device for host %s\n",
				host->driver->name);
		return RCODE_ADDRESS_ERROR;
	}

	if(ether1394_data_handler(hi->dev, srcid, destid, (char*)data, len))
		return RCODE_ADDRESS_ERROR;
	else
		return RCODE_COMPLETE;
}

static void ether1394_iso(struct hpsb_iso *iso)
{
	quadlet_t *data;
	char *buf;
	struct host_info *hi = hpsb_get_hostinfo(&eth1394_highlevel, iso->host);
	struct net_device *dev;
	struct eth1394_priv *priv;
	unsigned int len;
	u32 specifier_id;
	u16 source_id;
	int i;
	int nready;

	if(hi == NULL) {
		ETH1394_PRINT_G(KERN_ERR, "Could not find net device for host %s\n",
				iso->host->driver->name);
		return;
	}

	dev = hi->dev;

	nready = hpsb_iso_n_ready(iso);
	for(i = 0; i < nready; i++) {
		struct hpsb_iso_packet_info *info = &iso->infos[iso->first_packet + i];
		data = (quadlet_t*) (iso->data_buf.kvirt + info->offset);

		/* skip over GASP header */
		buf = (char *)data + 8;
		len = info->len - 8;

		specifier_id = (((be32_to_cpu(data[0]) & 0xffff) << 8) |
				((be32_to_cpu(data[1]) & 0xff000000) >> 24));
		source_id = be32_to_cpu(data[0]) >> 16;

		priv = (struct eth1394_priv *)dev->priv;

		if(info->channel != (iso->host->csr.broadcast_channel & 0x3f) ||
		   specifier_id != ETHER1394_GASP_SPECIFIER_ID) {
			/* This packet is not for us */
			continue;
		}
		ether1394_data_handler(dev, source_id, iso->host->node_id, buf, len);
	}

	hpsb_iso_recv_release_packets(iso, i);

	dev->last_rx = jiffies;
}

/******************************************
 * Datagram transmission code
 ******************************************/

/* We need to encapsulate the standard header with our own. We use the
 * ethernet header's proto for our own. */
static inline unsigned int ether1394_encapsulate_prep(unsigned int max_payload,
						      int proto,
						      union eth1394_hdr *hdr,
						      u16 dg_size, u16 dgl)
{
	unsigned int adj_max_payload = max_payload - hdr_type_len[ETH1394_HDR_LF_UF];

	/* Does it all fit in one packet? */
	if(dg_size <= adj_max_payload) {
		hdr->uf.lf = ETH1394_HDR_LF_UF;
		hdr->uf.ether_type = proto;
	} else {
		hdr->ff.lf = ETH1394_HDR_LF_FF;
		hdr->ff.ether_type = proto;
		hdr->ff.dg_size = dg_size;
		hdr->ff.dgl = dgl;
		adj_max_payload = max_payload - hdr_type_len[ETH1394_HDR_LF_FF];
	}

	return((dg_size + (adj_max_payload - 1)) / adj_max_payload);
}

static inline unsigned int ether1394_encapsulate(struct sk_buff *skb,
						 unsigned int max_payload,
						 union eth1394_hdr *hdr)
{
	union eth1394_hdr *bufhdr;
	int ftype = hdr->common.lf;
	int hdrsz = hdr_type_len[ftype];
	unsigned int adj_max_payload = max_payload - hdrsz;

	switch(ftype) {
	case ETH1394_HDR_LF_UF:
		bufhdr = (union eth1394_hdr *)skb_push(skb, hdrsz);
		bufhdr->words.word1 = htons(hdr->words.word1);
		bufhdr->words.word2 = hdr->words.word2;
		break;

	case ETH1394_HDR_LF_FF:
		bufhdr = (union eth1394_hdr *)skb_push(skb, hdrsz);
		bufhdr->words.word1 = htons(hdr->words.word1);
		bufhdr->words.word2 = hdr->words.word2;
		bufhdr->words.word3 = htons(hdr->words.word3);
		bufhdr->words.word4 = 0;

		/* Set frag type here for future interior fragments */
		hdr->common.lf = ETH1394_HDR_LF_IF;
		hdr->sf.fg_off = 0;
		break;
		
	default:
		hdr->sf.fg_off += adj_max_payload;
		bufhdr = (union eth1394_hdr *)skb_pull(skb, adj_max_payload);
		if(max_payload >= skb->len)
			hdr->common.lf = ETH1394_HDR_LF_LF;
		bufhdr->words.word1 = htons(hdr->words.word1);
		bufhdr->words.word2 = htons(hdr->words.word2);
		bufhdr->words.word3 = htons(hdr->words.word3);
		bufhdr->words.word4 = 0;
	}

	return min(max_payload, skb->len);
}

static inline struct hpsb_packet *ether1394_alloc_common_packet(struct hpsb_host *host)
{
	struct hpsb_packet *p;

	p = alloc_hpsb_packet(0);
	if(p) {
		p->host = host;
		p->data = NULL;
		p->generation = get_hpsb_generation(host);
		p->type = hpsb_async;
	}
	return p;
}

static inline int ether1394_prep_write_packet(struct hpsb_packet *p,
					      struct hpsb_host *host,
					      nodeid_t node, u64 addr,
					      void * data, int tx_len)
{
	p->node_id = node;
	p->data = NULL;

	p->tcode = TCODE_WRITEB;
	p->header[1] = (host->node_id << 16) | (addr >> 32);
	p->header[2] = addr & 0xffffffff;

	p->header_size = 16;
	p->expect_response = 1;

	if(hpsb_get_tlabel(p, !in_interrupt())) {
		ETH1394_PRINT_G(KERN_ERR, "No more tlabels left");
		return -1;
	}		
	p->header[0] = (p->node_id << 16) | (p->tlabel << 10)
		| (1 << 8) | (TCODE_WRITEB << 4);

	p->header[3] = tx_len << 16;
	p->data_size = tx_len + (tx_len % 4 ? 4 - (tx_len % 4) : 0);
	p->data = (quadlet_t*)data;

	return 0;
}

static inline void ether1394_prep_gasp_packet(struct hpsb_packet *p,
					      struct hpsb_host *host,
					      struct sk_buff *skb, int length)
{
	p->header_size = 4;
	p->tcode = TCODE_STREAM_DATA;

	p->header[0] = (length << 16) | (3 << 14)
		| ((host->csr.broadcast_channel & 0x3f) << 8)
		| (TCODE_STREAM_DATA << 4);
	p->data_size = length;
	p->data = (quadlet_t*)skb_push(skb, 2 * sizeof(quadlet_t));
	p->data[0] = cpu_to_be32((host->node_id << 16) |
				      ETHER1394_GASP_SPECIFIER_ID_HI);
	p->data[1] = cpu_to_be32((ETHER1394_GASP_SPECIFIER_ID_LO << 24) |
				      ETHER1394_GASP_VERSION);
}

static inline void ether1394_free_packet(struct hpsb_packet *packet)
{
	packet->data = NULL;
	free_hpsb_packet(packet); packet = NULL;
}

static void ether1394_complete_cb(void *__ptask);
static int ether1394_send_packet(struct packet_task *ptask, unsigned int tx_len)
{
	struct eth1394_priv *priv = ptask->priv;
	struct hpsb_packet *packet;

	packet = ether1394_alloc_common_packet(priv->host);
	if(!packet)
		return -1;

	if(ptask->tx_type == ETH1394_GASP) {
		int length = tx_len + (2 * sizeof(quadlet_t));

		ether1394_prep_gasp_packet(packet, priv->host,
						    ptask->skb, length);

	} else {
		if(ether1394_prep_write_packet(packet, priv->host,
					       ptask->dest_node,
					       ptask->addr, ptask->skb->data,
					       tx_len))
			goto fail;
	}

	ptask->packet = packet;
	hpsb_set_packet_complete_task(ptask->packet, ether1394_complete_cb,
				      ptask);

	if(hpsb_send_packet(packet)) {
		return 0;
	}
fail:
	return -1;
}


/* Task function to be run when a datagram transmission is completed */
static inline void ether1394_dg_complete(struct packet_task *ptask, int fail)
{
	struct sk_buff *skb = ptask->skb;
	struct net_device *dev = skb->dev;
	struct eth1394_priv *priv = (struct eth1394_priv *)dev->priv;
        unsigned long flags;
		
	/* Statistics */
	if(fail) {
		spin_lock_irqsave(&priv->lock, flags);
		priv->stats.tx_dropped++;
		priv->stats.tx_errors++;
		spin_unlock_irqrestore(&priv->lock, flags);
	} else {
		spin_lock_irqsave(&priv->lock, flags);
		priv->stats.tx_bytes += skb->len;
		priv->stats.tx_packets++;
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	dev_kfree_skb_any(skb); skb = NULL;
	kmem_cache_free(packet_task_cache, ptask); ptask = NULL;
}


/* Callback for when a packet has been sent and the status of that packet is
 * known */
static void ether1394_complete_cb(void *__ptask)
{
	struct packet_task *ptask = (struct packet_task *)__ptask;
	struct hpsb_packet *packet = ptask->packet;
	int fail = 0;

	if(packet->tcode != TCODE_STREAM_DATA) {
		fail = hpsb_packet_success(packet);
		hpsb_free_tlabel(packet);
	}

	ether1394_free_packet(packet); packet = ptask->packet = NULL;

	ptask->outstanding_pkts--;
	if(ptask->outstanding_pkts > 0 && !fail)
	{
		int tx_len;

		/* Add the encapsulation header to the fragment */
		tx_len = ether1394_encapsulate(ptask->skb, ptask->max_payload,
					       &ptask->hdr);
		if(ether1394_send_packet(ptask, tx_len))
			ether1394_dg_complete(ptask, 1);
	} else {
		ether1394_dg_complete(ptask, fail);
	}
}



/* Transmit a packet (called by kernel) */
static int ether1394_tx (struct sk_buff *skb, struct net_device *dev)
{
	int kmflags = in_interrupt() ? GFP_ATOMIC : GFP_KERNEL;
	struct ethhdr *eth;
	struct eth1394_priv *priv = (struct eth1394_priv *)dev->priv;
	int proto;
	unsigned long flags;
	nodeid_t dest_node;
	eth1394_tx_type tx_type;
	int ret = 0;
	unsigned int tx_len;
	unsigned int max_payload;
	u16 dg_size;
	u16 dgl;
	struct packet_task *ptask;

	ptask = kmem_cache_alloc(packet_task_cache, kmflags);
	if(ptask == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	spin_lock_irqsave (&priv->lock, flags);
	if (priv->bc_state == ETHER1394_BC_CLOSED) {
		ETH1394_PRINT(KERN_ERR, dev->name,
			      "Cannot send packet, no broadcast channel available.\n");
		ret = -EAGAIN;
		spin_unlock_irqrestore (&priv->lock, flags);
		goto fail;
	}

	if (priv->bc_state == ETHER1394_BC_CHECK) {
		if(ether1394_init_bc(dev)) {
			spin_unlock_irqrestore (&priv->lock, flags);
			goto fail;
		}
	}

	spin_unlock_irqrestore (&priv->lock, flags);

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

	max_payload = 1 << (min(priv->max_rec[NODEID_TO_NODE(priv->host->node_id)],
                                priv->max_rec[NODEID_TO_NODE(dest_node)]) + 1);

	if(max_payload < 512)
		max_payload = 512;

	/* Set the transmission type for the packet.  Right now only ARP
	 * packets are sent via GASP.  IP broadcast and IP multicast are not
	 * yet supported properly, they too should use GASP. */
	switch(proto) {
	case __constant_htons(ETH_P_ARP):
		tx_type = ETH1394_GASP;
                max_payload -= ETHER1394_OVERHEAD;
		break;
	default:
		tx_type = ETH1394_WRREQ;
	}

	dg_size = skb->len;

	spin_lock_irqsave (&priv->lock, flags);
	dgl = priv->dgl[NODEID_TO_NODE(dest_node)];
	if(max_payload < dg_size + hdr_type_len[ETH1394_HDR_LF_UF])
		priv->dgl[NODEID_TO_NODE(dest_node)]++;
	spin_unlock_irqrestore (&priv->lock, flags);

	ptask->hdr.words.word1 = 0;
	ptask->hdr.words.word2 = 0;
	ptask->hdr.words.word3 = 0;
	ptask->hdr.words.word4 = 0;
	ptask->skb = skb;
	ptask->priv = priv;
	ptask->tx_type = tx_type;

	if(tx_type != ETH1394_GASP) {
		u64 addr;

		spin_lock_irqsave(&priv->lock, flags);
		addr = (u64)priv->fifo_hi[NODEID_TO_NODE(dest_node)] << 32 |
			priv->fifo_lo[NODEID_TO_NODE(dest_node)];
		spin_unlock_irqrestore(&priv->lock, flags);

		ptask->addr = addr;
		ptask->dest_node = dest_node;
	}

	ptask->tx_type = tx_type;
	ptask->max_payload = max_payload;
        ptask->outstanding_pkts = ether1394_encapsulate_prep(max_payload, proto,
							     &ptask->hdr, dg_size,
							     dgl);

	/* Add the encapsulation header to the fragment */
	tx_len = ether1394_encapsulate(skb, max_payload, &ptask->hdr);
	dev->trans_start = jiffies;
	if(ether1394_send_packet(ptask, tx_len))
		goto fail;

	netif_wake_queue(dev);
	return 0;
fail:
	if(ptask->packet)
		ether1394_free_packet(ptask->packet); ptask->packet = NULL;
	if(ptask)
		kmem_cache_free(packet_task_cache, ptask); ptask = NULL;
	if(skb != NULL) {
		dev_kfree_skb(skb); skb = NULL;
	}

	spin_lock_irqsave (&priv->lock, flags);
	priv->stats.tx_dropped++;
	priv->stats.tx_errors++;
	spin_unlock_irqrestore (&priv->lock, flags);

	if (netif_queue_stopped(dev))
		netif_wake_queue(dev);

	return 0;  /* returning non-zero causes serious problems */
}

/* Function for incoming 1394 packets */
static struct hpsb_address_ops addr_ops = {
	.write =	ether1394_write,
};

/* Ieee1394 highlevel driver functions */
static struct hpsb_highlevel eth1394_highlevel = {
	.name =		ETHER1394_DRIVER_NAME,
	.add_host =	ether1394_add_host,
	.remove_host =	ether1394_remove_host,
	.host_reset =	ether1394_host_reset,
};

static int __init ether1394_init_module (void)
{
	packet_task_cache = kmem_cache_create("packet_task", sizeof(struct packet_task),
					      0, 0, NULL, NULL);

	/* Register ourselves as a highlevel driver */
	hpsb_register_highlevel(&eth1394_highlevel);

	hpsb_register_addrspace(&eth1394_highlevel, &addr_ops, ETHER1394_REGION_ADDR,
				 ETHER1394_REGION_ADDR_END);

	return 0;
}

static void __exit ether1394_exit_module (void)
{
	hpsb_unregister_highlevel(&eth1394_highlevel);
	kmem_cache_destroy(packet_task_cache);
}

module_init(ether1394_init_module);
module_exit(ether1394_exit_module);
