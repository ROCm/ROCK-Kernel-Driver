/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the AF_INET socket handler.
 *
 * Version:	@(#)sock.h	1.0.4	05/13/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche <flla@stud.uni-sb.de>
 *
 * Fixes:
 *		Alan Cox	:	Volatiles in skbuff pointers. See
 *					skbuff comments. May be overdone,
 *					better to prove they can be removed
 *					than the reverse.
 *		Alan Cox	:	Added a zapped field for tcp to note
 *					a socket is reset and must stay shut up
 *		Alan Cox	:	New fields for options
 *	Pauline Middelink	:	identd support
 *		Alan Cox	:	Eliminate low level recv/recvfrom
 *		David S. Miller	:	New socket lookup architecture.
 *              Steve Whitehouse:       Default routines for sock_ops
 *              Arnaldo C. Melo :	removed net_pinfo, tp_pinfo and made
 *              			protinfo be just a void pointer, as the
 *              			protocol specific parts were moved to
 *              			respective headers and ipv4/v6, etc now
 *              			use private slabcaches for its socks
 *              Pedro Hortas	:	New flags field for socket options
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _SOCK_H
#define _SOCK_H

#include <linux/config.h>
#include <linux/timer.h>
#include <linux/cache.h>

#include <linux/netdevice.h>
#include <linux/skbuff.h>	/* struct sk_buff */
#include <linux/security.h>

#include <linux/filter.h>

#include <asm/atomic.h>
#include <net/dst.h>

/*
 * This structure really needs to be cleaned up.
 * Most of it is for TCP, and not used by any of
 * the other protocols.
 */

/* Sock flags */
enum {
	SOCK_DEAD,
	SOCK_DONE,
	SOCK_URGINLINE,
	SOCK_KEEPOPEN,
	SOCK_LINGER,
	SOCK_DESTROY,
	SOCK_BROADCAST,
};

/* Define this to get the sk->debug debugging facility. */
#define SOCK_DEBUGGING
#ifdef SOCK_DEBUGGING
#define SOCK_DEBUG(sk, msg...) do { if((sk) && ((sk)->debug)) printk(KERN_DEBUG msg); } while (0)
#else
#define SOCK_DEBUG(sk, msg...) do { } while (0)
#endif

/* This is the per-socket lock.  The spinlock provides a synchronization
 * between user contexts and software interrupt processing, whereas the
 * mini-semaphore synchronizes multiple users amongst themselves.
 */
struct sock_iocb;
typedef struct {
	spinlock_t		slock;
	struct sock_iocb	*owner;
	wait_queue_head_t	wq;
} socket_lock_t;

#define sock_lock_init(__sk) \
do {	spin_lock_init(&((__sk)->lock.slock)); \
	(__sk)->lock.owner = NULL; \
	init_waitqueue_head(&((__sk)->lock.wq)); \
} while(0)

struct sock {
	/* Begin of struct sock/struct tcp_tw_bucket shared layout */
	volatile unsigned char	state,		/* Connection state */
				zapped;		/* ax25 & ipx means !linked */
	unsigned char		reuse;		/* SO_REUSEADDR setting */
	unsigned char		shutdown;
	int			bound_dev_if;	/* Bound device index if != 0 */
	/* Main hash linkage for various protocol lookup tables. */
	struct sock		*next;
	struct sock		**pprev;
	struct sock		*bind_next;
	struct sock		**bind_pprev;
	atomic_t		refcnt;		/* Reference count			*/
	unsigned short		family;		/* Address family */
	/* End of struct sock/struct tcp_tw_bucket shared layout */
	unsigned char		use_write_queue;
	unsigned char		userlocks;
	socket_lock_t		lock;		/* Synchronizer...			*/
	int			rcvbuf;		/* Size of receive buffer in bytes	*/

	wait_queue_head_t	*sleep;		/* Sock wait queue			*/
	struct dst_entry	*dst_cache;	/* Destination cache			*/
	rwlock_t		dst_lock;
	struct xfrm_policy	*policy[2];
	atomic_t		rmem_alloc;	/* Receive queue bytes committed	*/
	struct sk_buff_head	receive_queue;	/* Incoming packets			*/
	atomic_t		wmem_alloc;	/* Transmit queue bytes committed	*/
	struct sk_buff_head	write_queue;	/* Packet sending queue			*/
	atomic_t		omem_alloc;	/* "o" is "option" or "other" */
	int			wmem_queued;	/* Persistent queue size */
	int			forward_alloc;	/* Space allocated forward. */
	unsigned int		allocation;	/* Allocation mode			*/
	int			sndbuf;		/* Size of send buffer in bytes		*/
	struct sock		*prev;

	unsigned long 		flags;
	char		 	no_check;
	unsigned char		debug;
	unsigned char		rcvtstamp;
	unsigned char		no_largesend;
	int			route_caps;
	unsigned long	        lingertime;

	int			hashent;
	struct sock		*pair;

	/* The backlog queue is special, it is always used with
	 * the per-socket spinlock held and requires low latency
	 * access.  Therefore we special case it's implementation.
	 */
	struct {
		struct sk_buff *head;
		struct sk_buff *tail;
	} backlog;

	rwlock_t		callback_lock;

	/* Error queue, rarely used. */
	struct sk_buff_head	error_queue;

	struct proto		*prot;

	int			err, err_soft;	/* Soft holds errors that don't
						   cause failure but are the cause
						   of a persistent failure not just
						   'timed out' */
	unsigned short		ack_backlog;
	unsigned short		max_ack_backlog;
	__u32			priority;
	unsigned short		type;
	unsigned char		localroute;	/* Route locally only */
	unsigned char		protocol;
	struct ucred		peercred;
	int			rcvlowat;
	long			rcvtimeo;
	long			sndtimeo;

	/* Socket Filtering Instructions */
	struct sk_filter      	*filter;

	/* This is where all the private (optional) areas that don't
	 * overlap will eventually live. 
	 */
	void *protinfo;

	/* The slabcache this instance was allocated from, it is sk_cachep for most
	 * protocols, but a private slab for protocols such as IPv4, IPv6, SPX
	 * and Unix.
	 */
	kmem_cache_t *slab;

	/* This part is used for the timeout functions. */
	struct timer_list	timer;		/* This is the sock cleanup timer. */
	struct timeval		stamp;

	/* Identd and reporting IO signals */
	struct socket		*socket;

	/* RPC layer private data */
	void			*user_data;
  
	/* Callbacks */
	void			(*state_change)(struct sock *sk);
	void			(*data_ready)(struct sock *sk,int bytes);
	void			(*write_space)(struct sock *sk);
	void			(*error_report)(struct sock *sk);

  	int			(*backlog_rcv) (struct sock *sk,
						struct sk_buff *skb);  
	void                    (*destruct)(struct sock *sk);
};

/* The per-socket spinlock must be held here. */
#define sk_add_backlog(__sk, __skb)			\
do {	if((__sk)->backlog.tail == NULL) {		\
		(__sk)->backlog.head =			\
		     (__sk)->backlog.tail = (__skb);	\
	} else {					\
		((__sk)->backlog.tail)->next = (__skb);	\
		(__sk)->backlog.tail = (__skb);		\
	}						\
	(__skb)->next = NULL;				\
} while(0)

/* IP protocol blocks we attach to sockets.
 * socket layer -> transport layer interface
 * transport -> network interface is defined by struct inet_proto
 */
struct proto {
	void			(*close)(struct sock *sk, 
					long timeout);
	int			(*connect)(struct sock *sk,
				        struct sockaddr *uaddr, 
					int addr_len);
	int			(*disconnect)(struct sock *sk, int flags);

	struct sock *		(*accept) (struct sock *sk, int flags, int *err);

	int			(*ioctl)(struct sock *sk, int cmd,
					 unsigned long arg);
	int			(*init)(struct sock *sk);
	int			(*destroy)(struct sock *sk);
	void			(*shutdown)(struct sock *sk, int how);
	int			(*setsockopt)(struct sock *sk, int level, 
					int optname, char *optval, int optlen);
	int			(*getsockopt)(struct sock *sk, int level, 
					int optname, char *optval, 
					int *option);  	 
	int			(*sendmsg)(struct kiocb *iocb, struct sock *sk,
					   struct msghdr *msg, int len);
	int			(*recvmsg)(struct kiocb *iocb, struct sock *sk,
					   struct msghdr *msg,
					int len, int noblock, int flags, 
					int *addr_len);
	int			(*sendpage)(struct sock *sk, struct page *page,
					int offset, size_t size, int flags);
	int			(*bind)(struct sock *sk, 
					struct sockaddr *uaddr, int addr_len);

	int			(*backlog_rcv) (struct sock *sk, 
						struct sk_buff *skb);

	/* Keeping track of sk's, looking them up, and port selection methods. */
	void			(*hash)(struct sock *sk);
	void			(*unhash)(struct sock *sk);
	int			(*get_port)(struct sock *sk, unsigned short snum);

	char			name[32];

	struct {
		int inuse;
		u8  __pad[SMP_CACHE_BYTES - sizeof(int)];
	} stats[NR_CPUS];
};

/* Called with local bh disabled */
static __inline__ void sock_prot_inc_use(struct proto *prot)
{
	prot->stats[smp_processor_id()].inuse++;
}

static __inline__ void sock_prot_dec_use(struct proto *prot)
{
	prot->stats[smp_processor_id()].inuse--;
}

/* About 10 seconds */
#define SOCK_DESTROY_TIME (10*HZ)

/* Sockets 0-1023 can't be bound to unless you are superuser */
#define PROT_SOCK	1024

#define SHUTDOWN_MASK	3
#define RCV_SHUTDOWN	1
#define SEND_SHUTDOWN	2

#define SOCK_SNDBUF_LOCK	1
#define SOCK_RCVBUF_LOCK	2
#define SOCK_BINDADDR_LOCK	4
#define SOCK_BINDPORT_LOCK	8

/* sock_iocb: used to kick off async processing of socket ios */
struct sock_iocb {
	struct list_head	list;

	int			flags;
	int			size;
	struct socket		*sock;
	struct sock		*sk;
	struct scm_cookie	*scm;
	struct msghdr		*msg, async_msg;
	struct iovec		async_iov;
};

static inline struct sock_iocb *kiocb_to_siocb(struct kiocb *iocb)
{
	BUG_ON(sizeof(struct sock_iocb) > KIOCB_PRIVATE_SIZE);
	return (struct sock_iocb *)iocb->private;
}

static inline struct kiocb *siocb_to_kiocb(struct sock_iocb *si)
{
	return container_of((void *)si, struct kiocb, private);
}

struct socket_alloc {
	struct socket socket;
	struct inode vfs_inode;
};

static inline struct socket *SOCKET_I(struct inode *inode)
{
	return &container_of(inode, struct socket_alloc, vfs_inode)->socket;
}

static inline struct inode *SOCK_INODE(struct socket *socket)
{
	return &container_of(socket, struct socket_alloc, socket)->vfs_inode;
}

/* Used by processes to "lock" a socket state, so that
 * interrupts and bottom half handlers won't change it
 * from under us. It essentially blocks any incoming
 * packets, so that we won't get any new data or any
 * packets that change the state of the socket.
 *
 * While locked, BH processing will add new packets to
 * the backlog queue.  This queue is processed by the
 * owner of the socket lock right before it is released.
 *
 * Since ~2.3.5 it is also exclusive sleep lock serializing
 * accesses from user process context.
 */
extern void __lock_sock(struct sock *sk);
extern void __release_sock(struct sock *sk);
#define sock_owned_by_user(sk)	(NULL != (sk)->lock.owner)
#define lock_sock(__sk) \
do {	might_sleep(); \
	spin_lock_bh(&((__sk)->lock.slock)); \
	if ((__sk)->lock.owner != NULL) \
		__lock_sock(__sk); \
	(__sk)->lock.owner = (void *)1; \
	spin_unlock_bh(&((__sk)->lock.slock)); \
} while(0)

#define release_sock(__sk) \
do {	spin_lock_bh(&((__sk)->lock.slock)); \
	if ((__sk)->backlog.tail != NULL) \
		__release_sock(__sk); \
	(__sk)->lock.owner = NULL; \
        if (waitqueue_active(&((__sk)->lock.wq))) wake_up(&((__sk)->lock.wq)); \
	spin_unlock_bh(&((__sk)->lock.slock)); \
} while(0)

/* BH context may only use the following locking interface. */
#define bh_lock_sock(__sk)	spin_lock(&((__sk)->lock.slock))
#define bh_unlock_sock(__sk)	spin_unlock(&((__sk)->lock.slock))

extern struct sock *		sk_alloc(int family, int priority, int zero_it,
					 kmem_cache_t *slab);
extern void			sk_free(struct sock *sk);

extern struct sk_buff		*sock_wmalloc(struct sock *sk,
					      unsigned long size, int force,
					      int priority);
extern struct sk_buff		*sock_rmalloc(struct sock *sk,
					      unsigned long size, int force,
					      int priority);
extern void			sock_wfree(struct sk_buff *skb);
extern void			sock_rfree(struct sk_buff *skb);

extern int			sock_setsockopt(struct socket *sock, int level,
						int op, char *optval,
						int optlen);

extern int			sock_getsockopt(struct socket *sock, int level,
						int op, char *optval, 
						int *optlen);
extern struct sk_buff 		*sock_alloc_send_skb(struct sock *sk,
						     unsigned long size,
						     int noblock,
						     int *errcode);
extern struct sk_buff 		*sock_alloc_send_pskb(struct sock *sk,
						      unsigned long header_len,
						      unsigned long data_len,
						      int noblock,
						      int *errcode);
extern void *sock_kmalloc(struct sock *sk, int size, int priority);
extern void sock_kfree_s(struct sock *sk, void *mem, int size);
extern void sk_send_sigurg(struct sock *sk);

/*
 * Functions to fill in entries in struct proto_ops when a protocol
 * does not implement a particular function.
 */
extern int                      sock_no_release(struct socket *);
extern int                      sock_no_bind(struct socket *, 
					     struct sockaddr *, int);
extern int                      sock_no_connect(struct socket *,
						struct sockaddr *, int, int);
extern int                      sock_no_socketpair(struct socket *,
						   struct socket *);
extern int                      sock_no_accept(struct socket *,
					       struct socket *, int);
extern int                      sock_no_getname(struct socket *,
						struct sockaddr *, int *, int);
extern unsigned int             sock_no_poll(struct file *, struct socket *,
					     struct poll_table_struct *);
extern int                      sock_no_ioctl(struct socket *, unsigned int,
					      unsigned long);
extern int			sock_no_listen(struct socket *, int);
extern int                      sock_no_shutdown(struct socket *, int);
extern int			sock_no_getsockopt(struct socket *, int , int,
						   char *, int *);
extern int			sock_no_setsockopt(struct socket *, int, int,
						   char *, int);
extern int                      sock_no_sendmsg(struct kiocb *, struct socket *,
						struct msghdr *, int);
extern int                      sock_no_recvmsg(struct kiocb *, struct socket *,
						struct msghdr *, int, int);
extern int			sock_no_mmap(struct file *file,
					     struct socket *sock,
					     struct vm_area_struct *vma);
extern ssize_t			sock_no_sendpage(struct socket *sock,
						struct page *page,
						int offset, size_t size, 
						int flags);

/*
 *	Default socket callbacks and setup code
 */
 
extern void sock_def_destruct(struct sock *);

/* Initialise core socket variables */
extern void sock_init_data(struct socket *sock, struct sock *sk);

/**
 *	__sk_filter - run a packet through a socket filter
 *	@sk: sock associated with &sk_buff
 *	@skb: buffer to filter
 *	@needlock: set to 1 if the sock is not locked by caller.
 *
 * Run the filter code and then cut skb->data to correct size returned by
 * sk_run_filter. If pkt_len is 0 we toss packet. If skb->len is smaller
 * than pkt_len we keep whole skb->data. This is the socket level
 * wrapper to sk_run_filter. It returns 0 if the packet should
 * be accepted or -EPERM if the packet should be tossed.
 *
 * This function should not be called directly, use sk_filter instead
 * to ensure that the LSM security check is also performed.
 */

static inline int __sk_filter(struct sock *sk, struct sk_buff *skb, int needlock)
{
	int err = 0;

	if (sk->filter) {
		struct sk_filter *filter;
		
		if (needlock)
			bh_lock_sock(sk);
		
		filter = sk->filter;
		if (filter) {
			int pkt_len = sk_run_filter(skb, filter->insns,
						    filter->len);
			if (!pkt_len)
				err = -EPERM;
			else
				skb_trim(skb, pkt_len);
		}

		if (needlock)
			bh_unlock_sock(sk);
	}
	return err;
}

/**
 *	sk_filter_release: Release a socket filter
 *	@sk: socket
 *	@fp: filter to remove
 *
 *	Remove a filter from a socket and release its resources.
 */
 
static inline void sk_filter_release(struct sock *sk, struct sk_filter *fp)
{
	unsigned int size = sk_filter_len(fp);

	atomic_sub(size, &sk->omem_alloc);

	if (atomic_dec_and_test(&fp->refcnt))
		kfree(fp);
}

static inline void sk_filter_charge(struct sock *sk, struct sk_filter *fp)
{
	atomic_inc(&fp->refcnt);
	atomic_add(sk_filter_len(fp), &sk->omem_alloc);
}

static inline int sk_filter(struct sock *sk, struct sk_buff *skb, int needlock)
{
	int err;
	
	err = security_sock_rcv_skb(sk, skb);
	if (err)
		return err;
	
	return __sk_filter(sk, skb, needlock);
}

/*
 * Socket reference counting postulates.
 *
 * * Each user of socket SHOULD hold a reference count.
 * * Each access point to socket (an hash table bucket, reference from a list,
 *   running timer, skb in flight MUST hold a reference count.
 * * When reference count hits 0, it means it will never increase back.
 * * When reference count hits 0, it means that no references from
 *   outside exist to this socket and current process on current CPU
 *   is last user and may/should destroy this socket.
 * * sk_free is called from any context: process, BH, IRQ. When
 *   it is called, socket has no references from outside -> sk_free
 *   may release descendant resources allocated by the socket, but
 *   to the time when it is called, socket is NOT referenced by any
 *   hash tables, lists etc.
 * * Packets, delivered from outside (from network or from another process)
 *   and enqueued on receive/error queues SHOULD NOT grab reference count,
 *   when they sit in queue. Otherwise, packets will leak to hole, when
 *   socket is looked up by one cpu and unhasing is made by another CPU.
 *   It is true for udp/raw, netlink (leak to receive and error queues), tcp
 *   (leak to backlog). Packet socket does all the processing inside
 *   BR_NETPROTO_LOCK, so that it has not this race condition. UNIX sockets
 *   use separate SMP lock, so that they are prone too.
 */

/* Grab socket reference count. This operation is valid only
   when sk is ALREADY grabbed f.e. it is found in hash table
   or a list and the lookup is made under lock preventing hash table
   modifications.
 */

static inline void sock_hold(struct sock *sk)
{
	atomic_inc(&sk->refcnt);
}

/* Ungrab socket in the context, which assumes that socket refcnt
   cannot hit zero, f.e. it is true in context of any socketcall.
 */
static inline void __sock_put(struct sock *sk)
{
	atomic_dec(&sk->refcnt);
}

/* Ungrab socket and destroy it, if it was the last reference. */
static inline void sock_put(struct sock *sk)
{
	if (atomic_dec_and_test(&sk->refcnt))
		sk_free(sk);
}

/* Detach socket from process context.
 * Announce socket dead, detach it from wait queue and inode.
 * Note that parent inode held reference count on this struct sock,
 * we do not release it in this function, because protocol
 * probably wants some additional cleanups or even continuing
 * to work with this socket (TCP).
 */
static inline void sock_orphan(struct sock *sk)
{
	write_lock_bh(&sk->callback_lock);
	__set_bit(SOCK_DEAD, &sk->flags);
	sk->socket = NULL;
	sk->sleep = NULL;
	write_unlock_bh(&sk->callback_lock);
}

static inline void sock_graft(struct sock *sk, struct socket *parent)
{
	write_lock_bh(&sk->callback_lock);
	sk->sleep = &parent->wait;
	parent->sk = sk;
	sk->socket = parent;
	write_unlock_bh(&sk->callback_lock);
}

static inline int sock_i_uid(struct sock *sk)
{
	int uid;

	read_lock(&sk->callback_lock);
	uid = sk->socket ? SOCK_INODE(sk->socket)->i_uid : 0;
	read_unlock(&sk->callback_lock);
	return uid;
}

static inline unsigned long sock_i_ino(struct sock *sk)
{
	unsigned long ino;

	read_lock(&sk->callback_lock);
	ino = sk->socket ? SOCK_INODE(sk->socket)->i_ino : 0;
	read_unlock(&sk->callback_lock);
	return ino;
}

static inline struct dst_entry *
__sk_dst_get(struct sock *sk)
{
	return sk->dst_cache;
}

static inline struct dst_entry *
sk_dst_get(struct sock *sk)
{
	struct dst_entry *dst;

	read_lock(&sk->dst_lock);
	dst = sk->dst_cache;
	if (dst)
		dst_hold(dst);
	read_unlock(&sk->dst_lock);
	return dst;
}

static inline void
__sk_dst_set(struct sock *sk, struct dst_entry *dst)
{
	struct dst_entry *old_dst;

	old_dst = sk->dst_cache;
	sk->dst_cache = dst;
	dst_release(old_dst);
}

static inline void
sk_dst_set(struct sock *sk, struct dst_entry *dst)
{
	write_lock(&sk->dst_lock);
	__sk_dst_set(sk, dst);
	write_unlock(&sk->dst_lock);
}

static inline void
__sk_dst_reset(struct sock *sk)
{
	struct dst_entry *old_dst;

	old_dst = sk->dst_cache;
	sk->dst_cache = NULL;
	dst_release(old_dst);
}

static inline void
sk_dst_reset(struct sock *sk)
{
	write_lock(&sk->dst_lock);
	__sk_dst_reset(sk);
	write_unlock(&sk->dst_lock);
}

static inline struct dst_entry *
__sk_dst_check(struct sock *sk, u32 cookie)
{
	struct dst_entry *dst = sk->dst_cache;

	if (dst && dst->obsolete && dst->ops->check(dst, cookie) == NULL) {
		sk->dst_cache = NULL;
		return NULL;
	}

	return dst;
}

static inline struct dst_entry *
sk_dst_check(struct sock *sk, u32 cookie)
{
	struct dst_entry *dst = sk_dst_get(sk);

	if (dst && dst->obsolete && dst->ops->check(dst, cookie) == NULL) {
		sk_dst_reset(sk);
		return NULL;
	}

	return dst;
}


/*
 * 	Queue a received datagram if it will fit. Stream and sequenced
 *	protocols can't normally use this as they need to fit buffers in
 *	and play with them.
 *
 * 	Inlined as it's very short and called for pretty much every
 *	packet ever received.
 */

static inline void skb_set_owner_w(struct sk_buff *skb, struct sock *sk)
{
	sock_hold(sk);
	skb->sk = sk;
	skb->destructor = sock_wfree;
	atomic_add(skb->truesize, &sk->wmem_alloc);
}

static inline void skb_set_owner_r(struct sk_buff *skb, struct sock *sk)
{
	skb->sk = sk;
	skb->destructor = sock_rfree;
	atomic_add(skb->truesize, &sk->rmem_alloc);
}

static inline int sock_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	int err = 0;

	/* Cast skb->rcvbuf to unsigned... It's pointless, but reduces
	   number of warnings when compiling with -W --ANK
	 */
	if (atomic_read(&sk->rmem_alloc) + skb->truesize >= (unsigned)sk->rcvbuf) {
		err = -ENOMEM;
		goto out;
	}

	/* It would be deadlock, if sock_queue_rcv_skb is used
	   with socket lock! We assume that users of this
	   function are lock free.
	*/
	err = sk_filter(sk, skb, 1);
	if (err)
		goto out;

	skb->dev = NULL;
	skb_set_owner_r(skb, sk);
	skb_queue_tail(&sk->receive_queue, skb);
	if (!test_bit(SOCK_DEAD, &sk->flags))
		sk->data_ready(sk,skb->len);
out:
	return err;
}

static inline int sock_queue_err_skb(struct sock *sk, struct sk_buff *skb)
{
	/* Cast skb->rcvbuf to unsigned... It's pointless, but reduces
	   number of warnings when compiling with -W --ANK
	 */
	if (atomic_read(&sk->rmem_alloc) + skb->truesize >= (unsigned)sk->rcvbuf)
		return -ENOMEM;
	skb_set_owner_r(skb, sk);
	skb_queue_tail(&sk->error_queue,skb);
	if (!test_bit(SOCK_DEAD, &sk->flags))
		sk->data_ready(sk,skb->len);
	return 0;
}

/*
 *	Recover an error report and clear atomically
 */
 
static inline int sock_error(struct sock *sk)
{
	int err=xchg(&sk->err,0);
	return -err;
}

static inline unsigned long sock_wspace(struct sock *sk)
{
	int amt = 0;

	if (!(sk->shutdown & SEND_SHUTDOWN)) {
		amt = sk->sndbuf - atomic_read(&sk->wmem_alloc);
		if (amt < 0) 
			amt = 0;
	}
	return amt;
}

static inline void sk_wake_async(struct sock *sk, int how, int band)
{
	if (sk->socket && sk->socket->fasync_list)
		sock_wake_async(sk->socket, how, band);
}

#define SOCK_MIN_SNDBUF 2048
#define SOCK_MIN_RCVBUF 256

/*
 *	Default write policy as shown to user space via poll/select/SIGIO
 */
static inline int sock_writeable(struct sock *sk) 
{
	return atomic_read(&sk->wmem_alloc) < (sk->sndbuf / 2);
}

static inline int gfp_any(void)
{
	return in_softirq() ? GFP_ATOMIC : GFP_KERNEL;
}

static inline long sock_rcvtimeo(struct sock *sk, int noblock)
{
	return noblock ? 0 : sk->rcvtimeo;
}

static inline long sock_sndtimeo(struct sock *sk, int noblock)
{
	return noblock ? 0 : sk->sndtimeo;
}

static inline int sock_rcvlowat(struct sock *sk, int waitall, int len)
{
	return (waitall ? len : min_t(int, sk->rcvlowat, len)) ? : 1;
}

/* Alas, with timeout socket operations are not restartable.
 * Compare this to poll().
 */
static inline int sock_intr_errno(long timeo)
{
	return timeo == MAX_SCHEDULE_TIMEOUT ? -ERESTARTSYS : -EINTR;
}

static __inline__ void
sock_recv_timestamp(struct msghdr *msg, struct sock *sk, struct sk_buff *skb)
{
	if (sk->rcvtstamp)
		put_cmsg(msg, SOL_SOCKET, SO_TIMESTAMP, sizeof(skb->stamp), &skb->stamp);
	else
		sk->stamp = skb->stamp;
}

/* 
 *	Enable debug/info messages 
 */

#if 0
#define NETDEBUG(x)	do { } while (0)
#else
#define NETDEBUG(x)	do { x; } while (0)
#endif

/*
 * Macros for sleeping on a socket. Use them like this:
 *
 * SOCK_SLEEP_PRE(sk)
 * if (condition)
 * 	schedule();
 * SOCK_SLEEP_POST(sk)
 *
 */

#define SOCK_SLEEP_PRE(sk) 	{ struct task_struct *tsk = current; \
				DECLARE_WAITQUEUE(wait, tsk); \
				tsk->state = TASK_INTERRUPTIBLE; \
				add_wait_queue((sk)->sleep, &wait); \
				release_sock(sk);

#define SOCK_SLEEP_POST(sk)	tsk->state = TASK_RUNNING; \
				remove_wait_queue((sk)->sleep, &wait); \
				lock_sock(sk); \
				}

static inline void sock_valbool_flag(struct sock *sk, int bit, int valbool)
{
	if (valbool)
		__set_bit(bit, &sk->flags);
	else
		__clear_bit(bit, &sk->flags);
}

extern __u32 sysctl_wmem_max;
extern __u32 sysctl_rmem_max;

#endif	/* _SOCK_H */
