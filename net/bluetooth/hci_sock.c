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
 * BlueZ HCI socket layer.
 *
 * $Id: hci_sock.c,v 1.9 2001/08/05 06:02:16 maxk Exp $
 */

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
#include <linux/tqueue.h>
#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/ioctl.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/bluez.h>
#include <net/bluetooth/hci_core.h>

#ifndef HCI_SOCK_DEBUG
#undef  DBG
#define DBG( A... )
#endif

/* HCI socket interface */

static struct bluez_sock_list hci_sk_list = {
	lock: RW_LOCK_UNLOCKED
};

static struct sock *hci_sock_lookup(struct hci_dev *hdev)
{
	struct sock *sk;

	read_lock(&hci_sk_list.lock);
	for (sk = hci_sk_list.head; sk; sk = sk->next) {
		if (hci_pi(sk)->hdev == hdev)
			break;
	}
	read_unlock(&hci_sk_list.lock);
	return sk;
}

/* Send frame to RAW socket */
void hci_send_to_sock(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct sock * sk;

	DBG("hdev %p len %d", hdev, skb->len);

	read_lock(&hci_sk_list.lock);
	for (sk = hci_sk_list.head; sk; sk = sk->next) {
		struct hci_filter *flt;	
		struct sk_buff *nskb;

		if (sk->state != BT_BOUND || hci_pi(sk)->hdev != hdev)
			continue;

		/* Don't send frame to the socket it came from */
		if (skb->sk == sk)
			continue;

		/* Apply filter */
		flt = &hci_pi(sk)->filter;

		if (!test_bit(skb->pkt_type, &flt->type_mask))
			continue;

		if (skb->pkt_type == HCI_EVENT_PKT) {
			register int evt = (*(__u8 *)skb->data & 63);

			if (!test_bit(evt, &flt->event_mask))
				continue;
		}

		if (!(nskb = skb_clone(skb, GFP_ATOMIC)))
			continue;

		/* Put type byte before the data */
		memcpy(skb_push(nskb, 1), &nskb->pkt_type, 1);

		skb_queue_tail(&sk->receive_queue, nskb);
		sk->data_ready(sk, nskb->len);
	}
	read_unlock(&hci_sk_list.lock);
}

static int hci_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct hci_dev *hdev = hci_pi(sk)->hdev;

	DBG("sock %p sk %p", sock, sk);

	if (!sk)
		return 0;

	bluez_sock_unlink(&hci_sk_list, sk);

	if (hdev) {
		if (!hci_sock_lookup(hdev))
			hdev->flags &= ~HCI_SOCK;

		hci_dev_put(hdev);
	}

	sock_orphan(sk);

	skb_queue_purge(&sk->receive_queue);
	skb_queue_purge(&sk->write_queue);

	sock_put(sk);

	MOD_DEC_USE_COUNT;

	return 0;
}

static int hci_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct hci_dev *hdev = hci_pi(sk)->hdev;
	__u32 mode;

	DBG("cmd %x arg %lx", cmd, arg);

	switch (cmd) {
	case HCIGETINFO:
		return hci_dev_info(arg);

	case HCIGETDEVLIST:
		return hci_dev_list(arg);

	case HCIDEVUP:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_dev_open(arg);

	case HCIDEVDOWN:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_dev_close(arg);

	case HCIDEVRESET:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_dev_reset(arg);

	case HCIRESETSTAT:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_dev_reset_stat(arg);

	case HCISETSCAN:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_dev_setscan(arg);

	case HCISETAUTH:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_dev_setauth(arg);

	case HCISETRAW:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;

		if (!hdev)
			return -EBADFD;

		if (arg)
			mode = HCI_RAW;
		else
			mode = HCI_NORMAL;

		return hci_dev_setmode(hdev, mode);

	case HCISETPTYPE:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return hci_dev_setptype(arg);

	case HCIINQUIRY:
		return hci_inquiry(arg);

	case HCIGETCONNLIST:
		return hci_conn_list(arg);

	default:
		return -EINVAL;
	};
}

static int hci_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_hci *haddr = (struct sockaddr_hci *) addr;
	struct sock *sk = sock->sk;
	struct hci_dev *hdev = NULL;

	DBG("sock %p sk %p", sock, sk);

	if (!haddr || haddr->hci_family != AF_BLUETOOTH)
		return -EINVAL;

	if (hci_pi(sk)->hdev) {
		/* Already bound */
		return 0;
	}

	if (haddr->hci_dev != HCI_DEV_NONE) {
		if (!(hdev = hci_dev_get(haddr->hci_dev)))
			return -ENODEV;

		hdev->flags |= HCI_SOCK;
	}

	hci_pi(sk)->hdev = hdev;
	sk->state = BT_BOUND;

	return 0;
}

static int hci_sock_getname(struct socket *sock, struct sockaddr *addr, int *addr_len, int peer)
{
	struct sockaddr_hci *haddr = (struct sockaddr_hci *) addr;
	struct sock *sk = sock->sk;

	DBG("sock %p sk %p", sock, sk);

	*addr_len = sizeof(*haddr);
	haddr->hci_family = AF_BLUETOOTH;
	haddr->hci_dev    = hci_pi(sk)->hdev->id;

	return 0;
}

static int hci_sock_sendmsg(struct socket *sock, struct msghdr *msg, int len,
                            struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct hci_dev *hdev = hci_pi(sk)->hdev;
	struct sk_buff *skb;
	int err;

	DBG("sock %p sk %p", sock, sk);

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (msg->msg_flags & ~(MSG_DONTWAIT|MSG_NOSIGNAL|MSG_ERRQUEUE))
		return -EINVAL;

	if (!hdev)
		return -EBADFD;

	if (!(skb = bluez_skb_send_alloc(sk, len, msg->msg_flags & MSG_DONTWAIT, &err)))
		return err;

	if (memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len)) {
		kfree_skb(skb);
		return -EFAULT;
	}

	skb->dev = (void *) hdev;
	skb->pkt_type = *((unsigned char *) skb->data);
	skb_pull(skb, 1);

	/* Send frame to HCI core */
	hci_send_raw(skb);

	return len;
}

static inline void hci_sock_cmsg(struct sock *sk, struct msghdr *msg, struct sk_buff *skb)
{
	__u32 mask = hci_pi(sk)->cmsg_mask;

	if (mask & HCI_CMSG_DIR)
        	put_cmsg(msg, SOL_HCI, HCI_CMSG_DIR, sizeof(int), &bluez_cb(skb)->incomming);
}
 
static int hci_sock_recvmsg(struct socket *sock, struct msghdr *msg, int len,
                            int flags, struct scm_cookie *scm)
{
	int noblock = flags & MSG_DONTWAIT;
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;

	DBG("sock %p sk %p", sock, sk);

	if (flags & (MSG_OOB | MSG_PEEK))
		return -EOPNOTSUPP;

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

	if (hci_pi(sk)->cmsg_mask)
		hci_sock_cmsg(sk, msg, skb);

	skb_free_datagram(sk, skb);

	return err ? : copied;
}

int hci_sock_setsockopt(struct socket *sock, int level, int optname, char *optval, int len)
{
	struct sock *sk = sock->sk;
	struct hci_filter flt;
	int err = 0, opt = 0;

	DBG("sk %p, opt %d", sk, optname);

	lock_sock(sk);

	switch (optname) {
	case HCI_DATA_DIR:
		if (get_user(opt, (int *)optval))
			return -EFAULT;

		if (opt)
			hci_pi(sk)->cmsg_mask |= HCI_CMSG_DIR;
		else
			hci_pi(sk)->cmsg_mask &= ~HCI_CMSG_DIR;
		break;

	case HCI_FILTER:
		len = MIN(len, sizeof(struct hci_filter));
		if (copy_from_user(&flt, optval, len)) {
			err = -EFAULT;
			break;
		}
		memcpy(&hci_pi(sk)->filter, &flt, len);
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	};

	release_sock(sk);
	return err;
}

int hci_sock_getsockopt(struct socket *sock, int level, int optname, char *optval, int *optlen)
{
	struct sock *sk = sock->sk;
	int len, opt; 

	if (get_user(len, optlen))
		return -EFAULT;

	switch (optname) {
	case HCI_DATA_DIR:
		if (hci_pi(sk)->cmsg_mask & HCI_CMSG_DIR)
			opt = 1;
		else 
			opt = 0;

		if (put_user(opt, optval))
			return -EFAULT;
		break;

	case HCI_FILTER:
		len = MIN(len, sizeof(struct hci_filter));
		if (copy_to_user(optval, &hci_pi(sk)->filter, len))
			return -EFAULT;
		break;

	default:
		return -ENOPROTOOPT;
		break;
	};

	return 0;
}

struct proto_ops hci_sock_ops = {
	family:		PF_BLUETOOTH,
	release:	hci_sock_release,
	bind:		hci_sock_bind,
	getname:	hci_sock_getname,
	sendmsg:	hci_sock_sendmsg,
	recvmsg:	hci_sock_recvmsg,
	ioctl:		hci_sock_ioctl,
	poll:		datagram_poll,
	listen:		sock_no_listen,
	shutdown:	sock_no_shutdown,
	setsockopt:	hci_sock_setsockopt,
	getsockopt:	hci_sock_getsockopt,
	connect:	sock_no_connect,
	socketpair:	sock_no_socketpair,
	accept:		sock_no_accept,
	mmap:		sock_no_mmap
};

static int hci_sock_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	DBG("sock %p", sock);

	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sock->ops = &hci_sock_ops;

	if (!(sk = sk_alloc(PF_BLUETOOTH, GFP_KERNEL, 1)))
		return -ENOMEM;

	sock->state = SS_UNCONNECTED;
	sock_init_data(sock, sk);

	memset(&sk->protinfo, 0, sizeof(struct hci_pinfo));
	sk->destruct = NULL;
	sk->protocol = protocol;
	sk->state    = BT_OPEN;

	/* Initialize filter */
	hci_pi(sk)->filter.type_mask  = (1<<HCI_EVENT_PKT);
	hci_pi(sk)->filter.event_mask[0] = ~0L;
	hci_pi(sk)->filter.event_mask[1] = ~0L;

	bluez_sock_link(&hci_sk_list, sk);

	MOD_INC_USE_COUNT;

	return 0;
}

static int hci_sock_dev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct hci_dev *hdev = (struct hci_dev *) ptr;
	struct sk_buff *skb;

	DBG("hdev %s event %ld", hdev->name, event);

	/* Send event to sockets */
	if ((skb = bluez_skb_alloc(HCI_EVENT_HDR_SIZE + EVT_HCI_DEV_EVENT_SIZE, GFP_ATOMIC))) {
		hci_event_hdr eh = { EVT_HCI_DEV_EVENT, EVT_HCI_DEV_EVENT_SIZE };
		evt_hci_dev_event he = { event, hdev->id };

		skb->pkt_type = HCI_EVENT_PKT;
		memcpy(skb_put(skb, HCI_EVENT_HDR_SIZE), &eh, HCI_EVENT_HDR_SIZE);
		memcpy(skb_put(skb, EVT_HCI_DEV_EVENT_SIZE), &he, EVT_HCI_DEV_EVENT_SIZE);

		hci_send_to_sock(NULL, skb);
		kfree_skb(skb);
	}

	if (event == HCI_DEV_UNREG) {
		struct sock *sk;

		/* Detach sockets from device */
		read_lock(&hci_sk_list.lock);
		for (sk = hci_sk_list.head; sk; sk = sk->next) {
			if (hci_pi(sk)->hdev == hdev) {
				hci_pi(sk)->hdev = NULL;
				sk->err = EPIPE;
				sk->state = BT_OPEN;
				sk->state_change(sk);

				hci_dev_put(hdev);
			}
		}
		read_unlock(&hci_sk_list.lock);
	}

	return NOTIFY_DONE;
}

struct net_proto_family hci_sock_family_ops = {
	family: PF_BLUETOOTH,
	create: hci_sock_create
};

struct notifier_block hci_sock_nblock = {
	notifier_call: hci_sock_dev_event
};

int hci_sock_init(void)
{
	if (bluez_sock_register(BTPROTO_HCI, &hci_sock_family_ops)) {
		ERR("Can't register HCI socket");
		return -EPROTO;
	}

	hci_register_notifier(&hci_sock_nblock);

	return 0;
}

int hci_sock_cleanup(void)
{
	if (bluez_sock_unregister(BTPROTO_HCI))
		ERR("Can't unregister HCI socket");

	hci_unregister_notifier(&hci_sock_nblock);

	return 0;
}
