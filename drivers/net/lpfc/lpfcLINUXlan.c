/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif

#include <linux/version.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/errno.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include <linux/blk.h>
#else
#include <linux/blkdev.h>
#endif
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/unistd.h>
#include <linux/timex.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,17)
#include <linux/spinlock.h>
#else
#include <asm/spinlock.h>
#endif
#include <linux/smp.h>
#include <asm/byteorder.h>

#include <linux/module.h>
extern int lpfn_probe(void);

/* lpfcLINUXlan.c IP interface network driver */

#include "elx_os.h"
#include "prod_os.h"
#include "elx_util.h"
#include "elx_clock.h"
#include "elx_hw.h"
#include "elx_sli.h"
#include "elx_mem.h"
#include "elx_sched.h"
#include "elx.h"
#include "elx_logmsg.h"
#include "elx_disc.h"
#include "elx_scsi.h"
#include "elx_crtn.h"
#include "elx_cfgparm.h"
#include "lpfc_hba.h"
#include "lpfc_ip.h"
#include "lpfc_crtn.h"
#include "prod_crtn.h"

MODULE_DESCRIPTION("Emulex LightPulse Fibre Channel driver - Open Source");
MODULE_AUTHOR("Emulex Corporation - tech.support@emulex.com");

#ifndef LPFN_DRIVER_VERSION
#define LPFN_DRIVER_VERSION "2.10f"
#endif				/* LPFC_DRIVER_VERSION */

char *lpfn_release_version = LPFN_DRIVER_VERSION;

/* IP / ARP layer */
extern int arp_find(unsigned char *, struct sk_buff *);

/* lpfcdd driver entry points */
extern int lpfc_xmit(elxHBA_t *, struct sk_buff *);
extern int lpfc_ipioctl(int, void *);

void lpfn_receive(elxHBA_t *, void *, uint32_t);
#ifdef MODVERSIONS
EXPORT_SYMBOL(lpfn_receive);
#else
EXPORT_SYMBOL_NOVERS(lpfn_receive);
#endif				/* MODVERSIONS */

int lpfn_probe(void);
#ifdef MODVERSIONS
EXPORT_SYMBOL(lpfn_probe);
#else
EXPORT_SYMBOL_NOVERS(lpfn_probe);
#endif				/* MODVERSIONS */

static int
lpfn_open(NETDEVICE * dev)
{
	elxHBA_t *phba;
	LPFCHBA_t *plhba;

	if ((phba = (elxHBA_t *) (dev->priv)) == 0) {
		return (-ENODEV);
	}
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	plhba->fc_open_count |= FC_LAN_OPEN;

	netdevice_start(dev);
	netif_start_queue(dev);

	return 0;
}

static int
lpfn_close(NETDEVICE * dev)
{
	elxHBA_t *phba;
	LPFCHBA_t *plhba;

	if ((phba = (elxHBA_t *) (dev->priv)) == 0) {
		return (-ENODEV);
	}
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	plhba->fc_open_count &= ~FC_LAN_OPEN;

	netdevice_stop(dev);
	netif_stop_queue(dev);

	return 0;
}

static int
lpfn_change_mtu(NETDEVICE * dev, int new_mtu)
{
	elxHBA_t *phba;
	LPFCHBA_t *plhba;

	if ((phba = (elxHBA_t *) (dev->priv)) == 0) {
		return (-ENODEV);
	}
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	if ((new_mtu < FC_MIN_MTU) || (new_mtu > plhba->lpfn_max_mtu))
		return -EINVAL;
	dev->mtu = new_mtu;
	return (0);
}

/******************************************************************************
* Function name : lpfn_header
*
* Description   : Create the FC MAC/LLC/SNAP header for an arbitrary protocol 
*                 layer
*              saddr=NULL   means use device source address
*              daddr=NULL   means leave destination address (eg unresolved arp)
* 
******************************************************************************/

static int
lpfn_header(struct sk_buff *skb,
	    NETDEVICE * dev,
	    unsigned short type, void *daddr, void *saddr, unsigned len)
{
	LPFC_IPHDR_t *fchdr;
	int rc;

	if (type == ETH_P_IP || type == ETH_P_ARP) {
		fchdr = (LPFC_IPHDR_t *) skb_push(skb, sizeof (LPFC_IPHDR_t));

		fchdr->llc.dsap = FC_LLC_DSAP;	/* DSAP                    */
		fchdr->llc.ssap = FC_LLC_SSAP;	/* SSAP                    */
		fchdr->llc.ctrl = FC_LLC_CTRL;	/* control field           */
		fchdr->llc.prot_id[0] = 0;	/* protocol id             */
		fchdr->llc.prot_id[1] = 0;	/* protocol id             */
		fchdr->llc.prot_id[2] = 0;	/* protocol id             */
		fchdr->llc.type = htons(type);	/* type field              */
		rc = sizeof (LPFC_IPHDR_t);
	} else {
		fchdr = (LPFC_IPHDR_t *) skb_push(skb, sizeof (LPFC_IPHDR_t));
		rc = sizeof (LPFC_IPHDR_t);
	}

	/* Set the source and destination hardware addresses */
	if (saddr != NULL)
		memcpy(fchdr->fcnet.fc_srcname.IEEE, saddr, dev->addr_len);
	else
		memcpy(fchdr->fcnet.fc_srcname.IEEE, dev->dev_addr,
		       dev->addr_len);

	fchdr->fcnet.fc_srcname.nameType = NAME_IEEE;	/* IEEE name */
	fchdr->fcnet.fc_srcname.IEEEextMsn = 0;
	fchdr->fcnet.fc_srcname.IEEEextLsb = 0;

	if (daddr != NULL) {
		memcpy(fchdr->fcnet.fc_destname.IEEE, daddr, dev->addr_len);
		fchdr->fcnet.fc_destname.nameType = NAME_IEEE;	/* IEEE name */
		fchdr->fcnet.fc_destname.IEEEextMsn = 0;
		fchdr->fcnet.fc_destname.IEEEextLsb = 0;
		return (rc);
	}
	return (-rc);
}

/******************************************************************************
* Function name : lpfn_rebuild_header
*
* Description   : Rebuild the FC MAC/LLC/SNAP header. 
*                 This is called after an ARP (or in future other address 
*                 resolution) has completed on this sk_buff.  
*                 We now let ARP fill in the other fields.
******************************************************************************/

static int
lpfn_rebuild_header(struct sk_buff *skb)
{
	LPFC_IPHDR_t *fchdr = (LPFC_IPHDR_t *) skb->data;
	NETDEVICE *dev = skb->dev;

	if (fchdr->llc.type == htons(ETH_P_IP)) {
		return arp_find(fchdr->fcnet.fc_destname.IEEE, skb);
	}

	memcpy(fchdr->fcnet.fc_srcname.IEEE, dev->dev_addr, dev->addr_len);
	fchdr->fcnet.fc_srcname.nameType = NAME_IEEE;	/* IEEE name */
	fchdr->fcnet.fc_srcname.IEEEextMsn = 0;
	fchdr->fcnet.fc_srcname.IEEEextLsb = 0;

	return (0);
}

/******************************************************************************
* Function name : lpfn_xmit
*
* Description   : 
* 
******************************************************************************/

static int
lpfn_xmit(struct sk_buff *skb, NETDEVICE * dev)
{
	elxHBA_t *phba;
	int rc;

	if ((phba = (elxHBA_t *) (dev->priv)) == 0) {
		return (-ENODEV);
	}

	rc = lpfc_xmit(phba, skb);
	return rc;
}

void
lpfn_coalesce_skbuff(elxHBA_t * phba, void *ip_buff, uint32_t pktsize)
{
	LPFCHBA_t *plhba;
	struct sk_buff *new_skb, *tmp_skb;
	uint8_t *new_buff;
	DMABUFIP_t *tmp_ip_buff;
	uint32_t tmp_length;

	plhba = (LPFCHBA_t *) phba->pHbaProto;

	if (pktsize <= plhba->lpfn_rcv_buf_size)
		return;

	/* Allocate big skb */

	new_skb = alloc_skb(pktsize, GFP_ATOMIC);
	if (!new_skb) {
		/* 
		   free all the sk_buffs and return . 
		   This will cause the packet to be dropped 
		 */
		tmp_ip_buff = (DMABUFIP_t *) ip_buff;
		while (tmp_ip_buff) {
			tmp_skb = (struct sk_buff *)tmp_ip_buff->ipbuf;
			tmp_ip_buff->ipbuf = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
			if (in_irq()) {
				dev_kfree_skb_irq(tmp_skb);
			} else {
				dev_kfree_skb(tmp_skb);
			}
#else
			dev_kfree_skb(tmp_skb);
#endif
			tmp_ip_buff = (DMABUFIP_t *) tmp_ip_buff->dma.next;
		}
		return;
	}

	new_buff = new_skb->data;
	tmp_ip_buff = (DMABUFIP_t *) ip_buff;
	tmp_length = 0;
	while (tmp_ip_buff) {
		tmp_skb = (struct sk_buff *)tmp_ip_buff->ipbuf;
		tmp_ip_buff->ipbuf = NULL;
		memcpy(new_buff, tmp_skb->data, tmp_skb->len);
		new_buff += tmp_skb->len;
		tmp_length += tmp_skb->len;
		tmp_ip_buff = (DMABUFIP_t *) tmp_ip_buff->dma.next;

		/* Free the skb here */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
		if (in_irq()) {
			dev_kfree_skb_irq(tmp_skb);
		} else {
			dev_kfree_skb(tmp_skb);
		}
#else
		dev_kfree_skb(tmp_skb);
#endif

	}

	tmp_ip_buff = (DMABUFIP_t *) ip_buff;
	new_skb->len = pktsize;
	tmp_ip_buff->ipbuf = (void *)new_skb;

	return;
}

void
lpfn_receive(elxHBA_t * phba, void *ip_buff, uint32_t pktsize)
{
	LPFCHBA_t *plhba;
	NETDEVICE *dev;
	struct sk_buff *skb;
	LPFC_IPHDR_t *fchdr;
	struct ethhdr *eth;
	unsigned short *sp;

	lpfn_coalesce_skbuff(phba, ip_buff, pktsize);

	skb = (struct sk_buff *)(((DMABUFIP_t *) ip_buff)->ipbuf);

	if (!skb)
		return;

	fchdr = (LPFC_IPHDR_t *) skb->data;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	dev = plhba->lpfn_dev;
	skb->dev = dev;

	skb->mac.raw = fchdr->fcnet.fc_destname.IEEE;
	sp = (unsigned short *)fchdr->fcnet.fc_srcname.IEEE;
	*(sp - 1) = *sp;
	sp++;
	*(sp - 1) = *sp;
	sp++;
	*(sp - 1) = *sp;

	skb_pull(skb, dev->hard_header_len);
	eth = skb->mac.ethernet;

	if (*eth->h_dest & 1) {
		if (memcmp(eth->h_dest, dev->broadcast, ETH_ALEN) == 0)
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
	}

	else if (dev->flags & (IFF_PROMISC)) {
		if (memcmp(eth->h_dest, dev->dev_addr, ETH_ALEN))
			skb->pkt_type = PACKET_OTHERHOST;
	}

	skb->protocol = fchdr->llc.type;

	if (skb->protocol == ntohs(ETH_P_ARP))
		skb->data[1] = 0x06;

	netif_rx(skb);
}

static struct net_device_stats *
lpfn_get_stats(NETDEVICE * dev)
{
	elxHBA_t *phba;
	LINUX_HBA_t *plxhba;
	struct net_device_stats *stats;
	LPFCHBA_t *plhba;

	phba = (elxHBA_t *) dev->priv;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	stats = &plxhba->ndstats;
	if (plhba && plhba->ip_stat) {
		stats->rx_packets = plhba->ip_stat->lpfn_ipackets_lsw;
		stats->tx_packets = plhba->ip_stat->lpfn_opackets_lsw;
		stats->rx_bytes = plhba->ip_stat->lpfn_rcvbytes_lsw;
		stats->tx_bytes = plhba->ip_stat->lpfn_xmtbytes_lsw;
		stats->rx_errors = plhba->ip_stat->lpfn_ierrors;
		stats->tx_errors = plhba->ip_stat->lpfn_oerrors;
		stats->rx_dropped = plhba->ip_stat->lpfn_rx_dropped;
		stats->tx_dropped = plhba->ip_stat->lpfn_tx_dropped;
	}
	return (stats);
}

int
lpfn_init(void)
{
	printk("Emulex LightPulse FC IP %s\n", lpfn_release_version);
	return lpfn_probe();
}

module_init(lpfn_init);

int
lpfn_probe(void)
{
	struct lpfn_probe lp;

	lp.hard_start_xmit = &lpfn_xmit;
	lp.receive = &lpfn_receive;
	lp.get_stats = &lpfn_get_stats;
	lp.open = &lpfn_open;
	lp.stop = &lpfn_close;
	lp.hard_header = &lpfn_header;
	lp.rebuild_header = &lpfn_rebuild_header;
	lp.change_mtu = &lpfn_change_mtu;
	lp.probe = &lpfn_probe;
	if (lpfc_ipioctl(LPFN_PROBE, (void *)&lp) == 0)
		return (-ENODEV);

	return (0);
}

void
lpfn_exit(void)
{
	lpfc_ipioctl(LPFN_DETACH, 0);
}

module_exit(lpfn_exit);
MODULE_LICENSE("GPL");
