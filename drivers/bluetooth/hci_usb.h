/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 * $Id: hci_usb.h,v 1.2 2002/03/18 19:10:04 maxk Exp $
 */

#ifdef __KERNEL__

/* Class, SubClass, and Protocol codes that describe a Bluetooth device */
#define HCI_DEV_CLASS        0xe0	/* Wireless class */
#define HCI_DEV_SUBCLASS     0x01	/* RF subclass */
#define HCI_DEV_PROTOCOL     0x01	/* Bluetooth programming protocol */

#define HCI_CTRL_REQ	     0x20

#define HCI_MAX_IFACE_NUM	3 

#define HCI_MAX_BULK_TX     	4
#define HCI_MAX_BULK_RX     	1

struct hci_usb {
	struct hci_dev		hdev;

	unsigned long		state;
	
	struct usb_device 	*udev;
	struct usb_interface    *isoc_iface;
	
	__u8			bulk_out_ep;
	__u8			bulk_in_ep;
	__u8			isoc_out_ep;
	__u8			isoc_in_ep;

	__u8			intr_ep;
	__u8			intr_interval;
	struct urb		*intr_urb;
	struct sk_buff		*intr_skb;

	rwlock_t		completion_lock;
	
	struct sk_buff_head	cmd_q;	     // TX Commands
	struct sk_buff_head	acl_q;	     // TX ACLs
	struct sk_buff_head	pending_q;   // Pending requests
	struct sk_buff_head	completed_q; // Completed requests
};

struct hci_usb_scb {
	struct urb *urb;
	int    intr_len;
};

/* States  */
#define HCI_USB_TX_PROCESS	1
#define HCI_USB_TX_WAKEUP	2
#define HCI_USB_CTRL_TX		3

#endif /* __KERNEL__ */
