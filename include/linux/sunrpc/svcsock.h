/*
 * linux/include/linux/sunrpc/svcsock.h
 *
 * RPC server socket I/O.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef SUNRPC_SVCSOCK_H
#define SUNRPC_SVCSOCK_H

#include <linux/sunrpc/svc.h>

/*
 * RPC server socket.
 * NOTE: First two items must be prev/next.
 */
struct svc_sock {
	struct svc_sock *	sk_prev;	/* list of ready sockets */
	struct svc_sock *	sk_next;
	struct svc_sock *	sk_list;	/* list of all sockets */
	struct socket *		sk_sock;	/* berkeley socket layer */
	struct sock *		sk_sk;		/* INET layer */
	spinlock_t		sk_lock;

	struct svc_serv *	sk_server;	/* service for this socket */
	unsigned char		sk_inuse;	/* use count */
	unsigned char		sk_busy;	/* enqueued/receiving */
	unsigned char		sk_conn;	/* conn pending */
	unsigned char		sk_close;	/* dead or dying */
	int			sk_data;	/* data pending */
	unsigned int		sk_temp : 1,	/* temp socket */
				sk_qued : 1,	/* on serv->sk_sockets */
				sk_dead : 1;	/* socket closed */
	int			(*sk_recvfrom)(struct svc_rqst *rqstp);
	int			(*sk_sendto)(struct svc_rqst *rqstp);

	/* We keep the old state_change and data_ready CB's here */
	void			(*sk_ostate)(struct sock *);
	void			(*sk_odata)(struct sock *, int bytes);

	/* private TCP part */
	int			sk_reclen;	/* length of record */
	int			sk_tcplen;	/* current read length */

	/* Debugging */
	struct svc_rqst *	sk_rqstp;
};

/*
 * Function prototypes.
 */
int		svc_makesock(struct svc_serv *, int, unsigned short);
void		svc_delete_socket(struct svc_sock *);
int		svc_recv(struct svc_serv *, struct svc_rqst *, long);
int		svc_send(struct svc_rqst *);
void		svc_drop(struct svc_rqst *);

#endif /* SUNRPC_SVCSOCK_H */
