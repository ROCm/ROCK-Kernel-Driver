/*
 * Generic HDLC support routines for Linux
 *
 * Copyright (C) 1999, 2000 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Current status:
 *    - this is work in progress
 *    - not heavily tested on SMP
 *    - currently supported:
 *	* raw IP-in-HDLC
 *	* Cisco HDLC
 *	* Frame Relay with ANSI or CCITT LMI (both user and network side)
 *	* PPP (using syncppp.c)
 *	* X.25
 *
 * Use sethdlc utility to set line parameters, protocol and PVCs
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/pkt_sched.h>
#include <linux/inetdevice.h>
#include <linux/lapb.h>
#include <linux/rtnetlink.h>
#include <linux/hdlc.h>

/* #define DEBUG_PKT */
/* #define DEBUG_HARD_HEADER */
/* #define DEBUG_FECN */
/* #define DEBUG_BECN */

static const char* version = "HDLC support module revision 1.02 for Linux 2.4";


#define CISCO_MULTICAST		0x8F	/* Cisco multicast address */
#define CISCO_UNICAST		0x0F	/* Cisco unicast address */
#define CISCO_KEEPALIVE		0x8035	/* Cisco keepalive protocol */
#define CISCO_SYS_INFO		0x2000	/* Cisco interface/system info */
#define CISCO_ADDR_REQ		0	/* Cisco address request */
#define CISCO_ADDR_REPLY	1	/* Cisco address reply */
#define CISCO_KEEPALIVE_REQ	2	/* Cisco keepalive request */

static int hdlc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);

/********************************************************
 *
 * Cisco HDLC support
 *
 *******************************************************/

static int cisco_hard_header(struct sk_buff *skb, struct net_device *dev,
			     u16 type, void *daddr, void *saddr,
			     unsigned int len)
{
	hdlc_header *data;
#ifdef DEBUG_HARD_HEADER
	printk(KERN_DEBUG "%s: cisco_hard_header called\n", dev->name);
#endif

	skb_push(skb, sizeof(hdlc_header));
	data = (hdlc_header*)skb->data;
	if (type == CISCO_KEEPALIVE)
		data->address = CISCO_MULTICAST;
	else
		data->address = CISCO_UNICAST;
	data->control = 0;
	data->protocol = htons(type);

	return sizeof(hdlc_header);
}



static void cisco_keepalive_send(hdlc_device *hdlc, u32 type,
				 u32 par1, u32 par2)
{
	struct sk_buff *skb;
	cisco_packet *data;

	skb = dev_alloc_skb(sizeof(hdlc_header)+sizeof(cisco_packet));
	if (!skb) {
		printk(KERN_WARNING "%s: Memory squeeze on cisco_keepalive_send()\n",
			       hdlc_to_name(hdlc));
		return;
	}
	skb_reserve(skb, 4);
	cisco_hard_header(skb, hdlc_to_dev(hdlc), CISCO_KEEPALIVE,
			  NULL, NULL, 0);
	data = (cisco_packet*)skb->tail;

	data->type = htonl(type);
	data->par1 = htonl(par1);
	data->par2 = htonl(par2);
	data->rel = 0xFFFF;
	data->time = htonl(jiffies * 1000 / HZ);

	skb_put(skb, sizeof(cisco_packet));
	skb->priority = TC_PRIO_CONTROL;
	skb->dev = hdlc_to_dev(hdlc);

	dev_queue_xmit(skb);
}



static void cisco_netif(hdlc_device *hdlc, struct sk_buff *skb)
{
	hdlc_header *data = (hdlc_header*)skb->data;
	cisco_packet *cisco_data;
	struct in_device *in_dev;
	u32 addr, mask;

	if (skb->len<sizeof(hdlc_header))
		goto rx_error;

	if (data->address != CISCO_MULTICAST &&
	    data->address != CISCO_UNICAST)
		goto rx_error;

	skb_pull(skb, sizeof(hdlc_header));

	switch(ntohs(data->protocol)) {
	case ETH_P_IP:
	case ETH_P_IPX:
	case ETH_P_IPV6:
		skb->protocol = data->protocol;
		skb->dev = hdlc_to_dev(hdlc);
		netif_rx(skb);
		return;

	case CISCO_SYS_INFO:
		/* Packet is not needed, drop it. */
		dev_kfree_skb_any(skb);
		return;

	case CISCO_KEEPALIVE:
		if (skb->len != CISCO_PACKET_LEN &&
		    skb->len != CISCO_BIG_PACKET_LEN) {
			printk(KERN_INFO "%s: Invalid length of Cisco "
			       "control packet (%d bytes)\n",
			       hdlc_to_name(hdlc), skb->len);
			goto rx_error;
		}

		cisco_data = (cisco_packet*)skb->data;

		switch(ntohl (cisco_data->type)) {
		case CISCO_ADDR_REQ: /* Stolen from syncppp.c :-) */
			in_dev = hdlc_to_dev(hdlc)->ip_ptr;
			addr = 0;
			mask = ~0; /* is the mask correct? */

			if (in_dev != NULL) {
				struct in_ifaddr **ifap = &in_dev->ifa_list;

				while (*ifap != NULL) {
					if (strcmp(hdlc_to_name(hdlc),
						   (*ifap)->ifa_label) == 0) {
						addr = (*ifap)->ifa_local;
						mask = (*ifap)->ifa_mask;
						break;
					}
					ifap = &(*ifap)->ifa_next;
				}

				cisco_keepalive_send(hdlc, CISCO_ADDR_REPLY,
						     addr, mask);
			}
			dev_kfree_skb_any(skb);
			return;

		case CISCO_ADDR_REPLY:
			printk(KERN_INFO "%s: Unexpected Cisco IP address "
			       "reply\n", hdlc_to_name(hdlc));
			goto rx_error;

		case CISCO_KEEPALIVE_REQ:
			hdlc->lmi.rxseq = ntohl(cisco_data->par1);
			if (ntohl(cisco_data->par2) == hdlc->lmi.txseq) {
				hdlc->lmi.last_poll = jiffies;
				if (!(hdlc->lmi.state & LINK_STATE_RELIABLE)) {
					u32 sec, min, hrs, days;
					sec = ntohl(cisco_data->time) / 1000;
					min = sec / 60; sec -= min * 60;
					hrs = min / 60; min -= hrs * 60;
					days = hrs / 24; hrs -= days * 24;
					printk(KERN_INFO "%s: Link up (peer "
					       "uptime %ud%uh%um%us)\n",
					       hdlc_to_name(hdlc), days, hrs,
					       min, sec);
				}
				hdlc->lmi.state |= LINK_STATE_RELIABLE;
			}

			dev_kfree_skb_any(skb);
			return;
		} /* switch(keepalive type) */
	} /* switch(protocol) */

	printk(KERN_INFO "%s: Unsupported protocol %x\n", hdlc_to_name(hdlc),
	       data->protocol);
	dev_kfree_skb_any(skb);
	return;

 rx_error:
	hdlc->stats.rx_errors++; /* Mark error */
	dev_kfree_skb_any(skb);
}



static void cisco_timer(unsigned long arg)
{
	hdlc_device *hdlc = (hdlc_device*)arg;

	if ((hdlc->lmi.state & LINK_STATE_RELIABLE) &&
	    (jiffies - hdlc->lmi.last_poll >= hdlc->lmi.T392 * HZ)) {
		hdlc->lmi.state &= ~LINK_STATE_RELIABLE;
		printk(KERN_INFO "%s: Link down\n", hdlc_to_name(hdlc));
	}

	cisco_keepalive_send(hdlc, CISCO_KEEPALIVE_REQ, ++hdlc->lmi.txseq,
			     hdlc->lmi.rxseq);
	hdlc->timer.expires = jiffies + hdlc->lmi.T391*HZ;

	hdlc->timer.function = cisco_timer;
	hdlc->timer.data = arg;
	add_timer(&hdlc->timer);
}



/******************************************************************
 *
 *     generic Frame Relay routines
 *
 *****************************************************************/


static int fr_hard_header(struct sk_buff *skb, struct net_device *dev,
			  u16 type, void *daddr, void *saddr, unsigned int len)
{
	u16 head_len;

	if (!daddr)
		daddr = dev->broadcast;

#ifdef DEBUG_HARD_HEADER
	printk(KERN_DEBUG "%s: fr_hard_header called\n", dev->name);
#endif

	switch(type) {
	case ETH_P_IP:
		head_len = 4;
		skb_push(skb, head_len);
		skb->data[3] = NLPID_IP;
		break;

	case ETH_P_IPV6:
		head_len = 4;
		skb_push(skb, head_len);
		skb->data[3] = NLPID_IPV6;
		break;

	case LMI_PROTO:
		head_len = 4;
		skb_push(skb, head_len);
		skb->data[3] = LMI_PROTO;
		break;

	default:
		head_len = 10;
		skb_push(skb, head_len);
		skb->data[3] = FR_PAD;
		skb->data[4] = NLPID_SNAP;
		skb->data[5] = FR_PAD;
		skb->data[6] = FR_PAD;
		skb->data[7] = FR_PAD;
		skb->data[8] = type>>8;
		skb->data[9] = (u8)type;
	}

	memcpy(skb->data, daddr, 2);
	skb->data[2] = FR_UI;

	return head_len;
}



static inline void fr_log_dlci_active(pvc_device *pvc)
{
	printk(KERN_INFO "%s: %sactive%s\n", pvc_to_name(pvc),
	       pvc->state & PVC_STATE_ACTIVE ? "" : "in",
	       pvc->state & PVC_STATE_NEW ? " new" : "");
}



static inline u8 fr_lmi_nextseq(u8 x)
{
	x++;
	return x ? x : 1;
}



static void fr_lmi_send(hdlc_device *hdlc, int fullrep)
{
	struct sk_buff *skb;
	pvc_device *pvc = hdlc->first_pvc;
	int len = mode_is(hdlc, MODE_FR_ANSI) ? LMI_ANSI_LENGTH : LMI_LENGTH;
	int stat_len = 3;
	u8 *data;
	int i = 0;

	if (mode_is(hdlc, MODE_DCE) && fullrep) {
		len += hdlc->pvc_count * (2 + stat_len);
		if (len > HDLC_MAX_MTU) {
			printk(KERN_WARNING "%s: Too many PVCs while sending "
			       "LMI full report\n", hdlc_to_name(hdlc));
			return;
		}
	}

	skb = dev_alloc_skb(len);
	if (!skb) {
		printk(KERN_WARNING "%s: Memory squeeze on fr_lmi_send()\n",
			       hdlc_to_name(hdlc));
		return;
	}
	memset(skb->data, 0, len);
	skb_reserve(skb, 4);
	fr_hard_header(skb, hdlc_to_dev(hdlc), LMI_PROTO, NULL, NULL, 0);
	data = skb->tail;
	data[i++] = LMI_CALLREF;
	data[i++] = mode_is(hdlc, MODE_DCE) ? LMI_STATUS : LMI_STATUS_ENQUIRY;
	if (mode_is(hdlc, MODE_FR_ANSI))
		data[i++] = LMI_ANSI_LOCKSHIFT;
	data[i++] = mode_is(hdlc, MODE_FR_CCITT) ? LMI_CCITT_REPTYPE :
		LMI_REPTYPE;
	data[i++] = LMI_REPT_LEN;
	data[i++] = fullrep ? LMI_FULLREP : LMI_INTEGRITY;

	data[i++] = mode_is(hdlc, MODE_FR_CCITT) ? LMI_CCITT_ALIVE : LMI_ALIVE;
	data[i++] = LMI_INTEG_LEN;
	data[i++] = hdlc->lmi.txseq = fr_lmi_nextseq(hdlc->lmi.txseq);
	data[i++] = hdlc->lmi.rxseq;

	if (mode_is(hdlc, MODE_DCE) && fullrep) {
		while (pvc) {
			data[i++] = mode_is(hdlc, MODE_FR_CCITT) ?
				LMI_CCITT_PVCSTAT:LMI_PVCSTAT;
			data[i++] = stat_len;

			if ((hdlc->lmi.state & LINK_STATE_RELIABLE) &&
			    (pvc->netdev.flags & IFF_UP) &&
			    !(pvc->state & (PVC_STATE_ACTIVE|PVC_STATE_NEW))) {
				pvc->state |= PVC_STATE_NEW;
				fr_log_dlci_active(pvc);
			}

			dlci_to_status(hdlc, netdev_dlci(&pvc->netdev),
				       data+i, pvc->state);
			i += stat_len;
			pvc = pvc->next;
		}
	}

	skb_put(skb, i);
	skb->priority = TC_PRIO_CONTROL;
	skb->dev = hdlc_to_dev(hdlc);

	dev_queue_xmit(skb);
}



static void fr_timer(unsigned long arg)
{
	hdlc_device *hdlc = (hdlc_device*)arg;
	int i, cnt = 0, reliable;
	u32 list;

	if (mode_is(hdlc, MODE_DCE))
		reliable = (jiffies - hdlc->lmi.last_poll < hdlc->lmi.T392*HZ);
	else {
		hdlc->lmi.last_errors <<= 1; /* Shift the list */
		if (hdlc->lmi.state & LINK_STATE_REQUEST) {
			printk(KERN_INFO "%s: No LMI status reply received\n",
			       hdlc_to_name(hdlc));
			hdlc->lmi.last_errors |= 1;
		}

		for (i = 0, list = hdlc->lmi.last_errors; i < hdlc->lmi.N393;
		     i++, list >>= 1)
			cnt += (list & 1);	/* errors count */

		reliable = (cnt < hdlc->lmi.N392);
	}

	if ((hdlc->lmi.state & LINK_STATE_RELIABLE) !=
	    (reliable ? LINK_STATE_RELIABLE : 0)) {
		pvc_device *pvc = hdlc->first_pvc;

		while (pvc) {/* Deactivate all PVCs */
			pvc->state &= ~(PVC_STATE_NEW | PVC_STATE_ACTIVE);
			pvc = pvc->next;
		}

		hdlc->lmi.state ^= LINK_STATE_RELIABLE;
		printk(KERN_INFO "%s: Link %sreliable\n", hdlc_to_name(hdlc),
		       reliable ? "" : "un");

		if (reliable) {
			hdlc->lmi.N391cnt = 0; /* Request full status */
			hdlc->lmi.state |= LINK_STATE_CHANGED;
		}
	}

	if (mode_is(hdlc, MODE_DCE))
		hdlc->timer.expires = jiffies + hdlc->lmi.T392*HZ;
	else {
		if (hdlc->lmi.N391cnt)
			hdlc->lmi.N391cnt--;

		fr_lmi_send(hdlc, hdlc->lmi.N391cnt == 0);

		hdlc->lmi.state |= LINK_STATE_REQUEST;
		hdlc->timer.expires = jiffies + hdlc->lmi.T391*HZ;
	}

	hdlc->timer.function = fr_timer;
	hdlc->timer.data = arg;
	add_timer(&hdlc->timer);
}



static int fr_lmi_recv(hdlc_device *hdlc, struct sk_buff *skb)
{
	int stat_len;
	pvc_device *pvc;
	int reptype = -1, error;
	u8 rxseq, txseq;
	int i;

	if (skb->len < (mode_is(hdlc, MODE_FR_ANSI) ?
			LMI_ANSI_LENGTH : LMI_LENGTH)) {
		printk(KERN_INFO "%s: Short LMI frame\n", hdlc_to_name(hdlc));
		return 1;
	}

	if (skb->data[5] != (!mode_is(hdlc, MODE_DCE) ?
			     LMI_STATUS : LMI_STATUS_ENQUIRY)) {
		printk(KERN_INFO "%s: LMI msgtype=%x, Not LMI status %s\n",
		       hdlc_to_name(hdlc), skb->data[2],
		       mode_is(hdlc, MODE_DCE) ? "enquiry" : "reply");
		return 1;
	}

	i = mode_is(hdlc, MODE_FR_ANSI) ? 7 : 6;

	if (skb->data[i] !=
	    (mode_is(hdlc, MODE_FR_CCITT) ? LMI_CCITT_REPTYPE : LMI_REPTYPE)) {
		printk(KERN_INFO "%s: Not a report type=%x\n",
		       hdlc_to_name(hdlc), skb->data[i]);
		return 1;
	}
	i++;

	i++;				/* Skip length field */

	reptype = skb->data[i++];

	if (skb->data[i]!=
	    (mode_is(hdlc, MODE_FR_CCITT) ? LMI_CCITT_ALIVE : LMI_ALIVE)) {
		printk(KERN_INFO "%s: Unsupported status element=%x\n",
		       hdlc_to_name(hdlc), skb->data[i]);
		return 1;
	}
	i++;

	i++;			/* Skip length field */

	hdlc->lmi.rxseq = skb->data[i++]; /* TX sequence from peer */
	rxseq = skb->data[i++];	/* Should confirm our sequence */

	txseq = hdlc->lmi.txseq;

	if (mode_is(hdlc, MODE_DCE)) {
		if (reptype != LMI_FULLREP && reptype != LMI_INTEGRITY) {
			printk(KERN_INFO "%s: Unsupported report type=%x\n",
			       hdlc_to_name(hdlc), reptype);
			return 1;
		}
	}

	error = 0;
	if (!(hdlc->lmi.state & LINK_STATE_RELIABLE))
		error = 1;

	if (rxseq == 0 || rxseq != txseq) {
		hdlc->lmi.N391cnt = 0; /* Ask for full report next time */
		error = 1;
	}

	if (mode_is(hdlc, MODE_DCE)) {
		if ((hdlc->lmi.state & LINK_STATE_FULLREP_SENT) && !error) {
/* Stop sending full report - the last one has been confirmed by DTE */
			hdlc->lmi.state &= ~LINK_STATE_FULLREP_SENT;
			pvc = hdlc->first_pvc;
			while (pvc) {
				if (pvc->state & PVC_STATE_NEW) {
					pvc->state &= ~PVC_STATE_NEW;
					pvc->state |= PVC_STATE_ACTIVE;
					fr_log_dlci_active(pvc);

/* Tell DTE that new PVC is now active */
					hdlc->lmi.state |= LINK_STATE_CHANGED;
				}
				pvc = pvc->next;
			}
		}

		if (hdlc->lmi.state & LINK_STATE_CHANGED) {
			reptype = LMI_FULLREP;
			hdlc->lmi.state |= LINK_STATE_FULLREP_SENT;
			hdlc->lmi.state &= ~LINK_STATE_CHANGED;
		}

		fr_lmi_send(hdlc, reptype == LMI_FULLREP ? 1 : 0);
		return 0;
	}

	/* DTE */

	if (reptype != LMI_FULLREP || error)
		return 0;

	stat_len = 3;
	pvc = hdlc->first_pvc;

	while (pvc) {
		pvc->newstate = 0;
		pvc = pvc->next;
	}

	while (skb->len >= i + 2 + stat_len) {
		u16 dlci;
		u8 state = 0;

		if (skb->data[i] != (mode_is(hdlc, MODE_FR_CCITT) ?
				     LMI_CCITT_PVCSTAT : LMI_PVCSTAT)) {
			printk(KERN_WARNING "%s: Invalid PVCSTAT ID: %x\n",
			       hdlc_to_name(hdlc), skb->data[i]);
			return 1;
		}
		i++;

		if (skb->data[i] != stat_len) {
			printk(KERN_WARNING "%s: Invalid PVCSTAT length: %x\n",
			       hdlc_to_name(hdlc), skb->data[i]);
			return 1;
		}
		i++;

		dlci = status_to_dlci(hdlc, skb->data+i, &state);
		pvc = find_pvc(hdlc, dlci);

		if (pvc)
			pvc->newstate = state;
		else if (state == PVC_STATE_NEW)
			printk(KERN_INFO "%s: new PVC available, DLCI=%u\n",
			       hdlc_to_name(hdlc), dlci);

		i += stat_len;
	}

	pvc = hdlc->first_pvc;

	while (pvc) {
		if (pvc->newstate == PVC_STATE_NEW)
			pvc->newstate = PVC_STATE_ACTIVE;

		pvc->newstate |= (pvc->state &
				  ~(PVC_STATE_NEW|PVC_STATE_ACTIVE));
		if (pvc->state != pvc->newstate) {
			pvc->state = pvc->newstate;
			fr_log_dlci_active(pvc);
		}
		pvc = pvc->next;
	}

	/* Next full report after N391 polls */
	hdlc->lmi.N391cnt = hdlc->lmi.N391;

	return 0;
}



static void fr_netif(hdlc_device *hdlc, struct sk_buff *skb)
{
	fr_hdr *fh = (fr_hdr*)skb->data;
	u8 *data = skb->data;
	u16 dlci;
	pvc_device *pvc;

	if (skb->len<4 || fh->ea1 || data[2] != FR_UI)
		goto rx_error;

	dlci = q922_to_dlci(skb->data);

	if (dlci == LMI_DLCI) {
		if (data[3] == LMI_PROTO) {
			if (fr_lmi_recv(hdlc, skb))
				goto rx_error;
			else {
				/* No request pending */
				hdlc->lmi.state &= ~LINK_STATE_REQUEST;
				hdlc->lmi.last_poll = jiffies;
				dev_kfree_skb_any(skb);
				return;
			}
		}

		printk(KERN_INFO "%s: Received non-LMI frame with LMI DLCI\n",
		       hdlc_to_name(hdlc));
		goto rx_error;
	}

	pvc = find_pvc(hdlc, dlci);
	if (!pvc) {
#ifdef DEBUG_PKT
		printk(KERN_INFO "%s: No PVC for received frame's DLCI %d\n",
		       hdlc_to_name(hdlc), dlci);
#endif
		goto rx_error;
	}

	if ((pvc->netdev.flags & IFF_UP) == 0) {
#ifdef DEBUG_PKT
		printk(KERN_INFO "%s: PVC for received frame's DLCI %d is down\n",
		       hdlc_to_name(hdlc), dlci);
#endif
		goto rx_error;
	}

	pvc->stats.rx_packets++; /* PVC traffic */
	pvc->stats.rx_bytes += skb->len;

	if ((pvc->state & PVC_STATE_FECN) != (fh->fecn ? PVC_STATE_FECN : 0)) {
#ifdef DEBUG_FECN
		printk(KERN_DEBUG "%s: FECN O%s\n", pvc_to_name(pvc),
		       fh->fecn ? "N" : "FF");
#endif
		pvc->state ^= PVC_STATE_FECN;
	}

	if ((pvc->state & PVC_STATE_BECN) != (fh->becn ? PVC_STATE_BECN : 0)) {
#ifdef DEBUG_FECN
		printk(KERN_DEBUG "%s: BECN O%s\n", pvc_to_name(pvc),
		       fh->becn ? "N" : "FF");
#endif
		pvc->state ^= PVC_STATE_BECN;
	}

	if (pvc->state & PVC_STATE_BECN)
		pvc->stats.rx_compressed++;

	if (data[3] == NLPID_IP) {
		skb_pull(skb, 4); /* Remove 4-byte header (hdr, UI, NLPID) */
		skb->protocol = htons(ETH_P_IP);
		skb->dev = &pvc->netdev;
		netif_rx(skb);
		return;
	}


	if (data[3] == NLPID_IPV6) {
		skb_pull(skb, 4); /* Remove 4-byte header (hdr, UI, NLPID) */
		skb->protocol = htons(ETH_P_IPV6);
		skb->dev = &pvc->netdev;
		netif_rx(skb);
		return;
	}

	if (data[3] == FR_PAD && data[4] == NLPID_SNAP && data[5] == FR_PAD &&
	    data[6] == FR_PAD && data[7] == FR_PAD &&
	    ((data[8]<<8) | data[9]) == ETH_P_ARP) {
		skb_pull(skb, 10);
		skb->protocol = htons(ETH_P_ARP);
		skb->dev = &pvc->netdev;
		netif_rx(skb);
		return;
	}

	printk(KERN_INFO "%s: Unusupported protocol %x\n",
	       hdlc_to_name(hdlc), data[3]);
	dev_kfree_skb_any(skb);
	return;

 rx_error:
	hdlc->stats.rx_errors++; /* Mark error */
	dev_kfree_skb_any(skb);
}



static void fr_cisco_open(hdlc_device *hdlc)
{
	hdlc->lmi.state = LINK_STATE_CHANGED;
	hdlc->lmi.txseq = hdlc->lmi.rxseq = 0;
	hdlc->lmi.last_errors = 0xFFFFFFFF;
	hdlc->lmi.N391cnt = 0;

	init_timer(&hdlc->timer);
	hdlc->timer.expires = jiffies + HZ; /* First poll after 1 second */
	hdlc->timer.function = mode_is(hdlc, MODE_FR) ? fr_timer : cisco_timer;
	hdlc->timer.data = (unsigned long)hdlc;
	add_timer(&hdlc->timer);
}



static void fr_cisco_close(hdlc_device *hdlc)
{
	pvc_device *pvc = hdlc->first_pvc;

	del_timer_sync(&hdlc->timer);

	while(pvc) {		/* NULL in Cisco mode */
		dev_close(&pvc->netdev); /* Shutdown all PVCs for this FRAD */
		pvc = pvc->next;
	}
}



/******************************************************************
 *
 *     generic HDLC routines
 *
 *****************************************************************/



static int hdlc_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > HDLC_MAX_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}



/********************************************************
 *
 * PVC device routines
 *
 *******************************************************/

static int pvc_open(struct net_device *dev)
{
	pvc_device *pvc = dev_to_pvc(dev);
	int result = 0;

	if ((hdlc_to_dev(pvc->master)->flags & IFF_UP) == 0)
		return -EIO;  /* Master must be UP in order to activate PVC */

	memset(&(pvc->stats), 0, sizeof(struct net_device_stats));
	pvc->state = 0;

	if (!mode_is(pvc->master, MODE_SOFT) && pvc->master->open_pvc)
		result = pvc->master->open_pvc(pvc);
	if (result)
		return result;

	pvc->master->lmi.state |= LINK_STATE_CHANGED;
	return 0;
}



static int pvc_close(struct net_device *dev)
{
	pvc_device *pvc = dev_to_pvc(dev);
	pvc->state = 0;

	if (!mode_is(pvc->master, MODE_SOFT) && pvc->master->close_pvc)
		pvc->master->close_pvc(pvc);

	pvc->master->lmi.state |= LINK_STATE_CHANGED;
	return 0;
}



static int pvc_xmit(struct sk_buff *skb, struct net_device *dev)
{
	pvc_device *pvc = dev_to_pvc(dev);

	if (pvc->state & PVC_STATE_ACTIVE) {
		skb->dev = hdlc_to_dev(pvc->master);
		pvc->stats.tx_bytes += skb->len;
		pvc->stats.tx_packets++;
		if (pvc->state & PVC_STATE_FECN)
			pvc->stats.tx_compressed++; /* TX Congestion counter */
		dev_queue_xmit(skb);
	} else {
		pvc->stats.tx_dropped++;
		dev_kfree_skb(skb);
	}

	return 0;
}



static struct net_device_stats *pvc_get_stats(struct net_device *dev)
{
	pvc_device *pvc = dev_to_pvc(dev);
	return &pvc->stats;
}



static int pvc_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > HDLC_MAX_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}



static void destroy_pvc_list(hdlc_device *hdlc)
{
	pvc_device *pvc = hdlc->first_pvc;
	while(pvc) {
		pvc_device *next = pvc->next;
		unregister_netdevice(&pvc->netdev);
		kfree(pvc);
		pvc = next;
	}

	hdlc->first_pvc = NULL;	/* All PVCs destroyed */
	hdlc->pvc_count = 0;
	hdlc->lmi.state |= LINK_STATE_CHANGED;
}



/********************************************************
 *
 * X.25 protocol support routines
 *
 *******************************************************/

#ifdef CONFIG_HDLC_X25
/* These functions are callbacks called by LAPB layer */

void x25_connect_disconnect(void *token, int reason, int code)
{
	hdlc_device *hdlc = token;
	struct sk_buff *skb;
	unsigned char *ptr;

	if ((skb = dev_alloc_skb(1)) == NULL) {
		printk(KERN_ERR "%s: out of memory\n", hdlc_to_name(hdlc));
		return;
	}

	ptr = skb_put(skb, 1);
	*ptr = code;

	skb->dev = hdlc_to_dev(hdlc);
	skb->protocol = htons(ETH_P_X25);
	skb->mac.raw = skb->data;
	skb->pkt_type = PACKET_HOST;

	netif_rx(skb);
}

void x25_connected(void *token, int reason)
{
	x25_connect_disconnect(token, reason, 1);
}

void x25_disconnected(void *token, int reason)
{
	x25_connect_disconnect(token, reason, 2);
}


int x25_data_indication(void *token, struct sk_buff *skb)
{
	hdlc_device *hdlc = token;
	unsigned char *ptr;

	ptr = skb_push(skb, 1);
	*ptr = 0;

	skb->dev = hdlc_to_dev(hdlc);
	skb->protocol = htons(ETH_P_X25);
	skb->mac.raw = skb->data;
	skb->pkt_type = PACKET_HOST;

	return netif_rx(skb);
}


void x25_data_transmit(void *token, struct sk_buff *skb)
{
	hdlc_device *hdlc = token;
	hdlc->xmit(hdlc, skb);	/* Ignore return value :-( */
}
#endif /* CONFIG_HDLC_X25 */


/********************************************************
 *
 * HDLC device routines
 *
 *******************************************************/

static int hdlc_open(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);
	int result;

	if (hdlc->mode == MODE_NONE)
		return -ENOSYS;

	memset(&(hdlc->stats), 0, sizeof(struct net_device_stats));

	if (mode_is(hdlc, MODE_FR | MODE_SOFT) ||
	    mode_is(hdlc, MODE_CISCO | MODE_SOFT))
		fr_cisco_open(hdlc);
#ifdef CONFIG_HDLC_PPP
	else if (mode_is(hdlc, MODE_PPP | MODE_SOFT)) {
		sppp_attach(&hdlc->pppdev);
		/* sppp_attach nukes them. We don't need syncppp's ioctl */
		dev->do_ioctl = hdlc_ioctl;
		hdlc->pppdev.sppp.pp_flags &= ~PP_CISCO;
		dev->type = ARPHRD_PPP;
		result = sppp_open(dev);
		if (result) {
			sppp_detach(dev);
			return result;
		}
	}
#endif
#ifdef CONFIG_HDLC_X25
	else if (mode_is(hdlc, MODE_X25)) {
		struct lapb_register_struct cb;

		cb.connect_confirmation = x25_connected;
		cb.connect_indication = x25_connected;
		cb.disconnect_confirmation = x25_disconnected;
		cb.disconnect_indication = x25_disconnected;
		cb.data_indication = x25_data_indication;
		cb.data_transmit = x25_data_transmit;

		result = lapb_register(hdlc, &cb);
		if (result != LAPB_OK)
			return result;
	}
#endif
	result = hdlc->open(hdlc);
	if (result) {
		if (mode_is(hdlc, MODE_FR | MODE_SOFT) ||
		    mode_is(hdlc, MODE_CISCO | MODE_SOFT))
			fr_cisco_close(hdlc);
#ifdef CONFIG_HDLC_PPP
		else if (mode_is(hdlc, MODE_PPP | MODE_SOFT)) {
			sppp_close(dev);
			sppp_detach(dev);
			dev->rebuild_header = NULL;
			dev->change_mtu = hdlc_change_mtu;
			dev->mtu = HDLC_MAX_MTU;
			dev->hard_header_len = 16;
		}
#endif
#ifdef CONFIG_HDLC_X25
		else if (mode_is(hdlc, MODE_X25))
			lapb_unregister(hdlc);
#endif
	}

	return result;
}



static int hdlc_close(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);

	hdlc->close(hdlc);

	if (mode_is(hdlc, MODE_FR | MODE_SOFT) ||
	    mode_is(hdlc, MODE_CISCO | MODE_SOFT))
		fr_cisco_close(hdlc);
#ifdef CONFIG_HDLC_PPP
	else if (mode_is(hdlc, MODE_PPP | MODE_SOFT)) {
		sppp_close(dev);
		sppp_detach(dev);
		dev->rebuild_header = NULL;
		dev->change_mtu = hdlc_change_mtu;
		dev->mtu = HDLC_MAX_MTU;
		dev->hard_header_len = 16;
	}
#endif
#ifdef CONFIG_HDLC_X25
	else if (mode_is(hdlc, MODE_X25))
		lapb_unregister(hdlc);
#endif
	return 0;
}



static int hdlc_xmit(struct sk_buff *skb, struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);

#ifdef CONFIG_HDLC_X25
	if (mode_is(hdlc, MODE_X25 | MODE_SOFT)) {
		int result;


		/* X.25 to LAPB */
		switch (skb->data[0]) {
		case 0:		/* Data to be transmitted */
			skb_pull(skb, 1);
			if ((result = lapb_data_request(hdlc, skb)) != LAPB_OK)
				dev_kfree_skb(skb);
			return 0;

		case 1:
			if ((result = lapb_connect_request(hdlc))!= LAPB_OK) {
				if (result == LAPB_CONNECTED) {
				/* Send connect confirm. msg to level 3 */
					x25_connected(hdlc, 0);
				} else {
					printk(KERN_ERR "%s: LAPB connect "
					       "request failed, error code = "
					       "%i\n", hdlc_to_name(hdlc),
					       result);
				}
			}
			break;

		case 2:
			if ((result=lapb_disconnect_request(hdlc))!=LAPB_OK) {
				if (result == LAPB_NOTCONNECTED) {
				/* Send disconnect confirm. msg to level 3 */
					x25_disconnected(hdlc, 0);
				} else {
					printk(KERN_ERR "%s: LAPB disconnect "
					       "request failed, error code = "
					       "%i\n", hdlc_to_name(hdlc),
					       result);
				}
			}
			break;

		default:
			/* to be defined */
			break;
		}

		dev_kfree_skb(skb);
		return 0;
	} /* MODE_X25 */
#endif /* CONFIG_HDLC_X25 */

	return hdlc->xmit(hdlc, skb);
}



void hdlc_netif_rx(hdlc_device *hdlc, struct sk_buff *skb)
{
/* skb contains raw HDLC frame, in both hard- and software modes */
	skb->mac.raw = skb->data;

	switch(hdlc->mode & MODE_MASK) {
	case MODE_HDLC:
		skb->protocol = htons(ETH_P_IP);
		skb->dev = hdlc_to_dev(hdlc);
		netif_rx(skb);
		return;

	case MODE_FR:
		fr_netif(hdlc, skb);
		return;

	case MODE_CISCO:
		cisco_netif(hdlc, skb);
		return;

#ifdef CONFIG_HDLC_PPP
	case MODE_PPP:
#if 0
		sppp_input(hdlc_to_dev(hdlc), skb);
#else
		skb->protocol = htons(ETH_P_WAN_PPP);
		skb->dev = hdlc_to_dev(hdlc);
		netif_rx(skb);
#endif
		return;
#endif
#ifdef CONFIG_HDLC_X25
	case MODE_X25:
		skb->dev = hdlc_to_dev(hdlc);
		if (lapb_data_received(hdlc, skb) == LAPB_OK)
			return;
		break;
#endif
	}

	hdlc->stats.rx_errors++;
	dev_kfree_skb_any(skb);
}



static struct net_device_stats *hdlc_get_stats(struct net_device *dev)
{
	return &dev_to_hdlc(dev)->stats;
}



static int hdlc_set_mode(hdlc_device *hdlc, int mode)
{
	int result = -1;	/* Default to soft modes */
	struct net_device *dev = hdlc_to_dev(hdlc);

	if(!capable(CAP_NET_ADMIN))
		return -EPERM;

	if(dev->flags & IFF_UP)
		return -EBUSY;

	dev->addr_len = 0;
	dev->hard_header = NULL;
	hdlc->mode = MODE_NONE;

	if (!(mode & MODE_SOFT))
		switch(mode & MODE_MASK) {
		case MODE_HDLC:
			result = hdlc->set_mode ?
				hdlc->set_mode(hdlc, MODE_HDLC) : 0;
			break;

		case MODE_CISCO: /* By card */
#ifdef CONFIG_HDLC_PPP
		case MODE_PPP:
#endif
#ifdef CONFIG_HDLC_X25
		case MODE_X25:
#endif
		case MODE_FR:
			result = hdlc->set_mode ?
				hdlc->set_mode(hdlc, mode) : -ENOSYS;
			break;

		default:
			return -EINVAL;
		}

	if (result) {
		mode |= MODE_SOFT; /* Try "host software" protocol */

		switch(mode & MODE_MASK) {
		case MODE_CISCO:
			dev->hard_header = cisco_hard_header;
			break;

#ifdef CONFIG_HDLC_PPP
		case MODE_PPP:
			break;
#endif
#ifdef CONFIG_HDLC_X25
		case MODE_X25:
			break;
#endif

		case MODE_FR:
			dev->hard_header = fr_hard_header;
			dev->addr_len = 2;
			*(u16*)dev->dev_addr = htons(LMI_DLCI);
			dlci_to_q922(dev->broadcast, LMI_DLCI);
			break;

		default:
			return -EINVAL;
		}

		result = hdlc->set_mode ?
			hdlc->set_mode(hdlc, MODE_HDLC) : 0;
	}

	if (result)
		return result;

	hdlc->mode = mode;
	switch(mode & MODE_MASK) {
#ifdef CONFIG_HDLC_PPP
	case MODE_PPP:   dev->type = ARPHRD_PPP;   break;
#endif
#ifdef CONFIG_HDLC_X25
	case MODE_X25:   dev->type = ARPHRD_X25;   break;
#endif
	case MODE_FR:    dev->type = ARPHRD_FRAD;  break;
	case MODE_CISCO: dev->type = ARPHRD_CISCO; break;
	default:         dev->type = ARPHRD_RAWHDLC;
	}

	memset(&(hdlc->stats), 0, sizeof(struct net_device_stats));
	destroy_pvc_list(hdlc);
	return 0;
}



static int hdlc_fr_pvc(hdlc_device *hdlc, int dlci)
{
	pvc_device **pvc_p = &hdlc->first_pvc;
	pvc_device *pvc;
	int result, create = 1;	/* Create or delete PVC */

	if(!capable(CAP_NET_ADMIN))
		return -EPERM;

	if(dlci<0) {
		dlci = -dlci;
		create = 0;
	}

	if(dlci <= 0 || dlci >= 1024)
		return -EINVAL;	/* Only 10 bits for DLCI, DLCI=0 is reserved */

	if(!mode_is(hdlc, MODE_FR))
		return -EINVAL;	/* Only meaningfull on FR */

	while(*pvc_p) {
		if (netdev_dlci(&(*pvc_p)->netdev) == dlci)
			break;
		pvc_p = &(*pvc_p)->next;
	}

	if (create) {		/* Create PVC */
		if (*pvc_p != NULL)
			return -EEXIST;

		pvc = *pvc_p = kmalloc(sizeof(pvc_device), GFP_KERNEL);
		if (!pvc) {
			printk(KERN_WARNING "%s: Memory squeeze on "
			       "hdlc_fr_pvc()\n", hdlc_to_name(hdlc));
			return -ENOBUFS;
		}
		memset(pvc, 0, sizeof(pvc_device));

		pvc->netdev.hard_start_xmit = pvc_xmit;
		pvc->netdev.get_stats = pvc_get_stats;
		pvc->netdev.open = pvc_open;
		pvc->netdev.stop = pvc_close;
		pvc->netdev.change_mtu = pvc_change_mtu;
		pvc->netdev.mtu = HDLC_MAX_MTU;

		pvc->netdev.type = ARPHRD_DLCI;
		pvc->netdev.hard_header_len = 16;
		pvc->netdev.hard_header = fr_hard_header;
		pvc->netdev.tx_queue_len = 0;
		pvc->netdev.flags = IFF_POINTOPOINT;

		pvc->master = hdlc;
		*(u16*)pvc->netdev.dev_addr = htons(dlci);
		dlci_to_q922(pvc->netdev.broadcast, dlci);
		pvc->netdev.addr_len = 2;
		pvc->netdev.irq = hdlc_to_dev(hdlc)->irq;

		result = dev_alloc_name(&pvc->netdev, "pvc%d");
		if (result < 0) {
			kfree(pvc);
			*pvc_p = NULL;
			return result;
		}

		if (register_netdevice(&pvc->netdev) != 0) {
			kfree(pvc);
			*pvc_p = NULL;
			return -EIO;
		}

		if (!mode_is(hdlc, MODE_SOFT) && hdlc->create_pvc) {
			result = hdlc->create_pvc(pvc);
			if (result) {
				unregister_netdevice(&pvc->netdev);
				kfree(pvc);
				*pvc_p = NULL;
				return result;
			}
		}

		hdlc->lmi.state |= LINK_STATE_CHANGED;
		hdlc->pvc_count++;
		return 0;
	}

	if (*pvc_p == NULL)		/* Delete PVC */
		return -ENOENT;

	pvc = *pvc_p;

	if (pvc->netdev.flags & IFF_UP)
		return -EBUSY;		/* PVC in use */

	if (!mode_is(hdlc, MODE_SOFT) && hdlc->destroy_pvc)
		hdlc->destroy_pvc(pvc);

	hdlc->lmi.state |= LINK_STATE_CHANGED;
	hdlc->pvc_count--;
	*pvc_p = pvc->next;
	unregister_netdevice(&pvc->netdev);
	kfree(pvc);
	return 0;
}



static int hdlc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);

	switch(cmd) {
	case HDLCGMODE:
		ifr->ifr_ifru.ifru_ivalue = hdlc->mode;
		return 0;

	case HDLCSMODE:
		return hdlc_set_mode(hdlc, ifr->ifr_ifru.ifru_ivalue);

	case HDLCPVC:
		return hdlc_fr_pvc(hdlc, ifr->ifr_ifru.ifru_ivalue);

	default:
		if (hdlc->ioctl != NULL)
			return hdlc->ioctl(hdlc, ifr, cmd);
	}

	return -EINVAL;
}



static int hdlc_init(struct net_device *dev)
{
	hdlc_device *hdlc = dev_to_hdlc(dev);

	memset(&(hdlc->stats), 0, sizeof(struct net_device_stats));

	dev->get_stats = hdlc_get_stats;
	dev->open = hdlc_open;
	dev->stop = hdlc_close;
	dev->hard_start_xmit = hdlc_xmit;
	dev->do_ioctl = hdlc_ioctl;
	dev->change_mtu = hdlc_change_mtu;
	dev->mtu = HDLC_MAX_MTU;

	dev->type = ARPHRD_RAWHDLC;
	dev->hard_header_len = 16;

	dev->flags = IFF_POINTOPOINT | IFF_NOARP;

	return 0;
}



int register_hdlc_device(hdlc_device *hdlc)
{
	int result;
	struct net_device *dev = hdlc_to_dev(hdlc);

	dev->init = hdlc_init;
	dev->priv = &hdlc->syncppp_ptr;
	hdlc->syncppp_ptr = &hdlc->pppdev;
	hdlc->pppdev.dev = dev;
	hdlc->mode = MODE_NONE;
	hdlc->lmi.T391 = 10;	/* polling verification timer */
	hdlc->lmi.T392 = 15;	/* link integrity verification polling timer */
	hdlc->lmi.N391 = 6;	/* full status polling counter */
	hdlc->lmi.N392 = 3;	/* error threshold */
	hdlc->lmi.N393 = 4;	/* monitored events count */

	result = dev_alloc_name(dev, "hdlc%d");
	if (result<0)
		return result;

	result = register_netdev(dev);
	if (result != 0)
		return -EIO;

	MOD_INC_USE_COUNT;
	return 0;
}



void unregister_hdlc_device(hdlc_device *hdlc)
{
	destroy_pvc_list(hdlc);
	unregister_netdev(hdlc_to_dev(hdlc));
	MOD_DEC_USE_COUNT;
}

MODULE_AUTHOR("Krzysztof Halasa <khc@pm.waw.pl>");
MODULE_DESCRIPTION("HDLC support module");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(hdlc_netif_rx);
EXPORT_SYMBOL(register_hdlc_device);
EXPORT_SYMBOL(unregister_hdlc_device);

static int __init hdlc_module_init(void)
{
	printk(KERN_INFO "%s\n", version);
	return 0;
}


module_init(hdlc_module_init);
