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
 * $Id: hci_uart.h,v 1.2 2001/06/02 01:40:08 maxk Exp $
 */

#ifndef N_HCI
#define N_HCI	15
#endif

#ifdef __KERNEL__

#define tty2n_hci(tty)  ((struct n_hci *)((tty)->disc_data))
#define n_hci2tty(n_hci) ((n_hci)->tty)

struct n_hci {
	struct tty_struct *tty;
	struct hci_dev hdev;

	struct sk_buff_head txq;
	unsigned long tx_state;

	spinlock_t rx_lock;
	unsigned long rx_state;
	unsigned long rx_count;
	struct sk_buff *rx_skb;
};

/* Transmit states  */
#define TRANS_SENDING		1
#define TRANS_WAKEUP		2

/* Receiver States */
#define WAIT_PACKET_TYPE	0
#define WAIT_EVENT_HDR	 	1
#define WAIT_ACL_HDR		2
#define WAIT_SCO_HDR		3
#define WAIT_DATA	        4

#endif /* __KERNEL__ */
