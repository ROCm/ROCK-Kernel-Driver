/* Linux ISDN subsystem, network interfaces and related functions (linklevel).
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/isdn.h>
#include <linux/inetdevice.h>
#include <net/arp.h>
#include "isdn_common.h"
#include "isdn_net_lib.h"
#include "isdn_net.h"

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
	u16 protocol;

	get_u16(skb->data, &protocol);
	skb_pull(skb, 2);
	isdn_netif_rx(idev, skb, protocol);
}

struct isdn_netif_ops isdn_iptyp_ops = {
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
	skb_pull(skb, 2);
	isdn_netif_rx(idev, skb, htons(ETH_P_IP));
}

struct isdn_netif_ops isdn_uihdlc_ops = {
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

struct isdn_netif_ops isdn_rawip_ops = {
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
	isdn_netif_rx(idev, skb, eth_type_trans(skb, &lp->dev));
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

struct isdn_netif_ops isdn_ether_ops = {
	.hard_start_xmit     = isdn_net_start_xmit,
	.receive             = isdn_ether_receive,
	.init                = isdn_ether_init,
	.open                = isdn_ether_open,
};
