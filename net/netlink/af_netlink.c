/*
 * NETLINK      Kernel-user communication protocol.
 *
 * 		Authors:	Alan Cox <alan@redhat.com>
 * 				Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 * 
 * Tue Jun 26 14:36:48 MEST 2001 Herbert "herp" Rosmanith
 *                               added netlink_proto_exit
 * Tue Jan 22 18:32:44 BRST 2002 Arnaldo C. de Melo <acme@conectiva.com.br>
 * 				 use nlk_sk, as sk->protinfo is on a diet 8)
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/smp_lock.h>
#include <linux/notifier.h>
#include <linux/security.h>
#include <net/sock.h>
#include <net/scm.h>

#define Nprintk(a...)

#if defined(CONFIG_NETLINK_DEV) || defined(CONFIG_NETLINK_DEV_MODULE)
#define NL_EMULATE_DEV
#endif

struct netlink_opt
{
	u32			pid;
	unsigned		groups;
	u32			dst_pid;
	unsigned		dst_groups;
	unsigned long		state;
	int			(*handler)(int unit, struct sk_buff *skb);
	wait_queue_head_t	wait;
	struct netlink_callback	*cb;
	spinlock_t		cb_lock;
	void			(*data_ready)(struct sock *sk, int bytes);
};

#define nlk_sk(__sk) ((struct netlink_opt *)(__sk)->sk_protinfo)

static struct hlist_head nl_table[MAX_LINKS];
static DECLARE_WAIT_QUEUE_HEAD(nl_table_wait);
static unsigned nl_nonroot[MAX_LINKS];

#ifdef NL_EMULATE_DEV
static struct socket *netlink_kernel[MAX_LINKS];
#endif

static int netlink_dump(struct sock *sk);
static void netlink_destroy_callback(struct netlink_callback *cb);

atomic_t netlink_sock_nr;

static rwlock_t nl_table_lock = RW_LOCK_UNLOCKED;
static atomic_t nl_table_users = ATOMIC_INIT(0);

static struct notifier_block *netlink_chain;

static void netlink_sock_destruct(struct sock *sk)
{
	skb_queue_purge(&sk->sk_receive_queue);

	if (!sock_flag(sk, SOCK_DEAD)) {
		printk("Freeing alive netlink socket %p\n", sk);
		return;
	}
	BUG_TRAP(!atomic_read(&sk->sk_rmem_alloc));
	BUG_TRAP(!atomic_read(&sk->sk_wmem_alloc));
	BUG_TRAP(!nlk_sk(sk)->cb);

	kfree(nlk_sk(sk));

	atomic_dec(&netlink_sock_nr);
#ifdef NETLINK_REFCNT_DEBUG
	printk(KERN_DEBUG "NETLINK %p released, %d are still alive\n", sk, atomic_read(&netlink_sock_nr));
#endif
}

/* This lock without WQ_FLAG_EXCLUSIVE is good on UP and it is _very_ bad on SMP.
 * Look, when several writers sleep and reader wakes them up, all but one
 * immediately hit write lock and grab all the cpus. Exclusive sleep solves
 * this, _but_ remember, it adds useless work on UP machines.
 */

static void netlink_table_grab(void)
{
	write_lock_bh(&nl_table_lock);

	if (atomic_read(&nl_table_users)) {
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue_exclusive(&nl_table_wait, &wait);
		for(;;) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			if (atomic_read(&nl_table_users) == 0)
				break;
			write_unlock_bh(&nl_table_lock);
			schedule();
			write_lock_bh(&nl_table_lock);
		}

		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&nl_table_wait, &wait);
	}
}

static __inline__ void netlink_table_ungrab(void)
{
	write_unlock_bh(&nl_table_lock);
	wake_up(&nl_table_wait);
}

static __inline__ void
netlink_lock_table(void)
{
	/* read_lock() synchronizes us to netlink_table_grab */

	read_lock(&nl_table_lock);
	atomic_inc(&nl_table_users);
	read_unlock(&nl_table_lock);
}

static __inline__ void
netlink_unlock_table(void)
{
	if (atomic_dec_and_test(&nl_table_users))
		wake_up(&nl_table_wait);
}

static __inline__ struct sock *netlink_lookup(int protocol, u32 pid)
{
	struct sock *sk;
	struct hlist_node *node;

	read_lock(&nl_table_lock);
	sk_for_each(sk, node, &nl_table[protocol]) {
		if (nlk_sk(sk)->pid == pid) {
			sock_hold(sk);
			goto found;
		}
	}
	sk = NULL;
found:
	read_unlock(&nl_table_lock);
	return sk;
}

extern struct proto_ops netlink_ops;

static int netlink_insert(struct sock *sk, u32 pid)
{
	int err = -EADDRINUSE;
	struct sock *osk;
	struct hlist_node *node;

	netlink_table_grab();
	sk_for_each(osk, node, &nl_table[sk->sk_protocol]) {
		if (nlk_sk(osk)->pid == pid)
			break;
	}
	if (!node) {
		err = -EBUSY;
		if (nlk_sk(sk)->pid == 0) {
			nlk_sk(sk)->pid = pid;
			sk_add_node(sk, &nl_table[sk->sk_protocol]);
			err = 0;
		}
	}
	netlink_table_ungrab();
	return err;
}

static void netlink_remove(struct sock *sk)
{
	netlink_table_grab();
	sk_del_node_init(sk);
	netlink_table_ungrab();
}

static int netlink_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct netlink_opt *nlk;

	sock->state = SS_UNCONNECTED;

	if (sock->type != SOCK_RAW && sock->type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	if (protocol<0 || protocol >= MAX_LINKS)
		return -EPROTONOSUPPORT;

	sock->ops = &netlink_ops;

	sk = sk_alloc(PF_NETLINK, GFP_KERNEL, 1, NULL);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock,sk);
	sk_set_owner(sk, THIS_MODULE);

	nlk = sk->sk_protinfo = kmalloc(sizeof(*nlk), GFP_KERNEL);
	if (!nlk) {
		sk_free(sk);
		return -ENOMEM;
	}
	memset(nlk, 0, sizeof(*nlk));

	spin_lock_init(&nlk->cb_lock);
	init_waitqueue_head(&nlk->wait);
	sk->sk_destruct = netlink_sock_destruct;
	atomic_inc(&netlink_sock_nr);

	sk->sk_protocol = protocol;
	return 0;
}

static int netlink_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct netlink_opt *nlk;

	if (!sk)
		return 0;

	netlink_remove(sk);
	nlk = nlk_sk(sk);

	spin_lock(&nlk->cb_lock);
	if (nlk->cb) {
		nlk->cb->done(nlk->cb);
		netlink_destroy_callback(nlk->cb);
		nlk->cb = NULL;
		__sock_put(sk);
	}
	spin_unlock(&nlk->cb_lock);

	/* OK. Socket is unlinked, and, therefore,
	   no new packets will arrive */

	sock_orphan(sk);
	sock->sk = NULL;
	wake_up_interruptible_all(&nlk->wait);

	skb_queue_purge(&sk->sk_write_queue);

	if (nlk->pid && !nlk->groups) {
		struct netlink_notify n = {
						.protocol = sk->sk_protocol,
						.pid = nlk->pid,
					  };
		notifier_call_chain(&netlink_chain, NETLINK_URELEASE, &n);
	}	
	
	sock_put(sk);
	return 0;
}

static int netlink_autobind(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct sock *osk;
	struct hlist_node *node;
	s32 pid = current->pid;
	int err;

retry:
	netlink_table_grab();
	sk_for_each(osk, node, &nl_table[sk->sk_protocol]) {
		if (nlk_sk(osk)->pid == pid) {
			/* Bind collision, search negative pid values. */
			if (pid > 0)
				pid = -4096;
			pid--;
			netlink_table_ungrab();
			goto retry;
		}
	}
	netlink_table_ungrab();

	err = netlink_insert(sk, pid);
	if (err == -EADDRINUSE)
		goto retry;
	nlk_sk(sk)->groups = 0;
	return 0;
}

static inline int netlink_capable(struct socket *sock, unsigned flag) 
{ 
	return (nl_nonroot[sock->sk->sk_protocol] & flag) ||
	       capable(CAP_NET_ADMIN);
} 

static int netlink_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct netlink_opt *nlk = nlk_sk(sk);
	struct sockaddr_nl *nladdr = (struct sockaddr_nl *)addr;
	int err;
	
	if (nladdr->nl_family != AF_NETLINK)
		return -EINVAL;

	/* Only superuser is allowed to listen multicasts */
	if (nladdr->nl_groups && !netlink_capable(sock, NL_NONROOT_RECV))
		return -EPERM;

	if (nlk->pid) {
		if (nladdr->nl_pid != nlk->pid)
			return -EINVAL;
		nlk->groups = nladdr->nl_groups;
		return 0;
	}

	if (nladdr->nl_pid == 0) {
		err = netlink_autobind(sock);
		if (err == 0)
			nlk->groups = nladdr->nl_groups;
		return err;
	}

	err = netlink_insert(sk, nladdr->nl_pid);
	if (err == 0)
		nlk->groups = nladdr->nl_groups;
	return err;
}

static int netlink_connect(struct socket *sock, struct sockaddr *addr,
			   int alen, int flags)
{
	int err = 0;
	struct sock *sk = sock->sk;
	struct netlink_opt *nlk = nlk_sk(sk);
	struct sockaddr_nl *nladdr=(struct sockaddr_nl*)addr;

	if (addr->sa_family == AF_UNSPEC) {
		nlk->dst_pid	= 0;
		nlk->dst_groups = 0;
		return 0;
	}
	if (addr->sa_family != AF_NETLINK)
		return -EINVAL;

	/* Only superuser is allowed to send multicasts */
	if (nladdr->nl_groups && !netlink_capable(sock, NL_NONROOT_SEND))
		return -EPERM;

	if (!nlk->pid)
		err = netlink_autobind(sock);

	if (err == 0) {
		nlk->dst_pid 	= nladdr->nl_pid;
		nlk->dst_groups = nladdr->nl_groups;
	}

	return 0;
}

static int netlink_getname(struct socket *sock, struct sockaddr *addr, int *addr_len, int peer)
{
	struct sock *sk = sock->sk;
	struct netlink_opt *nlk = nlk_sk(sk);
	struct sockaddr_nl *nladdr=(struct sockaddr_nl *)addr;
	
	nladdr->nl_family = AF_NETLINK;
	nladdr->nl_pad = 0;
	*addr_len = sizeof(*nladdr);

	if (peer) {
		nladdr->nl_pid = nlk->dst_pid;
		nladdr->nl_groups = nlk->dst_groups;
	} else {
		nladdr->nl_pid = nlk->pid;
		nladdr->nl_groups = nlk->groups;
	}
	return 0;
}

static void netlink_overrun(struct sock *sk)
{
	if (!test_and_set_bit(0, &nlk_sk(sk)->state)) {
		sk->sk_err = ENOBUFS;
		sk->sk_error_report(sk);
	}
}

struct sock *netlink_getsockbypid(struct sock *ssk, u32 pid)
{
	int protocol = ssk->sk_protocol;
	struct sock *sock;
	struct netlink_opt *nlk;

	sock = netlink_lookup(protocol, pid);
	if (!sock)
		return ERR_PTR(-ECONNREFUSED);

	/* Don't bother queuing skb if kernel socket has no input function */
	nlk = nlk_sk(sock);
	if (nlk->pid == 0 && !nlk->data_ready) {
		sock_put(sock);
		return ERR_PTR(-ECONNREFUSED);
	}
	return sock;
}

struct sock *netlink_getsockbyfilp(struct file *filp)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct socket *socket;
	struct sock *sock;

	if (!inode->i_sock || !(socket = SOCKET_I(inode)))
		return ERR_PTR(-ENOTSOCK);

	sock = socket->sk;
	if (sock->sk_family != AF_NETLINK)
		return ERR_PTR(-EINVAL);

	sock_hold(sock);
	return sock;
}

/*
 * Attach a skb to a netlink socket.
 * The caller must hold a reference to the destination socket. On error, the
 * reference is dropped. The skb is not send to the destination, just all
 * all error checks are performed and memory in the queue is reserved.
 * Return values:
 * < 0: error. skb freed, reference to sock dropped.
 * 0: continue
 * 1: repeat lookup - reference dropped while waiting for socket memory.
 */
int netlink_attachskb(struct sock *sk, struct sk_buff *skb, int nonblock, long timeo)
{
	struct netlink_opt *nlk;

	nlk = nlk_sk(sk);

#ifdef NL_EMULATE_DEV
	if (nlk->handler)
		return 0;
#endif
	if (atomic_read(&sk->sk_rmem_alloc) > sk->sk_rcvbuf ||
	    test_bit(0, &nlk->state)) {
		DECLARE_WAITQUEUE(wait, current);
		if (!timeo) {
			if (!nlk->pid)
				netlink_overrun(sk);
			sock_put(sk);
			kfree_skb(skb);
			return -EAGAIN;
		}

		__set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&nlk->wait, &wait);

		if ((atomic_read(&sk->sk_rmem_alloc) > sk->sk_rcvbuf ||
		     test_bit(0, &nlk->state)) &&
		    !sock_flag(sk, SOCK_DEAD))
			timeo = schedule_timeout(timeo);

		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&nlk->wait, &wait);
		sock_put(sk);

		if (signal_pending(current)) {
			kfree_skb(skb);
			return sock_intr_errno(timeo);
		}
		return 1;
	}
	skb_orphan(skb);
	skb_set_owner_r(skb, sk);
	return 0;
}

int netlink_sendskb(struct sock *sk, struct sk_buff *skb, int protocol)
{
	struct netlink_opt *nlk;
	int len = skb->len;

	nlk = nlk_sk(sk);
#ifdef NL_EMULATE_DEV
	if (nlk->handler) {
		skb_orphan(skb);
		len = nlk->handler(protocol, skb);
		sock_put(sk);
		return len;
	}
#endif

	skb_queue_tail(&sk->sk_receive_queue, skb);
	sk->sk_data_ready(sk, len);
	sock_put(sk);
	return len;
}

void netlink_detachskb(struct sock *sk, struct sk_buff *skb)
{
	kfree_skb(skb);
	sock_put(sk);
}

int netlink_unicast(struct sock *ssk, struct sk_buff *skb, u32 pid, int nonblock)
{
	struct sock *sk;
	int err;
	long timeo;

	timeo = sock_sndtimeo(ssk, nonblock);
retry:
	sk = netlink_getsockbypid(ssk, pid);
	if (IS_ERR(sk)) {
		kfree_skb(skb);
		return PTR_ERR(sk);
	}
	err = netlink_attachskb(sk, skb, nonblock, timeo);
	if (err == 1)
		goto retry;
	if (err)
		return err;

	return netlink_sendskb(sk, skb, ssk->sk_protocol);
}

static __inline__ int netlink_broadcast_deliver(struct sock *sk, struct sk_buff *skb)
{
	struct netlink_opt *nlk = nlk_sk(sk);
#ifdef NL_EMULATE_DEV
	if (nlk->handler) {
		skb_orphan(skb);
		nlk->handler(sk->sk_protocol, skb);
		return 0;
	} else
#endif
	if (atomic_read(&sk->sk_rmem_alloc) <= sk->sk_rcvbuf &&
	    !test_bit(0, &nlk->state)) {
                skb_orphan(skb);
		skb_set_owner_r(skb, sk);
		skb_queue_tail(&sk->sk_receive_queue, skb);
		sk->sk_data_ready(sk, skb->len);
		return 0;
	}
	return -1;
}

int netlink_broadcast(struct sock *ssk, struct sk_buff *skb, u32 pid,
		      u32 group, int allocation)
{
	struct sock *sk;
	struct hlist_node *node;
	struct sk_buff *skb2 = NULL;
	int protocol = ssk->sk_protocol;
	int failure = 0, delivered = 0;

	/* While we sleep in clone, do not allow to change socket list */

	netlink_lock_table();

	sk_for_each(sk, node, &nl_table[protocol]) {
		struct netlink_opt *nlk = nlk_sk(sk);

		if (ssk == sk)
			continue;

		if (nlk->pid == pid || !(nlk->groups & group))
			continue;

		if (failure) {
			netlink_overrun(sk);
			continue;
		}

		sock_hold(sk);
		if (skb2 == NULL) {
			if (atomic_read(&skb->users) != 1) {
				skb2 = skb_clone(skb, allocation);
			} else {
				skb2 = skb;
				atomic_inc(&skb->users);
			}
		}
		if (skb2 == NULL) {
			netlink_overrun(sk);
			/* Clone failed. Notify ALL listeners. */
			failure = 1;
		} else if (netlink_broadcast_deliver(sk, skb2)) {
			netlink_overrun(sk);
		} else {
			delivered = 1;
			skb2 = NULL;
		}
		sock_put(sk);
	}

	netlink_unlock_table();

	if (skb2)
		kfree_skb(skb2);
	kfree_skb(skb);

	if (delivered)
		return 0;
	if (failure)
		return -ENOBUFS;
	return -ESRCH;
}

void netlink_set_err(struct sock *ssk, u32 pid, u32 group, int code)
{
	struct sock *sk;
	struct hlist_node *node;
	int protocol = ssk->sk_protocol;

	read_lock(&nl_table_lock);
	sk_for_each(sk, node, &nl_table[protocol]) {
		struct netlink_opt *nlk = nlk_sk(sk);
		if (ssk == sk)
			continue;

		if (nlk->pid == pid || !(nlk->groups & group))
			continue;

		sk->sk_err = code;
		sk->sk_error_report(sk);
	}
	read_unlock(&nl_table_lock);
}

static inline void netlink_rcv_wake(struct sock *sk)
{
	struct netlink_opt *nlk = nlk_sk(sk);

	if (!skb_queue_len(&sk->sk_receive_queue))
		clear_bit(0, &nlk->state);
	if (!test_bit(0, &nlk->state))
		wake_up_interruptible(&nlk->wait);
}

static int netlink_sendmsg(struct kiocb *kiocb, struct socket *sock,
			   struct msghdr *msg, size_t len)
{
	struct sock_iocb *siocb = kiocb_to_siocb(kiocb);
	struct sock *sk = sock->sk;
	struct netlink_opt *nlk = nlk_sk(sk);
	struct sockaddr_nl *addr=msg->msg_name;
	u32 dst_pid;
	u32 dst_groups;
	struct sk_buff *skb;
	int err;
	struct scm_cookie scm;

	if (msg->msg_flags&MSG_OOB)
		return -EOPNOTSUPP;

	if (NULL == siocb->scm)
		siocb->scm = &scm;
	err = scm_send(sock, msg, siocb->scm);
	if (err < 0)
		return err;

	if (msg->msg_namelen) {
		if (addr->nl_family != AF_NETLINK)
			return -EINVAL;
		dst_pid = addr->nl_pid;
		dst_groups = addr->nl_groups;
		if (dst_groups && !netlink_capable(sock, NL_NONROOT_SEND))
			return -EPERM;
	} else {
		dst_pid = nlk->dst_pid;
		dst_groups = nlk->dst_groups;
	}

	if (!nlk->pid) {
		err = netlink_autobind(sock);
		if (err)
			goto out;
	}

	err = -EMSGSIZE;
	if (len > sk->sk_sndbuf - 32)
		goto out;
	err = -ENOBUFS;
	skb = alloc_skb(len, GFP_KERNEL);
	if (skb==NULL)
		goto out;

	NETLINK_CB(skb).pid	= nlk->pid;
	NETLINK_CB(skb).groups	= nlk->groups;
	NETLINK_CB(skb).dst_pid = dst_pid;
	NETLINK_CB(skb).dst_groups = dst_groups;
	memcpy(NETLINK_CREDS(skb), &siocb->scm->creds, sizeof(struct ucred));

	/* What can I do? Netlink is asynchronous, so that
	   we will have to save current capabilities to
	   check them, when this message will be delivered
	   to corresponding kernel module.   --ANK (980802)
	 */

	err = -EFAULT;
	if (memcpy_fromiovec(skb_put(skb,len), msg->msg_iov, len)) {
		kfree_skb(skb);
		goto out;
	}

	err = security_netlink_send(sk, skb);
	if (err) {
		kfree_skb(skb);
		goto out;
	}

	if (dst_groups) {
		atomic_inc(&skb->users);
		netlink_broadcast(sk, skb, dst_pid, dst_groups, GFP_KERNEL);
	}
	err = netlink_unicast(sk, skb, dst_pid, msg->msg_flags&MSG_DONTWAIT);

out:
	return err;
}

static int netlink_recvmsg(struct kiocb *kiocb, struct socket *sock,
			   struct msghdr *msg, size_t len,
			   int flags)
{
	struct sock_iocb *siocb = kiocb_to_siocb(kiocb);
	struct scm_cookie scm;
	struct sock *sk = sock->sk;
	struct netlink_opt *nlk = nlk_sk(sk);
	int noblock = flags&MSG_DONTWAIT;
	size_t copied;
	struct sk_buff *skb;
	int err;

	if (flags&MSG_OOB)
		return -EOPNOTSUPP;

	copied = 0;

	skb = skb_recv_datagram(sk,flags,noblock,&err);
	if (skb==NULL)
		goto out;

	msg->msg_namelen = 0;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	skb->h.raw = skb->data;
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	if (msg->msg_name) {
		struct sockaddr_nl *addr = (struct sockaddr_nl*)msg->msg_name;
		addr->nl_family = AF_NETLINK;
		addr->nl_pad    = 0;
		addr->nl_pid	= NETLINK_CB(skb).pid;
		addr->nl_groups	= NETLINK_CB(skb).dst_groups;
		msg->msg_namelen = sizeof(*addr);
	}

	if (NULL == siocb->scm) {
		memset(&scm, 0, sizeof(scm));
		siocb->scm = &scm;
	}
	siocb->scm->creds = *NETLINK_CREDS(skb);
	skb_free_datagram(sk, skb);

	if (nlk->cb && atomic_read(&sk->sk_rmem_alloc) <= sk->sk_rcvbuf / 2)
		netlink_dump(sk);

	scm_recv(sock, msg, siocb->scm, flags);

out:
	netlink_rcv_wake(sk);
	return err ? : copied;
}

static void netlink_data_ready(struct sock *sk, int len)
{
	struct netlink_opt *nlk = nlk_sk(sk);

	if (nlk->data_ready)
		nlk->data_ready(sk, len);
	netlink_rcv_wake(sk);
}

/*
 *	We export these functions to other modules. They provide a 
 *	complete set of kernel non-blocking support for message
 *	queueing.
 */

struct sock *
netlink_kernel_create(int unit, void (*input)(struct sock *sk, int len))
{
	struct socket *sock;
	struct sock *sk;

	if (unit<0 || unit>=MAX_LINKS)
		return NULL;

	if (sock_create_lite(PF_NETLINK, SOCK_DGRAM, unit, &sock))
		return NULL;

	if (netlink_create(sock, unit) < 0) {
		sock_release(sock);
		return NULL;
	}
	sk = sock->sk;
	sk->sk_data_ready = netlink_data_ready;
	if (input)
		nlk_sk(sk)->data_ready = input;

	netlink_insert(sk, 0);
	return sk;
}

void netlink_set_nonroot(int protocol, unsigned flags)
{ 
	if ((unsigned)protocol < MAX_LINKS) 
		nl_nonroot[protocol] = flags;
} 

static void netlink_destroy_callback(struct netlink_callback *cb)
{
	if (cb->skb)
		kfree_skb(cb->skb);
	kfree(cb);
}

/*
 * It looks a bit ugly.
 * It would be better to create kernel thread.
 */

static int netlink_dump(struct sock *sk)
{
	struct netlink_opt *nlk = nlk_sk(sk);
	struct netlink_callback *cb;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int len;
	
	skb = sock_rmalloc(sk, NLMSG_GOODSIZE, 0, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	spin_lock(&nlk->cb_lock);

	cb = nlk->cb;
	if (cb == NULL) {
		spin_unlock(&nlk->cb_lock);
		kfree_skb(skb);
		return -EINVAL;
	}

	len = cb->dump(skb, cb);

	if (len > 0) {
		spin_unlock(&nlk->cb_lock);
		skb_queue_tail(&sk->sk_receive_queue, skb);
		sk->sk_data_ready(sk, len);
		return 0;
	}

	nlh = __nlmsg_put(skb, NETLINK_CB(cb->skb).pid, cb->nlh->nlmsg_seq, NLMSG_DONE, sizeof(int));
	nlh->nlmsg_flags |= NLM_F_MULTI;
	memcpy(NLMSG_DATA(nlh), &len, sizeof(len));
	skb_queue_tail(&sk->sk_receive_queue, skb);
	sk->sk_data_ready(sk, skb->len);

	cb->done(cb);
	nlk->cb = NULL;
	spin_unlock(&nlk->cb_lock);

	netlink_destroy_callback(cb);
	sock_put(sk);
	return 0;
}

int netlink_dump_start(struct sock *ssk, struct sk_buff *skb,
		       struct nlmsghdr *nlh,
		       int (*dump)(struct sk_buff *skb, struct netlink_callback*),
		       int (*done)(struct netlink_callback*))
{
	struct netlink_callback *cb;
	struct sock *sk;
	struct netlink_opt *nlk;

	cb = kmalloc(sizeof(*cb), GFP_KERNEL);
	if (cb == NULL)
		return -ENOBUFS;

	memset(cb, 0, sizeof(*cb));
	cb->dump = dump;
	cb->done = done;
	cb->nlh = nlh;
	atomic_inc(&skb->users);
	cb->skb = skb;

	sk = netlink_lookup(ssk->sk_protocol, NETLINK_CB(skb).pid);
	if (sk == NULL) {
		netlink_destroy_callback(cb);
		return -ECONNREFUSED;
	}
	nlk = nlk_sk(sk);
	/* A dump is in progress... */
	spin_lock(&nlk->cb_lock);
	if (nlk->cb) {
		spin_unlock(&nlk->cb_lock);
		netlink_destroy_callback(cb);
		sock_put(sk);
		return -EBUSY;
	}
	nlk->cb = cb;
	spin_unlock(&nlk->cb_lock);

	netlink_dump(sk);
	return 0;
}

void netlink_ack(struct sk_buff *in_skb, struct nlmsghdr *nlh, int err)
{
	struct sk_buff *skb;
	struct nlmsghdr *rep;
	struct nlmsgerr *errmsg;
	int size;

	if (err == 0)
		size = NLMSG_SPACE(sizeof(struct nlmsgerr));
	else
		size = NLMSG_SPACE(4 + NLMSG_ALIGN(nlh->nlmsg_len));

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb) {
		struct sock *sk;

		sk = netlink_lookup(in_skb->sk->sk_protocol,
				    NETLINK_CB(in_skb).pid);
		if (sk) {
			sk->sk_err = ENOBUFS;
			sk->sk_error_report(sk);
			sock_put(sk);
		}
		return;
	}

	rep = __nlmsg_put(skb, NETLINK_CB(in_skb).pid, nlh->nlmsg_seq,
			  NLMSG_ERROR, sizeof(struct nlmsgerr));
	errmsg = NLMSG_DATA(rep);
	errmsg->error = err;
	memcpy(&errmsg->msg, nlh, err ? nlh->nlmsg_len : sizeof(struct nlmsghdr));
	netlink_unicast(in_skb->sk, skb, NETLINK_CB(in_skb).pid, MSG_DONTWAIT);
}


#ifdef NL_EMULATE_DEV

static rwlock_t nl_emu_lock = RW_LOCK_UNLOCKED;

/*
 *	Backward compatibility.
 */	
 
int netlink_attach(int unit, int (*function)(int, struct sk_buff *skb))
{
	struct sock *sk = netlink_kernel_create(unit, NULL);
	if (sk == NULL)
		return -ENOBUFS;
	nlk_sk(sk)->handler = function;
	write_lock_bh(&nl_emu_lock);
	netlink_kernel[unit] = sk->sk_socket;
	write_unlock_bh(&nl_emu_lock);
	return 0;
}

void netlink_detach(int unit)
{
	struct socket *sock;

	write_lock_bh(&nl_emu_lock);
	sock = netlink_kernel[unit];
	netlink_kernel[unit] = NULL;
	write_unlock_bh(&nl_emu_lock);

	sock_release(sock);
}

int netlink_post(int unit, struct sk_buff *skb)
{
	struct socket *sock;

	read_lock(&nl_emu_lock);
	sock = netlink_kernel[unit];
	if (sock) {
		struct sock *sk = sock->sk;
		memset(skb->cb, 0, sizeof(skb->cb));
		sock_hold(sk);
		read_unlock(&nl_emu_lock);

		netlink_broadcast(sk, skb, 0, ~0, GFP_ATOMIC);

		sock_put(sk);
		return 0;
	}
	read_unlock(&nl_emu_lock);
	return -EUNATCH;
}

#endif

#ifdef CONFIG_PROC_FS
static struct sock *netlink_seq_socket_idx(struct seq_file *seq, loff_t pos)
{
	long i;
	struct sock *s;
	struct hlist_node *node;
	loff_t off = 0;

	for (i=0; i<MAX_LINKS; i++) {
		sk_for_each(s, node, &nl_table[i]) {
			if (off == pos) {
				seq->private = (void *) i;
				return s;
			}
			++off;
		}
	}
	return NULL;
}

static void *netlink_seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock(&nl_table_lock);
	return *pos ? netlink_seq_socket_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *netlink_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock *s;

	++*pos;

	if (v == SEQ_START_TOKEN)
		return netlink_seq_socket_idx(seq, 0);
		
	s = sk_next(v);
	if (!s) {
		long i = (long)seq->private;

		while (++i < MAX_LINKS) {
			s = sk_head(&nl_table[i]);
			if (s) {
				seq->private = (void *) i;
				break;
			}
		}
	}
	return s;
}

static void netlink_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&nl_table_lock);
}


static int netlink_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq,
			 "sk       Eth Pid    Groups   "
			 "Rmem     Wmem     Dump     Locks\n");
	else {
		struct sock *s = v;
		struct netlink_opt *nlk = nlk_sk(s);

		seq_printf(seq, "%p %-3d %-6d %08x %-8d %-8d %p %d\n",
			   s,
			   s->sk_protocol,
			   nlk->pid,
			   nlk->groups,
			   atomic_read(&s->sk_rmem_alloc),
			   atomic_read(&s->sk_wmem_alloc),
			   nlk->cb,
			   atomic_read(&s->sk_refcnt)
			);

	}
	return 0;
}

static struct seq_operations netlink_seq_ops = {
	.start  = netlink_seq_start,
	.next   = netlink_seq_next,
	.stop   = netlink_seq_stop,
	.show   = netlink_seq_show,
};


static int netlink_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &netlink_seq_ops);
}

static struct file_operations netlink_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= netlink_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

#endif

int netlink_register_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&netlink_chain, nb);
}

int netlink_unregister_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&netlink_chain, nb);
}
                
static struct proto_ops netlink_ops = {
	.family =	PF_NETLINK,
	.owner =	THIS_MODULE,
	.release =	netlink_release,
	.bind =		netlink_bind,
	.connect =	netlink_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	netlink_getname,
	.poll =		datagram_poll,
	.ioctl =	sock_no_ioctl,
	.listen =	sock_no_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	sock_no_setsockopt,
	.getsockopt =	sock_no_getsockopt,
	.sendmsg =	netlink_sendmsg,
	.recvmsg =	netlink_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};

static struct net_proto_family netlink_family_ops = {
	.family = PF_NETLINK,
	.create = netlink_create,
	.owner	= THIS_MODULE,	/* for consistency 8) */
};

static int __init netlink_proto_init(void)
{
	struct sk_buff *dummy_skb;

	if (sizeof(struct netlink_skb_parms) > sizeof(dummy_skb->cb)) {
		printk(KERN_CRIT "netlink_init: panic\n");
		return -1;
	}
	sock_register(&netlink_family_ops);
#ifdef CONFIG_PROC_FS
	proc_net_fops_create("netlink", 0, &netlink_seq_fops);
#endif
	/* The netlink device handler may be needed early. */ 
	rtnetlink_init();
	return 0;
}

static void __exit netlink_proto_exit(void)
{
       sock_unregister(PF_NETLINK);
       proc_net_remove("netlink");
}

core_initcall(netlink_proto_init);
module_exit(netlink_proto_exit);

MODULE_LICENSE("GPL");

MODULE_ALIAS_NETPROTO(PF_NETLINK);

EXPORT_SYMBOL(netlink_ack);
EXPORT_SYMBOL(netlink_broadcast);
EXPORT_SYMBOL(netlink_broadcast_deliver);
EXPORT_SYMBOL(netlink_dump_start);
EXPORT_SYMBOL(netlink_kernel_create);
EXPORT_SYMBOL(netlink_register_notifier);
EXPORT_SYMBOL(netlink_set_err);
EXPORT_SYMBOL(netlink_set_nonroot);
EXPORT_SYMBOL(netlink_unicast);
EXPORT_SYMBOL(netlink_unregister_notifier);

#if defined(CONFIG_NETLINK_DEV) || defined(CONFIG_NETLINK_DEV_MODULE)
EXPORT_SYMBOL(netlink_attach);
EXPORT_SYMBOL(netlink_detach);
EXPORT_SYMBOL(netlink_post);
#endif
