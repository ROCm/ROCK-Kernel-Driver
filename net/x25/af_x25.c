/*
 *	X.25 Packet Layer release 002
 *
 *	This is ALPHA test software. This code may break your machine,
 *	randomly fail to work with new releases, misbehave and/or generally
 *	screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	X.25 001	Jonathan Naylor	Started coding.
 *	X.25 002	Jonathan Naylor	Centralised disconnect handling.
 *					New timer architecture.
 *	2000-03-11	Henner Eisen	MSG_EOR handling more POSIX compliant.
 *	2000-03-22	Daniela Squassoni Allowed disabling/enabling of 
 *					  facilities negotiation and increased 
 *					  the throughput upper limit.
 *	2000-08-27	Arnaldo C. Melo s/suser/capable/ + micro cleanups
 *	2000-09-04	Henner Eisen	Set sock->state in x25_accept(). 
 *					Fixed x25_output() related skb leakage.
 *	2000-10-02	Henner Eisen	Made x25_kick() single threaded per socket.
 *	2000-10-27	Henner Eisen    MSG_DONTWAIT for fragment allocation.
 *	2000-11-14	Henner Eisen    Closing datalink from NETDEV_GOING_DOWN
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/stat.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <net/x25.h>

int sysctl_x25_restart_request_timeout = X25_DEFAULT_T20;
int sysctl_x25_call_request_timeout    = X25_DEFAULT_T21;
int sysctl_x25_reset_request_timeout   = X25_DEFAULT_T22;
int sysctl_x25_clear_request_timeout   = X25_DEFAULT_T23;
int sysctl_x25_ack_holdback_timeout    = X25_DEFAULT_T2;

static struct sock *volatile x25_list /* = NULL initially */;

static struct proto_ops x25_proto_ops;

static struct x25_address null_x25_address = {"               "};

int x25_addr_ntoa(unsigned char *p, struct x25_address *called_addr,
		  struct x25_address *calling_addr)
{
	int called_len, calling_len;
	char *called, *calling;
	int i;

	called_len  = (*p >> 0) & 0x0F;
	calling_len = (*p >> 4) & 0x0F;

	called  = called_addr->x25_addr;
	calling = calling_addr->x25_addr;
	p++;

	for (i = 0; i < (called_len + calling_len); i++) {
		if (i < called_len) {
			if (i % 2 != 0) {
				*called++ = ((*p >> 0) & 0x0F) + '0';
				p++;
			} else {
				*called++ = ((*p >> 4) & 0x0F) + '0';
			}
		} else {
			if (i % 2 != 0) {
				*calling++ = ((*p >> 0) & 0x0F) + '0';
				p++;
			} else {
				*calling++ = ((*p >> 4) & 0x0F) + '0';
			}
		}
	}

	*called = *calling = '\0';

	return 1 + (called_len + calling_len + 1) / 2;
}

int x25_addr_aton(unsigned char *p, struct x25_address *called_addr,
		  struct x25_address *calling_addr)
{
	unsigned int called_len, calling_len;
	char *called, *calling;
	int i;

	called  = called_addr->x25_addr;
	calling = calling_addr->x25_addr;

	called_len  = strlen(called);
	calling_len = strlen(calling);

	*p++ = (calling_len << 4) | (called_len << 0);

	for (i = 0; i < (called_len + calling_len); i++) {
		if (i < called_len) {
			if (i % 2 != 0) {
				*p |= (*called++ - '0') << 0;
				p++;
			} else {
				*p = 0x00;
				*p |= (*called++ - '0') << 4;
			}
		} else {
			if (i % 2 != 0) {
				*p |= (*calling++ - '0') << 0;
				p++;
			} else {
				*p = 0x00;
				*p |= (*calling++ - '0') << 4;
			}
		}
	}

	return 1 + (called_len + calling_len + 1) / 2;
}

/*
 *	Socket removal during an interrupt is now safe.
 */
static void x25_remove_socket(struct sock *sk)
{
	struct sock *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	if ((s = x25_list) == sk)
		x25_list = s->next;
	else while (s && s->next) {
		if (s->next == sk) {
			s->next = sk->next;
			break;
		}

		s = s->next;
	}
	restore_flags(flags);
}

/*
 *	Kill all bound sockets on a dropped device.
 */
static void x25_kill_by_device(struct net_device *dev)
{
	struct sock *s;

	for (s = x25_list; s; s = s->next)
		if (x25_sk(s)->neighbour && x25_sk(s)->neighbour->dev == dev)
			x25_disconnect(s, ENETUNREACH, 0, 0);
}

/*
 *	Handle device status changes.
 */
static int x25_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct x25_neigh *nb;

	if (dev->type == ARPHRD_X25
#if defined(CONFIG_LLC) || defined(CONFIG_LLC_MODULE)
	 || dev->type == ARPHRD_ETHER
#endif
	 ) {
		switch (event) {
			case NETDEV_UP:
				x25_link_device_up(dev);
				break;
			case NETDEV_GOING_DOWN:
				nb = x25_get_neigh(dev);
				if (nb) {
					x25_terminate_link(nb);
					x25_neigh_put(nb);
				}
				break;
			case NETDEV_DOWN:
				x25_kill_by_device(dev);
				x25_route_device_down(dev);
				x25_link_device_down(dev);
				break;
		}
	}

	return NOTIFY_DONE;
}

/*
 *	Add a socket to the bound sockets list.
 */
static void x25_insert_socket(struct sock *sk)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	sk->next = x25_list;
	x25_list = sk;

	restore_flags(flags);
}

/*
 *	Find a socket that wants to accept the Call Request we just
 *	received.
 */
static struct sock *x25_find_listener(struct x25_address *addr)
{
	unsigned long flags;
	struct sock *s;

	save_flags(flags);
	cli();

	for (s = x25_list; s; s = s->next)
		if ((!strcmp(addr->x25_addr, x25_sk(s)->source_addr.x25_addr) ||
		     !strcmp(addr->x25_addr, null_x25_address.x25_addr)) &&
		     s->state == TCP_LISTEN)
			break;

	restore_flags(flags);
	return s;
}

/*
 *	Find a connected X.25 socket given my LCI and neighbour.
 */
struct sock *x25_find_socket(unsigned int lci, struct x25_neigh *nb)
{
	struct sock *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (s = x25_list; s; s = s->next)
		if (x25_sk(s)->lci == lci && x25_sk(s)->neighbour == nb)
			break;

	restore_flags(flags);
	return s;
}

/*
 *	Find a unique LCI for a given device.
 */
unsigned int x25_new_lci(struct x25_neigh *nb)
{
	unsigned int lci = 1;

	while (x25_find_socket(lci, nb))
		if (++lci == 4096) {
			lci = 0;
			break;
		}

	return lci;
}

/*
 *	Deferred destroy.
 */
void x25_destroy_socket(struct sock *);

/*
 *	handler for deferred kills.
 */
static void x25_destroy_timer(unsigned long data)
{
	x25_destroy_socket((struct sock *)data);
}

/*
 *	This is called from user mode and the timers. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */
void x25_destroy_socket(struct sock *sk)	/* Not static as it's used by the timer */
{
	struct sk_buff *skb;
	unsigned long flags;

	save_flags(flags);
	cli();

	x25_stop_heartbeat(sk);
	x25_stop_timer(sk);

	x25_remove_socket(sk);
	x25_clear_queues(sk);		/* Flush the queues */

	while ((skb = skb_dequeue(&sk->receive_queue)) != NULL) {
		if (skb->sk != sk) {		/* A pending connection */
			skb->sk->dead = 1;	/* Queue the unaccepted socket for death */
			x25_start_heartbeat(skb->sk);
			x25_sk(skb->sk)->state = X25_STATE_0;
		}

		kfree_skb(skb);
	}

	if (atomic_read(&sk->wmem_alloc) || atomic_read(&sk->rmem_alloc)) {
		/* Defer: outstanding buffers */
		init_timer(&sk->timer);
		sk->timer.expires  = jiffies + 10 * HZ;
		sk->timer.function = x25_destroy_timer;
		sk->timer.data     = (unsigned long)sk;
		add_timer(&sk->timer);
	} else {
		sk_free(sk);
		MOD_DEC_USE_COUNT;
	}

	restore_flags(flags);
}

/*
 *	Handling for system calls applied via the various interfaces to a
 *	X.25 socket object.
 */

static int x25_setsockopt(struct socket *sock, int level, int optname,
			  char *optval, int optlen)
{
	int opt;
	struct sock *sk = sock->sk;
	int rc = -ENOPROTOOPT;

	if (level != SOL_X25 || optname != X25_QBITINCL)
		goto out;

	rc = -EINVAL;
	if (optlen < sizeof(int))
		goto out;

	rc = -EFAULT;
	if (get_user(opt, (int *)optval))
		goto out;

	x25_sk(sk)->qbitincl = !!opt;
	rc = 0;
out:
	return rc;
}

static int x25_getsockopt(struct socket *sock, int level, int optname,
			  char *optval, int *optlen)
{
	struct sock *sk = sock->sk;
	int val, len, rc = -ENOPROTOOPT;
	
	if (level != SOL_X25 || optname != X25_QBITINCL)
		goto out;

	rc = -EFAULT;
	if (get_user(len, optlen))
		goto out;

	len = min_t(unsigned int, len, sizeof(int));

	rc = -EINVAL;
	if (len < 0)
		goto out;
		
	rc = -EFAULT;
	if (put_user(len, optlen))
		goto out;

	val = x25_sk(sk)->qbitincl;
	rc = copy_to_user(optval, &val, len) ? -EFAULT : 0;
out:
	return rc;
}

static int x25_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int rc = -EOPNOTSUPP;

	if (sk->state != TCP_LISTEN) {
		memset(&x25_sk(sk)->dest_addr, 0, X25_ADDR_LEN);
		sk->max_ack_backlog = backlog;
		sk->state           = TCP_LISTEN;
		rc = 0;
	}

	return rc;
}

static struct sock *x25_alloc_socket(void)
{
	struct sock *sk;
	struct x25_opt *x25;

	MOD_INC_USE_COUNT;

	if ((sk = sk_alloc(AF_X25, GFP_ATOMIC, 1, NULL)) == NULL)
		goto decmod;

	x25 = x25_sk(sk) = kmalloc(sizeof(*x25), GFP_ATOMIC);
	if (!x25)
		goto frees;

	memset(x25, 0, sizeof(*x25));

	x25->sk = sk;

	sock_init_data(NULL, sk);

	skb_queue_head_init(&x25->ack_queue);
	skb_queue_head_init(&x25->fragment_queue);
	skb_queue_head_init(&x25->interrupt_in_queue);
	skb_queue_head_init(&x25->interrupt_out_queue);
out:
	return sk;
frees:
	sk_free(sk);
	sk = NULL;
decmod:
	MOD_DEC_USE_COUNT;
	goto out;
}

static int x25_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct x25_opt *x25;
	int rc = -ESOCKTNOSUPPORT;

	if (sock->type != SOCK_SEQPACKET || protocol)
		goto out;

	rc = -ENOMEM;
	if ((sk = x25_alloc_socket()) == NULL)
		goto out;

	x25 = x25_sk(sk);

	sock_init_data(sock, sk);

	init_timer(&x25->timer);

	sock->ops    = &x25_proto_ops;
	sk->protocol = protocol;
	sk->backlog_rcv = x25_backlog_rcv;

	x25->t21   = sysctl_x25_call_request_timeout;
	x25->t22   = sysctl_x25_reset_request_timeout;
	x25->t23   = sysctl_x25_clear_request_timeout;
	x25->t2    = sysctl_x25_ack_holdback_timeout;
	x25->state = X25_STATE_0;

	x25->facilities.winsize_in  = X25_DEFAULT_WINDOW_SIZE;
	x25->facilities.winsize_out = X25_DEFAULT_WINDOW_SIZE;
	x25->facilities.pacsize_in  = X25_DEFAULT_PACKET_SIZE;
	x25->facilities.pacsize_out = X25_DEFAULT_PACKET_SIZE;
	x25->facilities.throughput  = X25_DEFAULT_THROUGHPUT;
	x25->facilities.reverse     = X25_DEFAULT_REVERSE;
	rc = 0;
out:
	return rc;
}

static struct sock *x25_make_new(struct sock *osk)
{
	struct sock *sk = NULL;
	struct x25_opt *x25, *ox25;

	if (osk->type != SOCK_SEQPACKET)
		goto out;

	if ((sk = x25_alloc_socket()) == NULL)
		goto out;

	x25 = x25_sk(sk);

	sk->type        = osk->type;
	sk->socket      = osk->socket;
	sk->priority    = osk->priority;
	sk->protocol    = osk->protocol;
	sk->rcvbuf      = osk->rcvbuf;
	sk->sndbuf      = osk->sndbuf;
	sk->debug       = osk->debug;
	sk->state       = TCP_ESTABLISHED;
	sk->sleep       = osk->sleep;
	sk->zapped      = osk->zapped;
	sk->backlog_rcv = osk->backlog_rcv;

	ox25 = x25_sk(osk);
	x25->t21        = ox25->t21;
	x25->t22        = ox25->t22;
	x25->t23        = ox25->t23;
	x25->t2         = ox25->t2;
	x25->facilities = ox25->facilities;
	x25->qbitincl   = ox25->qbitincl;

	init_timer(&x25->timer);
out:
	return sk;
}

static int x25_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct x25_opt *x25;

	if (!sk)
		goto out;

	x25 = x25_sk(sk);

	switch (x25->state) {

		case X25_STATE_0:
		case X25_STATE_2:
			x25_disconnect(sk, 0, 0, 0);
			x25_destroy_socket(sk);
			break;

		case X25_STATE_1:
		case X25_STATE_3:
		case X25_STATE_4:
			x25_clear_queues(sk);
			x25_write_internal(sk, X25_CLEAR_REQUEST);
			x25_start_t23timer(sk);
			x25->state = X25_STATE_2;
			sk->state               = TCP_CLOSE;
			sk->shutdown           |= SEND_SHUTDOWN;
			sk->state_change(sk);
			sk->dead                = 1;
			sk->destroy             = 1;
			break;
	}

	sock->sk   = NULL;	
	sk->socket = NULL;	/* Not used, but we should do this */
out:
	return 0;
}

static int x25_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_x25 *addr = (struct sockaddr_x25 *)uaddr;

	if (!sk->zapped ||
	    addr_len != sizeof(struct sockaddr_x25) ||
	    addr->sx25_family != AF_X25)
		return -EINVAL;

	x25_sk(sk)->source_addr = addr->sx25_addr;
	x25_insert_socket(sk);
	sk->zapped = 0;
	SOCK_DEBUG(sk, "x25_bind: socket is bound\n");

	return 0;
}

static int x25_connect(struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct x25_opt *x25 = x25_sk(sk);
	struct sockaddr_x25 *addr = (struct sockaddr_x25 *)uaddr;
	struct x25_route *rt;
	int rc = 0;

	if (sk->state == TCP_ESTABLISHED && sock->state == SS_CONNECTING) {
		sock->state = SS_CONNECTED;
		goto out;	/* Connect completed during a ERESTARTSYS event */
	}

	rc = -ECONNREFUSED;
	if (sk->state == TCP_CLOSE && sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		goto out;
	}

	rc = -EISCONN;	/* No reconnect on a seqpacket socket */
	if (sk->state == TCP_ESTABLISHED)
		goto out;

	sk->state   = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;

	rc = -EINVAL;
	if (addr_len != sizeof(struct sockaddr_x25) ||
	    addr->sx25_family != AF_X25)
		goto out;

	rc = -ENETUNREACH;
	rt = x25_get_route(&addr->sx25_addr);
	if (!rt)
		goto out;

	x25->neighbour = x25_get_neigh(rt->dev);
	if (!x25->neighbour)
		goto out_put_route;

	x25_limit_facilities(&x25->facilities, x25->neighbour);

	x25->lci = x25_new_lci(x25->neighbour);
	if (!x25->lci)
		goto out_put_neigh;

	rc = -EINVAL;
	if (sk->zapped)		/* Must bind first - autobinding does not work */
		goto out_put_neigh;

	if (!strcmp(x25->source_addr.x25_addr, null_x25_address.x25_addr))
		memset(&x25->source_addr, '\0', X25_ADDR_LEN);

	x25->dest_addr = addr->sx25_addr;

	/* Move to connecting socket, start sending Connect Requests */
	sock->state   = SS_CONNECTING;
	sk->state     = TCP_SYN_SENT;

	x25->state = X25_STATE_1;

	x25_write_internal(sk, X25_CALL_REQUEST);

	x25_start_heartbeat(sk);
	x25_start_t21timer(sk);

	/* Now the loop */
	rc = -EINPROGRESS;
	if (sk->state != TCP_ESTABLISHED && (flags & O_NONBLOCK))
		goto out_put_neigh;

	cli();	/* To avoid races on the sleep */

	/*
	 * A Clear Request or timeout or failed routing will go to closed.
	 */
	rc = -ERESTARTSYS;
	while (sk->state == TCP_SYN_SENT) {
		/* FIXME: going to sleep with interrupts disabled */
		interruptible_sleep_on(sk->sleep);
		if (signal_pending(current))
			goto out_unlock;
	}

	if (sk->state != TCP_ESTABLISHED) {
		sock->state = SS_UNCONNECTED;
		rc = sock_error(sk);	/* Always set at this point */
		goto out_unlock;
	}

	sock->state = SS_CONNECTED;
	rc = 0;
out_unlock:
	sti();
out_put_neigh:
	if (rc)
		x25_neigh_put(x25->neighbour);
out_put_route:
	x25_route_put(rt);
out:
	return rc;
}
	
static int x25_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk = sock->sk;
	struct sock *newsk;
	struct sk_buff *skb;
	int rc = -EINVAL;

	if (!sk || sk->state != TCP_LISTEN)
		goto out;

	rc = -EOPNOTSUPP;
	if (sk->type != SOCK_SEQPACKET)
		goto out;

	/*
	 *	The write queue this time is holding sockets ready to use
	 *	hooked into the CALL INDICATION we saved
	 */
	do {
		cli();
		if ((skb = skb_dequeue(&sk->receive_queue)) == NULL) {
			rc = -EWOULDBLOCK;
			if (flags & O_NONBLOCK)
				goto out_unlock;
			/* FIXME: going to sleep with interrupts disabled */
			interruptible_sleep_on(sk->sleep);
			rc = -ERESTARTSYS;
			if (signal_pending(current))
				goto out_unlock;
		}
	} while (!skb);

	newsk	      = skb->sk;
	newsk->pair   = NULL;
	newsk->socket = newsock;
	newsk->sleep  = &newsock->wait;
	sti();

	/* Now attach up the new socket */
	skb->sk = NULL;
	kfree_skb(skb);
	sk->ack_backlog--;
	newsock->sk    = newsk;
	newsock->state = SS_CONNECTED;
	rc = 0;
out:
	return rc;
out_unlock:
	sti();
	goto out;
}

static int x25_getname(struct socket *sock, struct sockaddr *uaddr, int *uaddr_len, int peer)
{
	struct sockaddr_x25 *sx25 = (struct sockaddr_x25 *)uaddr;
	struct sock *sk = sock->sk;
	struct x25_opt *x25 = x25_sk(sk);

	if (peer) {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		sx25->sx25_addr = x25->dest_addr;
	} else
		sx25->sx25_addr = x25->source_addr;

	sx25->sx25_family = AF_X25;
	*uaddr_len = sizeof(*sx25);

	return 0;
}
 
int x25_rx_call_request(struct sk_buff *skb, struct x25_neigh *nb, unsigned int lci)
{
	struct sock *sk;
	struct sock *make;
	struct x25_opt *makex25;
	struct x25_address source_addr, dest_addr;
	struct x25_facilities facilities;
	int len, rc;

	/*
	 *	Remove the LCI and frame type.
	 */
	skb_pull(skb, X25_STD_MIN_LEN);

	/*
	 *	Extract the X.25 addresses and convert them to ASCII strings,
	 *	and remove them.
	 */
	skb_pull(skb, x25_addr_ntoa(skb->data, &source_addr, &dest_addr));

	/*
	 *	Find a listener for the particular address.
	 */
	sk = x25_find_listener(&source_addr);

	/*
	 *	We can't accept the Call Request.
	 */
	if (!sk || sk->ack_backlog == sk->max_ack_backlog)
		goto out_clear_request;

	/*
	 *	Try to reach a compromise on the requested facilities.
	 */
	if ((len = x25_negotiate_facilities(skb, sk, &facilities)) == -1)
		goto out_clear_request;

	/*
	 * current neighbour/link might impose additional limits
	 * on certain facilties
	 */

	x25_limit_facilities(&facilities, nb);

	/*
	 *	Try to create a new socket.
	 */
	make = x25_make_new(sk);
	if (!make)
		goto out_clear_request;

	/*
	 *	Remove the facilities, leaving any Call User Data.
	 */
	skb_pull(skb, len);

	skb->sk     = make;
	make->state = TCP_ESTABLISHED;

	makex25 = x25_sk(make);
	makex25->lci           = lci;
	makex25->dest_addr     = dest_addr;
	makex25->source_addr   = source_addr;
	makex25->neighbour     = nb;
	makex25->facilities    = facilities;
	makex25->vc_facil_mask = x25_sk(sk)->vc_facil_mask;

	x25_write_internal(make, X25_CALL_ACCEPTED);

	/*
	 *	Incoming Call User Data.
	 */
	if (skb->len >= 0) {
		memcpy(makex25->calluserdata.cuddata, skb->data, skb->len);
		makex25->calluserdata.cudlength = skb->len;
	}

	makex25->state = X25_STATE_3;

	sk->ack_backlog++;
	make->pair = sk;

	x25_insert_socket(make);

	skb_queue_head(&sk->receive_queue, skb);

	x25_start_heartbeat(make);

	if (!sk->dead)
		sk->data_ready(sk, skb->len);
	rc = 1;
out:
	return rc;
out_clear_request:
	rc = 0;
	x25_transmit_clear_request(nb, lci, 0x01);
	goto out;
}

static int x25_sendmsg(struct socket *sock, struct msghdr *msg, int len, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct x25_opt *x25 = x25_sk(sk);
	struct sockaddr_x25 *usx25 = (struct sockaddr_x25 *)msg->msg_name;
	struct sockaddr_x25 sx25;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int noblock = msg->msg_flags & MSG_DONTWAIT;
	int size, qbit = 0, rc = -EINVAL;

	if (msg->msg_flags & ~(MSG_DONTWAIT | MSG_OOB | MSG_EOR))
		goto out;

	/* we currently don't support segmented records at the user interface */
	if (!(msg->msg_flags & (MSG_EOR|MSG_OOB)))
		goto out;

	rc = -EADDRNOTAVAIL;
	if (sk->zapped)
		goto out;

	rc = -EPIPE;
	if (sk->shutdown & SEND_SHUTDOWN) {
		send_sig(SIGPIPE, current, 0);
		goto out;
	}

	rc = -ENETUNREACH;
	if (!x25->neighbour)
		goto out;

	if (usx25) {
		rc = -EINVAL;
		if (msg->msg_namelen < sizeof(sx25))
			goto out;
		memcpy(&sx25, usx25, sizeof(sx25));
		rc = -EISCONN;
		if (strcmp(x25->dest_addr.x25_addr, sx25.sx25_addr.x25_addr))
			goto out;
		rc = -EINVAL;
		if (sx25.sx25_family != AF_X25)
			goto out;
	} else {
		/*
		 *	FIXME 1003.1g - if the socket is like this because
		 *	it has become closed (not started closed) we ought
		 *	to SIGPIPE, EPIPE;
		 */
		rc = -ENOTCONN;
		if (sk->state != TCP_ESTABLISHED)
			goto out;

		sx25.sx25_family = AF_X25;
		sx25.sx25_addr   = x25->dest_addr;
	}

	SOCK_DEBUG(sk, "x25_sendmsg: sendto: Addresses built.\n");

	/* Build a packet */
	SOCK_DEBUG(sk, "x25_sendmsg: sendto: building packet.\n");

	if ((msg->msg_flags & MSG_OOB) && len > 32)
		len = 32;

	size = len + X25_MAX_L2_LEN + X25_EXT_MIN_LEN;

	skb = sock_alloc_send_skb(sk, size, noblock, &rc);
	if (!skb)
		goto out;
	X25_SKB_CB(skb)->flags = msg->msg_flags;

	skb_reserve(skb, X25_MAX_L2_LEN + X25_EXT_MIN_LEN);

	/*
	 *	Put the data on the end
	 */
	SOCK_DEBUG(sk, "x25_sendmsg: Copying user data\n");

	asmptr = skb->h.raw = skb_put(skb, len);

	rc = memcpy_fromiovec(asmptr, msg->msg_iov, len);
	if (rc)
		goto out_kfree_skb;

	/*
	 *	If the Q BIT Include socket option is in force, the first
	 *	byte of the user data is the logical value of the Q Bit.
	 */
	if (x25->qbitincl) {
		qbit = skb->data[0];
		skb_pull(skb, 1);
	}

	/*
	 *	Push down the X.25 header
	 */
	SOCK_DEBUG(sk, "x25_sendmsg: Building X.25 Header.\n");

	if (msg->msg_flags & MSG_OOB) {
		if (x25->neighbour->extended) {
			asmptr    = skb_push(skb, X25_STD_MIN_LEN);
			*asmptr++ = ((x25->lci >> 8) & 0x0F) | X25_GFI_EXTSEQ;
			*asmptr++ = (x25->lci >> 0) & 0xFF;
			*asmptr++ = X25_INTERRUPT;
		} else {
			asmptr    = skb_push(skb, X25_STD_MIN_LEN);
			*asmptr++ = ((x25->lci >> 8) & 0x0F) | X25_GFI_STDSEQ;
			*asmptr++ = (x25->lci >> 0) & 0xFF;
			*asmptr++ = X25_INTERRUPT;
		}
	} else {
		if (x25->neighbour->extended) {
			/* Build an Extended X.25 header */
			asmptr    = skb_push(skb, X25_EXT_MIN_LEN);
			*asmptr++ = ((x25->lci >> 8) & 0x0F) | X25_GFI_EXTSEQ;
			*asmptr++ = (x25->lci >> 0) & 0xFF;
			*asmptr++ = X25_DATA;
			*asmptr++ = X25_DATA;
		} else {
			/* Build an Standard X.25 header */
			asmptr    = skb_push(skb, X25_STD_MIN_LEN);
			*asmptr++ = ((x25->lci >> 8) & 0x0F) | X25_GFI_STDSEQ;
			*asmptr++ = (x25->lci >> 0) & 0xFF;
			*asmptr++ = X25_DATA;
		}

		if (qbit)
			skb->data[0] |= X25_Q_BIT;
	}

	SOCK_DEBUG(sk, "x25_sendmsg: Built header.\n");
	SOCK_DEBUG(sk, "x25_sendmsg: Transmitting buffer\n");

	rc = -ENOTCONN;
	if (sk->state != TCP_ESTABLISHED)
		goto out_kfree_skb;

	if (msg->msg_flags & MSG_OOB)
		skb_queue_tail(&x25->interrupt_out_queue, skb);
	else {
	        len = x25_output(sk, skb);
		if (len < 0)
			kfree_skb(skb);
		else if (x25->qbitincl)
			len++;
	}

	/*
	 * lock_sock() is currently only used to serialize this x25_kick()
	 * against input-driven x25_kick() calls. It currently only blocks
	 * incoming packets for this socket and does not protect against
	 * any other socket state changes and is not called from anywhere
	 * else. As x25_kick() cannot block and as long as all socket
	 * operations are BKL-wrapped, we don't need take to care about
	 * purging the backlog queue in x25_release().
	 *
	 * Using lock_sock() to protect all socket operations entirely
	 * (and making the whole x25 stack SMP aware) unfortunately would
	 * require major changes to {send,recv}msg and skb allocation methods.
	 * -> 2.5 ;)
	 */
	lock_sock(sk);
	x25_kick(sk);
	release_sock(sk);
	rc = len;
out:
	return rc;
out_kfree_skb:
	kfree_skb(skb);
	goto out;
}


static int x25_recvmsg(struct socket *sock, struct msghdr *msg, int size,
		       int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct x25_opt *x25 = x25_sk(sk);
	struct sockaddr_x25 *sx25 = (struct sockaddr_x25 *)msg->msg_name;
	int copied, qbit;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int rc = -ENOTCONN;

	/*
	 * This works for seqpacket too. The receiver has ordered the queue for
	 * us! We do one quick check first though
	 */
	if (sk->state != TCP_ESTABLISHED)
		goto out;

	if (flags & MSG_OOB) {
		rc = -EINVAL;
		if (sk->urginline || !skb_peek(&x25->interrupt_in_queue))
			goto out;

		skb = skb_dequeue(&x25->interrupt_in_queue);

		skb_pull(skb, X25_STD_MIN_LEN);

		/*
		 *	No Q bit information on Interrupt data.
		 */
		if (x25->qbitincl) {
			asmptr  = skb_push(skb, 1);
			*asmptr = 0x00;
		}

		msg->msg_flags |= MSG_OOB;
	} else {
		/* Now we can treat all alike */
		skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT, flags & MSG_DONTWAIT, &rc);
		if (!skb)
			goto out;

		qbit = (skb->data[0] & X25_Q_BIT) == X25_Q_BIT;

		skb_pull(skb, x25->neighbour->extended ?
				X25_EXT_MIN_LEN : X25_STD_MIN_LEN);

		if (x25->qbitincl) {
			asmptr  = skb_push(skb, 1);
			*asmptr = qbit;
		}
	}

	skb->h.raw = skb->data;

	copied = skb->len;

	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	/* Currently, each datagram always contains a complete record */ 
	msg->msg_flags |= MSG_EOR;

	rc = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (rc)
		goto out_free_dgram;

	if (sx25) {
		sx25->sx25_family = AF_X25;
		sx25->sx25_addr   = x25->dest_addr;
	}

	msg->msg_namelen = sizeof(struct sockaddr_x25);

	lock_sock(sk);
	x25_check_rbuf(sk);
	release_sock(sk);
	rc = copied;
out_free_dgram:
	skb_free_datagram(sk, skb);
out:
	return rc;
}


static int x25_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct x25_opt *x25 = x25_sk(sk);
	int rc;

	switch (cmd) {
		case TIOCOUTQ: {
			int amount;
			amount = sk->sndbuf - atomic_read(&sk->wmem_alloc);
			if (amount < 0)
				amount = 0;
			rc = put_user(amount, (unsigned int *)arg);
			break;
		}

		case TIOCINQ: {
			struct sk_buff *skb;
			int amount = 0;
			/*
			 * These two are safe on a single CPU system as
			 * only user tasks fiddle here
			 */
			if ((skb = skb_peek(&sk->receive_queue)) != NULL)
				amount = skb->len;
			rc = put_user(amount, (unsigned int *)arg);
			break;
		}

		case SIOCGSTAMP:
			if (sk) {
				rc = -ENOENT;
				if (!sk->stamp.tv_sec)
					break;
				rc = copy_to_user((void *)arg, &sk->stamp,
						  sizeof(struct timeval)) ? -EFAULT : 0;
			}
			rc = -EINVAL;
			break;
		case SIOCGIFADDR:
		case SIOCSIFADDR:
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
		case SIOCGIFMETRIC:
		case SIOCSIFMETRIC:
			rc = -EINVAL;
			break;
		case SIOCADDRT:
		case SIOCDELRT:
			rc = -EPERM;
			if (!capable(CAP_NET_ADMIN))
				break;
			rc = x25_route_ioctl(cmd, (void *)arg);
			break;
		case SIOCX25GSUBSCRIP:
			rc = x25_subscr_ioctl(cmd, (void *)arg);
			break;
		case SIOCX25SSUBSCRIP:
			rc = -EPERM;
			if (!capable(CAP_NET_ADMIN))
				break;
			rc = x25_subscr_ioctl(cmd, (void *)arg);
			break;
		case SIOCX25GFACILITIES: {
			struct x25_facilities fac = x25->facilities;
			rc = copy_to_user((void *)arg, &fac, sizeof(fac)) ? -EFAULT : 0;
			break;
		}

		case SIOCX25SFACILITIES: {
			struct x25_facilities facilities;
			rc = -EFAULT;
			if (copy_from_user(&facilities, (void *)arg, sizeof(facilities)))
				break;
			rc = -EINVAL;
			if (sk->state != TCP_LISTEN && sk->state != TCP_CLOSE)
				break;
			if (facilities.pacsize_in < X25_PS16 ||
			    facilities.pacsize_in > X25_PS4096)
				break;
			if (facilities.pacsize_out < X25_PS16 ||
			    facilities.pacsize_out > X25_PS4096)
				break;
			if (facilities.winsize_in < 1 ||
			    facilities.winsize_in > 127)
				break;
			if (facilities.throughput < 0x03 ||
			    facilities.throughput > 0xDD)
				break;
			if (facilities.reverse && facilities.reverse != 1)
				break;
			x25->facilities = facilities;
			rc = 0;
			break;
		}

		case SIOCX25GCALLUSERDATA: {
			struct x25_calluserdata cud = x25->calluserdata;
			rc = copy_to_user((void *)arg, &cud, sizeof(cud)) ? -EFAULT : 0;
			break;
		}

		case SIOCX25SCALLUSERDATA: {
			struct x25_calluserdata calluserdata;

			rc = -EFAULT;
			if (copy_from_user(&calluserdata, (void *)arg, sizeof(calluserdata)))
				break;
			rc = -EINVAL;
			if (calluserdata.cudlength > X25_MAX_CUD_LEN)
				break;
			x25->calluserdata = calluserdata;
			rc = 0;
			break;
		}

		case SIOCX25GCAUSEDIAG: {
			struct x25_causediag causediag;
			causediag = x25->causediag;
			rc = copy_to_user((void *)arg, &causediag, sizeof(causediag)) ? -EFAULT : 0;
			break;
		}

 		default:
			rc = dev_ioctl(cmd, (void *)arg);
			break;
	}

	return rc;
}

static int x25_get_info(char *buffer, char **start, off_t offset, int length)
{
	struct sock *s;
	struct net_device *dev;
	const char *devname;
	int len;
	off_t pos = 0;
	off_t begin = 0;

	cli();

	len = sprintf(buffer, "dest_addr  src_addr   dev   lci st vs vr va   "
			      "t  t2 t21 t22 t23 Snd-Q Rcv-Q inode\n");

	for (s = x25_list; s; s = s->next) {
		struct x25_opt *x25 = x25_sk(s);

		if (!x25->neighbour || (dev = x25->neighbour->dev) == NULL)
			devname = "???";
		else
			devname = x25->neighbour->dev->name;

		len += sprintf(buffer + len, "%-10s %-10s %-5s %3.3X  %d  %d  "
					     "%d  %d %3lu %3lu %3lu %3lu %3lu "
					     "%5d %5d %ld\n",
			!x25->dest_addr.x25_addr[0] ? "*" :
						x25->dest_addr.x25_addr,
			!x25->source_addr.x25_addr[0] ? "*" :
						x25->source_addr.x25_addr,
			devname, 
			x25->lci & 0x0FFF,
			x25->state,
			x25->vs,
			x25->vr,
			x25->va,
			x25_display_timer(s) / HZ,
			x25->t2  / HZ,
			x25->t21 / HZ,
			x25->t22 / HZ,
			x25->t23 / HZ,
			atomic_read(&s->wmem_alloc),
			atomic_read(&s->rmem_alloc),
			s->socket ? SOCK_INODE(s->socket)->i_ino : 0L);

		pos = begin + len;

		if (pos < offset) {
			len   = 0;
			begin = pos;
		}

		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= (offset - begin);

	if (len > length)
		len = length;

	return len;
} 

struct net_proto_family x25_family_ops = {
	.family =	AF_X25,
	.create =	x25_create,
};

static struct proto_ops SOCKOPS_WRAPPED(x25_proto_ops) = {
	.family =	AF_X25,

	.release =	x25_release,
	.bind =		x25_bind,
	.connect =	x25_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	x25_accept,
	.getname =	x25_getname,
	.poll =		datagram_poll,
	.ioctl =	x25_ioctl,
	.listen =	x25_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	x25_setsockopt,
	.getsockopt =	x25_getsockopt,
	.sendmsg =	x25_sendmsg,
	.recvmsg =	x25_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};

#include <linux/smp_lock.h>
SOCKOPS_WRAP(x25_proto, AF_X25);


static struct packet_type x25_packet_type = {
	.type =	__constant_htons(ETH_P_X25),
	.func =	x25_lapb_receive_frame,
};

struct notifier_block x25_dev_notifier = {
	.notifier_call = x25_device_event,
};

void x25_kill_by_neigh(struct x25_neigh *nb)
{
	struct sock *s;

	for (s = x25_list; s; s = s->next)
		if (x25_sk(s)->neighbour == nb)
			x25_disconnect(s, ENETUNREACH, 0, 0);
}

static int __init x25_init(void)
{
#ifdef MODULE
	struct net_device *dev;
#endif /* MODULE */
	sock_register(&x25_family_ops);

	dev_add_pack(&x25_packet_type);

	register_netdevice_notifier(&x25_dev_notifier);

	printk(KERN_INFO "X.25 for Linux. Version 0.2 for Linux 2.1.15\n");

#ifdef CONFIG_SYSCTL
	x25_register_sysctl();
#endif

	proc_net_create("x25", 0, x25_get_info);
	proc_net_create("x25_routes", 0, x25_routes_get_info);

#ifdef MODULE
	/*
	 *	Register any pre existing devices.
	 */
	read_lock(&dev_base_lock);
	for (dev = dev_base; dev; dev = dev->next) {
		if ((dev->flags & IFF_UP) && (dev->type == ARPHRD_X25
#if defined(CONFIG_LLC) || defined(CONFIG_LLC_MODULE)
					   || dev->type == ARPHRD_ETHER
#endif
			))
			x25_link_device_up(dev);
	}
	read_unlock(&dev_base_lock);
#endif /* MODULE */
	return 0;
}
module_init(x25_init);



MODULE_AUTHOR("Jonathan Naylor <g4klx@g4klx.demon.co.uk>");
MODULE_DESCRIPTION("The X.25 Packet Layer network layer protocol");
MODULE_LICENSE("GPL");

static void __exit x25_exit(void)
{

	proc_net_remove("x25");
	proc_net_remove("x25_routes");

	x25_link_free();
	x25_route_free();

#ifdef CONFIG_SYSCTL
	x25_unregister_sysctl();
#endif

	unregister_netdevice_notifier(&x25_dev_notifier);

	dev_remove_pack(&x25_packet_type);

	sock_unregister(AF_X25);
}
module_exit(x25_exit);

