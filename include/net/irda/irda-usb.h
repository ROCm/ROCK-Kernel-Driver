/*****************************************************************************
 *
 * Filename:      irda-usb.h
 * Version:       0.8
 * Description:   IrDA-USB Driver
 * Status:        Experimental 
 * Author:        Dag Brattli <dag@brattli.net>
 *
 *      Copyright (C) 2001, Dag Brattli <dag@brattli.net>
 *	Copyright (C) 2000, Roman Weissgaerber <weissg@vienna.at>
 *          
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#include <linux/time.h>

#include <net/irda/irda.h>
#include <net/irda/irlap.h>
#include <net/irda/irda_device.h>

#define RX_COPY_THRESHOLD 200
#define IRDA_USB_MAX_MTU 2051
#define IRDA_USB_SPEED_MTU 64		/* Weird, but work like this */

/*
 * Maximum number of URB on the Rx and Tx path, a number larger than 1
 * is required for handling back-to-back (brickwalled) frames 
 */
#define IU_MAX_ACTIVE_RX_URBS 1
#define IU_MAX_RX_URBS	(IU_MAX_ACTIVE_RX_URBS + 1) 
#define IU_MAX_TX_URBS  1

/* Inbound header */
#define MEDIA_BUSY    0x80

#define SPEED_2400    0x01
#define SPEED_9600    0x02
#define SPEED_19200   0x03
#define SPEED_38400   0x04
#define SPEED_57600   0x05
#define SPEED_115200  0x06
#define SPEED_576000  0x07
#define SPEED_1152000 0x08
#define SPEED_4000000 0x09

/* device_info flags in struct usb_device_id */
#define IUC_DEFAULT	0x00	/* Basic device compliant with 1.0 spec */
#define IUC_SPEED_BUG	0x01	/* Device doesn't set speed after the frame */
#define IUC_SIR_ONLY	0x02	/* Device doesn't behave at FIR speeds */
#define IUC_SMALL_PKT	0x04	/* Device doesn't behave with big Rx packets */
#define IUC_NO_WINDOW	0x08	/* Device doesn't behave with big Rx window */
#define IUC_MAX_WINDOW	0x10	/* Device underestimate the Rx window */
#define IUC_MAX_XBOFS	0x20	/* Device need more xbofs than advertised */

#define USB_IRDA_HEADER   0x01
#define USB_CLASS_IRDA    0x02 /* USB_CLASS_APP_SPEC subclass */ 
#define USB_DT_IRDA       0x21

struct irda_class_desc {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 bcdSpecRevision;
	__u8  bmDataSize;
	__u8  bmWindowSize;
	__u8  bmMinTurnaroundTime;
	__u16 wBaudRate;
	__u8  bmAdditionalBOFs;
	__u8  bIrdaRateSniff;
	__u8  bMaxUnicastList;
} __attribute__ ((packed));

struct irda_usb_cb {
	struct irda_class_desc *irda_desc;
	struct usb_device *usbdev;	/* init: probe_irda */
	unsigned int ifnum;		/* Interface number of the USB dev. */
	int netopen;			/* Device is active for network */
	int present;			/* Device is present on the bus */
	__u32 capability;		/* Capability of the hardware */
	__u8  bulk_in_ep, bulk_out_ep;	/* Endpoint assignments */
	__u16 bulk_out_mtu;
	
	wait_queue_head_t wait_q;	/* for timeouts */

	struct urb rx_urb[IU_MAX_RX_URBS];  /* URBs used to receive data frames */
	struct urb *rx_idle_urb;         /* Pointer to idle URB in Rx path */
	struct urb tx_urb;		/* URB used to send data frames */
	struct urb speed_urb;		/* URB used to send speed commands */
	
	struct net_device *netdev;	/* Yes! we are some kind of netdev. */
	struct net_device_stats stats;
	struct irlap_cb   *irlap;	/* The link layer we are binded to */
	struct qos_info qos;		 
	hashbin_t *tx_list;		/* Queued transmit skb's */

        struct timeval stamp;
	struct timeval now;

	spinlock_t lock;		/* For serializing operations */

	__u16 xbofs;			/* Current xbofs setting */
	__s16 new_xbofs;		/* xbofs we need to set */
	__u32 speed;			/* Current speed */
	__s32 new_speed;		/* speed we need to set */
	__u32 flags;			/* Interface flags */
};


