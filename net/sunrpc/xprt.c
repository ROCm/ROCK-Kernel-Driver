/*
 *  linux/net/sunrpc/xprt.c
 *
 *  This is a generic RPC call interface supporting congestion avoidance,
 *  and asynchronous calls.
 *
 *  The interface works like this:
 *
 *  -	When a process places a call, it allocates a request slot if
 *	one is available. Otherwise, it sleeps on the backlog queue
 *	(xprt_reserve).
 *  -	Next, the caller puts together the RPC message, stuffs it into
 *	the request struct, and calls xprt_call().
 *  -	xprt_call transmits the message and installs the caller on the
 *	socket's wait list. At the same time, it installs a timer that
 *	is run after the packet's timeout has expired.
 *  -	When a packet arrives, the data_ready handler walks the list of
 *	pending requests for that socket. If a matching XID is found, the
 *	caller is woken up, and the timer removed.
 *  -	When no reply arrives within the timeout interval, the timer is
 *	fired by the kernel and runs xprt_timer(). It either adjusts the
 *	timeout values (minor timeout) or wakes up the caller with a status
 *	of -ETIMEDOUT.
 *  -	When the caller receives a notification from RPC that a reply arrived,
 *	it should release the RPC slot, and process the reply.
 *	If the call timed out, it may choose to retry the operation by
 *	adjusting the initial timeout value, and simply calling rpc_call
 *	again.
 *
 *  Support for async RPC is done through a set of RPC-specific scheduling
 *  primitives that `transparently' work for processes as well as async
 *  tasks that rely on callbacks.
 *
 *  Copyright (C) 1995-1997, Olaf Kirch <okir@monad.swb.de>
 *
 *  TCP callback races fixes (C) 1998 Red Hat Software <alan@redhat.com>
 *  TCP send fixes (C) 1998 Red Hat Software <alan@redhat.com>
 *  TCP NFS related read + write fixes
 *   (C) 1999 Dave Airlie, University of Limerick, Ireland <airlied@linux.ie>
 *
 *  Rewrite of larges part of the code in order to stabilize TCP stuff.
 *  Fix behaviour when socket buffer is full.
 *   (C) 1999 Trond Myklebust <trond.myklebust@fys.uio.no>
 */

#define __KERNEL_SYSCALLS__

#include <linux/version.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/udp.h>
#include <linux/unistd.h>
#include <linux/sunrpc/clnt.h>
#include <linux/file.h>

#include <net/sock.h>
#include <net/checksum.h>
#include <net/udp.h>

#include <asm/uaccess.h>

/* Following value should be > 32k + RPC overhead */
#define XPRT_MIN_WRITE_SPACE (35000 + SOCK_MIN_WRITE_SPACE)

extern spinlock_t rpc_queue_lock;

/*
 * Local variables
 */

#ifdef RPC_DEBUG
# undef  RPC_DEBUG_DATA
# define RPCDBG_FACILITY	RPCDBG_XPRT
#endif

/*
 * Local functions
 */
static void	xprt_request_init(struct rpc_task *, struct rpc_xprt *);
static void	do_xprt_transmit(struct rpc_task *);
static void	xprt_reserve_status(struct rpc_task *task);
static void	xprt_disconnect(struct rpc_xprt *);
static void	xprt_reconn_status(struct rpc_task *task);
static struct socket *xprt_create_socket(int, struct rpc_timeout *);
static int	xprt_bind_socket(struct rpc_xprt *, struct socket *);
static void	xprt_remove_pending(struct rpc_xprt *);

#ifdef RPC_DEBUG_DATA
/*
 * Print the buffer contents (first 128 bytes only--just enough for
 * diropres return).
 */
static void
xprt_pktdump(char *msg, u32 *packet, unsigned int count)
{
	u8	*buf = (u8 *) packet;
	int	j;

	dprintk("RPC:      %s\n", msg);
	for (j = 0; j < count && j < 128; j += 4) {
		if (!(j & 31)) {
			if (j)
				dprintk("\n");
			dprintk("0x%04x ", j);
		}
		dprintk("%02x%02x%02x%02x ",
			buf[j], buf[j+1], buf[j+2], buf[j+3]);
	}
	dprintk("\n");
}
#else
static inline void
xprt_pktdump(char *msg, u32 *packet, unsigned int count)
{
	/* NOP */
}
#endif

/*
 * Look up RPC transport given an INET socket
 */
static inline struct rpc_xprt *
xprt_from_sock(struct sock *sk)
{
	return (struct rpc_xprt *) sk->user_data;
}

/*
 *	Adjust the iovec to move on 'n' bytes
 */
 
extern inline void
xprt_move_iov(struct msghdr *msg, struct iovec *niv, unsigned amount)
{
	struct iovec *iv=msg->msg_iov;
	int i;
	
	/*
	 *	Eat any sent iovecs
	 */
	while (iv->iov_len <= amount) {
		amount -= iv->iov_len;
		iv++;
		msg->msg_iovlen--;
	}

	/*
	 *	And chew down the partial one
	 */
	niv[0].iov_len = iv->iov_len-amount;
	niv[0].iov_base =((unsigned char *)iv->iov_base)+amount;
	iv++;

	/*
	 *	And copy any others
	 */
	for(i = 1; i < msg->msg_iovlen; i++)
		niv[i]=*iv++;

	msg->msg_iov=niv;
}

/*
 * Serialize write access to sockets, in order to prevent different
 * requests from interfering with each other.
 * Also prevents TCP socket reconnections from colliding with writes.
 */
static int
xprt_lock_write(struct rpc_xprt *xprt, struct rpc_task *task)
{
	int retval;
	spin_lock_bh(&xprt->sock_lock);
	if (!xprt->snd_task)
		xprt->snd_task = task;
	else if (xprt->snd_task != task) {
		dprintk("RPC: %4d TCP write queue full (task %d)\n",
			task->tk_pid, xprt->snd_task->tk_pid);
		task->tk_timeout = 0;
		task->tk_status = -EAGAIN;
		rpc_sleep_on(&xprt->sending, task, NULL, NULL);
	}
	retval = xprt->snd_task == task;
	spin_unlock_bh(&xprt->sock_lock);
	return retval;
}

/*
 * Releases the socket for use by other requests.
 */
static void
xprt_release_write(struct rpc_xprt *xprt, struct rpc_task *task)
{
	spin_lock_bh(&xprt->sock_lock);
	if (xprt->snd_task == task) {
		xprt->snd_task = NULL;
		rpc_wake_up_next(&xprt->sending);
	}
	spin_unlock_bh(&xprt->sock_lock);
}

/*
 * Write data to socket.
 */
static inline int
xprt_sendmsg(struct rpc_xprt *xprt, struct rpc_rqst *req)
{
	struct socket	*sock = xprt->sock;
	struct msghdr	msg;
	mm_segment_t	oldfs;
	int		result;
	int		slen = req->rq_slen - req->rq_bytes_sent;
	struct iovec	niv[MAX_IOVEC];

	if (slen <= 0)
		return 0;

	if (!sock)
		return -ENOTCONN;

	xprt_pktdump("packet data:",
				req->rq_svec->iov_base,
				req->rq_svec->iov_len);

	msg.msg_flags   = MSG_DONTWAIT|MSG_NOSIGNAL;
	msg.msg_iov	= req->rq_svec;
	msg.msg_iovlen	= req->rq_snr;
	msg.msg_name	= (struct sockaddr *) &xprt->addr;
	msg.msg_namelen = sizeof(xprt->addr);
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	/* Dont repeat bytes */
	if (req->rq_bytes_sent)
		xprt_move_iov(&msg, niv, req->rq_bytes_sent);

	oldfs = get_fs(); set_fs(get_ds());
	result = sock_sendmsg(sock, &msg, slen);
	set_fs(oldfs);

	dprintk("RPC:      xprt_sendmsg(%d) = %d\n", slen, result);

	if (result >= 0)
		return result;

	switch (result) {
	case -ECONNREFUSED:
		/* When the server has died, an ICMP port unreachable message
		 * prompts ECONNREFUSED.
		 */
		break;
	case -EAGAIN:
		if (test_bit(SOCK_NOSPACE, &sock->flags))
			result = -ENOMEM;
		break;
	case -ENOTCONN:
	case -EPIPE:
		/* connection broken */
		if (xprt->stream)
			result = -ENOTCONN;
		break;
	default:
		printk(KERN_NOTICE "RPC: sendmsg returned error %d\n", -result);
	}
	return result;
}

/*
 * Read data from socket
 */
static int
xprt_recvmsg(struct rpc_xprt *xprt, struct iovec *iov, int nr, unsigned len, unsigned shift)
{
	struct socket	*sock = xprt->sock;
	struct msghdr	msg;
	mm_segment_t	oldfs;
	struct iovec	niv[MAX_IOVEC];
	int		result;

	if (!sock)
		return -ENOTCONN;

	msg.msg_flags   = MSG_DONTWAIT|MSG_NOSIGNAL;
	msg.msg_iov	= iov;
	msg.msg_iovlen	= nr;
	msg.msg_name	= NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	/* Adjust the iovec if we've already filled it */
	if (shift)
		xprt_move_iov(&msg, niv, shift);

	oldfs = get_fs(); set_fs(get_ds());
	result = sock_recvmsg(sock, &msg, len, MSG_DONTWAIT);
	set_fs(oldfs);

	dprintk("RPC:      xprt_recvmsg(iov %p, len %d) = %d\n",
						iov, len, result);
	return result;
}


/*
 * Adjust RPC congestion window
 * We use a time-smoothed congestion estimator to avoid heavy oscillation.
 */
static void
xprt_adjust_cwnd(struct rpc_xprt *xprt, int result)
{
	unsigned long	cwnd;

	if (xprt->nocong)
		return;
	/*
	 * Note: we're in a BH context
	 */
	spin_lock(&xprt->xprt_lock);
	cwnd = xprt->cwnd;
	if (result >= 0) {
		if (xprt->cong < cwnd || time_before(jiffies, xprt->congtime))
			goto out;
		/* The (cwnd >> 1) term makes sure
		 * the result gets rounded properly. */
		cwnd += (RPC_CWNDSCALE * RPC_CWNDSCALE + (cwnd >> 1)) / cwnd;
		if (cwnd > RPC_MAXCWND)
			cwnd = RPC_MAXCWND;
		else
			pprintk("RPC: %lu %ld cwnd\n", jiffies, cwnd);
		xprt->congtime = jiffies + ((cwnd * HZ) << 2) / RPC_CWNDSCALE;
		dprintk("RPC:      cong %08lx, cwnd was %08lx, now %08lx, "
			"time %ld ms\n", xprt->cong, xprt->cwnd, cwnd,
			(xprt->congtime-jiffies)*1000/HZ);
	} else if (result == -ETIMEDOUT) {
		if ((cwnd >>= 1) < RPC_CWNDSCALE)
			cwnd = RPC_CWNDSCALE;
		xprt->congtime = jiffies + ((cwnd * HZ) << 3) / RPC_CWNDSCALE;
		dprintk("RPC:      cong %ld, cwnd was %ld, now %ld, "
			"time %ld ms\n", xprt->cong, xprt->cwnd, cwnd,
			(xprt->congtime-jiffies)*1000/HZ);
		pprintk("RPC: %lu %ld cwnd\n", jiffies, cwnd);
	}

	xprt->cwnd = cwnd;
 out:
	spin_unlock(&xprt->xprt_lock);
}

/*
 * Adjust timeout values etc for next retransmit
 */
int
xprt_adjust_timeout(struct rpc_timeout *to)
{
	if (to->to_retries > 0) {
		if (to->to_exponential)
			to->to_current <<= 1;
		else
			to->to_current += to->to_increment;
		if (to->to_maxval && to->to_current >= to->to_maxval)
			to->to_current = to->to_maxval;
	} else {
		if (to->to_exponential)
			to->to_initval <<= 1;
		else
			to->to_initval += to->to_increment;
		if (to->to_maxval && to->to_initval >= to->to_maxval)
			to->to_initval = to->to_maxval;
		to->to_current = to->to_initval;
	}

	if (!to->to_current) {
		printk(KERN_WARNING "xprt_adjust_timeout: to_current = 0!\n");
		to->to_current = 5 * HZ;
	}
	pprintk("RPC: %lu %s\n", jiffies,
			to->to_retries? "retrans" : "timeout");
	return to->to_retries-- > 0;
}

/*
 * Close down a transport socket
 */
static void
xprt_close(struct rpc_xprt *xprt)
{
	struct socket	*sock = xprt->sock;
	struct sock	*sk = xprt->inet;

	if (!sk)
		return;

	xprt->inet = NULL;
	xprt->sock = NULL;

	sk->user_data    = NULL;
	sk->data_ready   = xprt->old_data_ready;
	sk->state_change = xprt->old_state_change;
	sk->write_space  = xprt->old_write_space;

	xprt_disconnect(xprt);
	sk->no_check	 = 0;

	sock_release(sock);
	/*
	 *	TCP doesnt require the rpciod now - other things may
	 *	but rpciod handles that not us.
	 */
	if(xprt->stream)
		rpciod_down();
}

/*
 * Mark a transport as disconnected
 */
static void
xprt_disconnect(struct rpc_xprt *xprt)
{
	dprintk("RPC:      disconnected transport %p\n", xprt);
	xprt_clear_connected(xprt);
	xprt_remove_pending(xprt);
	rpc_wake_up_status(&xprt->pending, -ENOTCONN);
}

/*
 * Reconnect a broken TCP connection.
 *
 * Note: This cannot collide with the TCP reads, as both run from rpciod
 */
void
xprt_reconnect(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;
	struct socket	*sock = xprt->sock;
	struct sock	*inet = xprt->inet;
	int		status;

	dprintk("RPC: %4d xprt_reconnect %p connected %d\n",
				task->tk_pid, xprt, xprt_connected(xprt));
	if (xprt->shutdown)
		return;

	if (!xprt->stream)
		return;

	if (!xprt->addr.sin_port) {
		task->tk_status = -EIO;
		return;
	}

	if (!xprt_lock_write(xprt, task))
		return;
	if (xprt_connected(xprt))
		goto out_write;

	status = -ENOTCONN;
	if (!inet) {
		/* Create an unconnected socket */
		if (!(sock = xprt_create_socket(xprt->prot, &xprt->timeout)))
			goto defer;
		xprt_bind_socket(xprt, sock);
		inet = sock->sk;
	}

	xprt_disconnect(xprt);

	/* Reset TCP record info */
	xprt->tcp_offset = 0;
	xprt->tcp_reclen = 0;
	xprt->tcp_copied = 0;
	xprt->tcp_more = 0;

	/* Now connect it asynchronously. */
	dprintk("RPC: %4d connecting new socket\n", task->tk_pid);
	status = sock->ops->connect(sock, (struct sockaddr *) &xprt->addr,
				sizeof(xprt->addr), O_NONBLOCK);

	if (status < 0) {
		switch (status) {
		case -EALREADY:
		case -EINPROGRESS:
			status = 0;
			break;
		case -EISCONN:
		case -EPIPE:
			status = 0;
			xprt_close(xprt);
			goto defer;
		default:
			printk("RPC: TCP connect error %d!\n", -status);
			xprt_close(xprt);
			goto defer;
		}

		dprintk("RPC: %4d connect status %d connected %d\n",
				task->tk_pid, status, xprt_connected(xprt));

		spin_lock_bh(&xprt->sock_lock);
		if (!xprt_connected(xprt)) {
			task->tk_timeout = xprt->timeout.to_maxval;
			rpc_sleep_on(&xprt->sending, task, xprt_reconn_status, NULL);
			spin_unlock_bh(&xprt->sock_lock);
			return;
		}
		spin_unlock_bh(&xprt->sock_lock);
	}
defer:
	if (status < 0) {
		rpc_delay(task, 5*HZ);
		task->tk_status = -ENOTCONN;
	}
 out_write:
	xprt_release_write(xprt, task);
}

/*
 * Reconnect timeout. We just mark the transport as not being in the
 * process of reconnecting, and leave the rest to the upper layers.
 */
static void
xprt_reconn_status(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	dprintk("RPC: %4d xprt_reconn_timeout %d\n",
				task->tk_pid, task->tk_status);

	xprt_release_write(xprt, task);
}

/*
 * Look up the RPC request corresponding to a reply, and then lock it.
 */
static inline struct rpc_rqst *
xprt_lookup_rqst(struct rpc_xprt *xprt, u32 xid)
{
	struct rpc_task	*head, *task;
	struct rpc_rqst	*req;
	int		safe = 0;

	spin_lock_bh(&rpc_queue_lock);
	if ((head = xprt->pending.task) != NULL) {
		task = head;
		do {
			if ((req = task->tk_rqstp) && req->rq_xid == xid)
				goto out;
			task = task->tk_next;
			if (++safe > 100) {
				printk("xprt_lookup_rqst: loop in Q!\n");
				goto out_bad;
			}
		} while (task != head);
	}
	dprintk("RPC:      unknown XID %08x in reply.\n", xid);
 out_bad:
	req = NULL;
 out:
	if (req && !__rpc_lock_task(req->rq_task))
		req = NULL;
	spin_unlock_bh(&rpc_queue_lock);
	return req;
}

/*
 * Complete reply received.
 * The TCP code relies on us to remove the request from xprt->pending.
 */
static inline void
xprt_complete_rqst(struct rpc_xprt *xprt, struct rpc_rqst *req, int copied)
{
	struct rpc_task	*task = req->rq_task;

	/* Adjust congestion window */
	xprt_adjust_cwnd(xprt, copied);

#ifdef RPC_PROFILE
	/* Profile only reads for now */
	if (copied > 1024) {
		static unsigned long	nextstat = 0;
		static unsigned long	pkt_rtt = 0, pkt_len = 0, pkt_cnt = 0;

		pkt_cnt++;
		pkt_len += req->rq_slen + copied;
		pkt_rtt += jiffies - req->rq_xtime;
		if (time_before(nextstat, jiffies)) {
			printk("RPC: %lu %ld cwnd\n", jiffies, xprt->cwnd);
			printk("RPC: %ld %ld %ld %ld stat\n",
					jiffies, pkt_cnt, pkt_len, pkt_rtt);
			pkt_rtt = pkt_len = pkt_cnt = 0;
			nextstat = jiffies + 5 * HZ;
		}
	}
#endif

	dprintk("RPC: %4d has input (%d bytes)\n", task->tk_pid, copied);
	task->tk_status = copied;
	req->rq_received = 1;

	/* ... and wake up the process. */
	rpc_wake_up_task(task);
	return;
}

/*
 * We have set things up such that we perform the checksum of the UDP
 * packet in parallel with the copies into the RPC client iovec.  -DaveM
 */
static int csum_partial_copy_to_page_cache(struct iovec *iov,
					   struct sk_buff *skb,
					   int copied)
{
	int offset = sizeof(struct udphdr);
	__u8 *cur_ptr = iov->iov_base;
	__kernel_size_t cur_len = iov->iov_len;
	unsigned int csum = skb->csum;
	int need_csum = (skb->ip_summed != CHECKSUM_UNNECESSARY);
	int slack = skb->len - copied - sizeof(struct udphdr);

	if (need_csum)
		csum = csum_partial(skb->data, sizeof(struct udphdr), csum);
	while (copied > 0) {
		if (cur_len) {
			int to_move = cur_len;
			if (to_move > copied)
				to_move = copied;
			if (need_csum) {
				unsigned int csum2;

				csum2 = skb_copy_and_csum_bits(skb, offset,
							       cur_ptr,
							       to_move, 0);
				csum = csum_block_add(csum, csum2, offset);
			} else
				skb_copy_bits(skb, offset, cur_ptr, to_move);
			offset += to_move;
			copied -= to_move;
			cur_ptr += to_move;
			cur_len -= to_move;
		}
		if (cur_len <= 0) {
			iov++;
			cur_len = iov->iov_len;
			cur_ptr = iov->iov_base;
		}
	}
	if (need_csum) {
		if (slack > 0) {
			unsigned int csum2;

			csum2 = skb_checksum(skb, offset, slack, 0);
			csum = csum_block_add(csum, csum2, offset);
		}
		if ((unsigned short)csum_fold(csum))
			return -1;
	}
	return 0;
}

/*
 * Input handler for RPC replies. Called from a bottom half and hence
 * atomic.
 */
static void
udp_data_ready(struct sock *sk, int len)
{
	struct rpc_task	*task;
	struct rpc_xprt	*xprt;
	struct rpc_rqst *rovr;
	struct sk_buff	*skb;
	int		err, repsize, copied;

	dprintk("RPC:      udp_data_ready...\n");
	if (!(xprt = xprt_from_sock(sk))) {
		printk("RPC:      udp_data_ready request not found!\n");
		goto out;
	}

	dprintk("RPC:      udp_data_ready client %p\n", xprt);

	if ((skb = skb_recv_datagram(sk, 0, 1, &err)) == NULL)
		goto out;

	if (xprt->shutdown)
		goto dropit;

	repsize = skb->len - sizeof(struct udphdr);
	if (repsize < 4) {
		printk("RPC: impossible RPC reply size %d!\n", repsize);
		goto dropit;
	}

	/* Look up and lock the request corresponding to the given XID */
	rovr = xprt_lookup_rqst(xprt, *(u32 *) (skb->h.raw + sizeof(struct udphdr)));
	if (!rovr)
		goto dropit;
	task = rovr->rq_task;

	dprintk("RPC: %4d received reply\n", task->tk_pid);
	xprt_pktdump("packet data:",
		     (u32 *) (skb->h.raw+sizeof(struct udphdr)), repsize);

	if ((copied = rovr->rq_rlen) > repsize)
		copied = repsize;

	/* Suck it into the iovec, verify checksum if not done by hw. */
	if (csum_partial_copy_to_page_cache(rovr->rq_rvec, skb, copied))
		goto out_unlock;

	/* Something worked... */
	dst_confirm(skb->dst);

	xprt_complete_rqst(xprt, rovr, copied);

 out_unlock:
	rpc_unlock_task(task);

 dropit:
	skb_free_datagram(sk, skb);
 out:
	if (sk->sleep && waitqueue_active(sk->sleep))
		wake_up_interruptible(sk->sleep);
}

/*
 * TCP read fragment marker
 */
static inline int
tcp_read_fraghdr(struct rpc_xprt *xprt)
{
	struct iovec	riov;
	int		want, result;

	if (xprt->tcp_offset >= sizeof(xprt->tcp_recm))
		goto done;

	want = sizeof(xprt->tcp_recm) - xprt->tcp_offset;
	dprintk("RPC:      reading header (%d bytes)\n", want);
	do {
		riov.iov_base = ((u8*) &xprt->tcp_recm) + xprt->tcp_offset;
		riov.iov_len  = want;
		result = xprt_recvmsg(xprt, &riov, 1, want, 0);
		if (result < 0)
			return result;
		xprt->tcp_offset += result;
		want -= result;
	} while (want);

	/* Get the record length and mask out the last fragment bit */
	xprt->tcp_reclen = ntohl(xprt->tcp_recm);
	xprt->tcp_more = (xprt->tcp_reclen & 0x80000000) ? 0 : 1;
	xprt->tcp_reclen &= 0x7fffffff;

	dprintk("RPC:      New record reclen %d morefrags %d\n",
				   xprt->tcp_reclen, xprt->tcp_more);
 done:
	return xprt->tcp_reclen + sizeof(xprt->tcp_recm) - xprt->tcp_offset;
}

/*
 * TCP read xid
 */
static inline int
tcp_read_xid(struct rpc_xprt *xprt, int avail)
{
	struct iovec	riov;
	int		want, result;

	if (xprt->tcp_copied >= sizeof(xprt->tcp_xid) || !avail)
		goto done;
	want = min_t(unsigned int, sizeof(xprt->tcp_xid) - xprt->tcp_copied, avail);
	do {
		dprintk("RPC:      reading xid (%d bytes)\n", want);
		riov.iov_base = ((u8*) &xprt->tcp_xid) + xprt->tcp_copied;
		riov.iov_len  = want;
		result = xprt_recvmsg(xprt, &riov, 1, want, 0);
		if (result < 0)
			return result;
		xprt->tcp_copied += result;
		xprt->tcp_offset += result;
		want  -= result;
		avail -= result;
	} while (want);
 done:
	return avail;
}

/*
 * TCP read and complete request
 */
static inline int
tcp_read_request(struct rpc_xprt *xprt, struct rpc_rqst *req, int avail)
{
	int	want, result;

	if (req->rq_rlen <= xprt->tcp_copied || !avail)
		goto done;
	want = min_t(unsigned int, req->rq_rlen - xprt->tcp_copied, avail);
	do {
		dprintk("RPC: %4d TCP receiving %d bytes\n",
			req->rq_task->tk_pid, want);

		result = xprt_recvmsg(xprt, req->rq_rvec, req->rq_rnr, want, xprt->tcp_copied);
		if (result < 0)
			return result;
		xprt->tcp_copied += result;
		xprt->tcp_offset += result;
		avail  -= result;
		want   -= result;
	} while (want);

 done:
	if (req->rq_rlen > xprt->tcp_copied && xprt->tcp_more)
		return avail;
	dprintk("RPC: %4d received reply complete\n", req->rq_task->tk_pid);
	xprt_complete_rqst(xprt, req, xprt->tcp_copied);

	return avail;
}

/*
 * TCP discard extra bytes from a short read
 */
static inline int
tcp_read_discard(struct rpc_xprt *xprt, int avail)
{
	struct iovec	riov;
	static u8	dummy[64];
	int		want, result = 0;

	while (avail) {
		want = min_t(unsigned int, avail, sizeof(dummy));
		riov.iov_base = dummy;
		riov.iov_len  = want;
		dprintk("RPC:      TCP skipping %d bytes\n", want);
		result = xprt_recvmsg(xprt, &riov, 1, want, 0);
		if (result < 0)
			return result;
		xprt->tcp_offset += result;
		avail  -= result;
	}
	return avail;
}

/*
 * TCP record receive routine
 * This is not the most efficient code since we call recvfrom thrice--
 * first receiving the record marker, then the XID, then the data.
 * 
 * The optimal solution would be a RPC support in the TCP layer, which
 * would gather all data up to the next record marker and then pass us
 * the list of all TCP segments ready to be copied.
 */
static int
tcp_input_record(struct rpc_xprt *xprt)
{
	struct rpc_rqst	*req = NULL;
	struct rpc_task	*task = NULL;
	int		avail, result;

	dprintk("RPC:      tcp_input_record\n");

	if (xprt->shutdown)
		return -EIO;
	if (!xprt_connected(xprt))
		return -ENOTCONN;

	/* Read in a new fragment marker if necessary */
	/* Can we ever really expect to get completely empty fragments? */
	if ((result = tcp_read_fraghdr(xprt)) < 0)
		return result;
	avail = result;

	/* Read in the xid if necessary */
	if ((result = tcp_read_xid(xprt, avail)) < 0)
		return result;
	if (!(avail = result))
		goto out_ok;

	/* Find and lock the request corresponding to this xid */
	req = xprt_lookup_rqst(xprt, xprt->tcp_xid);
	if (req) {
		task = req->rq_task;
		/* Read in the request data */
		result = tcp_read_request(xprt,  req, avail);
		rpc_unlock_task(task);
		if (result < 0)
			return result;
		avail = result;
	}

	/* Skip over any trailing bytes on short reads */
	if ((result = tcp_read_discard(xprt, avail)) < 0)
		return result;

 out_ok:
	dprintk("RPC:      tcp_input_record done (off %d reclen %d copied %d)\n",
			xprt->tcp_offset, xprt->tcp_reclen, xprt->tcp_copied);
	result = xprt->tcp_reclen;
	xprt->tcp_reclen = 0;
	xprt->tcp_offset = 0;
	if (!xprt->tcp_more)
		xprt->tcp_copied = 0;
	return result;
}

/*
 *	TCP task queue stuff
 */
LIST_HEAD(rpc_xprt_pending);	/* List of xprts having pending tcp requests */

static inline
void tcp_rpciod_queue(void)
{
	rpciod_wake_up();
}

int xprt_tcp_pending(void)
{
	int retval;

	spin_lock_bh(&rpc_queue_lock);
	retval = !list_empty(&rpc_xprt_pending);
	spin_unlock_bh(&rpc_queue_lock);
	return retval;
}

static inline
void xprt_append_pending(struct rpc_xprt *xprt)
{
	spin_lock_bh(&rpc_queue_lock);
	if (list_empty(&xprt->rx_pending)) {
		list_add(&xprt->rx_pending, rpc_xprt_pending.prev);
		dprintk("RPC:     xprt queue %p\n", xprt);
		tcp_rpciod_queue();
	}
	spin_unlock_bh(&rpc_queue_lock);
}

static
void xprt_remove_pending(struct rpc_xprt *xprt)
{
	spin_lock_bh(&rpc_queue_lock);
	if (!list_empty(&xprt->rx_pending)) {
		list_del(&xprt->rx_pending);
		INIT_LIST_HEAD(&xprt->rx_pending);
	}
	spin_unlock_bh(&rpc_queue_lock);
}

static inline
struct rpc_xprt *xprt_remove_pending_next(void)
{
	struct rpc_xprt	*xprt = NULL;

	spin_lock_bh(&rpc_queue_lock);
	if (!list_empty(&rpc_xprt_pending)) {
		xprt = list_entry(rpc_xprt_pending.next, struct rpc_xprt, rx_pending);
		list_del(&xprt->rx_pending);
		INIT_LIST_HEAD(&xprt->rx_pending);
	}
	spin_unlock_bh(&rpc_queue_lock);
	return xprt;
}

/*
 *	This is protected from tcp_data_ready and the stack as its run
 *	inside of the RPC I/O daemon
 */
void
__rpciod_tcp_dispatcher(void)
{
	struct rpc_xprt *xprt;
	int safe_retry = 0, result;

	dprintk("rpciod_tcp_dispatcher: Queue Running\n");

	/*
	 *	Empty each pending socket
	 */
	while ((xprt = xprt_remove_pending_next()) != NULL) {
		dprintk("rpciod_tcp_dispatcher: Processing %p\n", xprt);

		do {
			result = tcp_input_record(xprt);
		} while (result >= 0);

		if (safe_retry++ > 200) {
			schedule();
			safe_retry = 0;
		}
	}
}

/*
 *	data_ready callback for TCP. We can't just jump into the
 *	tcp recvmsg functions inside of the network receive bh or
 * 	bad things occur. We queue it to pick up after networking
 *	is done.
 */
 
static void tcp_data_ready(struct sock *sk, int len)
{
	struct rpc_xprt	*xprt;

	dprintk("RPC:      tcp_data_ready...\n");
	if (!(xprt = xprt_from_sock(sk)))
	{
		printk("Not a socket with xprt %p\n", sk);
		goto out;
	}

	if (xprt->shutdown)
		goto out;

	xprt_append_pending(xprt);

	dprintk("RPC:      tcp_data_ready client %p\n", xprt);
	dprintk("RPC:      state %x conn %d dead %d zapped %d\n",
				sk->state, xprt_connected(xprt),
				sk->dead, sk->zapped);
 out:
	if (sk->sleep && waitqueue_active(sk->sleep))
		wake_up_interruptible(sk->sleep);
}


static void
tcp_state_change(struct sock *sk)
{
	struct rpc_xprt	*xprt;

	if (!(xprt = xprt_from_sock(sk)))
		goto out;
	dprintk("RPC:      tcp_state_change client %p...\n", xprt);
	dprintk("RPC:      state %x conn %d dead %d zapped %d\n",
				sk->state, xprt_connected(xprt),
				sk->dead, sk->zapped);

	switch (sk->state) {
	case TCP_ESTABLISHED:
		if (xprt_test_and_set_connected(xprt))
			break;
		spin_lock(&xprt->sock_lock);
		if (xprt->snd_task && xprt->snd_task->tk_rpcwait == &xprt->sending)
			rpc_wake_up_task(xprt->snd_task);
		spin_unlock(&xprt->sock_lock);
		break;
	case TCP_SYN_SENT:
	case TCP_SYN_RECV:
		break;
	default:
		xprt_disconnect(xprt);
		break;
	}
 out:
	if (sk->sleep && waitqueue_active(sk->sleep))
		wake_up_interruptible_all(sk->sleep);
}

/*
 * The following 2 routines allow a task to sleep while socket memory is
 * low.
 */
static void
tcp_write_space(struct sock *sk)
{
	struct rpc_xprt	*xprt;
	struct socket	*sock;

	if (!(xprt = xprt_from_sock(sk)) || !(sock = sk->socket))
		return;
	if (xprt->shutdown)
		return;

	/* Wait until we have enough socket memory */
	if (!sock_writeable(sk))
		return;

	if (!xprt_test_and_set_wspace(xprt)) {
		spin_lock(&xprt->sock_lock);
		if (xprt->snd_task && xprt->snd_task->tk_rpcwait == &xprt->sending)
			rpc_wake_up_task(xprt->snd_task);
		spin_unlock(&xprt->sock_lock);
	}

	if (test_bit(SOCK_NOSPACE, &sock->flags)) {
		if (sk->sleep && waitqueue_active(sk->sleep)) {
			clear_bit(SOCK_NOSPACE, &sock->flags);
			wake_up_interruptible(sk->sleep);
		}
	}
}

static void
udp_write_space(struct sock *sk)
{
	struct rpc_xprt *xprt;

	if (!(xprt = xprt_from_sock(sk)))
		return;
	if (xprt->shutdown)
		return;


	/* Wait until we have enough socket memory */
	if (sock_wspace(sk) < min_t(int, sk->sndbuf,XPRT_MIN_WRITE_SPACE))
		return;

	if (!xprt_test_and_set_wspace(xprt)) {
		spin_lock(&xprt->sock_lock);
		if (xprt->snd_task && xprt->snd_task->tk_rpcwait == &xprt->sending)
			rpc_wake_up_task(xprt->snd_task);
		spin_unlock(&xprt->sock_lock);
	}

	if (sk->sleep && waitqueue_active(sk->sleep))
		wake_up_interruptible(sk->sleep);
}

/*
 * RPC receive timeout handler.
 */
static void
xprt_timer(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;

	if (req)
		xprt_adjust_cwnd(task->tk_xprt, -ETIMEDOUT);

	dprintk("RPC: %4d xprt_timer (%s request)\n",
		task->tk_pid, req ? "pending" : "backlogged");

	task->tk_status  = -ETIMEDOUT;
	task->tk_timeout = 0;
	rpc_wake_up_task(task);
}

/*
 * Place the actual RPC call.
 * We have to copy the iovec because sendmsg fiddles with its contents.
 */
void
xprt_transmit(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt	*xprt = req->rq_xprt;

	dprintk("RPC: %4d xprt_transmit(%x)\n", task->tk_pid, 
				*(u32 *)(req->rq_svec[0].iov_base));

	if (xprt->shutdown)
		task->tk_status = -EIO;

	if (!xprt_connected(xprt))
		task->tk_status = -ENOTCONN;

	if (task->tk_status < 0)
		return;

	if (task->tk_rpcwait)
		rpc_remove_wait_queue(task);

	/* set up everything as needed. */
	/* Write the record marker */
	if (xprt->stream) {
		u32	*marker = req->rq_svec[0].iov_base;

		*marker = htonl(0x80000000|(req->rq_slen-sizeof(*marker)));
	}

	if (!xprt_lock_write(xprt, task))
		return;

#ifdef RPC_PROFILE
	req->rq_xtime = jiffies;
#endif
	do_xprt_transmit(task);
}

static void
do_xprt_transmit(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt	*xprt = req->rq_xprt;
	int status, retry = 0;


	/* For fast networks/servers we have to put the request on
	 * the pending list now:
	 * Note that we don't want the task timing out during the
	 * call to xprt_sendmsg(), so we initially disable the timeout,
	 * and then reset it later...
	 */
	xprt_receive(task);

	/* Continue transmitting the packet/record. We must be careful
	 * to cope with writespace callbacks arriving _after_ we have
	 * called xprt_sendmsg().
	 */
	while (1) {
		xprt_clear_wspace(xprt);
		status = xprt_sendmsg(xprt, req);

		if (status < 0)
			break;

		if (xprt->stream) {
			req->rq_bytes_sent += status;

			if (req->rq_bytes_sent >= req->rq_slen)
				goto out_receive;
		} else {
			if (status >= req->rq_slen)
				goto out_receive;
			status = -ENOMEM;
			break;
		}

		dprintk("RPC: %4d xmit incomplete (%d left of %d)\n",
				task->tk_pid, req->rq_slen - req->rq_bytes_sent,
				req->rq_slen);

		status = -EAGAIN;
		if (retry++ > 50)
			break;
	}
	rpc_unlock_task(task);

	/* Note: at this point, task->tk_sleeping has not yet been set,
	 *	 hence there is no danger of the waking up task being put on
	 *	 schedq, and being picked up by a parallel run of rpciod().
	 */
	rpc_wake_up_task(task);
	if (!RPC_IS_RUNNING(task))
		goto out_release;
	if (req->rq_received)
		goto out_release;

	task->tk_status = status;

	switch (status) {
	case -ENOMEM:
		/* Protect against (udp|tcp)_write_space */
		spin_lock_bh(&xprt->sock_lock);
		if (!xprt_wspace(xprt)) {
			task->tk_timeout = req->rq_timeout.to_current;
			rpc_sleep_on(&xprt->sending, task, NULL, NULL);
		}
		spin_unlock_bh(&xprt->sock_lock);
		return;
	case -EAGAIN:
		/* Keep holding the socket if it is blocked */
		rpc_delay(task, HZ>>4);
		return;
	case -ECONNREFUSED:
	case -ENOTCONN:
		if (!xprt->stream)
			return;
	default:
		if (xprt->stream)
			xprt_disconnect(xprt);
		req->rq_bytes_sent = 0;
		goto out_release;
	}

 out_receive:
	dprintk("RPC: %4d xmit complete\n", task->tk_pid);
	/* Set the task's receive timeout value */
	task->tk_timeout = req->rq_timeout.to_current;
	rpc_add_timer(task, xprt_timer);
	rpc_unlock_task(task);
 out_release:
	xprt_release_write(xprt, task);
}

/*
 * Queue the task for a reply to our call.
 * When the callback is invoked, the congestion window should have
 * been updated already.
 */
void
xprt_receive(struct rpc_task *task)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	struct rpc_xprt	*xprt = req->rq_xprt;

	dprintk("RPC: %4d xprt_receive\n", task->tk_pid);

	req->rq_received = 0;
	task->tk_timeout = 0;
	rpc_sleep_locked(&xprt->pending, task, NULL, NULL);
}

/*
 * Reserve an RPC call slot.
 */
int
xprt_reserve(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;

	/* We already have an initialized request. */
	if (task->tk_rqstp)
		return 0;

	dprintk("RPC: %4d xprt_reserve cong = %ld cwnd = %ld\n",
				task->tk_pid, xprt->cong, xprt->cwnd);
	spin_lock_bh(&xprt->xprt_lock);
	xprt_reserve_status(task);
	if (task->tk_rqstp) {
		task->tk_timeout = 0;
	} else if (!task->tk_timeout) {
		task->tk_status = -ENOBUFS;
	} else {
		dprintk("RPC:      xprt_reserve waiting on backlog\n");
		task->tk_status = -EAGAIN;
		rpc_sleep_on(&xprt->backlog, task, NULL, NULL);
	}
	spin_unlock_bh(&xprt->xprt_lock);
	dprintk("RPC: %4d xprt_reserve returns %d\n",
				task->tk_pid, task->tk_status);
	return task->tk_status;
}

/*
 * Reservation callback
 */
static void
xprt_reserve_status(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;
	struct rpc_rqst	*req;

	if (xprt->shutdown) {
		task->tk_status = -EIO;
	} else if (task->tk_status < 0) {
		/* NOP */
	} else if (task->tk_rqstp) {
		/* We've already been given a request slot: NOP */
	} else {
		if (RPCXPRT_CONGESTED(xprt) || !(req = xprt->free))
			goto out_nofree;
		/* OK: There's room for us. Grab a free slot and bump
		 * congestion value */
		xprt->free     = req->rq_next;
		req->rq_next   = NULL;
		xprt->cong    += RPC_CWNDSCALE;
		task->tk_rqstp = req;
		xprt_request_init(task, xprt);

		if (xprt->free)
			xprt_clear_backlog(xprt);
	}

	return;

out_nofree:
	task->tk_status = -EAGAIN;
}

/*
 * Initialize RPC request
 */
static void
xprt_request_init(struct rpc_task *task, struct rpc_xprt *xprt)
{
	struct rpc_rqst	*req = task->tk_rqstp;
	static u32	xid = 0;

	if (!xid)
		xid = CURRENT_TIME << 12;

	dprintk("RPC: %4d reserved req %p xid %08x\n", task->tk_pid, req, xid);
	task->tk_status = 0;
	req->rq_timeout = xprt->timeout;
	req->rq_task	= task;
	req->rq_xprt    = xprt;
	req->rq_xid     = xid++;
	if (!xid)
		xid++;
}

/*
 * Release an RPC call slot
 */
void
xprt_release(struct rpc_task *task)
{
	struct rpc_xprt	*xprt = task->tk_xprt;
	struct rpc_rqst	*req;

	if (xprt->snd_task == task) {
		if (xprt->stream)
			xprt_disconnect(xprt);
		xprt_release_write(xprt, task);
	}
	if (!(req = task->tk_rqstp))
		return;
	task->tk_rqstp = NULL;
	memset(req, 0, sizeof(*req));	/* mark unused */

	dprintk("RPC: %4d release request %p\n", task->tk_pid, req);

	spin_lock_bh(&xprt->xprt_lock);
	req->rq_next = xprt->free;
	xprt->free   = req;

	/* Decrease congestion value. */
	xprt->cong -= RPC_CWNDSCALE;

	xprt_clear_backlog(xprt);
	spin_unlock_bh(&xprt->xprt_lock);
}

/*
 * Set default timeout parameters
 */
void
xprt_default_timeout(struct rpc_timeout *to, int proto)
{
	if (proto == IPPROTO_UDP)
		xprt_set_timeout(to, 5,  5 * HZ);
	else
		xprt_set_timeout(to, 5, 60 * HZ);
}

/*
 * Set constant timeout
 */
void
xprt_set_timeout(struct rpc_timeout *to, unsigned int retr, unsigned long incr)
{
	to->to_current   = 
	to->to_initval   = 
	to->to_increment = incr;
	to->to_maxval    = incr * retr;
	to->to_resrvval  = incr * retr;
	to->to_retries   = retr;
	to->to_exponential = 0;
}

/*
 * Initialize an RPC client
 */
static struct rpc_xprt *
xprt_setup(struct socket *sock, int proto,
			struct sockaddr_in *ap, struct rpc_timeout *to)
{
	struct rpc_xprt	*xprt;
	struct rpc_rqst	*req;
	int		i;

	dprintk("RPC:      setting up %s transport...\n",
				proto == IPPROTO_UDP? "UDP" : "TCP");

	if ((xprt = kmalloc(sizeof(struct rpc_xprt), GFP_KERNEL)) == NULL)
		return NULL;
	memset(xprt, 0, sizeof(*xprt)); /* Nnnngh! */

	xprt->addr = *ap;
	xprt->prot = proto;
	xprt->stream = (proto == IPPROTO_TCP)? 1 : 0;
	if (xprt->stream) {
		xprt->cwnd = RPC_MAXCWND;
		xprt->nocong = 1;
	} else
		xprt->cwnd = RPC_INITCWND;
	xprt->congtime = jiffies;
	spin_lock_init(&xprt->sock_lock);
	spin_lock_init(&xprt->xprt_lock);
	init_waitqueue_head(&xprt->cong_wait);

	/* Set timeout parameters */
	if (to) {
		xprt->timeout = *to;
		xprt->timeout.to_current = to->to_initval;
		xprt->timeout.to_resrvval = to->to_maxval << 1;
	} else
		xprt_default_timeout(&xprt->timeout, xprt->prot);

	xprt->pending = RPC_INIT_WAITQ("xprt_pending");
	xprt->sending = RPC_INIT_WAITQ("xprt_sending");
	xprt->backlog = RPC_INIT_WAITQ("xprt_backlog");

	/* initialize free list */
	for (i = 0, req = xprt->slot; i < RPC_MAXREQS-1; i++, req++)
		req->rq_next = req + 1;
	req->rq_next = NULL;
	xprt->free = xprt->slot;

	INIT_LIST_HEAD(&xprt->rx_pending);

	dprintk("RPC:      created transport %p\n", xprt);
	
	xprt_bind_socket(xprt, sock);
	return xprt;
}

/*
 * Bind to a reserved port
 */
static inline int
xprt_bindresvport(struct socket *sock)
{
	struct sockaddr_in myaddr;
	int		err, port;

	memset(&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	port = 800;
	do {
		myaddr.sin_port = htons(port);
		err = sock->ops->bind(sock, (struct sockaddr *) &myaddr,
						sizeof(myaddr));
	} while (err == -EADDRINUSE && --port > 0);

	if (err < 0)
		printk("RPC: Can't bind to reserved port (%d).\n", -err);

	return err;
}

static int 
xprt_bind_socket(struct rpc_xprt *xprt, struct socket *sock)
{
	struct sock	*sk = sock->sk;

	if (xprt->inet)
		return -EBUSY;

	sk->user_data = xprt;
	xprt->old_data_ready = sk->data_ready;
	xprt->old_state_change = sk->state_change;
	xprt->old_write_space = sk->write_space;
	if (xprt->prot == IPPROTO_UDP) {
		sk->data_ready = udp_data_ready;
		sk->write_space = udp_write_space;
		sk->no_check = UDP_CSUM_NORCV;
		xprt_set_connected(xprt);
	} else {
		sk->data_ready = tcp_data_ready;
		sk->state_change = tcp_state_change;
		sk->write_space = tcp_write_space;
		xprt_clear_connected(xprt);
	}

	/* Reset to new socket */
	xprt->sock = sock;
	xprt->inet = sk;
	/*
	 *	TCP requires the rpc I/O daemon is present
	 */
	if(xprt->stream)
		rpciod_up();

	return 0;
}

/*
 * Create a client socket given the protocol and peer address.
 */
static struct socket *
xprt_create_socket(int proto, struct rpc_timeout *to)
{
	struct socket	*sock;
	int		type, err;

	dprintk("RPC:      xprt_create_socket(%s %d)\n",
			   (proto == IPPROTO_UDP)? "udp" : "tcp", proto);

	type = (proto == IPPROTO_UDP)? SOCK_DGRAM : SOCK_STREAM;

	if ((err = sock_create(PF_INET, type, proto, &sock)) < 0) {
		printk("RPC: can't create socket (%d).\n", -err);
		goto failed;
	}

	/* If the caller has the capability, bind to a reserved port */
	if (capable(CAP_NET_BIND_SERVICE) && xprt_bindresvport(sock) < 0)
		goto failed;

	return sock;

failed:
	sock_release(sock);
	return NULL;
}

/*
 * Create an RPC client transport given the protocol and peer address.
 */
struct rpc_xprt *
xprt_create_proto(int proto, struct sockaddr_in *sap, struct rpc_timeout *to)
{
	struct socket	*sock;
	struct rpc_xprt	*xprt;

	dprintk("RPC:      xprt_create_proto called\n");

	if (!(sock = xprt_create_socket(proto, to)))
		return NULL;

	if (!(xprt = xprt_setup(sock, proto, sap, to)))
		sock_release(sock);

	return xprt;
}

/*
 * Prepare for transport shutdown.
 */
void
xprt_shutdown(struct rpc_xprt *xprt)
{
	xprt->shutdown = 1;
	rpc_wake_up(&xprt->sending);
	rpc_wake_up(&xprt->pending);
	rpc_wake_up(&xprt->backlog);
	if (waitqueue_active(&xprt->cong_wait))
		wake_up(&xprt->cong_wait);
}

/*
 * Clear the xprt backlog queue
 */
int
xprt_clear_backlog(struct rpc_xprt *xprt) {
	if (RPCXPRT_CONGESTED(xprt))
		return 0;
	rpc_wake_up_next(&xprt->backlog);
	if (waitqueue_active(&xprt->cong_wait))
		wake_up(&xprt->cong_wait);
	return 1;
}

/*
 * Destroy an RPC transport, killing off all requests.
 */
int
xprt_destroy(struct rpc_xprt *xprt)
{
	dprintk("RPC:      destroying transport %p\n", xprt);
	xprt_shutdown(xprt);
	xprt_close(xprt);
	kfree(xprt);

	return 0;
}
