/*
 * Generic HDLC support routines for Linux
 *
 * Copyright (C) 1999-2002 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __HDLC_H
#define __HDLC_H

#define GENERIC_HDLC_VERSION 4	/* For synchronization with sethdlc utility */

#define CLOCK_DEFAULT   0	/* Default setting */
#define CLOCK_EXT	1	/* External TX and RX clock - DTE */
#define CLOCK_INT	2	/* Internal TX and RX clock - DCE */
#define CLOCK_TXINT	3	/* Internal TX and external RX clock */
#define CLOCK_TXFROMRX	4	/* TX clock derived from external RX clock */


#define ENCODING_DEFAULT	0 /* Default setting */
#define ENCODING_NRZ		1
#define ENCODING_NRZI		2
#define ENCODING_FM_MARK	3
#define ENCODING_FM_SPACE	4
#define ENCODING_MANCHESTER	5


#define PARITY_DEFAULT		0 /* Default setting */
#define PARITY_NONE		1 /* No parity */
#define PARITY_CRC16_PR0	2 /* CRC16, initial value 0x0000 */
#define PARITY_CRC16_PR1	3 /* CRC16, initial value 0xFFFF */
#define PARITY_CRC16_PR0_CCITT	4 /* CRC16, initial 0x0000, ITU-T version */
#define PARITY_CRC16_PR1_CCITT	5 /* CRC16, initial 0xFFFF, ITU-T version */
#define PARITY_CRC32_PR0_CCITT	6 /* CRC32, initial value 0x00000000 */
#define PARITY_CRC32_PR1_CCITT	7 /* CRC32, initial value 0xFFFFFFFF */

#define LMI_DEFAULT		0 /* Default setting */
#define LMI_NONE		1 /* No LMI, all PVCs are static */
#define LMI_ANSI		2 /* ANSI Annex D */
#define LMI_CCITT		3 /* ITU-T Annex A */


#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/syncppp.h>
#include <linux/hdlc/ioctl.h>

#define HDLC_MAX_MTU 1500	/* Ethernet 1500 bytes */
#define HDLC_MAX_MRU (HDLC_MAX_MTU + 10) /* max 10 bytes for FR */

#define MAXLEN_LMISTAT  20	/* max size of status enquiry frame */

#define PVC_STATE_NEW       0x01
#define PVC_STATE_ACTIVE    0x02
#define PVC_STATE_FECN	    0x08 /* FECN condition */
#define PVC_STATE_BECN      0x10 /* BECN condition */


#define FR_UI              0x03
#define FR_PAD             0x00

#define NLPID_IP           0xCC
#define NLPID_IPV6         0x8E
#define NLPID_SNAP         0x80
#define NLPID_PAD          0x00
#define NLPID_Q933         0x08


#define LMI_DLCI                   0 /* LMI DLCI */
#define LMI_PROTO               0x08
#define LMI_CALLREF             0x00 /* Call Reference */
#define LMI_ANSI_LOCKSHIFT      0x95 /* ANSI lockshift */
#define LMI_REPTYPE                1 /* report type */
#define LMI_CCITT_REPTYPE       0x51
#define LMI_ALIVE                  3 /* keep alive */
#define LMI_CCITT_ALIVE         0x53
#define LMI_PVCSTAT                7 /* pvc status */
#define LMI_CCITT_PVCSTAT       0x57
#define LMI_FULLREP                0 /* full report  */
#define LMI_INTEGRITY              1 /* link integrity report */
#define LMI_SINGLE                 2 /* single pvc report */
#define LMI_STATUS_ENQUIRY      0x75
#define LMI_STATUS              0x7D /* reply */

#define LMI_REPT_LEN               1 /* report type element length */
#define LMI_INTEG_LEN              2 /* link integrity element length */

#define LMI_LENGTH                13 /* standard LMI frame length */
#define LMI_ANSI_LENGTH           14



typedef struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned ea1  : 1;
	unsigned cr   : 1;
	unsigned dlcih: 6;
  
	unsigned ea2  : 1;
	unsigned de   : 1;
	unsigned becn : 1;
	unsigned fecn : 1;
	unsigned dlcil: 4;
#elif defined (__BIG_ENDIAN_BITFIELD)
	unsigned dlcih: 6;
	unsigned cr   : 1;
	unsigned ea1  : 1;
  
	unsigned dlcil: 4;
	unsigned fecn : 1;
	unsigned becn : 1;
	unsigned de   : 1;
	unsigned ea2  : 1;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
}__attribute__ ((packed)) fr_hdr;



typedef struct {		/* Used in Cisco and PPP mode */
	u8 address;
	u8 control;
	u16 protocol;
}__attribute__ ((packed)) hdlc_header;



typedef struct {
	u32 type;		/* code */
	u32 par1;
	u32 par2;
	u16 rel;		/* reliability */
	u32 time;
}__attribute__ ((packed)) cisco_packet;
#define	CISCO_PACKET_LEN	18
#define	CISCO_BIG_PACKET_LEN	20



typedef struct pvc_device_struct {
	struct net_device netdev; /* PVC net device - must be first */
	struct net_device_stats stats;
	struct hdlc_device_struct *master;
	struct pvc_device_struct *next;

	struct {
		int active;
		int new;
		int deleted;
		int fecn;
		int becn;
	}state;
}pvc_device;



typedef struct hdlc_device_struct {
	/* To be initialized by hardware driver */
	struct net_device netdev; /* master net device - must be first */
	struct net_device_stats stats;

	/* used by HDLC layer to take control over HDLC device from hw driver*/
	int (*attach)(struct hdlc_device_struct *hdlc,
		      unsigned short encoding, unsigned short parity);

	/* hardware driver must handle this instead of dev->hard_start_xmit */
	int (*xmit)(struct sk_buff *skb, struct net_device *dev);


	/* Things below are for HDLC layer internal use only */
	int (*ioctl)(struct net_device *dev, struct ifreq *ifr, int cmd);
	int (*open)(struct hdlc_device_struct *hdlc);
	void (*stop)(struct hdlc_device_struct *hdlc);
	void (*proto_detach)(struct hdlc_device_struct *hdlc);
	void (*netif_rx)(struct sk_buff *skb);
	int proto;		/* IF_PROTO_HDLC/CISCO/FR/etc. */

	union {
		struct {
			fr_proto settings;
			pvc_device *first_pvc;
			int pvc_count;

			struct timer_list timer;
			int last_poll;
			int reliable;
			int changed;
			int request;
			int fullrep_sent;
			u32 last_errors; /* last errors bit list */
			u8 n391cnt;
			u8 txseq; /* TX sequence number */
			u8 rxseq; /* RX sequence number */
		}fr;

		struct {
			cisco_proto settings;

			struct timer_list timer;
			int last_poll;
			int up;
			u32 txseq; /* TX sequence number */
			u32 rxseq; /* RX sequence number */
		}cisco;

		struct {
			raw_hdlc_proto settings;
		}raw_hdlc;

		struct {
			struct ppp_device pppdev;
			struct ppp_device *syncppp_ptr;
			int (*old_change_mtu)(struct net_device *dev,
					      int new_mtu);
		}ppp;
	}state;
}hdlc_device;



int hdlc_raw_ioctl(hdlc_device *hdlc, struct ifreq *ifr);
int hdlc_cisco_ioctl(hdlc_device *hdlc, struct ifreq *ifr);
int hdlc_ppp_ioctl(hdlc_device *hdlc, struct ifreq *ifr);
int hdlc_fr_ioctl(hdlc_device *hdlc, struct ifreq *ifr);
int hdlc_x25_ioctl(hdlc_device *hdlc, struct ifreq *ifr);


/* Exported from hdlc.o */

/* Called by hardware driver when a user requests HDLC service */
int hdlc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);

/* Must be used by hardware driver on module startup/exit */
int register_hdlc_device(hdlc_device *hdlc);
void unregister_hdlc_device(hdlc_device *hdlc);


static __inline__ struct net_device* hdlc_to_dev(hdlc_device *hdlc)
{
	return &hdlc->netdev;
}


static __inline__ hdlc_device* dev_to_hdlc(struct net_device *dev)
{
	return (hdlc_device*)dev;
}


static __inline__ struct net_device* pvc_to_dev(pvc_device *pvc)
{
	return &pvc->netdev;
}


static __inline__ pvc_device* dev_to_pvc(struct net_device *dev)
{
	return (pvc_device*)dev;
}


static __inline__ const char *hdlc_to_name(hdlc_device *hdlc)
{
	return hdlc_to_dev(hdlc)->name;
}


static __inline__ const char *pvc_to_name(pvc_device *pvc)
{
	return pvc_to_dev(pvc)->name;
}


static __inline__ u16 netdev_dlci(struct net_device *dev)
{
	return ntohs(*(u16*)dev->dev_addr);
}



static __inline__ u16 q922_to_dlci(u8 *hdr)
{
	return ((hdr[0] & 0xFC) << 2) | ((hdr[1] & 0xF0) >> 4);
}



static __inline__ void dlci_to_q922(u8 *hdr, u16 dlci)
{
	hdr[0] = (dlci >> 2) & 0xFC;
	hdr[1] = ((dlci << 4) & 0xF0) | 0x01;
}



static __inline__ void debug_frame(const struct sk_buff *skb)
{
	int i;

	for (i=0; i<skb->len; i++) {
		if (i == 100) {
			printk("...\n");
			return;
		}
		printk(" %02X", skb->data[i]);
	}
	printk("\n");
}



/* Must be called by hardware driver when HDLC device is being opened */
static __inline__ int hdlc_open(hdlc_device *hdlc)
{
	if (hdlc->proto == -1)
		return -ENOSYS;		/* no protocol attached */

	if (hdlc->open)
		return hdlc->open(hdlc);
	return 0;
}


/* Must be called by hardware driver when HDLC device is being closed */
static __inline__ void hdlc_close(hdlc_device *hdlc)
{
	if (hdlc->stop)
		hdlc->stop(hdlc);
}


/* May be used by hardware driver to gain control over HDLC device */
static __inline__ void hdlc_proto_detach(hdlc_device *hdlc)
{
	if (hdlc->proto_detach)
		hdlc->proto_detach(hdlc);
	hdlc->proto_detach = NULL;
}


#endif /* __KERNEL */
#endif /* __HDLC_H */
