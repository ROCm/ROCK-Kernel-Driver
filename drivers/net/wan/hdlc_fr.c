/*
 * Generic HDLC support routines for Linux
 * Frame Relay support
 *
 * Copyright (C) 1999 - 2001 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/pkt_sched.h>
#include <linux/inetdevice.h>
#include <linux/lapb.h>
#include <linux/rtnetlink.h>
#include <linux/hdlc.h>


__inline__ pvc_device* find_pvc(hdlc_device *hdlc, u16 dlci)
{
	pvc_device *pvc=hdlc->state.fr.first_pvc;
	
	while (pvc) {
		if (netdev_dlci(&pvc->netdev) == dlci)
			return pvc;
		pvc = pvc->next;
	}

	return NULL;
}



__inline__ u16 status_to_dlci(hdlc_device *hdlc, u8 *status,
			      int *active, int *new)
{
	*new = (status[2] & 0x08);
	*active = (!*new && (status[2] & 0x02));

	return ((status[0] & 0x3F)<<4) | ((status[1] & 0x78)>>3);
}


__inline__ void dlci_to_status(hdlc_device *hdlc, u16 dlci, u8 *status,
			       int active, int new)
{
	status[0] = (dlci>>4) & 0x3F;
	status[1] = ((dlci<<3) & 0x78) | 0x80;
	status[2] = 0x80;

	if (new)
		status[2] |= 0x08;
	else if (active)
		status[2] |= 0x02;
}



static int fr_hard_header(struct sk_buff *skb, struct net_device *dev,
			  u16 type, void *daddr, void *saddr, unsigned int len)
{
	u16 head_len;

	if (!daddr)
		daddr = dev->broadcast;

#ifdef CONFIG_HDLC_DEBUG_HARD_HEADER
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



static int pvc_open(struct net_device *dev)
{
	pvc_device *pvc = dev_to_pvc(dev);

	if ((hdlc_to_dev(pvc->master)->flags & IFF_UP) == 0)
		return -EIO;  /* Master must be UP in order to activate PVC */

	if (pvc->master->state.fr.settings.lmi != LMI_NONE)
		pvc->state.active = 0;
	else
		pvc->state.active = 1;

	pvc->state.new = 0;
	pvc->master->state.fr.changed = 1;
	return 0;
}



static int pvc_close(struct net_device *dev)
{
	pvc_device *pvc = dev_to_pvc(dev);
	pvc->state.active = pvc->state.new = 0;
	pvc->master->state.fr.changed = 1;
	return 0;
}



static int pvc_xmit(struct sk_buff *skb, struct net_device *dev)
{
	pvc_device *pvc = dev_to_pvc(dev);

	if (pvc->state.active) {
		skb->dev = hdlc_to_dev(pvc->master);
		pvc->stats.tx_bytes += skb->len;
		pvc->stats.tx_packets++;
		if (pvc->state.fecn)
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



static inline void fr_log_dlci_active(pvc_device *pvc)
{
	printk(KERN_INFO "%s: %sactive%s\n", pvc_to_name(pvc),
	       pvc->state.active ? "" : "in",
	       pvc->state.new ? " new" : "");
}



static inline u8 fr_lmi_nextseq(u8 x)
{
	x++;
	return x ? x : 1;
}



static void fr_lmi_send(hdlc_device *hdlc, int fullrep)
{
	struct sk_buff *skb;
	pvc_device *pvc = hdlc->state.fr.first_pvc;
	int len = (hdlc->state.fr.settings.lmi == LMI_ANSI) ? LMI_ANSI_LENGTH
		: LMI_LENGTH;
	int stat_len = 3;
	u8 *data;
	int i = 0;

	if (hdlc->state.fr.settings.dce && fullrep) {
		len += hdlc->state.fr.pvc_count * (2 + stat_len);
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
	data[i++] = hdlc->state.fr.settings.dce
		? LMI_STATUS : LMI_STATUS_ENQUIRY;
	if (hdlc->state.fr.settings.lmi == LMI_ANSI)
		data[i++] = LMI_ANSI_LOCKSHIFT;
	data[i++] = (hdlc->state.fr.settings.lmi == LMI_CCITT)
		? LMI_CCITT_REPTYPE : LMI_REPTYPE;
	data[i++] = LMI_REPT_LEN;
	data[i++] = fullrep ? LMI_FULLREP : LMI_INTEGRITY;

	data[i++] = (hdlc->state.fr.settings.lmi == LMI_CCITT)
		? LMI_CCITT_ALIVE : LMI_ALIVE;
	data[i++] = LMI_INTEG_LEN;
	data[i++] = hdlc->state.fr.txseq =fr_lmi_nextseq(hdlc->state.fr.txseq);
	data[i++] = hdlc->state.fr.rxseq;

	if (hdlc->state.fr.settings.dce && fullrep) {
		while (pvc) {
			data[i++] = (hdlc->state.fr.settings.lmi == LMI_CCITT)
				? LMI_CCITT_PVCSTAT : LMI_PVCSTAT;
			data[i++] = stat_len;

			if (hdlc->state.fr.reliable &&
			    (pvc->netdev.flags & IFF_UP) &&
			    !pvc->state.active &&
			    !pvc->state.new) {
				pvc->state.new = 1;
				fr_log_dlci_active(pvc);
			}

			dlci_to_status(hdlc, netdev_dlci(&pvc->netdev),
				       data + i,
				       pvc->state.active, pvc->state.new);
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

	if (hdlc->state.fr.settings.dce)
		reliable = (jiffies - hdlc->state.fr.last_poll <
			    hdlc->state.fr.settings.t392 * HZ);
	else {
		hdlc->state.fr.last_errors <<= 1; /* Shift the list */
		if (hdlc->state.fr.request) {
			if (hdlc->state.fr.reliable)
				printk(KERN_INFO "%s: No LMI status reply "
				       "received\n", hdlc_to_name(hdlc));
			hdlc->state.fr.last_errors |= 1;
		}

		list = hdlc->state.fr.last_errors;
		for (i = 0; i < hdlc->state.fr.settings.n393; i++, list >>= 1)
			cnt += (list & 1);	/* errors count */

		reliable = (cnt < hdlc->state.fr.settings.n392);
	}

	if (hdlc->state.fr.reliable != reliable) {
		pvc_device *pvc = hdlc->state.fr.first_pvc;

		hdlc->state.fr.reliable = reliable;
		printk(KERN_INFO "%s: Link %sreliable\n", hdlc_to_name(hdlc),
		       reliable ? "" : "un");

		if (reliable) {
			hdlc->state.fr.n391cnt = 0; /* Request full status */
			hdlc->state.fr.changed = 1;
		} else {
			while (pvc) {	/* Deactivate all PVCs */
				pvc->state.new = pvc->state.active = 0;
				pvc = pvc->next;
			}
		}
	}

	if (hdlc->state.fr.settings.dce)
		hdlc->state.fr.timer.expires = jiffies +
			hdlc->state.fr.settings.t392 * HZ;
	else {
		if (hdlc->state.fr.n391cnt)
			hdlc->state.fr.n391cnt--;

		fr_lmi_send(hdlc, hdlc->state.fr.n391cnt == 0);

		hdlc->state.fr.request = 1;
		hdlc->state.fr.timer.expires = jiffies +
			hdlc->state.fr.settings.t391 * HZ;
	}

	hdlc->state.fr.timer.function = fr_timer;
	hdlc->state.fr.timer.data = arg;
	add_timer(&hdlc->state.fr.timer);
}



static int fr_lmi_recv(hdlc_device *hdlc, struct sk_buff *skb)
{
	int stat_len;
	pvc_device *pvc;
	int reptype = -1, error;
	u8 rxseq, txseq;
	int i;

	if (skb->len < ((hdlc->state.fr.settings.lmi == LMI_ANSI)
			? LMI_ANSI_LENGTH : LMI_LENGTH)) {
		printk(KERN_INFO "%s: Short LMI frame\n", hdlc_to_name(hdlc));
		return 1;
	}

	if (skb->data[5] != (!hdlc->state.fr.settings.dce ?
			     LMI_STATUS : LMI_STATUS_ENQUIRY)) {
		printk(KERN_INFO "%s: LMI msgtype=%x, Not LMI status %s\n",
		       hdlc_to_name(hdlc), skb->data[2],
		       hdlc->state.fr.settings.dce ? "enquiry" : "reply");
		return 1;
	}

	i = (hdlc->state.fr.settings.lmi == LMI_ANSI) ? 7 : 6;

	if (skb->data[i] !=
	    ((hdlc->state.fr.settings.lmi == LMI_CCITT)
	     ? LMI_CCITT_REPTYPE : LMI_REPTYPE)) {
		printk(KERN_INFO "%s: Not a report type=%x\n",
		       hdlc_to_name(hdlc), skb->data[i]);
		return 1;
	}
	i++;

	i++;				/* Skip length field */

	reptype = skb->data[i++];

	if (skb->data[i]!=
	    ((hdlc->state.fr.settings.lmi == LMI_CCITT)
	     ? LMI_CCITT_ALIVE : LMI_ALIVE)) {
		printk(KERN_INFO "%s: Unsupported status element=%x\n",
		       hdlc_to_name(hdlc), skb->data[i]);
		return 1;
	}
	i++;

	i++;			/* Skip length field */

	hdlc->state.fr.rxseq = skb->data[i++]; /* TX sequence from peer */
	rxseq = skb->data[i++];	/* Should confirm our sequence */

	txseq = hdlc->state.fr.txseq;

	if (hdlc->state.fr.settings.dce) {
		if (reptype != LMI_FULLREP && reptype != LMI_INTEGRITY) {
			printk(KERN_INFO "%s: Unsupported report type=%x\n",
			       hdlc_to_name(hdlc), reptype);
			return 1;
		}
	}

	error = 0;
	if (!hdlc->state.fr.reliable)
		error = 1;

	if (rxseq == 0 || rxseq != txseq) {
		hdlc->state.fr.n391cnt = 0; /* Ask for full report next time */
		error = 1;
	}

	if (hdlc->state.fr.settings.dce) {
		if (hdlc->state.fr.fullrep_sent && !error) {
/* Stop sending full report - the last one has been confirmed by DTE */
			hdlc->state.fr.fullrep_sent = 0;
			pvc = hdlc->state.fr.first_pvc;
			while (pvc) {
				if (pvc->state.new) {
					pvc->state.new = 0;
					pvc->state.active = 1;
					fr_log_dlci_active(pvc);

/* Tell DTE that new PVC is now active */
					hdlc->state.fr.changed = 1;
				}
				pvc = pvc->next;
			}
		}

		if (hdlc->state.fr.changed) {
			reptype = LMI_FULLREP;
			hdlc->state.fr.fullrep_sent = 1;
			hdlc->state.fr.changed = 0;
		}

		fr_lmi_send(hdlc, reptype == LMI_FULLREP ? 1 : 0);
		return 0;
	}

	/* DTE */

	if (reptype != LMI_FULLREP || error)
		return 0;

	stat_len = 3;
	pvc = hdlc->state.fr.first_pvc;

	while (pvc) {
		pvc->state.deleted = pvc->state.active; /* mark active PVCs */
		pvc = pvc->next;
	}

	while (skb->len >= i + 2 + stat_len) {
		u16 dlci;
		int active, new;

		if (skb->data[i] != ((hdlc->state.fr.settings.lmi == LMI_CCITT)
				     ? LMI_CCITT_PVCSTAT : LMI_PVCSTAT)) {
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

		dlci = status_to_dlci(hdlc, skb->data + i, &active, &new);
		pvc = find_pvc(hdlc, dlci);

		active |= new;
		if (pvc) {
			if (active && !pvc->state.active &&
			    (pvc->netdev.flags & IFF_UP)) {
				pvc->state.active = active;
				fr_log_dlci_active(pvc);
			}
			pvc->state.deleted = 0;
		}
		else if (new)
			printk(KERN_INFO "%s: new PVC available, DLCI=%u\n",
			       hdlc_to_name(hdlc), dlci);

		i += stat_len;
	}

	pvc = hdlc->state.fr.first_pvc;

	while (pvc) {
		if (pvc->state.deleted) {
			pvc->state.active = pvc->state.new = 0;
			fr_log_dlci_active(pvc);
			pvc->state.deleted = 0;
		}
		pvc = pvc->next;
	}

	/* Next full report after N391 polls */
	hdlc->state.fr.n391cnt = hdlc->state.fr.settings.n391;

	return 0;
}



static void fr_rx(struct sk_buff *skb)
{
	hdlc_device *hdlc = dev_to_hdlc(skb->dev);
	fr_hdr *fh = (fr_hdr*)skb->data;
	u8 *data = skb->data;
	u16 dlci;
	pvc_device *pvc;

	if (skb->len<4 || fh->ea1 || data[2] != FR_UI)
		goto rx_error;

	dlci = q922_to_dlci(skb->data);

	if (dlci == LMI_DLCI) {
		if (hdlc->state.fr.settings.lmi == LMI_NONE)
			goto rx_error; /* LMI packet with no LMI? */

		if (data[3] == LMI_PROTO) {
			if (fr_lmi_recv(hdlc, skb))
				goto rx_error;
			else {
				/* No request pending */
				hdlc->state.fr.request = 0;
				hdlc->state.fr.last_poll = jiffies;
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
#ifdef CONFIG_HDLC_DEBUG_PKT
		printk(KERN_INFO "%s: No PVC for received frame's DLCI %d\n",
		       hdlc_to_name(hdlc), dlci);
#endif
		goto rx_error;
	}

	if ((pvc->netdev.flags & IFF_UP) == 0) {
#ifdef CONFIG_HDLC_DEBUG_PKT
		printk(KERN_INFO "%s: PVC for received frame's DLCI %d is down\n",
		       hdlc_to_name(hdlc), dlci);
#endif
		goto rx_error;
	}

	pvc->stats.rx_packets++; /* PVC traffic */
	pvc->stats.rx_bytes += skb->len;

	if (pvc->state.fecn != (fh->fecn ? PVC_STATE_FECN : 0)) {
#ifdef CONFIG_HDLC_DEBUG_ECN
		printk(KERN_DEBUG "%s: FECN O%s\n", pvc_to_name(pvc),
		       fh->fecn ? "N" : "FF");
#endif
		pvc->state.fecn ^= 1;
	}

	if (pvc->state.becn != (fh->becn ? PVC_STATE_BECN : 0)) {
#ifdef CONFIG_HDLC_DEBUG_ECN
		printk(KERN_DEBUG "%s: BECN O%s\n", pvc_to_name(pvc),
		       fh->becn ? "N" : "FF");
#endif
		pvc->state.becn ^= 1;
	}

	if (pvc->state.becn)
		pvc->stats.rx_compressed++;

	skb->dev = &pvc->netdev;

	if (data[3] == NLPID_IP) {
		skb_pull(skb, 4); /* Remove 4-byte header (hdr, UI, NLPID) */
		skb->protocol = htons(ETH_P_IP);
		netif_rx(skb);
		return;
	}


	if (data[3] == NLPID_IPV6) {
		skb_pull(skb, 4); /* Remove 4-byte header (hdr, UI, NLPID) */
		skb->protocol = htons(ETH_P_IPV6);
		netif_rx(skb);
		return;
	}

	if (data[3] == FR_PAD && data[4] == NLPID_SNAP && data[5] == FR_PAD) {
		u16 oui = ntohs(*(u16*)(data + 6));
		u16 pid = ntohs(*(u16*)(data + 8));
		skb_pull(skb, 10);

		switch ((((u32)oui) << 16) | pid) {
		case ETH_P_ARP: /* routed frame with SNAP */
		case ETH_P_IPX:
		case ETH_P_IP:	/* a long variant */
		case ETH_P_IPV6:
			skb->protocol = htons(pid);
			break;

		default:
			printk(KERN_INFO "%s: Unsupported protocol, OUI=%x "
			       "PID=%x\n", hdlc_to_name(hdlc), oui, pid);
			dev_kfree_skb_any(skb);
			return;
		}

		netif_rx(skb);
		return;
	}

	printk(KERN_INFO "%s: Unsupported protocol, NLPID=%x\n",
	       hdlc_to_name(hdlc), data[3]);
	dev_kfree_skb_any(skb);
	return;

 rx_error:
	hdlc->stats.rx_errors++; /* Mark error */
	dev_kfree_skb_any(skb);
}



static int fr_open(hdlc_device *hdlc)
{
	if (hdlc->state.fr.settings.lmi != LMI_NONE) {
		hdlc->state.fr.last_poll = 0;
		hdlc->state.fr.reliable = 0;
		hdlc->state.fr.changed = 1;
		hdlc->state.fr.request = 0;
		hdlc->state.fr.fullrep_sent = 0;
		hdlc->state.fr.last_errors = 0xFFFFFFFF;
		hdlc->state.fr.n391cnt = 0;
		hdlc->state.fr.txseq = hdlc->state.fr.rxseq = 0;

		init_timer(&hdlc->state.fr.timer);
		/* First poll after 1 s */
		hdlc->state.fr.timer.expires = jiffies + HZ;
		hdlc->state.fr.timer.function = fr_timer;
		hdlc->state.fr.timer.data = (unsigned long)hdlc;
		add_timer(&hdlc->state.fr.timer);
	} else
		hdlc->state.fr.reliable = 1;

	return 0;
}



static void fr_close(hdlc_device *hdlc)
{
	pvc_device *pvc = hdlc->state.fr.first_pvc;

	if (hdlc->state.fr.settings.lmi != LMI_NONE)
		del_timer_sync(&hdlc->state.fr.timer);

	while(pvc) {
		dev_close(&pvc->netdev); /* Shutdown all PVCs for this FRAD */
		pvc = pvc->next;
	}
}



static int fr_pvc(hdlc_device *hdlc, unsigned int dlci, int create)
{
	pvc_device **pvc_p = &hdlc->state.fr.first_pvc;
	pvc_device *pvc;
	int result;

	if(dlci <= 0 || dlci >= 1024)
		return -EINVAL;	/* Only 10 bits for DLCI, DLCI 0 reserved */

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
			printk(KERN_WARNING "%s: Memory squeeze on fr_pvc()\n",
			       hdlc_to_name(hdlc));
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

		hdlc->state.fr.changed = 1;
		hdlc->state.fr.pvc_count++;
		return 0;
	}

	if (*pvc_p == NULL)		/* Delete PVC */
		return -ENOENT;

	pvc = *pvc_p;

	if (pvc->netdev.flags & IFF_UP)
		return -EBUSY;		/* PVC in use */

	hdlc->state.fr.changed = 1;
	hdlc->state.fr.pvc_count--;
	*pvc_p = pvc->next;
	unregister_netdevice(&pvc->netdev);
	kfree(pvc);
	return 0;
}



static void fr_destroy(hdlc_device *hdlc)
{
	pvc_device *pvc = hdlc->state.fr.first_pvc;
	while(pvc) {
		pvc_device *next = pvc->next;
		unregister_netdev(&pvc->netdev);
		kfree(pvc);
		pvc = next;
	}

	hdlc->state.fr.first_pvc = NULL; /* All PVCs destroyed */
	hdlc->state.fr.pvc_count = 0;
	hdlc->state.fr.changed = 1;
}



int hdlc_fr_ioctl(hdlc_device *hdlc, struct ifreq *ifr)
{
	fr_proto *fr_s = ifr->ifr_settings.ifs_ifsu.fr;
	const size_t size = sizeof(fr_proto);
	fr_proto new_settings;
	struct net_device *dev = hdlc_to_dev(hdlc);
	fr_proto_pvc pvc;
	int result;

	switch (ifr->ifr_settings.type) {
	case IF_GET_PROTO:
		ifr->ifr_settings.type = IF_PROTO_FR;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}
		if (copy_to_user(fr_s, &hdlc->state.fr.settings, size))
			return -EFAULT;
		return 0;

	case IF_PROTO_FR:
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;

		if(dev->flags & IFF_UP)
			return -EBUSY;

		if (copy_from_user(&new_settings, fr_s, size))
			return -EFAULT;

		if (new_settings.lmi == LMI_DEFAULT)
			new_settings.lmi = LMI_ANSI;

		if ((new_settings.lmi != LMI_NONE &&
		     new_settings.lmi != LMI_ANSI &&
		     new_settings.lmi != LMI_CCITT) ||
		    new_settings.t391 < 1 ||
		    new_settings.t392 < 2 ||
		    new_settings.n391 < 1 ||
		    new_settings.n392 < 1 ||
		    new_settings.n393 < new_settings.n392 ||
		    new_settings.n393 > 32 ||
		    (new_settings.dce != 0 &&
		     new_settings.dce != 1))
			return -EINVAL;

		result=hdlc->attach(hdlc, ENCODING_NRZ,PARITY_CRC16_PR1_CCITT);
		if (result)
			return result;

		if (hdlc->proto != IF_PROTO_FR) {
			hdlc_proto_detach(hdlc);
			hdlc->state.fr.first_pvc = NULL;
			hdlc->state.fr.pvc_count = 0;
		}
		memcpy(&hdlc->state.fr.settings, &new_settings, size);

		hdlc->open = fr_open;
		hdlc->stop = fr_close;
		hdlc->netif_rx = fr_rx;
		hdlc->proto_detach = fr_destroy;
		hdlc->proto = IF_PROTO_FR;
		dev->hard_start_xmit = hdlc->xmit;
		dev->hard_header = fr_hard_header;
		dev->type = ARPHRD_FRAD;
		dev->addr_len = 2;
		*(u16*)dev->dev_addr = htons(LMI_DLCI);
		dlci_to_q922(dev->broadcast, LMI_DLCI);
		return 0;

	case IF_PROTO_FR_ADD_PVC:
	case IF_PROTO_FR_DEL_PVC:
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (copy_from_user(&pvc, ifr->ifr_settings.ifs_ifsu.fr_pvc,
				   sizeof(fr_proto_pvc)))
			return -EFAULT;

		return fr_pvc(hdlc, pvc.dlci,
			      ifr->ifr_settings.type == IF_PROTO_FR_ADD_PVC);
	}

	return -EINVAL;
}
