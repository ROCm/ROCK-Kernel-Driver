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
 * $Id: hci_uart.h,v 1.1.1.1 2002/03/08 21:03:15 maxk Exp $
 */

#ifndef N_HCI
#define N_HCI	15
#endif

/* Ioctls */
#define HCIUARTSETPROTO	_IOW('U', 200, int)
#define HCIUARTGETPROTO	_IOR('U', 201, int)

/* UART protocols */
#define HCI_UART_MAX_PROTO	3

#define HCI_UART_H4	0
#define HCI_UART_BCSP	1
#define HCI_UART_NCSP	2

#ifdef __KERNEL__
struct n_hci;

struct hci_uart_proto {
	unsigned int id;
	int (*open)(struct n_hci *n_hci);
	int (*recv)(struct n_hci *n_hci, void *data, int len);
	int (*send)(struct n_hci *n_hci, void *data, int len);
	int (*close)(struct n_hci *n_hci);
	int (*flush)(struct n_hci *n_hci);
	struct sk_buff* (*preq)(struct n_hci *n_hci, struct sk_buff *skb);
};

struct n_hci {
	struct tty_struct  *tty;
	struct hci_dev     hdev;
	unsigned long      flags;

	struct hci_uart_proto *proto;
	void               *priv;
	
	struct sk_buff_head txq;
	unsigned long       tx_state;
	spinlock_t          rx_lock;
};

/* N_HCI flag bits */
#define N_HCI_PROTO_SET		0x00

/* TX states  */
#define N_HCI_SENDING		1
#define N_HCI_TX_WAKEUP		2

int hci_uart_register_proto(struct hci_uart_proto *p);
int hci_uart_unregister_proto(struct hci_uart_proto *p);

#endif /* __KERNEL__ */
