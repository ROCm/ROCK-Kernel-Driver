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
 * $Id: hci_usb.h,v 1.3 2001/06/02 01:40:08 maxk Exp $
 */

#ifdef __KERNEL__

/* Class, SubClass, and Protocol codes that describe a Bluetooth device */
#define HCI_DEV_CLASS        0xe0	/* Wireless class */
#define HCI_DEV_SUBCLASS     0x01	/* RF subclass */
#define HCI_DEV_PROTOCOL     0x01	/* Bluetooth programming protocol */

#define HCI_CTRL_REQ	     0x20

struct hci_usb {
	struct usb_device 	*udev;

	devrequest		dev_req;
	struct urb 		*ctrl_urb;
	struct urb		*intr_urb;
	struct urb		*read_urb;
	struct urb		*write_urb;

	__u8			*read_buf;
	__u8			*intr_buf;
	struct sk_buff		*intr_skb;
	int			intr_count;

	__u8			bulk_out_ep_addr;
	__u8			bulk_in_ep_addr;
	__u8			intr_in_ep_addr;
	__u8			intr_in_interval;

	struct hci_dev		hdev;

	unsigned long		tx_state;
	struct sk_buff_head	tx_ctrl_q;
	struct sk_buff_head	tx_write_q;
};

/* Transmit states  */
#define HCI_TX_CTRL	1
#define HCI_TX_WRITE	2

#endif /* __KERNEL__ */
