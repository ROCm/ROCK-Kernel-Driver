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
 * BlueZ L2CAP core and sockets.
 *
 * $Id: l2cap_core.c,v 1.19 2001/08/03 04:19:50 maxk Exp $
 */
#define VERSION "1.1"

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/bluez.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/l2cap_core.h>

#ifndef L2CAP_DEBUG
#undef  DBG
#define DBG( A... )
#endif

struct proto_ops l2cap_sock_ops;

struct bluez_sock_list l2cap_sk_list = {
	lock: RW_LOCK_UNLOCKED
};

struct list_head l2cap_iff_list = LIST_HEAD_INIT(l2cap_iff_list);
rwlock_t l2cap_rt_lock = RW_LOCK_UNLOCKED;

static int  l2cap_conn_del(struct l2cap_conn *conn, int err);

static inline void l2cap_chan_add(struct l2cap_conn *conn, struct sock *sk, struct sock *parent);
static void l2cap_chan_del(struct sock *sk, int err);
static int  l2cap_chan_send(struct sock *sk, struct msghdr *msg, int len);

static void l2cap_sock_close(struct sock *sk);
static void l2cap_sock_kill(struct sock *sk);

static int l2cap_send_req(struct l2cap_conn *conn, __u8 code, __u16 len, void *data);
static int l2cap_send_rsp(struct l2cap_conn *conn, __u8 ident, __u8 code, __u16 len, void *data);

/* -------- L2CAP interfaces & routing --------- */
/* Add/delete L2CAP interface.
 * Must be called with locked rt_lock
 */ 

static void l2cap_iff_add(struct hci_dev *hdev)
{
	struct l2cap_iff *iff;

	DBG("%s", hdev->name);

	DBG("iff_list %p next %p prev %p", &l2cap_iff_list, l2cap_iff_list.next, l2cap_iff_list.prev);

	/* Allocate new interface and lock HCI device */
	if (!(iff = kmalloc(sizeof(struct l2cap_iff), GFP_KERNEL))) {
		ERR("Can't allocate new interface %s", hdev->name);
		return;
	}
	memset(iff, 0, sizeof(struct l2cap_iff));

	hci_dev_hold(hdev);
	hdev->l2cap_data = iff;
	iff->hdev   = hdev;
	iff->mtu    = hdev->acl_mtu - HCI_ACL_HDR_SIZE;
	iff->bdaddr = &hdev->bdaddr;

	spin_lock_init(&iff->lock);
	INIT_LIST_HEAD(&iff->conn_list);

	list_add(&iff->list, &l2cap_iff_list);
}

static void l2cap_iff_del(struct hci_dev *hdev)
{
	struct l2cap_iff *iff;

	if (!(iff = hdev->l2cap_data))
		return;

	DBG("%s iff %p", hdev->name, iff);

	list_del(&iff->list);

	l2cap_iff_lock(iff);

	/* Drop connections */
	while (!list_empty(&iff->conn_list)) {
		struct l2cap_conn *c;

		c = list_entry(iff->conn_list.next, struct l2cap_conn, list);
		l2cap_conn_del(c, ENODEV);
	}

	l2cap_iff_unlock(iff);

	/* Unlock HCI device */
	hdev->l2cap_data = NULL;
	hci_dev_put(hdev);

	kfree(iff);
}

/* Get route. Returns L2CAP interface.
 * Must be called with locked rt_lock
 */
static struct l2cap_iff *l2cap_get_route(bdaddr_t *src, bdaddr_t *dst)
{
	struct list_head *p;
	int use_src;

	DBG("%s -> %s", batostr(src), batostr(dst));

	use_src = bacmp(src, BDADDR_ANY) ? 0 : 1;
	
	/* Simple routing: 
	 * 	No source address - find interface with bdaddr != dst 
	 *	Source address 	  - find interface with bdaddr == src 
	 */

	list_for_each(p, &l2cap_iff_list) {
		struct l2cap_iff *iff;

		iff = list_entry(p, struct l2cap_iff, list);

		if (use_src && !bacmp(iff->bdaddr, src))
			return iff;
		else if (bacmp(iff->bdaddr, dst))
			return iff;
	}
	return NULL;
}

/* ----- L2CAP timers ------ */
static void l2cap_sock_timeout(unsigned long arg)
{
	struct sock *sk = (struct sock *) arg;

	DBG("sock %p state %d", sk, sk->state);

	bh_lock_sock(sk);
	switch (sk->state) {
	case BT_DISCONN:
		l2cap_chan_del(sk, ETIMEDOUT);
		break;

	default:
		sk->err = ETIMEDOUT;
		sk->state_change(sk);
		break;
	};
	bh_unlock_sock(sk);

	l2cap_sock_kill(sk);
	sock_put(sk);
}

static void l2cap_sock_set_timer(struct sock *sk, long timeout)
{
	DBG("sock %p state %d timeout %ld", sk, sk->state, timeout);

	if (!mod_timer(&sk->timer, jiffies + timeout))
		sock_hold(sk);
}

static void l2cap_sock_clear_timer(struct sock *sk)
{
	DBG("sock %p state %d", sk, sk->state);

	if (timer_pending(&sk->timer) && del_timer(&sk->timer))
		__sock_put(sk);
}

static void l2cap_sock_init_timer(struct sock *sk)
{
	init_timer(&sk->timer);
	sk->timer.function = l2cap_sock_timeout;
	sk->timer.data = (unsigned long)sk;
}

static void l2cap_conn_timeout(unsigned long arg)
{
	struct l2cap_conn *conn = (void *)arg;
	
	DBG("conn %p state %d", conn, conn->state);

	if (conn->state == BT_CONNECTED) {
		hci_disconnect(conn->hconn, 0x13);
	}
		
	return;
}

static void l2cap_conn_set_timer(struct l2cap_conn *conn, long timeout)
{
	DBG("conn %p state %d timeout %ld", conn, conn->state, timeout);

	mod_timer(&conn->timer, jiffies + timeout);
}

static void l2cap_conn_clear_timer(struct l2cap_conn *conn)
{
	DBG("conn %p state %d", conn, conn->state);

	del_timer(&conn->timer);
}

static void l2cap_conn_init_timer(struct l2cap_conn *conn)
{
	init_timer(&conn->timer);
	conn->timer.function = l2cap_conn_timeout;
	conn->timer.data = (unsigned long)conn;
}

/* -------- L2CAP connections --------- */
/* Add new connection to the interface.
 * Interface must be locked
 */
static struct l2cap_conn *l2cap_conn_add(struct l2cap_iff *iff, bdaddr_t *dst)
{
	struct l2cap_conn *conn;
	bdaddr_t *src = iff->bdaddr;

	if (!(conn = kmalloc(sizeof(struct l2cap_conn), GFP_KERNEL)))
		return NULL;

	memset(conn, 0, sizeof(struct l2cap_conn));

	conn->state = BT_OPEN;
	conn->iff   = iff;
	bacpy(&conn->src, src);
	bacpy(&conn->dst, dst);

	spin_lock_init(&conn->lock);
	conn->chan_list.lock = RW_LOCK_UNLOCKED;

	l2cap_conn_init_timer(conn);
	
	__l2cap_conn_link(iff, conn);

	DBG("%s -> %s, %p", batostr(src), batostr(dst), conn);

	MOD_INC_USE_COUNT;

	return conn;
}

/* Delete connection on the interface.
 * Interface must be locked
 */
static int l2cap_conn_del(struct l2cap_conn *conn, int err)
{
	struct sock *sk;

	DBG("conn %p, state %d, err %d", conn, conn->state, err);

	l2cap_conn_clear_timer(conn);
	__l2cap_conn_unlink(conn->iff, conn);

	conn->state = BT_CLOSED;

	if (conn->rx_skb)
		kfree_skb(conn->rx_skb);

	/* Kill channels */
	while ((sk = conn->chan_list.head)) {
		bh_lock_sock(sk);
		l2cap_sock_clear_timer(sk);
		l2cap_chan_del(sk, err);
		bh_unlock_sock(sk);

		l2cap_sock_kill(sk);
	}

	kfree(conn);

	MOD_DEC_USE_COUNT;
	return 0;
}

static inline struct l2cap_conn *l2cap_get_conn_by_addr(struct l2cap_iff *iff, bdaddr_t *dst)
{
	struct list_head *p;

	list_for_each(p, &iff->conn_list) {
		struct l2cap_conn *c;

		c = list_entry(p, struct l2cap_conn, list);
		if (!bacmp(&c->dst, dst))
			return c;
	}
	return NULL;
}

int l2cap_connect(struct sock *sk)
{
	bdaddr_t *src = &l2cap_pi(sk)->src;
	bdaddr_t *dst = &l2cap_pi(sk)->dst;
	struct l2cap_conn *conn;
	struct l2cap_iff *iff;
	int err = 0;

	DBG("%s -> %s psm 0x%2.2x", batostr(src), batostr(dst), l2cap_pi(sk)->psm);

	read_lock_bh(&l2cap_rt_lock);

	/* Get route to remote BD address */
	if (!(iff = l2cap_get_route(src, dst))) {
		err = -EHOSTUNREACH;
		goto done;
	}

	/* Update source addr of the socket */
	bacpy(src, iff->bdaddr);

	l2cap_iff_lock(iff);

	if (!(conn = l2cap_get_conn_by_addr(iff, dst))) {
		/* Connection doesn't exist */
		if (!(conn = l2cap_conn_add(iff, dst))) {
			l2cap_iff_unlock(iff);
			err = -ENOMEM;
			goto done;
		}
		conn->out = 1;
	}

	l2cap_iff_unlock(iff);

	l2cap_chan_add(conn, sk, NULL);

	sk->state = BT_CONNECT;
	l2cap_sock_set_timer(sk, sk->sndtimeo);

	switch (conn->state) {
	case BT_CONNECTED:
		if (sk->type == SOCK_SEQPACKET) {
			l2cap_conn_req req;
			req.scid = __cpu_to_le16(l2cap_pi(sk)->scid);
			req.psm  = l2cap_pi(sk)->psm;
			l2cap_send_req(conn, L2CAP_CONN_REQ, L2CAP_CONN_REQ_SIZE, &req);
		} else {
			l2cap_sock_clear_timer(sk);
			sk->state = BT_CONNECTED;
		}
		break;

	case BT_CONNECT:
		break;

	default:
		/* Create ACL connection */
		conn->state = BT_CONNECT;
		hci_connect(iff->hdev, dst);
		break;
	};

done:
	read_unlock_bh(&l2cap_rt_lock);
	return err;
}

/* ------ Channel queues for listening sockets ------ */
void l2cap_accept_queue(struct sock *parent, struct sock *sk)
{
	struct l2cap_accept_q *q = &l2cap_pi(parent)->accept_q;

	DBG("parent %p, sk %p", parent, sk);

	sock_hold(sk);
	l2cap_pi(sk)->parent = parent;
	l2cap_pi(sk)->next_q = NULL;

	if (!q->head) {
		q->head = q->tail = sk;
	} else {
		struct sock *tail = q->tail;

		l2cap_pi(sk)->prev_q = tail;
		l2cap_pi(tail)->next_q = sk;
		q->tail = sk;
	}

	parent->ack_backlog++;
}

void l2cap_accept_unlink(struct sock *sk)
{
	struct sock *parent = l2cap_pi(sk)->parent;
	struct l2cap_accept_q *q = &l2cap_pi(parent)->accept_q;
	struct sock *next, *prev;

	DBG("sk %p", sk);

	next = l2cap_pi(sk)->next_q;
	prev = l2cap_pi(sk)->prev_q;

	if (sk == q->head)
		q->head = next;
	if (sk == q->tail)
		q->tail = prev;

	if (next)
		l2cap_pi(next)->prev_q = prev;
	if (prev)
		l2cap_pi(prev)->next_q = next;

	l2cap_pi(sk)->parent = NULL;

	parent->ack_backlog--;
	__sock_put(sk);
}

/* Get next connected channel in queue. */
struct sock *l2cap_accept_dequeue(struct sock *parent, int state)
{
	struct l2cap_accept_q *q = &l2cap_pi(parent)->accept_q;
	struct sock *sk;

	for (sk = q->head; sk; sk = l2cap_pi(sk)->next_q) {
		if (!state || sk->state == state) {
			l2cap_accept_unlink(sk);
			break;
		}
	}

	DBG("parent %p, sk %p", parent, sk);

	return sk;
}

/* -------- Socket interface ---------- */
static struct sock *__l2cap_get_sock_by_addr(struct sockaddr_l2 *addr)
{
	bdaddr_t *src = &addr->l2_bdaddr;
	__u16 psm = addr->l2_psm;
	struct sock *sk;

	for (sk = l2cap_sk_list.head; sk; sk = sk->next) {
		if (l2cap_pi(sk)->psm == psm &&
		    !bacmp(&l2cap_pi(sk)->src, src))
			break;
	}

	return sk;
}

/* Find socket listening on psm and source bdaddr.
 * Returns closest match.
 */
static struct sock *l2cap_get_sock_listen(bdaddr_t *src, __u16 psm)
{
	struct sock *sk, *sk1 = NULL;

	read_lock(&l2cap_sk_list.lock);

	for (sk = l2cap_sk_list.head; sk; sk = sk->next) {
		struct l2cap_pinfo *pi;

		if (sk->state != BT_LISTEN)
			continue;

		pi = l2cap_pi(sk);

		if (pi->psm == psm) {
			/* Exact match. */
			if (!bacmp(&pi->src, src))
				break;

			/* Closest match */
			if (!bacmp(&pi->src, BDADDR_ANY))
				sk1 = sk;
		}
	}

	read_unlock(&l2cap_sk_list.lock);

	return sk ? sk : sk1;
}

static void l2cap_sock_destruct(struct sock *sk)
{
	DBG("sk %p", sk);

	skb_queue_purge(&sk->receive_queue);
	skb_queue_purge(&sk->write_queue);

	MOD_DEC_USE_COUNT;
}

static void l2cap_sock_cleanup_listen(struct sock *parent)
{
	struct sock *sk;

	DBG("parent %p", parent);

	/* Close not yet accepted channels */
	while ((sk = l2cap_accept_dequeue(parent, 0)))
		l2cap_sock_close(sk);

	parent->state  = BT_CLOSED;
	parent->zapped = 1;
}

/* Kill socket (only if zapped and orphan)
 * Must be called on unlocked socket.
 */
static void l2cap_sock_kill(struct sock *sk)
{
	if (!sk->zapped || sk->socket)
		return;

	DBG("sk %p state %d", sk, sk->state);

	/* Kill poor orphan */
	bluez_sock_unlink(&l2cap_sk_list, sk);
	sk->dead = 1;
	sock_put(sk);
}

/* Close socket.
 * Must be called on unlocked socket.
 */
static void l2cap_sock_close(struct sock *sk)
{
	struct l2cap_conn *conn;

	l2cap_sock_clear_timer(sk);

	lock_sock(sk);

	conn = l2cap_pi(sk)->conn;

	DBG("sk %p state %d conn %p socket %p", sk, sk->state, conn, sk->socket);

	switch (sk->state) {
	case BT_LISTEN:
		l2cap_sock_cleanup_listen(sk);
		break;

	case BT_CONNECTED:
	case BT_CONFIG:
		if (sk->type == SOCK_SEQPACKET) {
			l2cap_disconn_req req;

			sk->state = BT_DISCONN;

			req.dcid = __cpu_to_le16(l2cap_pi(sk)->dcid);
			req.scid = __cpu_to_le16(l2cap_pi(sk)->scid);
			l2cap_send_req(conn, L2CAP_DISCONN_REQ, L2CAP_DISCONN_REQ_SIZE, &req);

			l2cap_sock_set_timer(sk, sk->sndtimeo);
		} else {
			l2cap_chan_del(sk, ECONNRESET);
		}
		break;

	case BT_CONNECT:
	case BT_DISCONN:
		l2cap_chan_del(sk, ECONNRESET);
		break;

	default:
		sk->zapped = 1;
		break;
	};

	release_sock(sk);

	l2cap_sock_kill(sk);
}

static void l2cap_sock_init(struct sock *sk, struct sock *parent)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);

	DBG("sk %p", sk);

	if (parent) {
		sk->type = parent->type;

		pi->imtu = l2cap_pi(parent)->imtu;
		pi->omtu = l2cap_pi(parent)->omtu;
	} else {
		pi->imtu = L2CAP_DEFAULT_MTU;
		pi->omtu = 0;
	}

	/* Default config options */
	pi->conf_mtu = L2CAP_DEFAULT_MTU;
	pi->flush_to = L2CAP_DEFAULT_FLUSH_TO;
}

static struct sock *l2cap_sock_alloc(struct socket *sock, int proto, int prio)
{
	struct sock *sk;

	if (!(sk = sk_alloc(PF_BLUETOOTH, prio, 1)))
		return NULL;

	sock_init_data(sock, sk);

	sk->zapped   = 0;

	sk->destruct = l2cap_sock_destruct;
	sk->sndtimeo = L2CAP_CONN_TIMEOUT;

	sk->protocol = proto;
	sk->state    = BT_OPEN;

	l2cap_sock_init_timer(sk);

	bluez_sock_link(&l2cap_sk_list, sk);

	MOD_INC_USE_COUNT;

	return sk;
}

static int l2cap_sock_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	DBG("sock %p", sock);

	sock->state = SS_UNCONNECTED;

	if (sock->type != SOCK_SEQPACKET && sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sock->ops = &l2cap_sock_ops;

	if (!(sk = l2cap_sock_alloc(sock, protocol, GFP_KERNEL)))
		return -ENOMEM;

	l2cap_sock_init(sk, NULL);

	return 0;
}

static int l2cap_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_l2 *la = (struct sockaddr_l2 *) addr;
	struct sock *sk = sock->sk;
	int err = 0;

	DBG("sk %p, %s %d", sk, batostr(&la->l2_bdaddr), la->l2_psm);

	if (!addr || addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	lock_sock(sk);

	if (sk->state != BT_OPEN) {
		err = -EBADFD;
		goto done;
	}

	write_lock(&l2cap_sk_list.lock);

	if (la->l2_psm && __l2cap_get_sock_by_addr(la)) {
		err = -EADDRINUSE;
		goto unlock;
	}

	/* Save source address */
	bacpy(&l2cap_pi(sk)->src, &la->l2_bdaddr);
	l2cap_pi(sk)->psm = la->l2_psm;
	sk->state = BT_BOUND;

unlock:
	write_unlock(&l2cap_sk_list.lock);

done:
	release_sock(sk);

	return err;
}

static int l2cap_sock_w4_connect(struct sock *sk, int flags)
{
	DECLARE_WAITQUEUE(wait, current);
	long timeo = sock_sndtimeo(sk, flags & O_NONBLOCK);
	int err = 0;

	DBG("sk %p", sk);

	add_wait_queue(sk->sleep, &wait);
	current->state = TASK_INTERRUPTIBLE;

	while (sk->state != BT_CONNECTED) {
		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);

		err = 0;
		if (sk->state == BT_CONNECTED)
			break;

		if (sk->err) {
			err = sock_error(sk);
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(sk->sleep, &wait);

	return err;
}

static int l2cap_sock_connect(struct socket *sock, struct sockaddr *addr, int alen, int flags)
{
	struct sockaddr_l2 *la = (struct sockaddr_l2 *) addr;
	struct sock *sk = sock->sk;
	int err = 0;

	lock_sock(sk);

	DBG("sk %p", sk);

	if (addr->sa_family != AF_BLUETOOTH || alen < sizeof(struct sockaddr_l2)) {
		err = -EINVAL;
		goto done;
	}

	if (sk->state != BT_OPEN && sk->state != BT_BOUND) {
		err = -EBADFD;
		goto done;
	}

	if (sk->type == SOCK_SEQPACKET && !la->l2_psm) {
		err = -EINVAL;
		goto done;
	}

	/* Set destination address and psm */
	bacpy(&l2cap_pi(sk)->dst, &la->l2_bdaddr);
	l2cap_pi(sk)->psm = la->l2_psm;

	if ((err = l2cap_connect(sk)))
		goto done;

	err = l2cap_sock_w4_connect(sk, flags);

done:
	release_sock(sk);
	return err;
}

int l2cap_sock_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int err = 0;

	DBG("sk %p backlog %d", sk, backlog);

	lock_sock(sk);

	if (sk->state != BT_BOUND || sock->type != SOCK_SEQPACKET) {
		err = -EBADFD;
		goto done;
	}

	if (!l2cap_pi(sk)->psm) {
		err = -EINVAL;
		goto done;
	}

	sk->max_ack_backlog = backlog;
	sk->ack_backlog = 0;
	sk->state = BT_LISTEN;

done:
	release_sock(sk);
	return err;
}

int l2cap_sock_accept(struct socket *sock, struct socket *newsock, int flags)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sock *sk = sock->sk, *ch;
	long timeo;
	int err = 0;

	lock_sock(sk);

	if (sk->state != BT_LISTEN) {
		err = -EBADFD;
		goto done;
	}

	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

	DBG("sk %p timeo %ld", sk, timeo);

	/* Wait for an incoming connection. (wake-one). */
	add_wait_queue_exclusive(sk->sleep, &wait);
	current->state = TASK_INTERRUPTIBLE;
	while (!(ch = l2cap_accept_dequeue(sk, BT_CONNECTED))) {
		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);

		if (sk->state != BT_LISTEN) {
			err = -EBADFD;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(sk->sleep, &wait);

	if (err)
		goto done;

	sock_graft(ch, newsock);
	newsock->state = SS_CONNECTED;

	DBG("new socket %p", ch);

done:
	release_sock(sk);

	return err;
}

static int l2cap_sock_getname(struct socket *sock, struct sockaddr *addr, int *len, int peer)
{
	struct sockaddr_l2 *la = (struct sockaddr_l2 *) addr;
	struct sock *sk = sock->sk;

	DBG("sock %p, sk %p", sock, sk);

	addr->sa_family = AF_BLUETOOTH;
	*len = sizeof(struct sockaddr_l2);

	if (peer)
		bacpy(&la->l2_bdaddr, &l2cap_pi(sk)->dst);
	else
		bacpy(&la->l2_bdaddr, &l2cap_pi(sk)->src);

	la->l2_psm = l2cap_pi(sk)->psm;

	return 0;
}

static int l2cap_sock_sendmsg(struct socket *sock, struct msghdr *msg, int len, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	int err = 0;

	DBG("sock %p, sk %p", sock, sk);

	if (sk->err)
		return sock_error(sk);

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	lock_sock(sk);

	if (sk->state == BT_CONNECTED)
		err = l2cap_chan_send(sk, msg, len);
	else
		err = -ENOTCONN;

	release_sock(sk);
	return err;
}

static int l2cap_sock_recvmsg(struct socket *sock, struct msghdr *msg, int len, int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	int noblock = flags & MSG_DONTWAIT;
	int copied, err;
	struct sk_buff *skb;

	DBG("sock %p, sk %p", sock, sk);

	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	if (sk->state == BT_CLOSED)
		return 0;

	if (!(skb = skb_recv_datagram(sk, flags, noblock, &err)))
		return err;

	msg->msg_namelen = 0;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	skb->h.raw = skb->data;
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	skb_free_datagram(sk, skb);

	return err ? : copied;
}

int l2cap_sock_setsockopt(struct socket *sock, int level, int optname, char *optval, int optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_options opts;
	int err = 0;

	DBG("sk %p", sk);

	lock_sock(sk);

	switch (optname) {
	case L2CAP_OPTIONS:
		if (copy_from_user((char *)&opts, optval, optlen)) {
			err = -EFAULT;
			break;
		}
		l2cap_pi(sk)->imtu = opts.imtu;
		l2cap_pi(sk)->omtu = opts.omtu;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	};

	release_sock(sk);
	return err;
}

int l2cap_sock_getsockopt(struct socket *sock, int level, int optname, char *optval, int *optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_options opts;
	struct l2cap_conninfo cinfo;
	int len, err = 0; 

	if (get_user(len, optlen))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	case L2CAP_OPTIONS:
		opts.imtu     = l2cap_pi(sk)->imtu;
		opts.omtu     = l2cap_pi(sk)->omtu;
		opts.flush_to = l2cap_pi(sk)->flush_to;

		len = MIN(len, sizeof(opts));
		if (copy_to_user(optval, (char *)&opts, len))
			err = -EFAULT;

		break;

	case L2CAP_CONNINFO:
		if (sk->state != BT_CONNECTED) {
			err = -ENOTCONN;
			break;
		}

		cinfo.hci_handle = l2cap_pi(sk)->conn->hconn->handle;

		len = MIN(len, sizeof(cinfo));
		if (copy_to_user(optval, (char *)&cinfo, len))
			err = -EFAULT;

		break;

	default:
		err = -ENOPROTOOPT;
		break;
	};

	release_sock(sk);
	return err;
}

static unsigned int l2cap_sock_poll(struct file * file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct l2cap_accept_q *aq;
	unsigned int mask;

	DBG("sock %p, sk %p", sock, sk);

	poll_wait(file, sk->sleep, wait);
	mask = 0;

	if (sk->err || !skb_queue_empty(&sk->error_queue))
		mask |= POLLERR;

	if (sk->shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	aq = &l2cap_pi(sk)->accept_q;
	if (!skb_queue_empty(&sk->receive_queue) || aq->head || (sk->shutdown & RCV_SHUTDOWN))
		mask |= POLLIN | POLLRDNORM;

	if (sk->state == BT_CLOSED)
		mask |= POLLHUP;

	if (sock_writeable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
	else
		set_bit(SOCK_ASYNC_NOSPACE, &sk->socket->flags);

	return mask;
}

static int l2cap_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	DBG("sock %p, sk %p", sock, sk);

	if (!sk)
		return 0;

	sock_orphan(sk);

	l2cap_sock_close(sk);

	return 0;
}

/* --------- L2CAP channels --------- */
static struct sock * __l2cap_get_chan_by_dcid(struct l2cap_chan_list *l, __u16 cid)
{
	struct sock *s;

	for (s = l->head; s; s = l2cap_pi(s)->next_c) {
		if (l2cap_pi(s)->dcid == cid)
			break;
	}

	return s;
}

static inline struct sock *l2cap_get_chan_by_dcid(struct l2cap_chan_list *l, __u16 cid)
{
	struct sock *s;

	read_lock(&l->lock);
	s = __l2cap_get_chan_by_dcid(l, cid);
	read_unlock(&l->lock);

	return s;
}

static struct sock *__l2cap_get_chan_by_scid(struct l2cap_chan_list *l, __u16 cid)
{
	struct sock *s;

	for (s = l->head; s; s = l2cap_pi(s)->next_c) {
		if (l2cap_pi(s)->scid == cid)
			break;
	}

	return s;
}
static inline struct sock *l2cap_get_chan_by_scid(struct l2cap_chan_list *l, __u16 cid)
{
	struct sock *s;

	read_lock(&l->lock);
	s = __l2cap_get_chan_by_scid(l, cid);
	read_unlock(&l->lock);

	return s;
}

static struct sock *__l2cap_get_chan_by_ident(struct l2cap_chan_list *l, __u8 ident)
{
	struct sock *s;

	for (s = l->head; s; s = l2cap_pi(s)->next_c) {
		if (l2cap_pi(s)->ident == ident)
			break;
	}

	return s;
}

static inline struct sock *l2cap_get_chan_by_ident(struct l2cap_chan_list *l, __u8 ident)
{
	struct sock *s;

	read_lock(&l->lock);
	s = __l2cap_get_chan_by_ident(l, ident);
	read_unlock(&l->lock);

	return s;
}

static __u16 l2cap_alloc_cid(struct l2cap_chan_list *l)
{
	__u16 cid = 0x0040;

	for (; cid < 0xffff; cid++) {
		if(!__l2cap_get_chan_by_scid(l, cid))
			return cid;
	}

	return 0;
}

static inline void __l2cap_chan_link(struct l2cap_chan_list *l, struct sock *sk)
{
	sock_hold(sk);

	if (l->head)
		l2cap_pi(l->head)->prev_c = sk;

	l2cap_pi(sk)->next_c = l->head;
	l2cap_pi(sk)->prev_c = NULL;
	l->head = sk;
}

static inline void l2cap_chan_unlink(struct l2cap_chan_list *l, struct sock *sk)
{
	struct sock *next = l2cap_pi(sk)->next_c, *prev = l2cap_pi(sk)->prev_c;

	write_lock(&l->lock);
	if (sk == l->head)
		l->head = next;

	if (next)
		l2cap_pi(next)->prev_c = prev;
	if (prev)
		l2cap_pi(prev)->next_c = next;
	write_unlock(&l->lock);

	__sock_put(sk);
}

static void __l2cap_chan_add(struct l2cap_conn *conn, struct sock *sk, struct sock *parent)
{
	struct l2cap_chan_list *l = &conn->chan_list;

	DBG("conn %p, psm 0x%2.2x, dcid 0x%4.4x", conn, l2cap_pi(sk)->psm, l2cap_pi(sk)->dcid);

	l2cap_conn_clear_timer(conn);

	atomic_inc(&conn->refcnt);
	l2cap_pi(sk)->conn = conn;

	if (sk->type == SOCK_SEQPACKET) {
		/* Alloc CID for normal socket */
		l2cap_pi(sk)->scid = l2cap_alloc_cid(l);
	} else {
		/* Raw socket can send only signalling messages */
		l2cap_pi(sk)->scid = 0x0001;
		l2cap_pi(sk)->dcid = 0x0001;
		l2cap_pi(sk)->omtu = L2CAP_DEFAULT_MTU;
	}

	__l2cap_chan_link(l, sk);

	if (parent)
		l2cap_accept_queue(parent, sk);
}

static inline void l2cap_chan_add(struct l2cap_conn *conn, struct sock *sk, struct sock *parent)
{
	struct l2cap_chan_list *l = &conn->chan_list;

	write_lock(&l->lock);
	__l2cap_chan_add(conn, sk, parent);
	write_unlock(&l->lock);
}

/* Delete channel. 
 * Must be called on the locked socket. */
static void l2cap_chan_del(struct sock *sk, int err)
{
	struct l2cap_conn *conn;
	struct sock *parent;

	conn = l2cap_pi(sk)->conn;
	parent = l2cap_pi(sk)->parent;

	DBG("sk %p, conn %p, err %d", sk, conn, err);

	if (parent) {
		/* Unlink from parent accept queue */
		bh_lock_sock(parent);
		l2cap_accept_unlink(sk);
		bh_unlock_sock(parent);
	}

	if (conn) { 
		long timeout;

		/* Unlink from channel list */
		l2cap_chan_unlink(&conn->chan_list, sk);
		l2cap_pi(sk)->conn = NULL;

		if (conn->out)
			timeout = L2CAP_DISCONN_TIMEOUT;
		else
			timeout = L2CAP_CONN_IDLE_TIMEOUT;
		
		if (atomic_dec_and_test(&conn->refcnt) && conn->state == BT_CONNECTED) {
			/* Schedule Baseband disconnect */
			l2cap_conn_set_timer(conn, timeout);
		}
	}

	sk->state  = BT_CLOSED;
	sk->err    = err;
	sk->state_change(sk);

	sk->zapped = 1;
}

static void l2cap_conn_ready(struct l2cap_conn *conn)
{
	struct l2cap_chan_list *l = &conn->chan_list;
	struct sock *sk;

	DBG("conn %p", conn);

	read_lock(&l->lock);

	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		bh_lock_sock(sk);

		if (sk->type != SOCK_SEQPACKET) {
			sk->state = BT_CONNECTED;
			sk->state_change(sk);
			l2cap_sock_clear_timer(sk);
		} else if (sk->state == BT_CONNECT) {
			l2cap_conn_req req;
			req.scid = __cpu_to_le16(l2cap_pi(sk)->scid);
			req.psm  = l2cap_pi(sk)->psm;
			l2cap_send_req(conn, L2CAP_CONN_REQ, L2CAP_CONN_REQ_SIZE, &req);

			l2cap_sock_set_timer(sk, sk->sndtimeo);
		}

		bh_unlock_sock(sk);
	}

	read_unlock(&l->lock);
}

static void l2cap_chan_ready(struct sock *sk)
{
	struct sock *parent = l2cap_pi(sk)->parent;

	DBG("sk %p, parent %p", sk, parent);

	l2cap_pi(sk)->conf_state = 0;
	l2cap_sock_clear_timer(sk);

	if (!parent) {
		/* Outgoing channel.
		 * Wake up socket sleeping on connect.
		 */
		sk->state = BT_CONNECTED;
		sk->state_change(sk);
	} else {
		/* Incomming channel.
		 * Wake up socket sleeping on accept.
		 */
		parent->data_ready(parent, 1);
	}
}

/* Copy frame to all raw sockets on that connection */
void l2cap_raw_recv(struct l2cap_conn *conn, struct sk_buff *skb)
{
	struct l2cap_chan_list *l = &conn->chan_list;
	struct sk_buff *nskb;
	struct sock * sk;

	DBG("conn %p", conn);

	read_lock(&l->lock);
	for (sk = l->head; sk; sk = l2cap_pi(sk)->next_c) {
		if (sk->type != SOCK_RAW)
			continue;

		/* Don't send frame to the socket it came from */
		if (skb->sk == sk)
			continue;

		if (!(nskb = skb_clone(skb, GFP_ATOMIC)))
			continue;

		skb_queue_tail(&sk->receive_queue, nskb);
		sk->data_ready(sk, nskb->len);
	}
	read_unlock(&l->lock);
}

static int l2cap_chan_send(struct sock *sk, struct msghdr *msg, int len)
{
	struct l2cap_conn *conn = l2cap_pi(sk)->conn;
	struct sk_buff *skb, **frag;
	int err, size, count, sent=0;
	l2cap_hdr *lh;

	/* Check outgoing MTU */
	if (len > l2cap_pi(sk)->omtu)
		return -EINVAL;

	DBG("sk %p len %d", sk, len);

	/* First fragment (with L2CAP header) */
	count = MIN(conn->iff->mtu - L2CAP_HDR_SIZE, len);
	size  = L2CAP_HDR_SIZE + count;
	if (!(skb = bluez_skb_send_alloc(sk, size, msg->msg_flags & MSG_DONTWAIT, &err)))
		return err;

	/* Create L2CAP header */
	lh = (l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->len = __cpu_to_le16(len);
	lh->cid = __cpu_to_le16(l2cap_pi(sk)->dcid);

	if (memcpy_fromiovec(skb_put(skb, count), msg->msg_iov, count)) {
		err = -EFAULT;
		goto fail;
	}

	sent += count;
	len  -= count;

	/* Continuation fragments (no L2CAP header) */
	frag = &skb_shinfo(skb)->frag_list;
	while (len) {
		count = MIN(conn->iff->mtu, len);

		*frag = bluez_skb_send_alloc(sk, count, msg->msg_flags & MSG_DONTWAIT, &err);
		if (!*frag)
			goto fail;
		
		if (memcpy_fromiovec(skb_put(*frag, count), msg->msg_iov, count)) {
			err = -EFAULT;
			goto fail;
		}

		sent += count;
		len  -= count;

		frag = &(*frag)->next;
	}

	if ((err = hci_send_acl(conn->hconn, skb, 0)) < 0)
		goto fail;

	return sent;

fail:
	kfree_skb(skb);
	return err;
}

/* --------- L2CAP signalling commands --------- */
static inline __u8 l2cap_get_ident(struct l2cap_conn *conn)
{
	__u8 id;

	/* Get next available identificator.
	 *    1 - 199 are used by kernel.
	 *  200 - 254 are used by utilities like l2ping, etc 
	 */

	spin_lock(&conn->lock);

	if (++conn->tx_ident > 199)
		conn->tx_ident = 1;

	id = conn->tx_ident;

	spin_unlock(&conn->lock);

	return id;
}

static inline struct sk_buff *l2cap_build_cmd(__u8 code, __u8 ident, __u16 len, void *data)
{
	struct sk_buff *skb;
	l2cap_cmd_hdr *cmd;
	l2cap_hdr *lh;
	int size;

	DBG("code 0x%2.2x, ident 0x%2.2x, len %d", code, ident, len);

	size = L2CAP_HDR_SIZE + L2CAP_CMD_HDR_SIZE + len;
	if (!(skb = bluez_skb_alloc(size, GFP_ATOMIC)))
		return NULL;

	lh = (l2cap_hdr *) skb_put(skb, L2CAP_HDR_SIZE);
	lh->len = __cpu_to_le16(L2CAP_CMD_HDR_SIZE + len);
	lh->cid = __cpu_to_le16(0x0001);

	cmd = (l2cap_cmd_hdr *) skb_put(skb, L2CAP_CMD_HDR_SIZE);
	cmd->code  = code;
	cmd->ident = ident;
	cmd->len   = __cpu_to_le16(len);

	if (len)
		memcpy(skb_put(skb, len), data, len);

	return skb;
}

static int l2cap_send_req(struct l2cap_conn *conn, __u8 code, __u16 len, void *data)
{
	struct sk_buff *skb;
	__u8 ident;

	DBG("code 0x%2.2x", code);

	ident = l2cap_get_ident(conn);
	if (!(skb = l2cap_build_cmd(code, ident, len, data)))
		return -ENOMEM;
	return hci_send_acl(conn->hconn, skb, 0);
}

static int l2cap_send_rsp(struct l2cap_conn *conn, __u8 ident, __u8 code, __u16 len, void *data)
{
	struct sk_buff *skb;

	DBG("code 0x%2.2x", code);

	if (!(skb = l2cap_build_cmd(code, ident, len, data)))
		return -ENOMEM;
	return hci_send_acl(conn->hconn, skb, 0);
}

static inline int l2cap_get_conf_opt(__u8 **ptr, __u8 *type, __u32 *val)
{
	l2cap_conf_opt *opt = (l2cap_conf_opt *) (*ptr);
	int len;

	*type = opt->type;
	switch (opt->len) {
	case 1:
		*val = *((__u8 *) opt->val);
		break;

	case 2:
		*val = __le16_to_cpu(*((__u16 *)opt->val));
		break;

	case 4:
		*val = __le32_to_cpu(*((__u32 *)opt->val));
		break;

	default:
		*val = 0L;
		break;
	};

	DBG("type 0x%2.2x len %d val 0x%8.8x", *type, opt->len, *val);

	len = L2CAP_CONF_OPT_SIZE + opt->len;

	*ptr += len;

	return len;
}

static inline void l2cap_parse_conf_req(struct sock *sk, char *data, int len)
{
	__u8 type, hint; __u32 val;
	__u8 *ptr = data;

	DBG("sk %p len %d", sk, len);

	while (len >= L2CAP_CONF_OPT_SIZE) {
		len -= l2cap_get_conf_opt(&ptr, &type, &val);

		hint  = type & 0x80;
		type &= 0x7f;

		switch (type) {
		case L2CAP_CONF_MTU:
			l2cap_pi(sk)->conf_mtu = val;
			break;

		case L2CAP_CONF_FLUSH_TO:
			l2cap_pi(sk)->flush_to = val;
			break;

		case L2CAP_CONF_QOS:
			break;
		
		default:
			if (hint)
				break;

			/* FIXME: Reject unknon option */
			break;
		};
	}
}

static inline void l2cap_add_conf_opt(__u8 **ptr, __u8 type, __u8 len, __u32 val)
{
	register l2cap_conf_opt *opt = (l2cap_conf_opt *) (*ptr);

	DBG("type 0x%2.2x len %d val 0x%8.8x", type, len, val);

	opt->type = type;
	opt->len  = len;
	switch (len) {
	case 1:
		*((__u8 *) opt->val)  = val;
		break;

	case 2:
		*((__u16 *) opt->val) = __cpu_to_le16(val);
		break;

	case 4:
		*((__u32 *) opt->val) = __cpu_to_le32(val);
		break;
	};

	*ptr += L2CAP_CONF_OPT_SIZE + len;
}

static int l2cap_build_conf_req(struct sock *sk, __u8 *data)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	l2cap_conf_req *req = (l2cap_conf_req *) data;
	__u8 *ptr = req->data;

	DBG("sk %p", sk);

	if (pi->imtu != L2CAP_DEFAULT_MTU)
		l2cap_add_conf_opt(&ptr, L2CAP_CONF_MTU, 2, pi->imtu);

	/* FIXME. Need actual value of the flush timeout */
	//if (flush_to != L2CAP_DEFAULT_FLUSH_TO)
	//   l2cap_add_conf_opt(&ptr, L2CAP_CONF_FLUSH_TO, 2, pi->flush_to);

	req->dcid  = __cpu_to_le16(pi->dcid);
	req->flags = __cpu_to_le16(0);

	return ptr - data;
}

static int l2cap_conf_output(struct sock *sk, __u8 **ptr)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	int result = 0;

	/* Configure output options and let other side know
	 * which ones we don't like.
	 */
	if (pi->conf_mtu < pi->omtu) {
		l2cap_add_conf_opt(ptr, L2CAP_CONF_MTU, 2, l2cap_pi(sk)->omtu);
		result = L2CAP_CONF_UNACCEPT;
	} else {
		pi->omtu = pi->conf_mtu;
	}

	DBG("sk %p result %d", sk, result);
	return result;
}

static int l2cap_build_conf_rsp(struct sock *sk, __u8 *data, int *result)
{
	l2cap_conf_rsp *rsp = (l2cap_conf_rsp *) data;
	__u8 *ptr = rsp->data;

	DBG("sk %p complete %d", sk, result ? 1 : 0);

	if (result)
		*result = l2cap_conf_output(sk, &ptr);

	rsp->scid   = __cpu_to_le16(l2cap_pi(sk)->dcid);
	rsp->result = __cpu_to_le16(result ? *result : 0);
	rsp->flags  = __cpu_to_le16(0);

	return ptr - data;
}

static inline int l2cap_connect_req(struct l2cap_conn *conn, l2cap_cmd_hdr *cmd, __u8 *data)
{
	struct l2cap_chan_list *list = &conn->chan_list;
	l2cap_conn_req *req = (l2cap_conn_req *) data;
	l2cap_conn_rsp rsp;
	struct sock *sk, *parent;

	__u16 scid = __le16_to_cpu(req->scid);
	__u16 psm  = req->psm;

	DBG("psm 0x%2.2x scid 0x%4.4x", psm, scid);

	/* Check if we have socket listening on psm */
	if (!(parent = l2cap_get_sock_listen(&conn->src, psm)))
		goto reject;

	bh_lock_sock(parent);
	write_lock(&list->lock);

	/* Check if we already have channel with that dcid */
	if (__l2cap_get_chan_by_dcid(list, scid))
		goto unlock;

	/* Check for backlog size */
	if (parent->ack_backlog > parent->max_ack_backlog)
		goto unlock;

	if (!(sk = l2cap_sock_alloc(NULL, BTPROTO_L2CAP, GFP_ATOMIC)))
		goto unlock;

	l2cap_sock_init(sk, parent);

	bacpy(&l2cap_pi(sk)->src, &conn->src);
	bacpy(&l2cap_pi(sk)->dst, &conn->dst);
	l2cap_pi(sk)->psm  = psm;
	l2cap_pi(sk)->dcid = scid;

	__l2cap_chan_add(conn, sk, parent);
	sk->state = BT_CONFIG;

	write_unlock(&list->lock);
	bh_unlock_sock(parent);

	rsp.dcid   = __cpu_to_le16(l2cap_pi(sk)->scid);
	rsp.scid   = __cpu_to_le16(l2cap_pi(sk)->dcid);
	rsp.result = __cpu_to_le16(0);
	rsp.status = __cpu_to_le16(0);
	l2cap_send_rsp(conn, cmd->ident, L2CAP_CONN_RSP, L2CAP_CONN_RSP_SIZE, &rsp);

	return 0;

unlock:
	write_unlock(&list->lock);
	bh_unlock_sock(parent);

reject:
	rsp.scid   = __cpu_to_le16(scid);
	rsp.dcid   = __cpu_to_le16(0);
	rsp.status = __cpu_to_le16(0);
	rsp.result = __cpu_to_le16(L2CAP_CONN_NO_MEM);
	l2cap_send_rsp(conn, cmd->ident, L2CAP_CONN_RSP, L2CAP_CONN_RSP_SIZE, &rsp);

	return 0;
}

static inline int l2cap_connect_rsp(struct l2cap_conn *conn, l2cap_cmd_hdr *cmd, __u8 *data)
{
	l2cap_conn_rsp *rsp = (l2cap_conn_rsp *) data;
	__u16 scid, dcid, result, status;
	struct sock *sk;

	scid   = __le16_to_cpu(rsp->scid);
	dcid   = __le16_to_cpu(rsp->dcid);
	result = __le16_to_cpu(rsp->result);
	status = __le16_to_cpu(rsp->status);

	DBG("dcid 0x%4.4x scid 0x%4.4x result 0x%2.2x status 0x%2.2x", dcid, scid, result, status);

	if (!(sk = l2cap_get_chan_by_scid(&conn->chan_list, scid)))
		return -ENOENT;

	bh_lock_sock(sk);

	if (!result) {
		char req[64];

		sk->state = BT_CONFIG;
		l2cap_pi(sk)->dcid = dcid;
		l2cap_pi(sk)->conf_state |= CONF_REQ_SENT;

		l2cap_send_req(conn, L2CAP_CONF_REQ, l2cap_build_conf_req(sk, req), req);
	} else {
		l2cap_chan_del(sk, ECONNREFUSED);
	}

	bh_unlock_sock(sk);
	return 0;
}

static inline int l2cap_config_req(struct l2cap_conn *conn, l2cap_cmd_hdr *cmd, __u8 *data)
{
	l2cap_conf_req * req = (l2cap_conf_req *) data;
	__u16 dcid, flags;
	__u8 rsp[64];
	struct sock *sk;
	int result;

	dcid  = __le16_to_cpu(req->dcid);
	flags = __le16_to_cpu(req->flags);

	DBG("dcid 0x%4.4x flags 0x%2.2x", dcid, flags);

	if (!(sk = l2cap_get_chan_by_scid(&conn->chan_list, dcid)))
		return -ENOENT;

	bh_lock_sock(sk);

	l2cap_parse_conf_req(sk, req->data, cmd->len - L2CAP_CONF_REQ_SIZE);

	if (flags & 0x01) {
		/* Incomplete config. Send empty response. */
		l2cap_send_rsp(conn, cmd->ident, L2CAP_CONF_RSP, l2cap_build_conf_rsp(sk, rsp, NULL), rsp);
		goto unlock;
	}

	/* Complete config. */
	l2cap_send_rsp(conn, cmd->ident, L2CAP_CONF_RSP, l2cap_build_conf_rsp(sk, rsp, &result), rsp);

	if (result)
		goto unlock;

	/* Output config done */
	l2cap_pi(sk)->conf_state |= CONF_OUTPUT_DONE;

	if (l2cap_pi(sk)->conf_state & CONF_INPUT_DONE) {
		sk->state = BT_CONNECTED;
		l2cap_chan_ready(sk);
	} else if (!(l2cap_pi(sk)->conf_state & CONF_REQ_SENT)) {
		char req[64];
		l2cap_send_req(conn, L2CAP_CONF_REQ, l2cap_build_conf_req(sk, req), req);
	}

unlock:
	bh_unlock_sock(sk);

	return 0;
}

static inline int l2cap_config_rsp(struct l2cap_conn *conn, l2cap_cmd_hdr *cmd, __u8 *data)
{
	l2cap_conf_rsp *rsp = (l2cap_conf_rsp *)data;
	__u16 scid, flags, result;
	struct sock *sk;
	int err = 0;

	scid   = __le16_to_cpu(rsp->scid);
	flags  = __le16_to_cpu(rsp->flags);
	result = __le16_to_cpu(rsp->result);

	DBG("scid 0x%4.4x flags 0x%2.2x result 0x%2.2x", scid, flags, result);

	if (!(sk = l2cap_get_chan_by_scid(&conn->chan_list, scid)))
		return -ENOENT;

	bh_lock_sock(sk);

	if (result) {
		l2cap_disconn_req req;

		/* They didn't like our options. Well... we do not negotiate.
		 * Close channel.
		 */
		sk->state = BT_DISCONN;

		req.dcid = __cpu_to_le16(l2cap_pi(sk)->dcid);
		req.scid = __cpu_to_le16(l2cap_pi(sk)->scid);
		l2cap_send_req(conn, L2CAP_DISCONN_REQ, L2CAP_DISCONN_REQ_SIZE, &req);

		l2cap_sock_set_timer(sk, sk->sndtimeo);
		goto done;
	}

	if (flags & 0x01)
		goto done;

	/* Input config done */
	l2cap_pi(sk)->conf_state |= CONF_INPUT_DONE;

	if (l2cap_pi(sk)->conf_state & CONF_OUTPUT_DONE) {
		sk->state = BT_CONNECTED;
		l2cap_chan_ready(sk);
	}

done:
	bh_unlock_sock(sk);

	return err;
}

static inline int l2cap_disconnect_req(struct l2cap_conn *conn, l2cap_cmd_hdr *cmd, __u8 *data)
{
	l2cap_disconn_req *req = (l2cap_disconn_req *) data;
	l2cap_disconn_rsp rsp;
	__u16 dcid, scid;
	struct sock *sk;

	scid = __le16_to_cpu(req->scid);
	dcid = __le16_to_cpu(req->dcid);

	DBG("scid 0x%4.4x dcid 0x%4.4x", scid, dcid);

	if (!(sk = l2cap_get_chan_by_scid(&conn->chan_list, dcid)))
		return 0;

	bh_lock_sock(sk);

	rsp.dcid = __cpu_to_le16(l2cap_pi(sk)->scid);
	rsp.scid = __cpu_to_le16(l2cap_pi(sk)->dcid);
	l2cap_send_rsp(conn, cmd->ident, L2CAP_DISCONN_RSP, L2CAP_DISCONN_RSP_SIZE, &rsp);

	l2cap_chan_del(sk, ECONNRESET);

	bh_unlock_sock(sk);

	l2cap_sock_kill(sk);

	return 0;
}

static inline int l2cap_disconnect_rsp(struct l2cap_conn *conn, l2cap_cmd_hdr *cmd, __u8 *data)
{
	l2cap_disconn_rsp *rsp = (l2cap_disconn_rsp *) data;
	__u16 dcid, scid;
	struct sock *sk;

	scid = __le16_to_cpu(rsp->scid);
	dcid = __le16_to_cpu(rsp->dcid);

	DBG("dcid 0x%4.4x scid 0x%4.4x", dcid, scid);

	if (!(sk = l2cap_get_chan_by_scid(&conn->chan_list, scid)))
		return -ENOENT;

	bh_lock_sock(sk);
	l2cap_sock_clear_timer(sk);
	l2cap_chan_del(sk, ECONNABORTED);
	bh_unlock_sock(sk);

	l2cap_sock_kill(sk);

	return 0;
}

static inline void l2cap_sig_channel(struct l2cap_conn *conn, struct sk_buff *skb)
{
	__u8 *data = skb->data;
	int len = skb->len;
	l2cap_cmd_hdr cmd;
	int err = 0;

	while (len >= L2CAP_CMD_HDR_SIZE) {
		memcpy(&cmd, data, L2CAP_CMD_HDR_SIZE);
		data += L2CAP_CMD_HDR_SIZE;
		len  -= L2CAP_CMD_HDR_SIZE;

		cmd.len = __le16_to_cpu(cmd.len);

		DBG("code 0x%2.2x len %d id 0x%2.2x", cmd.code, cmd.len, cmd.ident);

		if (cmd.len > len || !cmd.ident) {
			DBG("corrupted command");
			break;
		}

		switch (cmd.code) {
		case L2CAP_CONN_REQ:
			err = l2cap_connect_req(conn, &cmd, data);
			break;

		case L2CAP_CONN_RSP:
			err = l2cap_connect_rsp(conn, &cmd, data);
			break;

		case L2CAP_CONF_REQ:
			err = l2cap_config_req(conn, &cmd, data);
			break;

		case L2CAP_CONF_RSP:
			err = l2cap_config_rsp(conn, &cmd, data);
			break;

		case L2CAP_DISCONN_REQ:
			err = l2cap_disconnect_req(conn, &cmd, data);
			break;

		case L2CAP_DISCONN_RSP:
			err = l2cap_disconnect_rsp(conn, &cmd, data);
			break;

		case L2CAP_COMMAND_REJ:
			/* FIXME: We should process this */
			l2cap_raw_recv(conn, skb);
			break;

		case L2CAP_ECHO_REQ:
			l2cap_send_rsp(conn, cmd.ident, L2CAP_ECHO_RSP, cmd.len, data);
			break;

		case L2CAP_ECHO_RSP:
		case L2CAP_INFO_REQ:
		case L2CAP_INFO_RSP:
			l2cap_raw_recv(conn, skb);
			break;

		default:
			ERR("Uknown signaling command 0x%2.2x", cmd.code);
			err = -EINVAL;
			break;
		};

		if (err) {
			l2cap_cmd_rej rej;
			DBG("error %d", err);

			/* FIXME: Map err to a valid reason. */
			rej.reason = __cpu_to_le16(0);
			l2cap_send_rsp(conn, cmd.ident, L2CAP_COMMAND_REJ, L2CAP_CMD_REJ_SIZE, &rej);
		}

		data += cmd.len;
		len  -= cmd.len;
	}

	kfree_skb(skb);
}

static inline int l2cap_data_channel(struct l2cap_conn *conn, __u16 cid, struct sk_buff *skb)
{
	struct sock *sk;

	if (!(sk = l2cap_get_chan_by_scid(&conn->chan_list, cid))) {
		DBG("unknown cid 0x%4.4x", cid);
		goto drop;
	}

	DBG("sk %p, len %d", sk, skb->len);

	if (sk->state != BT_CONNECTED)
		goto drop;

	if (l2cap_pi(sk)->imtu < skb->len)
		goto drop;

	skb_queue_tail(&sk->receive_queue, skb);
	sk->data_ready(sk, skb->len);

	return 0;

drop:
	kfree_skb(skb);

	return 0;
}

static void l2cap_recv_frame(struct l2cap_conn *conn, struct sk_buff *skb)
{
	l2cap_hdr *lh = (l2cap_hdr *) skb->data;
	__u16 cid, len;

	skb_pull(skb, L2CAP_HDR_SIZE);
	cid = __le16_to_cpu(lh->cid);
	len = __le16_to_cpu(lh->len);

	DBG("len %d, cid 0x%4.4x", len, cid);

	if (cid == 0x0001)
		l2cap_sig_channel(conn, skb);
	else	
		l2cap_data_channel(conn, cid, skb);
}

/* ------------ L2CAP interface with lower layer (HCI) ------------- */
static int l2cap_dev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct hci_dev *hdev = (struct hci_dev *) ptr;

	DBG("hdev %s, event %ld", hdev->name, event);

	write_lock(&l2cap_rt_lock);

	switch (event) {
	case HCI_DEV_UP:
		l2cap_iff_add(hdev);
		break;

	case HCI_DEV_DOWN:
		l2cap_iff_del(hdev);
		break;
	};

	write_unlock(&l2cap_rt_lock);

	return NOTIFY_DONE;
}

int l2cap_connect_ind(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct l2cap_iff *iff;

	DBG("hdev %s, bdaddr %s", hdev->name, batostr(bdaddr));

	if (!(iff = hdev->l2cap_data)) {
		ERR("unknown interface");
		return 0;
	}

	/* Always accept connection */
	return 1;
}

int l2cap_connect_cfm(struct hci_dev *hdev, bdaddr_t *bdaddr, __u8 status, struct hci_conn *hconn)
{
	struct l2cap_conn *conn;
	struct l2cap_iff *iff;
	int err = 0;

	DBG("hdev %s bdaddr %s hconn %p", hdev->name, batostr(bdaddr), hconn);

	if (!(iff = hdev->l2cap_data)) {
		ERR("unknown interface");
		return 0;
	}

	l2cap_iff_lock(iff);

	conn = l2cap_get_conn_by_addr(iff, bdaddr);

	if (conn) {
		/* Outgoing connection */
		DBG("Outgoing connection: %s -> %s, %p, %2.2x", batostr(iff->bdaddr), batostr(bdaddr), conn, status);

		if (!status && hconn) {
			conn->state = BT_CONNECTED;
			conn->hconn = hconn;

			hconn->l2cap_data = (void *)conn;

			/* Establish channels */
			l2cap_conn_ready(conn);
		} else {
			l2cap_conn_del(conn, bterr(status));
		}
	} else {
		/* Incomming connection */
		DBG("Incomming connection: %s -> %s, %2.2x", batostr(iff->bdaddr), batostr(bdaddr), status);
	
		if (status || !hconn)
			goto done;

		if (!(conn = l2cap_conn_add(iff, bdaddr))) {
			err = -ENOMEM;
			goto done;
		}

		conn->hconn = hconn;
		hconn->l2cap_data = (void *)conn;

		conn->state = BT_CONNECTED;
	}

done:
	l2cap_iff_unlock(iff);

	return err;
}

int l2cap_disconn_ind(struct hci_conn *hconn, __u8 reason)
{
	struct l2cap_conn *conn = hconn->l2cap_data;

	DBG("hconn %p reason %d", hconn, reason);

	if (!conn) {
		ERR("unknown connection");
		return 0;
	}
	conn->hconn = NULL;

	l2cap_iff_lock(conn->iff);
	l2cap_conn_del(conn, bterr(reason));
	l2cap_iff_unlock(conn->iff);

	return 0;
}

int l2cap_recv_acldata(struct hci_conn *hconn, struct sk_buff *skb, __u16 flags)
{
	struct l2cap_conn *conn = hconn->l2cap_data;

	if (!conn) {
		ERR("unknown connection %p", hconn);
		goto drop;
	}

	DBG("conn %p len %d flags 0x%x", conn, skb->len, flags);

	if (flags & ACL_START) {
		int flen, tlen, size;
		l2cap_hdr *lh;

		if (conn->rx_len) {
			ERR("Unexpected start frame (len %d)", skb->len);
			kfree_skb(conn->rx_skb); conn->rx_skb = NULL;
			conn->rx_len = 0;
		}

		if (skb->len < L2CAP_HDR_SIZE) {
			ERR("Frame is too small (len %d)", skb->len);
			goto drop;
		}

		lh = (l2cap_hdr *)skb->data;
		tlen = __le16_to_cpu(lh->len);
		flen = skb->len - L2CAP_HDR_SIZE;

		DBG("Start: total len %d, frag len %d", tlen, flen);

		if (flen == tlen) {
			/* Complete frame received */
			l2cap_recv_frame(conn, skb);
			return 0;
		}

		/* Allocate skb for the complete frame (with header) */
		size = L2CAP_HDR_SIZE + tlen;
		if (!(conn->rx_skb = bluez_skb_alloc(size, GFP_ATOMIC)))
			goto drop;

		memcpy(skb_put(conn->rx_skb, skb->len), skb->data, skb->len);

		conn->rx_len = tlen - flen;
	} else {
		DBG("Cont: frag len %d (expecting %d)", skb->len, conn->rx_len);

		if (!conn->rx_len) {
			ERR("Unexpected continuation frame (len %d)", skb->len);
			goto drop;
		}

		if (skb->len > conn->rx_len) {
			ERR("Fragment is too large (len %d)", skb->len);
			kfree_skb(conn->rx_skb); conn->rx_skb = NULL;
			goto drop;
		}

		memcpy(skb_put(conn->rx_skb, skb->len), skb->data, skb->len);
		conn->rx_len -= skb->len;

		if (!conn->rx_len) {
			/* Complete frame received */
			l2cap_recv_frame(conn, conn->rx_skb);
			conn->rx_skb = NULL;
		}
	}

drop:
	kfree_skb(skb);
	return 0;
}

struct proto_ops l2cap_sock_ops = {
	family:		PF_BLUETOOTH,
	release:	l2cap_sock_release,
	bind:		l2cap_sock_bind,
	connect:	l2cap_sock_connect,
	listen:		l2cap_sock_listen,
	accept:		l2cap_sock_accept,
	getname:	l2cap_sock_getname,
	sendmsg:	l2cap_sock_sendmsg,
	recvmsg:	l2cap_sock_recvmsg,
	poll:		l2cap_sock_poll,
	socketpair:	sock_no_socketpair,
	ioctl:		sock_no_ioctl,
	shutdown:	sock_no_shutdown,
	setsockopt:	l2cap_sock_setsockopt,
	getsockopt:	l2cap_sock_getsockopt,
	mmap:		sock_no_mmap
};

struct net_proto_family l2cap_sock_family_ops = {
	family:		PF_BLUETOOTH,
	create:		l2cap_sock_create
};

struct hci_proto l2cap_hci_proto = {
	name:		"L2CAP",
	id:		HCI_PROTO_L2CAP,
	connect_ind:	l2cap_connect_ind,
	connect_cfm:	l2cap_connect_cfm,
	disconn_ind:	l2cap_disconn_ind,
	recv_acldata:	l2cap_recv_acldata,
};

struct notifier_block l2cap_nblock = {
	notifier_call: l2cap_dev_event
};

int __init l2cap_init(void)
{
	INF("BlueZ L2CAP ver %s Copyright (C) 2000,2001 Qualcomm Inc",
		VERSION);
	INF("Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>");

	if (bluez_sock_register(BTPROTO_L2CAP, &l2cap_sock_family_ops)) {
		ERR("Can't register L2CAP socket");
		return -EPROTO;
	}

	if (hci_register_proto(&l2cap_hci_proto) < 0) {
		ERR("Can't register L2CAP protocol");
		return -EPROTO;
	}

	hci_register_notifier(&l2cap_nblock);

	l2cap_register_proc();

	return 0;
}

void l2cap_cleanup(void)
{
	l2cap_unregister_proc();

	/* Unregister socket, protocol and notifier */
	if (bluez_sock_unregister(BTPROTO_L2CAP))
		ERR("Can't unregister L2CAP socket");

	if (hci_unregister_proto(&l2cap_hci_proto) < 0)
		ERR("Can't unregister L2CAP protocol");

	hci_unregister_notifier(&l2cap_nblock);

	/* We _must_ not have any sockets and/or connections
	 * at this stage.
	 */

	/* Free interface list and unlock HCI devices */
	{
		struct list_head *list = &l2cap_iff_list;

		while (!list_empty(list)) {
			struct l2cap_iff *iff;

			iff = list_entry(list->next, struct l2cap_iff, list);
			l2cap_iff_del(iff->hdev);
		}
	}
}

module_init(l2cap_init);
module_exit(l2cap_cleanup);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>");
MODULE_DESCRIPTION("BlueZ L2CAP ver " VERSION);
MODULE_LICENSE("GPL");

