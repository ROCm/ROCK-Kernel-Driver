/* internal.h: internal Rx RPC stuff
 *
 * Copyright (c) 2002   David Howells (dhowells@redhat.com).
 */

#ifndef RXRPC_INTERNAL_H
#define RXRPC_INTERNAL_H

#include <linux/compiler.h>
#include <linux/kernel.h>

/*
 * debug accounting
 */
#if 1
#define __RXACCT_DECL(X) X
#define __RXACCT(X) do { X; } while(0) 
#else
#define __RXACCT_DECL(X)
#define __RXACCT(X) do { } while(0)
#endif

__RXACCT_DECL(extern atomic_t rxrpc_transport_count);
__RXACCT_DECL(extern atomic_t rxrpc_peer_count);
__RXACCT_DECL(extern atomic_t rxrpc_connection_count);
__RXACCT_DECL(extern atomic_t rxrpc_call_count);
__RXACCT_DECL(extern atomic_t rxrpc_message_count);

/*
 * debug tracing
 */
#define kenter(FMT,...)	printk("==> %s("FMT")\n",__FUNCTION__,##__VA_ARGS__)
#define kleave(FMT,...)	printk("<== %s()"FMT"\n",__FUNCTION__,##__VA_ARGS__)
#define kdebug(FMT,...)	printk("    "FMT"\n",##__VA_ARGS__)
#define kproto(FMT,...)	printk("### "FMT"\n",##__VA_ARGS__)
#define knet(FMT,...)	printk("    "FMT"\n",##__VA_ARGS__)

#if 0
#define _enter(FMT,...)	kenter(FMT,##__VA_ARGS__)
#define _leave(FMT,...)	kleave(FMT,##__VA_ARGS__)
#define _debug(FMT,...)	kdebug(FMT,##__VA_ARGS__)
#define _proto(FMT,...)	kproto(FMT,##__VA_ARGS__)
#define _net(FMT,...)	knet(FMT,##__VA_ARGS__)
#else
#define _enter(FMT,...)	do { if (rxrpc_ktrace) kenter(FMT,##__VA_ARGS__); } while(0)
#define _leave(FMT,...)	do { if (rxrpc_ktrace) kleave(FMT,##__VA_ARGS__); } while(0)
#define _debug(FMT,...)	do { if (rxrpc_kdebug) kdebug(FMT,##__VA_ARGS__); } while(0)
#define _proto(FMT,...)	do { if (rxrpc_kproto) kproto(FMT,##__VA_ARGS__); } while(0)
#define _net(FMT,...)	do { if (rxrpc_knet)   knet  (FMT,##__VA_ARGS__); } while(0)
#endif

static inline void rxrpc_discard_my_signals(void)
{
	while (signal_pending(current)) {
		siginfo_t sinfo;

		spin_lock_irq(&current->sig->siglock);
		dequeue_signal(&current->blocked,&sinfo);
		spin_unlock_irq(&current->sig->siglock);
	}
}

/*
 * call.c
 */
extern struct list_head rxrpc_calls;
extern struct rw_semaphore rxrpc_calls_sem;

/*
 * connection.c
 */
extern struct list_head rxrpc_conns;
extern struct rw_semaphore rxrpc_conns_sem;

extern void rxrpc_conn_do_timeout(struct rxrpc_connection *conn);
extern void rxrpc_conn_clearall(struct rxrpc_peer *peer);

/*
 * peer.c
 */
extern struct list_head rxrpc_peers;
extern struct rw_semaphore rxrpc_peers_sem;

extern void rxrpc_peer_calculate_rtt(struct rxrpc_peer *peer,
				     struct rxrpc_message *msg,
				     struct rxrpc_message *resp);

extern void rxrpc_peer_clearall(struct rxrpc_transport *trans);

extern void rxrpc_peer_do_timeout(struct rxrpc_peer *peer);


/*
 * proc.c
 */
#ifdef CONFIG_PROC_FS
extern int rxrpc_proc_init(void);
extern void rxrpc_proc_cleanup(void);
#endif

/*
 * transport.c
 */
extern struct list_head rxrpc_proc_transports;
extern struct rw_semaphore rxrpc_proc_transports_sem;

#endif /* RXRPC_INTERNAL_H */
