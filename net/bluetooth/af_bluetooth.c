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
 * BlueZ Bluetooth address family and sockets.
 *
 * $Id: af_bluetooth.c,v 1.3 2002/04/17 17:37:15 maxk Exp $
 */
#define VERSION "2.2"

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

#ifndef AF_BLUETOOTH_DEBUG
#undef  BT_DBG
#define BT_DBG( A... )
#endif

/* Bluetooth sockets */
#define BLUEZ_MAX_PROTO	5
static struct net_proto_family *bluez_proto[BLUEZ_MAX_PROTO];

static kmem_cache_t *bluez_sock_cache;

int bluez_sock_register(int proto, struct net_proto_family *ops)
{
	if (proto >= BLUEZ_MAX_PROTO)
		return -EINVAL;

	if (bluez_proto[proto])
		return -EEXIST;

	bluez_proto[proto] = ops;
	return 0;
}

int bluez_sock_unregister(int proto)
{
	if (proto >= BLUEZ_MAX_PROTO)
		return -EINVAL;

	if (!bluez_proto[proto])
		return -ENOENT;

	bluez_proto[proto] = NULL;
	return 0;
}

static int bluez_sock_create(struct socket *sock, int proto)
{
	if (proto > BLUEZ_MAX_PROTO)
		return -EINVAL;

#if defined(CONFIG_KMOD)
	if (!bluez_proto[proto]) {
		char module_name[30];
		sprintf(module_name, "bt-proto-%d", proto);
		request_module(module_name);
	}
#endif

	if (!bluez_proto[proto])
		return -ENOENT;

	return bluez_proto[proto]->create(sock, proto);
}

struct sock *bluez_sock_alloc(struct socket *sock, int proto, int pi_size, int prio)
{
	struct sock *sk;
	void *pi;

	sk = sk_alloc(PF_BLUETOOTH, prio, sizeof(struct bluez_sock), bluez_sock_cache);
	if (!sk)
		return NULL;
	
	if (pi_size) {
		pi = kmalloc(pi_size, prio);
		if (!pi) {
			sk_free(sk);
			return NULL;
		}
		memset(pi, 0, pi_size);
		sk->protinfo = pi;
	}

	sock_init_data(sock, sk);
	INIT_LIST_HEAD(&bluez_sk(sk)->accept_q);
	
	sk->zapped   = 0;
	sk->protocol = proto;
	sk->state    = BT_OPEN;

	return sk;
}

void bluez_sock_link(struct bluez_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk->next = l->head;
	l->head = sk;
	sock_hold(sk);
	write_unlock_bh(&l->lock);
}

void bluez_sock_unlink(struct bluez_sock_list *l, struct sock *sk)
{
	struct sock **skp;

	write_lock_bh(&l->lock);
	for (skp = &l->head; *skp; skp = &((*skp)->next)) {
		if (*skp == sk) {
			*skp = sk->next;
			__sock_put(sk);
			break;
		}
	}
	write_unlock_bh(&l->lock);
}

void bluez_accept_enqueue(struct sock *parent, struct sock *sk)
{
	BT_DBG("parent %p, sk %p", parent, sk);

	sock_hold(sk);
	list_add_tail(&bluez_sk(sk)->accept_q, &bluez_sk(parent)->accept_q);
	bluez_sk(sk)->parent = parent;
	parent->ack_backlog++;
}

static void bluez_accept_unlink(struct sock *sk)
{
	BT_DBG("sk %p state %d", sk, sk->state);

	list_del_init(&bluez_sk(sk)->accept_q);
	bluez_sk(sk)->parent->ack_backlog--;
	bluez_sk(sk)->parent = NULL;
	sock_put(sk);
}

struct sock *bluez_accept_dequeue(struct sock *parent, struct socket *newsock)
{
	struct list_head *p, *n;
	struct sock *sk;
	
	BT_DBG("parent %p", parent);

	list_for_each_safe(p, n, &bluez_sk(parent)->accept_q) {
		sk = (struct sock *) list_entry(p, struct bluez_sock, accept_q);
		
		lock_sock(sk);
		if (sk->state == BT_CLOSED) {
			release_sock(sk);
			bluez_accept_unlink(sk);
			continue;
		}
		
		if (sk->state == BT_CONNECTED || !newsock) {
			bluez_accept_unlink(sk);
			if (newsock)
				sock_graft(sk, newsock);
			release_sock(sk);
			return sk;
		}
		release_sock(sk);
	}
	return NULL;
}

int bluez_sock_recvmsg(struct socket *sock, struct msghdr *msg, int len, int flags, struct scm_cookie *scm)
{
	int noblock = flags & MSG_DONTWAIT;
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;

	BT_DBG("sock %p sk %p len %d", sock, sk, len);

	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	if (!(skb = skb_recv_datagram(sk, flags, noblock, &err))) {
		if (sk->shutdown & RCV_SHUTDOWN)
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

unsigned int bluez_sock_poll(struct file * file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask;

	BT_DBG("sock %p, sk %p", sock, sk);

	poll_wait(file, sk->sleep, wait);
	mask = 0;

	if (sk->err || !skb_queue_empty(&sk->error_queue))
		mask |= POLLERR;

	if (sk->shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	if (!skb_queue_empty(&sk->receive_queue) || 
			!list_empty(&bluez_sk(sk)->accept_q) ||
			(sk->shutdown & RCV_SHUTDOWN))
		mask |= POLLIN | POLLRDNORM;

	if (sk->state == BT_CLOSED)
		mask |= POLLHUP;

	if (sk->state == BT_CONNECT || sk->state == BT_CONNECT2)
		return mask;
	
	if (sock_writeable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
	else
		set_bit(SOCK_ASYNC_NOSPACE, &sk->socket->flags);

	return mask;
}

int bluez_sock_w4_connect(struct sock *sk, int flags)
{
	DECLARE_WAITQUEUE(wait, current);
	long timeo = sock_sndtimeo(sk, flags & O_NONBLOCK);
	int err = 0;

	BT_DBG("sk %p", sk);

	add_wait_queue(sk->sleep, &wait);
	while (sk->state != BT_CONNECTED) {
		set_current_state(TASK_INTERRUPTIBLE);
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
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk->sleep, &wait);
	return err;
}

struct net_proto_family bluez_sock_family_ops =
{
	PF_BLUETOOTH, bluez_sock_create
};

static int __init bluez_init(void)
{
	BT_INFO("BlueZ Core ver %s Copyright (C) 2000,2001 Qualcomm Inc",
		 VERSION);
	BT_INFO("Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>");

	proc_mkdir("bluetooth", NULL);

	/* Init socket cache */
	bluez_sock_cache = kmem_cache_create("bluez_sock",
			sizeof(struct bluez_sock), 0,
			SLAB_HWCACHE_ALIGN, 0, 0);

	if (!bluez_sock_cache) {
		BT_ERR("BlueZ socket cache creation failed");
		return -ENOMEM;
	}
	
	sock_register(&bluez_sock_family_ops);

	hci_core_init();
	hci_sock_init();
	return 0;
}

static void __exit bluez_cleanup(void)
{
	hci_sock_cleanup();
	hci_core_cleanup();

	sock_unregister(PF_BLUETOOTH);
	kmem_cache_destroy(bluez_sock_cache);

	remove_proc_entry("bluetooth", NULL);
}

#ifdef MODULE
module_init(bluez_init);
module_exit(bluez_cleanup);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>");
MODULE_DESCRIPTION("BlueZ Core ver " VERSION);
MODULE_LICENSE("GPL");
#endif
