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
 *  $Id: bluez.h,v 1.4 2001/08/03 04:19:49 maxk Exp $
 */

#ifndef __IF_BLUEZ_H
#define __IF_BLUEZ_H

#include <net/sock.h>

#define BLUEZ_MAX_PROTO 	2

/* Reserv for core and drivers use */
#define BLUEZ_SKB_RESERVE	8

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

/* Debugging */
#ifdef BLUEZ_DEBUG

#define HCI_CORE_DEBUG		1
#define HCI_SOCK_DEBUG		1
#define HCI_UART_DEBUG		1
#define HCI_USB_DEBUG		1
//#define HCI_DATA_DUMP		1

#define L2CAP_DEBUG			1

#endif /* BLUEZ_DEBUG */

extern void bluez_dump(char *pref, __u8 *buf, int count);

#define INF(fmt, arg...) printk(KERN_INFO fmt "\n" , ## arg)
#define DBG(fmt, arg...) printk(KERN_INFO __FUNCTION__ ": " fmt "\n" , ## arg)
#define ERR(fmt, arg...) printk(KERN_ERR  __FUNCTION__ ": " fmt "\n" , ## arg)

#ifdef HCI_DATA_DUMP
#define DMP(buf, len)    bluez_dump(__FUNCTION__, buf, len)
#else
#define DMP(D...)
#endif

/* ----- Sockets ------ */
struct bluez_sock_list {
	struct sock *head;
	rwlock_t     lock;
};

extern int  bluez_sock_register(int proto, struct net_proto_family *ops);
extern int  bluez_sock_unregister(int proto);

extern void bluez_sock_link(struct bluez_sock_list *l, struct sock *s);
extern void bluez_sock_unlink(struct bluez_sock_list *l, struct sock *s);

/* ----- SKB helpers ----- */
struct bluez_skb_cb {
	int    incomming;
};
#define bluez_cb(skb)	((struct bluez_skb_cb *)(skb->cb)) 

static inline struct sk_buff *bluez_skb_alloc(unsigned int len, int how)
{
	struct sk_buff *skb;

	if ((skb = alloc_skb(len + BLUEZ_SKB_RESERVE, how))) {
		skb_reserve(skb, BLUEZ_SKB_RESERVE);
		bluez_cb(skb)->incomming  = 0;
	}
	return skb;
}

static inline struct sk_buff *bluez_skb_send_alloc(struct sock *sk, unsigned long len, 
						       int nb, int *err)
{
	struct sk_buff *skb;

	if ((skb = sock_alloc_send_skb(sk, len + BLUEZ_SKB_RESERVE, nb, err))) {
		skb_reserve(skb, BLUEZ_SKB_RESERVE);
		bluez_cb(skb)->incomming  = 0;
	}

	return skb;
}

static inline int skb_frags_no(struct sk_buff *skb)
{
	register struct sk_buff *frag = skb_shinfo(skb)->frag_list;
	register int n = 1;

	for (; frag; frag=frag->next, n++);
	return n;
}

extern int hci_core_init(void);
extern int hci_core_cleanup(void);
extern int hci_sock_init(void);
extern int hci_sock_cleanup(void);

#endif /* __IF_BLUEZ_H */
