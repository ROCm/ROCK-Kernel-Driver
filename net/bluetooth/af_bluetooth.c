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
 *  Bluetooth address family and sockets.
 *
 * $Id: af_bluetooth.c,v 1.3 2002/04/17 17:37:15 maxk Exp $
 */
#define VERSION "2.3"

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <net/sock.h>

#if defined(CONFIG_KMOD)
#include <linux/kmod.h>
#endif

#include <net/bluetooth/bluetooth.h>

#ifndef CONFIG_BT_SOCK_DEBUG
#undef  BT_DBG
#define BT_DBG( A... )
#endif

struct proc_dir_entry *proc_bt;

/* Bluetooth sockets */
#define BT_MAX_PROTO	5
static struct net_proto_family *bt_proto[BT_MAX_PROTO];

static kmem_cache_t *bt_sock_cache;

int bt_sock_register(int proto, struct net_proto_family *ops)
{
	if (proto >= BT_MAX_PROTO)
		return -EINVAL;

	if (bt_proto[proto])
		return -EEXIST;

	bt_proto[proto] = ops;
	return 0;
}

int bt_sock_unregister(int proto)
{
	if (proto >= BT_MAX_PROTO)
		return -EINVAL;

	if (!bt_proto[proto])
		return -ENOENT;

	bt_proto[proto] = NULL;
	return 0;
}

static int bt_sock_create(struct socket *sock, int proto)
{
	int err = 0;

	if (proto >= BT_MAX_PROTO)
		return -EINVAL;

#if defined(CONFIG_KMOD)
	if (!bt_proto[proto]) {
		request_module("bt-proto-%d", proto);
	}
#endif
	err = -EPROTONOSUPPORT;
	if (bt_proto[proto] && try_module_get(bt_proto[proto]->owner)) {
		err = bt_proto[proto]->create(sock, proto);
		module_put(bt_proto[proto]->owner);
	}
	return err; 
}

struct sock *bt_sock_alloc(struct socket *sock, int proto, int pi_size, int prio)
{
	struct sock *sk;
	void *pi;

	sk = sk_alloc(PF_BLUETOOTH, prio, sizeof(struct bt_sock), bt_sock_cache);
	if (!sk)
		return NULL;
	
	if (pi_size) {
		pi = kmalloc(pi_size, prio);
		if (!pi) {
			sk_free(sk);
			return NULL;
		}
		memset(pi, 0, pi_size);
		sk->sk_protinfo = pi;
	}

	sock_init_data(sock, sk);
	INIT_LIST_HEAD(&bt_sk(sk)->accept_q);
	
	sk->sk_zapped   = 0;
	sk->sk_protocol = proto;
	sk->sk_state    = BT_OPEN;

	return sk;
}

void bt_sock_link(struct bt_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_add_node(sk, &l->head);
	write_unlock_bh(&l->lock);
}

void bt_sock_unlink(struct bt_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_del_node_init(sk);
	write_unlock_bh(&l->lock);
}

void bt_accept_enqueue(struct sock *parent, struct sock *sk)
{
	BT_DBG("parent %p, sk %p", parent, sk);

	sock_hold(sk);
	list_add_tail(&bt_sk(sk)->accept_q, &bt_sk(parent)->accept_q);
	bt_sk(sk)->parent = parent;
	parent->sk_ack_backlog++;
}

static void bt_accept_unlink(struct sock *sk)
{
	BT_DBG("sk %p state %d", sk, sk->sk_state);

	list_del_init(&bt_sk(sk)->accept_q);
	bt_sk(sk)->parent->sk_ack_backlog--;
	bt_sk(sk)->parent = NULL;
	sock_put(sk);
}

struct sock *bt_accept_dequeue(struct sock *parent, struct socket *newsock)
{
	struct list_head *p, *n;
	struct sock *sk;
	
	BT_DBG("parent %p", parent);

	list_for_each_safe(p, n, &bt_sk(parent)->accept_q) {
		sk = (struct sock *) list_entry(p, struct bt_sock, accept_q);
		
		lock_sock(sk);
		if (sk->sk_state == BT_CLOSED) {
			release_sock(sk);
			bt_accept_unlink(sk);
			continue;
		}
		
		if (sk->sk_state == BT_CONNECTED || !newsock) {
			bt_accept_unlink(sk);
			if (newsock)
				sock_graft(sk, newsock);
			release_sock(sk);
			return sk;
		}
		release_sock(sk);
	}
	return NULL;
}

int bt_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
	struct msghdr *msg, int len, int flags)
{
	int noblock = flags & MSG_DONTWAIT;
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;

	BT_DBG("sock %p sk %p len %d", sock, sk, len);

	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	if (!(skb = skb_recv_datagram(sk, flags, noblock, &err))) {
		if (sk->sk_shutdown & RCV_SHUTDOWN)
			return 0;
		return err;
	}

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

unsigned int bt_sock_poll(struct file * file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask;

	BT_DBG("sock %p, sk %p", sock, sk);

	poll_wait(file, sk->sk_sleep, wait);
	mask = 0;

	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= POLLERR;

	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	if (!skb_queue_empty(&sk->sk_receive_queue) || 
			!list_empty(&bt_sk(sk)->accept_q) ||
			(sk->sk_shutdown & RCV_SHUTDOWN))
		mask |= POLLIN | POLLRDNORM;

	if (sk->sk_state == BT_CLOSED)
		mask |= POLLHUP;

	if (sk->sk_state == BT_CONNECT || sk->sk_state == BT_CONNECT2)
		return mask;
	
	if (sock_writeable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
	else
		set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	return mask;
}

int bt_sock_wait_state(struct sock *sk, int state, unsigned long timeo)
{
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;

	BT_DBG("sk %p", sk);

	add_wait_queue(sk->sk_sleep, &wait);
	while (sk->sk_state != state) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);

		if (sk->sk_err) {
			err = sock_error(sk);
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk->sk_sleep, &wait);
	return err;
}

struct net_proto_family bt_sock_family_ops = {
	.owner  = THIS_MODULE,
	.family	= PF_BLUETOOTH,
	.create	= bt_sock_create,
};

extern int hci_sock_init(void);
extern int hci_sock_cleanup(void);
extern int hci_proc_init(void);
extern int hci_proc_cleanup(void);

static int __init bt_init(void)
{
	BT_INFO("Core ver %s", VERSION);

	proc_bt = proc_mkdir("bluetooth", NULL);
	if (proc_bt)
		proc_bt->owner = THIS_MODULE;
	
	/* Init socket cache */
	bt_sock_cache = kmem_cache_create("bt_sock",
			sizeof(struct bt_sock), 0,
			SLAB_HWCACHE_ALIGN, 0, 0);

	if (!bt_sock_cache) {
		BT_ERR("Socket cache creation failed");
		return -ENOMEM;
	}
	
	sock_register(&bt_sock_family_ops);

	BT_INFO("HCI device and connection manager initialized");

	hci_proc_init();
	hci_sock_init();
	return 0;
}

static void __exit bt_cleanup(void)
{
	hci_sock_cleanup();
	hci_proc_cleanup();

	sock_unregister(PF_BLUETOOTH);
	kmem_cache_destroy(bt_sock_cache);

	remove_proc_entry("bluetooth", NULL);
}

subsys_initcall(bt_init);
module_exit(bt_cleanup);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>");
MODULE_DESCRIPTION("Bluetooth Core ver " VERSION);
MODULE_LICENSE("GPL");
