/*
 * Linux ISDN subsystem, CISCO HDLC network interfaces
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *           2001       by Bjoern A. Zeeb <i4l@zabbadoz.net>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For info on the protocol, see http://i4l.zabbadoz.net/i4l/cisco-hdlc.txt
 */

#include "isdn_common.h"
#include "isdn_net.h"

#include <linux/inetdevice.h>

/* 
 * CISCO HDLC keepalive specific stuff
 */
static struct sk_buff*
isdn_net_ciscohdlck_alloc_skb(isdn_net_local *lp, int len)
{
	unsigned short hl = isdn_slot_hdrlen(lp->isdn_slot);
	struct sk_buff *skb;

	skb = alloc_skb(hl + len, GFP_ATOMIC);
	if (!skb) {
		printk("isdn out of mem at %s:%d!\n", __FILE__, __LINE__);
		return 0;
	}
	skb_reserve(skb, hl);
	return skb;
}

/* cisco hdlck device private ioctls */
int
isdn_ciscohdlck_dev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	isdn_net_local *lp = (isdn_net_local *) dev->priv;
	unsigned long len = 0;
	unsigned long expires = 0;
	int tmp = 0;
	int period = lp->cisco_keepalive_period;
	char debserint = lp->cisco_debserint;
	int rc = 0;

	if (lp->p_encap != ISDN_NET_ENCAP_CISCOHDLCK)
		return -EINVAL;

	switch (cmd) {
		/* get/set keepalive period */
		case SIOCGKEEPPERIOD:
			len = (unsigned long)sizeof(lp->cisco_keepalive_period);
			if (copy_to_user((char *)ifr->ifr_ifru.ifru_data,
				(int *)&lp->cisco_keepalive_period, len))
				rc = -EFAULT;
			break;
		case SIOCSKEEPPERIOD:
			tmp = lp->cisco_keepalive_period;
			len = (unsigned long)sizeof(lp->cisco_keepalive_period);
			if (copy_from_user((int *)&period,
				(char *)ifr->ifr_ifru.ifru_data, len))
				rc = -EFAULT;
			if ((period > 0) && (period <= 32767))
				lp->cisco_keepalive_period = period;
			else
				rc = -EINVAL;
			if (!rc && (tmp != lp->cisco_keepalive_period)) {
				expires = (unsigned long)(jiffies +
					lp->cisco_keepalive_period * HZ);
				mod_timer(&lp->cisco_timer, expires);
				printk(KERN_INFO "%s: Keepalive period set "
					"to %d seconds.\n",
					lp->name, lp->cisco_keepalive_period);
			}
			break;

		/* get/set debugging */
		case SIOCGDEBSERINT:
			len = (unsigned long)sizeof(lp->cisco_debserint);
			if (copy_to_user((char *)ifr->ifr_ifru.ifru_data,
				(char *)&lp->cisco_debserint, len))
				rc = -EFAULT;
			break;
		case SIOCSDEBSERINT:
			len = (unsigned long)sizeof(lp->cisco_debserint);
			if (copy_from_user((char *)&debserint,
				(char *)ifr->ifr_ifru.ifru_data, len))
				rc = -EFAULT;
			if ((debserint >= 0) && (debserint <= 64))
				lp->cisco_debserint = debserint;
			else
				rc = -EINVAL;
			break;

		default:
			rc = -EINVAL;
			break;
	}
	return (rc);
}

/* called via cisco_timer.function */
static void
isdn_net_ciscohdlck_slarp_send_keepalive(unsigned long data)
{
	isdn_net_local *lp = (isdn_net_local *) data;
	struct sk_buff *skb;
	unsigned char *p;
	unsigned long last_cisco_myseq = lp->cisco_myseq;
	int myseq_diff = 0;

	lp->cisco_myseq++;

	myseq_diff = (lp->cisco_myseq - lp->cisco_mineseen);
	if ((lp->cisco_line_state) && ((myseq_diff >= 3)||(myseq_diff <= -3))) {
		/* line up -> down */
		lp->cisco_line_state = 0;
		printk (KERN_WARNING
				"UPDOWN: Line protocol on Interface %s,"
				" changed state to down\n", lp->name);
		/* should stop routing higher-level data accross */
	} else if ((!lp->cisco_line_state) &&
		(myseq_diff >= 0) && (myseq_diff <= 2)) {
		/* line down -> up */
		lp->cisco_line_state = 1;
		printk (KERN_WARNING
				"UPDOWN: Line protocol on Interface %s,"
				" changed state to up\n", lp->name);
		/* restart routing higher-level data accross */
	}

	if (lp->cisco_debserint)
		printk (KERN_DEBUG "%s: HDLC "
			"myseq %lu, mineseen %lu%c, yourseen %lu, %s\n",
			lp->name, last_cisco_myseq, lp->cisco_mineseen,
			((last_cisco_myseq == lp->cisco_mineseen) ? '*' : 040),
			lp->cisco_yourseq,
			((lp->cisco_line_state) ? "line up" : "line down"));

	skb = isdn_net_ciscohdlck_alloc_skb(lp, 4 + 14);
	if (!skb)
		return;

	p = skb_put(skb, 4 + 14);

	/* cisco header */
	p += put_u8 (p, CISCO_ADDR_UNICAST);
	p += put_u8 (p, CISCO_CTRL);
	p += put_u16(p, CISCO_TYPE_SLARP);

	/* slarp keepalive */
	p += put_u32(p, CISCO_SLARP_KEEPALIVE);
	p += put_u32(p, lp->cisco_myseq);
	p += put_u32(p, lp->cisco_yourseq);
	p += put_u16(p, 0xffff); // reliablity, always 0xffff

	isdn_net_write_super(lp, skb);

	lp->cisco_timer.expires = jiffies + lp->cisco_keepalive_period * HZ;
	
	add_timer(&lp->cisco_timer);
}

static void
isdn_net_ciscohdlck_slarp_send_request(isdn_net_local *lp)
{
	struct sk_buff *skb;
	unsigned char *p;

	skb = isdn_net_ciscohdlck_alloc_skb(lp, 4 + 14);
	if (!skb)
		return;

	p = skb_put(skb, 4 + 14);

	/* cisco header */
	p += put_u8 (p, CISCO_ADDR_UNICAST);
	p += put_u8 (p, CISCO_CTRL);
	p += put_u16(p, CISCO_TYPE_SLARP);

	/* slarp request */
	p += put_u32(p, CISCO_SLARP_REQUEST);
	p += put_u32(p, 0); // address
	p += put_u32(p, 0); // netmask
	p += put_u16(p, 0); // unused

	isdn_net_write_super(lp, skb);
}

void 
isdn_ciscohdlck_connected(isdn_net_local *lp)
{
	lp->cisco_myseq = 0;
	lp->cisco_mineseen = 0;
	lp->cisco_yourseq = 0;
	lp->cisco_keepalive_period = ISDN_TIMER_KEEPINT;
	lp->cisco_last_slarp_in = 0;
	lp->cisco_line_state = 0;
	lp->cisco_debserint = 0;

	/* send slarp request because interface/seq.no.s reset */
	isdn_net_ciscohdlck_slarp_send_request(lp);

	init_timer(&lp->cisco_timer);
	lp->cisco_timer.data = (unsigned long) lp;
	lp->cisco_timer.function = isdn_net_ciscohdlck_slarp_send_keepalive;
	lp->cisco_timer.expires = jiffies + lp->cisco_keepalive_period * HZ;
	add_timer(&lp->cisco_timer);
}

void 
isdn_ciscohdlck_disconnected(isdn_net_local *lp)
{
	del_timer(&lp->cisco_timer);
}

static void
isdn_net_ciscohdlck_slarp_send_reply(isdn_net_local *lp)
{
	struct sk_buff *skb;
	unsigned char *p;
	struct in_device *in_dev = NULL;
	u32 addr = 0;		/* local ipv4 address */
	u32 mask = 0;		/* local netmask */

	if ((in_dev = lp->netdev->dev.ip_ptr) != NULL) {
		/* take primary(first) address of interface */
		struct in_ifaddr *ifa = in_dev->ifa_list;
		if (ifa != NULL) {
			addr = ifa->ifa_local;
			mask = ifa->ifa_mask;
		}
	}

	skb = isdn_net_ciscohdlck_alloc_skb(lp, 4 + 14);
	if (!skb)
		return;

	p = skb_put(skb, 4 + 14);

	/* cisco header */
	p += put_u8 (p, CISCO_ADDR_UNICAST);
	p += put_u8 (p, CISCO_CTRL);
	p += put_u16(p, CISCO_TYPE_SLARP);

	/* slarp reply, send own ip/netmask; if values are nonsense remote
	 * should think we are unable to provide it with an address via SLARP */
	p += put_u32(p, CISCO_SLARP_REPLY);
	p += put_u32(p, addr);	// address
	p += put_u32(p, mask);	// netmask
	p += put_u16(p, 0);	// unused

	isdn_net_write_super(lp, skb);
}

static void
isdn_net_ciscohdlck_slarp_in(isdn_net_local *lp, struct sk_buff *skb)
{
	unsigned char *p;
	int period;
	u32 code;
	u32 my_seq, addr;
	u32 your_seq, mask;
	u32 local;
	u16 unused;

	if (skb->len < 14)
		return;

	p = skb->data;
	p += get_u32(p, &code);
	
	switch (code) {
	case CISCO_SLARP_REQUEST:
		lp->cisco_yourseq = 0;
		isdn_net_ciscohdlck_slarp_send_reply(lp);
		break;
	case CISCO_SLARP_REPLY:
		addr = ntohl(*(u32 *)p);
		mask = ntohl(*(u32 *)(p+4));
		if (mask != 0xfffffffc)
			goto slarp_reply_out;
		if ((addr & 3) == 0 || (addr & 3) == 3)
			goto slarp_reply_out;
		local = addr ^ 3;
		printk(KERN_INFO "%s: got slarp reply: "
			"remote ip: %d.%d.%d.%d, "
			"local ip: %d.%d.%d.%d "
			"mask: %d.%d.%d.%d\n",
		       lp->name,
		       HIPQUAD(addr),
		       HIPQUAD(local),
		       HIPQUAD(mask));
		break;
  slarp_reply_out:
		 printk(KERN_INFO "%s: got invalid slarp "
				 "reply (%d.%d.%d.%d/%d.%d.%d.%d) "
				 "- ignored\n", lp->name,
				 HIPQUAD(addr), HIPQUAD(mask));
		break;
	case CISCO_SLARP_KEEPALIVE:
		period = (int)((jiffies - lp->cisco_last_slarp_in
				+ HZ/2 - 1) / HZ);
		if (lp->cisco_debserint &&
				(period != lp->cisco_keepalive_period) &&
				lp->cisco_last_slarp_in) {
			printk(KERN_DEBUG "%s: Keepalive period mismatch - "
				"is %d but should be %d.\n",
				lp->name, period, lp->cisco_keepalive_period);
		}
		lp->cisco_last_slarp_in = jiffies;
		p += get_u32(p, &my_seq);
		p += get_u32(p, &your_seq);
		p += get_u16(p, &unused);
		lp->cisco_yourseq = my_seq;
		lp->cisco_mineseen = your_seq;
		break;
	}
}

void
isdn_ciscohdlck_receive(isdn_net_local *lp, struct sk_buff *skb)
{
	unsigned char *p;
 	u8 addr;
 	u8 ctrl;
 	u16 type;
	
	if (skb->len < 4)
		goto out_free;

	p = skb->data;
	p += get_u8 (p, &addr);
	p += get_u8 (p, &ctrl);
	p += get_u16(p, &type);
	skb_pull(skb, 4);
	
	if (addr != CISCO_ADDR_UNICAST && addr != CISCO_ADDR_BROADCAST) {
		printk(KERN_WARNING "%s: Unknown Cisco addr 0x%02x\n",
		       lp->name, addr);
		goto out_free;
	}
	if (ctrl != CISCO_CTRL) {
		printk(KERN_WARNING "%s: Unknown Cisco ctrl 0x%02x\n",
		       lp->name, ctrl);
		goto out_free;
	}

	switch (type) {
	case CISCO_TYPE_SLARP:
		isdn_net_ciscohdlck_slarp_in(lp, skb);
		goto out_free;
	case CISCO_TYPE_CDP:
		if (lp->cisco_debserint)
			printk(KERN_DEBUG "%s: Received CDP packet. use "
				"\"no cdp enable\" on cisco.\n", lp->name);
		goto out_free;
	default:
		/* no special cisco protocol */
		skb->protocol = htons(type);
		netif_rx(skb);
		return;
	}

 out_free:
	kfree_skb(skb);
}

static int
isdn_ciscohdlck_header(struct sk_buff *skb, struct net_device *dev, 
		      unsigned short type,
		      void *daddr, void *saddr, unsigned plen)
{
	unsigned char *p = skb_push(skb, 4);

	p += put_u8 (p, CISCO_ADDR_UNICAST);
	p += put_u8 (p, CISCO_CTRL);
	p += put_u16(p, type);
	
	return 4;
}

int
isdn_ciscohdlck_setup(isdn_net_dev *p)
{
	isdn_net_local *lp = &p->local;

	p->dev.hard_header = isdn_ciscohdlck_header;
	p->dev.hard_header_cache = NULL;
	p->dev.header_cache_update = NULL;
	p->dev.flags = IFF_NOARP|IFF_POINTOPOINT;
	if (lp->p_encap == ISDN_NET_ENCAP_CISCOHDLCK)
		p->dev.do_ioctl = isdn_ciscohdlck_dev_ioctl;
	else
		p->dev.do_ioctl = NULL;

	return 0;
}

