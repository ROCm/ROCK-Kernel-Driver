/*
 *  linux/include/linux/sunrpc/clnt_xprt.h
 *
 *  Declarations for the RPC transport interface.
 *
 *  Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_XPRT_H
#define _LINUX_SUNRPC_XPRT_H

#include <linux/uio.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/sunrpc/sched.h>

/*
 * Maximum number of iov's we use.
 */
#define MAX_IOVEC	10

/*
 * The transport code maintains an estimate on the maximum number of out-
 * standing RPC requests, using a smoothed version of the congestion
 * avoidance implemented in 44BSD. This is basically the Van Jacobson
 * slow start algorithm: If a retransmit occurs, the congestion window is
 * halved; otherwise, it is incremented by 1/cwnd when
 *
 *	-	a reply is received and
 *	-	a full number of requests are outstanding and
 *	-	the congestion window hasn't been updated recently.
 *
 * Upper procedures may check whether a request would block waiting for
 * a free RPC slot by using the RPC_CONGESTED() macro.
 *
 * Note: on machines with low memory we should probably use a smaller
 * MAXREQS value: At 32 outstanding reqs with 8 megs of RAM, fragment
 * reassembly will frequently run out of memory.
 * Come Linux 2.3, we'll handle fragments directly.
 */
#define RPC_MAXCONG		16
#define RPC_MAXREQS		(RPC_MAXCONG + 1)
#define RPC_CWNDSCALE		256
#define RPC_MAXCWND		(RPC_MAXCONG * RPC_CWNDSCALE)
#define RPC_INITCWND		RPC_CWNDSCALE
#define RPCXPRT_CONGESTED(xprt) \
	((xprt)->cong >= (xprt)->cwnd)

/* Default timeout values */
#define RPC_MAX_UDP_TIMEOUT	(60*HZ)
#define RPC_MAX_TCP_TIMEOUT	(600*HZ)

/* RPC call and reply header size as number of 32bit words (verifier
 * size computed separately)
 */
#define RPC_CALLHDRSIZE		6
#define RPC_REPHDRSIZE		4

/*
 * This describes a timeout strategy
 */
struct rpc_timeout {
	unsigned long		to_current,		/* current timeout */
				to_initval,		/* initial timeout */
				to_maxval,		/* max timeout */
				to_increment,		/* if !exponential */
				to_resrvval;		/* reserve timeout */
	short			to_retries;		/* max # of retries */
	unsigned char		to_exponential;
};

/*
 * This is the RPC buffer
 */
struct rpc_iov {
	struct iovec		io_vec[MAX_IOVEC];
	unsigned int		io_nr;
	unsigned int		io_len;
};

/*
 * This describes a complete RPC request
 */
struct rpc_rqst {
	/*
	 * This is the user-visible part
	 */
	struct rpc_xprt *	rq_xprt;		/* RPC client */
	struct rpc_timeout	rq_timeout;		/* timeout parms */
	struct rpc_iov		rq_snd_buf;		/* send buffer */
	struct rpc_iov		rq_rcv_buf;		/* recv buffer */

	/*
	 * This is the private part
	 */
	struct rpc_task *	rq_task;	/* RPC task data */
	__u32			rq_xid;		/* request XID */
	struct rpc_rqst *	rq_next;	/* free list */
	volatile unsigned char	rq_received : 1;/* receive completed */

	/*
	 * For authentication (e.g. auth_des)
	 */
	u32			rq_creddata[2];
	
	/*
	 * Partial send handling
	 */
	
	u32			rq_bytes_sent;	/* Bytes we have sent */

#ifdef RPC_PROFILE
	unsigned long		rq_xtime;	/* when transmitted */
#endif
};
#define rq_svec			rq_snd_buf.io_vec
#define rq_snr			rq_snd_buf.io_nr
#define rq_slen			rq_snd_buf.io_len
#define rq_rvec			rq_rcv_buf.io_vec
#define rq_rnr			rq_rcv_buf.io_nr
#define rq_rlen			rq_rcv_buf.io_len

struct rpc_xprt {
	struct socket *		sock;		/* BSD socket layer */
	struct sock *		inet;		/* INET layer */

	struct rpc_timeout	timeout;	/* timeout parms */
	struct sockaddr_in	addr;		/* server address */
	int			prot;		/* IP protocol */

	unsigned long		cong;		/* current congestion */
	unsigned long		cwnd;		/* congestion window */
	unsigned long		congtime;	/* hold cwnd until then */

	struct rpc_wait_queue	sending;	/* requests waiting to send */
	struct rpc_wait_queue	pending;	/* requests in flight */
	struct rpc_wait_queue	backlog;	/* waiting for slot */
	struct rpc_wait_queue	reconn;		/* waiting for reconnect */
	struct rpc_rqst *	free;		/* free slots */
	struct rpc_rqst		slot[RPC_MAXREQS];
	unsigned int		sockstate;	/* Socket state */
	unsigned char		shutdown   : 1,	/* being shut down */
				nocong	   : 1,	/* no congestion control */
				stream     : 1,	/* TCP */
				tcp_more   : 1,	/* more record fragments */
				connecting : 1;	/* being reconnected */

	/*
	 * State of TCP reply receive stuff
	 */
	u32			tcp_recm;	/* Fragment header */
	u32			tcp_xid;	/* Current XID */
	unsigned int		tcp_reclen,	/* fragment length */
				tcp_offset,	/* fragment offset */
				tcp_copied;	/* copied to request */
	struct list_head	rx_pending;	/* receive pending list */

	/*
	 * Send stuff
	 */
	struct rpc_task *	snd_task;	/* Task blocked in send */


	void			(*old_data_ready)(struct sock *, int);
	void			(*old_state_change)(struct sock *);
	void			(*old_write_space)(struct sock *);

	wait_queue_head_t	cong_wait;
};

#ifdef __KERNEL__

struct rpc_xprt *	xprt_create_proto(int proto, struct sockaddr_in *addr,
					struct rpc_timeout *toparms);
int			xprt_destroy(struct rpc_xprt *);
void			xprt_shutdown(struct rpc_xprt *);
void			xprt_default_timeout(struct rpc_timeout *, int);
void			xprt_set_timeout(struct rpc_timeout *, unsigned int,
					unsigned long);

int			xprt_reserve(struct rpc_task *);
void			xprt_transmit(struct rpc_task *);
void			xprt_receive(struct rpc_task *);
int			xprt_adjust_timeout(struct rpc_timeout *);
void			xprt_release(struct rpc_task *);
void			xprt_reconnect(struct rpc_task *);
int			xprt_clear_backlog(struct rpc_xprt *);
void			__rpciod_tcp_dispatcher(void);

extern struct list_head	rpc_xprt_pending;

#define XPRT_WSPACE	0
#define XPRT_CONNECT	1

#define xprt_wspace(xp)			(test_bit(XPRT_WSPACE, &(xp)->sockstate))
#define xprt_test_and_set_wspace(xp)	(test_and_set_bit(XPRT_WSPACE, &(xp)->sockstate))
#define xprt_clear_wspace(xp)		(clear_bit(XPRT_WSPACE, &(xp)->sockstate))

#define xprt_connected(xp)		(!(xp)->stream || test_bit(XPRT_CONNECT, &(xp)->sockstate))
#define xprt_set_connected(xp)		(set_bit(XPRT_CONNECT, &(xp)->sockstate))
#define xprt_test_and_set_connected(xp)	(test_and_set_bit(XPRT_CONNECT, &(xp)->sockstate))
#define xprt_clear_connected(xp)	(clear_bit(XPRT_CONNECT, &(xp)->sockstate))

static inline
int xprt_tcp_pending(void)
{
	return !list_empty(&rpc_xprt_pending);
}

static inline
void rpciod_tcp_dispatcher(void)
{
	if (xprt_tcp_pending())
		__rpciod_tcp_dispatcher();
}

#endif /* __KERNEL__*/

#endif /* _LINUX_SUNRPC_XPRT_H */
