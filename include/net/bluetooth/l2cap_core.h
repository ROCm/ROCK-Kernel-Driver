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
 *  $Id: l2cap_core.h,v 1.6 2001/08/03 04:19:49 maxk Exp $
 */

#ifndef __L2CAP_CORE_H
#define __L2CAP_CORE_H

#ifdef __KERNEL__

/* ----- L2CAP interface ----- */
struct l2cap_iff {
	struct list_head list;
	struct hci_dev   *hdev;
	bdaddr_t         *bdaddr;
	__u16            mtu;
	spinlock_t       lock;
	struct list_head conn_list;
};

static inline void l2cap_iff_lock(struct l2cap_iff *iff)
{
	spin_lock(&iff->lock);
}

static inline void l2cap_iff_unlock(struct l2cap_iff *iff)
{
	spin_unlock(&iff->lock);
}

/* ----- L2CAP connections ----- */
struct l2cap_chan_list {
	struct sock	*head;
	rwlock_t	lock;
	long		num;
};

struct l2cap_conn {
	struct l2cap_iff *iff;
	struct list_head list;

	struct hci_conn	 *hconn;

	__u16		state;
	__u8		out;
	bdaddr_t	src;
	bdaddr_t	dst;

	spinlock_t	lock;
	atomic_t	refcnt;

	struct sk_buff *rx_skb;
	__u32		rx_len;
	__u8		rx_ident;
	__u8		tx_ident;

	struct l2cap_chan_list chan_list;

	struct timer_list timer;
};

static inline void __l2cap_conn_link(struct l2cap_iff *iff, struct l2cap_conn *c)
{
	list_add(&c->list, &iff->conn_list);
}

static inline void __l2cap_conn_unlink(struct l2cap_iff *iff, struct l2cap_conn *c)
{
	list_del(&c->list);
}

/* ----- L2CAP channel and socket info ----- */
#define l2cap_pi(sk)   ((struct l2cap_pinfo *) &sk->protinfo)

struct l2cap_accept_q {
	struct sock 	*head;
	struct sock 	*tail;
};

struct l2cap_pinfo {
	bdaddr_t	src;
	bdaddr_t	dst;
	__u16		psm;
	__u16		dcid;
	__u16		scid;
	__u32		flags;

	__u16		imtu;
	__u16		omtu;
	__u16		flush_to;

	__u8		conf_state;
	__u16		conf_mtu;

	__u8		ident;

	struct l2cap_conn 	*conn;
	struct sock 		*next_c;
	struct sock 		*prev_c;

	struct sock *parent;
	struct sock *next_q;
	struct sock *prev_q;

	struct l2cap_accept_q accept_q;
};

#define CONF_REQ_SENT    0x01
#define CONF_INPUT_DONE  0x02
#define CONF_OUTPUT_DONE 0x04

extern struct bluez_sock_list l2cap_sk_list;
extern struct list_head  l2cap_iff_list;
extern rwlock_t l2cap_rt_lock;

extern void l2cap_register_proc(void);
extern void l2cap_unregister_proc(void);

#endif /* __KERNEL__ */

#endif /* __L2CAP_CORE_H */
