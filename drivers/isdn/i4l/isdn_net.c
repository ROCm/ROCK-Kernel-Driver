/* $Id: isdn_net.c,v 1.140.6.11 2001/11/06 20:58:28 kai Exp $
 *
 * Linux ISDN subsystem, network interfaces and related functions (linklevel).
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Data Over Voice (DOV) support added - Guy Ellis 23-Mar-02 
 *                                       guy@traverse.com.au
 * Outgoing calls - looks for a 'V' in first char of dialed number
 * Incoming calls - checks first character of eaz as follows:
 *   Numeric - accept DATA only - original functionality
 *   'V'     - accept VOICE (DOV) only
 *   'B'     - accept BOTH DATA and DOV types
 *
 */

#include <linux/config.h>
#include <linux/isdn.h>
#include <net/arp.h>
#include <net/dst.h>
#include <net/pkt_sched.h>
#include <linux/inetdevice.h>
#include "isdn_common.h"
#include "isdn_net.h"
#include "isdn_ppp.h"
#include <linux/concap.h>
#include "isdn_concap.h"
#include "isdn_ciscohdlck.h"

#define ISDN_NET_MAX_QUEUE_LENGTH 2

/*
 * is this particular channel busy?
 */
static inline int
isdn_net_dev_busy(isdn_net_dev *idev)
{
	return idev->frame_cnt >= ISDN_NET_MAX_QUEUE_LENGTH;
}

/*
 * find out if the net_device which this mlp is belongs to is busy.
 * It's busy iff all channels are busy.
 * must hold mlp->xmit_lock
 * FIXME: Use a mlp->frame_cnt instead of loop?
 */
static inline int
isdn_net_local_busy(isdn_net_local *mlp)
{
	isdn_net_dev *idev;

	list_for_each_entry(idev, &mlp->online, online) {
		if (!isdn_net_dev_busy(idev))
			return 0;
	}
	return 1;
}

/*
 * For the given net device, this will get a non-busy channel out of the
 * corresponding bundle.
 */
static inline isdn_net_dev *
isdn_net_get_xmit_dev(isdn_net_local *mlp)
{
	isdn_net_dev *idev;

	list_for_each_entry(idev, &mlp->online, online) {
		if (!isdn_net_dev_busy(idev)) {
			/* point the head to next online channel */
			list_del(&mlp->online);
			list_add(&mlp->online, &idev->online);
			return idev;
		}
	}
	return NULL;
}

/* mlp->xmit_lock must be held */
static inline void
isdn_net_inc_frame_cnt(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;

	if (isdn_net_local_busy(mlp))
		isdn_BUG();
		
	idev->frame_cnt++;
	if (isdn_net_local_busy(mlp))
		netif_stop_queue(&mlp->dev);
}

/* mlp->xmit_lock must be held */
static inline void
isdn_net_dec_frame_cnt(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	int was_busy;

	was_busy = isdn_net_local_busy(mlp);

	idev->frame_cnt--;

	if (isdn_net_local_busy(mlp))
		isdn_BUG();

	if (!was_busy)
		return;

	if (!skb_queue_empty(&idev->super_tx_queue))
		tasklet_schedule(&idev->tlet);
	else
		netif_wake_queue(&mlp->dev);
}

/* Prototypes */

int isdn_net_handle_event(isdn_net_dev *idev, int pr, void *arg);

char *isdn_net_revision = "$Revision: 1.140.6.11 $";

/* A packet has successfully been sent out. */

int
isdn_net_bsent(isdn_net_dev *idev, isdn_ctrl *c)
{
	isdn_net_local *mlp = idev->mlp;
	unsigned long flags;

	spin_lock_irqsave(&mlp->xmit_lock, flags);
	isdn_net_dec_frame_cnt(idev);
	spin_unlock_irqrestore(&mlp->xmit_lock, flags);
	mlp->stats.tx_packets++;
	mlp->stats.tx_bytes += c->parm.length;
	return 1;
}

static void
isdn_net_unreachable(struct net_device *dev, struct sk_buff *skb, char *reason)
{
	u_short proto = ntohs(skb->protocol);
	
	printk(KERN_DEBUG "isdn_net: %s: %s, signalling dst_link_failure %s\n",
	       dev->name,
	       (reason != NULL) ? reason : "unknown",
	       (proto != ETH_P_IP) ? "Protocol != ETH_P_IP" : "");
	
	dst_link_failure(skb);
}

static void
isdn_net_log_skb(struct sk_buff *skb, isdn_net_dev *idev)
{
	unsigned char *p = skb->nh.raw; /* hopefully, this was set correctly */
	unsigned short proto = ntohs(skb->protocol);
	int data_ofs;
	struct ip_ports {
		unsigned short source;
		unsigned short dest;
	} *ipp;
	char addinfo[100];

	data_ofs = ((p[0] & 15) * 4);
	switch (proto) {
	case ETH_P_IP:
		switch (p[9]) {
		case IPPROTO_ICMP:
			strcpy(addinfo, "ICMP");
			break;
		case IPPROTO_TCP:
		case IPPROTO_UDP:
			ipp = (struct ip_ports *) (&p[data_ofs]);
			sprintf(addinfo, "%s, port: %d -> %d",
				p[9] == IPPROTO_TCP ? "TCP" : "UDP",
				ntohs(ipp->source), ntohs(ipp->dest));
			break;
		default:
			sprintf(addinfo, "type %d", p[9]);
		}
		printk(KERN_INFO
		       "OPEN: %u.%u.%u.%u -> %u.%u.%u.%u %s\n",
		       
		       NIPQUAD(*(u32 *)(p + 12)), NIPQUAD(*(u32 *)(p + 16)),
		       addinfo);
		break;
	case ETH_P_ARP:
		printk(KERN_INFO
		       "OPEN: ARP %d.%d.%d.%d -> *.*.*.* ?%d.%d.%d.%d\n",
		       NIPQUAD(*(u32 *)(p + 14)), NIPQUAD(*(u32 *)(p + 24)));
		break;
	default:
		printk(KERN_INFO "OPEN: unknown proto %#x\n", proto);
	}
}

/*
 * this function is used to send supervisory data, i.e. data which was
 * not received from the network layer, but e.g. frames from ipppd, CCP
 * reset frames etc.
 * must hold mlp->xmit_lock
 */
void
isdn_net_write_super(isdn_net_dev *idev, struct sk_buff *skb)
{
	if (!isdn_net_dev_busy(idev))
		isdn_net_writebuf_skb(idev, skb);
	else
		skb_queue_tail(&idev->super_tx_queue, skb);
}

/* 
 * all frames sent from the (net) LL to a HL driver should go via this function
 * it's serialized by the caller holding the idev->xmit_lock spinlock
 * must hold mlp->xmit_lock
 */
void
isdn_net_writebuf_skb(isdn_net_dev *idev, struct sk_buff *skb)
{
	isdn_net_local *mlp = idev->mlp;
	int ret;
	int len = skb->len;     /* save len */

	/* before obtaining the lock the caller should have checked that
	   the lp isn't busy */
	if (isdn_net_dev_busy(idev)) {
		isdn_BUG();
		goto error;
	}

	if (!isdn_net_online(idev)) {
		isdn_BUG();
		goto error;
	}
	ret = isdn_slot_write(idev->isdn_slot, skb);
	if (ret != len) {
		/* we should never get here */
		printk(KERN_WARNING "%s: HL driver queue full\n", idev->name);
		goto error;
	}
	
	idev->transcount += len;
	isdn_net_inc_frame_cnt(idev);
	return;

 error:
	dev_kfree_skb(skb);
	mlp->stats.tx_errors++;
}

static void
isdn_net_dial_slave(isdn_net_local *mlp)
{
	isdn_net_dev *idev;

	list_for_each_entry(idev, &mlp->slaves, slaves) {
		if (!isdn_net_bound(idev)) {
			isdn_net_dial(idev);
			break;
		}
	}
}

/*
 *  Based on cps-calculation, check if device is overloaded.
 *  If so, and if a slave exists, trigger dialing for it.
 *  If any slave is online, deliver packets using a simple round robin
 *  scheme.
 *
 *  Return: 0 on success, !0 on failure.
 */

int
isdn_net_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	isdn_net_dev *idev;
	isdn_net_local *mlp = ndev->priv;
	unsigned long flags;
	int retval;

	ndev->trans_start = jiffies;

	spin_lock_irqsave(&mlp->xmit_lock, flags);

	if (list_empty(&mlp->online)) {
		retval = isdn_net_autodial(skb, ndev);
		goto out;
	}

	idev = isdn_net_get_xmit_dev(mlp);
	if (!idev) {
		printk(KERN_INFO "%s: all channels busy - requeuing!\n", ndev->name);
		netif_stop_queue(ndev);
		retval = 1;
		goto out;
	}

	isdn_net_writebuf_skb(idev, skb);

	/* the following stuff is here for backwards compatibility.
	 * in future, start-up and hangup of slaves (based on current load)
	 * should move to userspace and get based on an overall cps
	 * calculation
	 */
	if (jiffies != idev->last_jiffies) {
		idev->cps = idev->transcount * HZ / (jiffies - idev->last_jiffies);
		idev->last_jiffies = jiffies;
		idev->transcount = 0;
	}
	if (dev->net_verbose > 3)
		printk(KERN_DEBUG "%s: %d bogocps\n", idev->name, idev->cps);

	if (idev->cps > mlp->triggercps) {
		if (!idev->sqfull) {
			/* First time overload: set timestamp only */
			idev->sqfull = 1;
			idev->sqfull_stamp = jiffies;
		} else {
			/* subsequent overload: if slavedelay exceeded, start dialing */
			if (time_after(jiffies, idev->sqfull_stamp + mlp->slavedelay)) {
				isdn_net_dial_slave(mlp);
			}
		}
	} else {
		if (idev->sqfull && time_after(jiffies, idev->sqfull_stamp + mlp->slavedelay + 10 * HZ)) {
			idev->sqfull = 0;
		}
		/* this is a hack to allow auto-hangup for slaves on moderate loads */
		list_del(&mlp->online);
		list_add_tail(&mlp->online, &idev->online);
	}

	retval = 0;
 out:
	spin_unlock_irqrestore(&mlp->xmit_lock, flags);
	return retval;
}

int
isdn_net_autodial(struct sk_buff *skb, struct net_device *ndev)
{
	isdn_net_local *mlp = ndev->priv;
	isdn_net_dev *idev = list_entry(mlp->slaves.next, isdn_net_dev, slaves);

	/* are we dialing already? */
	if (isdn_net_bound(idev))
		goto stop_queue;

	if (ISDN_NET_DIALMODE(*mlp) != ISDN_NET_DM_AUTO)
		goto discard;

	if (isdn_net_dial(idev) < 0)
		goto discard;

	/* Log packet, which triggered dialing */
	if (dev->net_verbose)
		isdn_net_log_skb(skb, idev);

 stop_queue:
	netif_stop_queue(ndev);
	return 1;

 discard:
	isdn_net_unreachable(ndev, skb, "dial rejected");
	dev_kfree_skb(skb);
	return 0;
}


/*
 * Got a packet from ISDN-Channel.
 */
static void
isdn_net_receive(isdn_net_dev *idev, struct sk_buff *skb)
{
	isdn_net_local *mlp = idev->mlp;

	idev->transcount += skb->len;

	mlp->stats.rx_packets++;
	mlp->stats.rx_bytes += skb->len;
	skb->dev = &mlp->dev;
	skb->pkt_type = PACKET_HOST;
	skb->mac.raw = skb->data;
	isdn_dumppkt("R:", skb->data, skb->len, 40);

	mlp->ops->receive(mlp, idev, skb);
}

/*
 * A packet arrived via ISDN. Search interface-chain for a corresponding
 * interface. If found, deliver packet to receiver-function and return 1,
 * else return 0.
 */
int
isdn_net_rcv_skb(int idx, struct sk_buff *skb)
{
	isdn_net_dev *idev = isdn_slot_idev(idx);

	if (!idev) {
		HERE;
		return 0;
	}
	if (!isdn_net_online(idev))
		return 0;

	isdn_net_receive(idev, skb);
	return 0;
}

/*
 * This is called from certain upper protocol layers (multilink ppp
 * and x25iface encapsulation module) that want to initiate dialing
 * themselves.
 */
int
isdn_net_dial_req(isdn_net_dev *idev)
{
	isdn_net_local *mlp = idev->mlp;
	/* is there a better error code? */
	if (ISDN_NET_DIALMODE(*mlp) != ISDN_NET_DM_AUTO)
		return -EBUSY;

	return isdn_net_dial(idev);
}

// ISDN_NET_ENCAP_IPTYP
// ethernet type field
// ======================================================================

static int
isdn_iptyp_header(struct sk_buff *skb, struct net_device *dev,
		   unsigned short type, void *daddr, void *saddr, 
		   unsigned plen)
{
	put_u16(skb_push(skb, 2), type);
	return 2;
}

static void
isdn_iptyp_receive(isdn_net_local *lp, isdn_net_dev *idev, 
		   struct sk_buff *skb)
{
	idev->huptimer = 0;
	get_u16(skb->data, &skb->protocol);
	skb_pull(skb, 2);
	netif_rx(skb);
}

static struct isdn_netif_ops iptyp_ops = {
	.hard_start_xmit     = isdn_net_start_xmit,
	.hard_header         = isdn_iptyp_header,
	.flags               = IFF_NOARP | IFF_POINTOPOINT,
	.type                = ARPHRD_PPP,
	.addr_len            = 2,
	.receive             = isdn_iptyp_receive,
};

// ISDN_NET_ENCAP_UIHDLC
// HDLC with UI-Frames (for ispa with -h1 option) */
// ======================================================================

static int
isdn_uihdlc_header(struct sk_buff *skb, struct net_device *dev,
		   unsigned short type, void *daddr, void *saddr, 
		   unsigned plen)
{
	put_u16(skb_push(skb, 2), 0x0103);
	return 2;
}

static void
isdn_uihdlc_receive(isdn_net_local *lp, isdn_net_dev *idev, 
		    struct sk_buff *skb)
{
	idev->huptimer = 0;
	skb_pull(skb, 2);
	skb->protocol = htons(ETH_P_IP);
	netif_rx(skb);
}

static struct isdn_netif_ops uihdlc_ops = {
	.hard_start_xmit     = isdn_net_start_xmit,
	.hard_header         = isdn_uihdlc_header,
	.flags               = IFF_NOARP | IFF_POINTOPOINT,
	.type                = ARPHRD_HDLC,
	.addr_len            = 2,
	.receive             = isdn_uihdlc_receive,
};

// ISDN_NET_ENCAP_RAWIP
// RAW-IP without MAC-Header
// ======================================================================

static void
isdn_rawip_receive(isdn_net_local *lp, isdn_net_dev *idev, 
		   struct sk_buff *skb)
{
	idev->huptimer = 0;
	skb->protocol = htons(ETH_P_IP);
	netif_rx(skb);
}

static struct isdn_netif_ops rawip_ops = {
	.hard_start_xmit     = isdn_net_start_xmit,
	.flags               = IFF_NOARP | IFF_POINTOPOINT,
	.type                = ARPHRD_PPP,
	.receive             = isdn_rawip_receive,
};

// ISDN_NET_ENCAP_ETHER
// Ethernet over ISDN
// ======================================================================

static void
isdn_ether_receive(isdn_net_local *lp, isdn_net_dev *idev, 
		   struct sk_buff *skb)
{
	idev->huptimer = 0;
	skb->protocol = eth_type_trans(skb, skb->dev);
	netif_rx(skb);
}

static int
isdn_ether_open(isdn_net_local *lp)
{
	struct net_device *dev = &lp->dev;
	struct in_device *in_dev;
	int i;

	/* Fill in the MAC-level header ... */
	for (i = 0; i < ETH_ALEN; i++)
		dev->dev_addr[i] = 0xfc;
	in_dev = dev->ip_ptr;
	if (in_dev) {
		/* any address will do - we take the first */
		struct in_ifaddr *ifa = in_dev->ifa_list;
		if (ifa)
			memcpy(dev->dev_addr+2, &ifa->ifa_local, 4);
	}
	return 0;
}

static int
isdn_ether_init(isdn_net_local *lp)
{
	struct net_device *dev = &lp->dev;

	ether_setup(dev);
	dev->tx_queue_len = 10;
	dev->hard_header_len += isdn_hard_header_len();

	return 0;
}

static struct isdn_netif_ops ether_ops = {
	.hard_start_xmit     = isdn_net_start_xmit,
	.hard_header         = eth_header,
	.receive             = isdn_ether_receive,
	.init                = isdn_ether_init,
	.open                = isdn_ether_open,
};

// ======================================================================

void
isdn_net_init(void)
{
	register_isdn_netif(ISDN_NET_ENCAP_ETHER,      &ether_ops);
	register_isdn_netif(ISDN_NET_ENCAP_RAWIP,      &rawip_ops);
	register_isdn_netif(ISDN_NET_ENCAP_IPTYP,      &iptyp_ops);
	register_isdn_netif(ISDN_NET_ENCAP_UIHDLC,     &uihdlc_ops);
	register_isdn_netif(ISDN_NET_ENCAP_CISCOHDLC,  &ciscohdlck_ops);
	register_isdn_netif(ISDN_NET_ENCAP_CISCOHDLCK, &ciscohdlck_ops);
#ifdef CONFIG_ISDN_X25
	register_isdn_netif(ISDN_NET_ENCAP_X25IFACE,   &isdn_x25_ops);
#endif
#ifdef CONFIG_ISDN_PPP
	register_isdn_netif(ISDN_NET_ENCAP_SYNCPPP,    &isdn_ppp_ops);
#endif

	isdn_net_lib_init();
}

