/* connection.h: Rx connection record
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RXRPC_CONNECTION_H
#define _LINUX_RXRPC_CONNECTION_H

#include <rxrpc/types.h>
#include <rxrpc/krxtimod.h>

struct sk_buff;

/*****************************************************************************/
/*
 * Rx connection
 * - connections are matched by (rmt_port,rmt_addr,service_id,conn_id,clientflag)
 * - connections only retain a refcount on the peer when they are active
 * - connections with refcount==0 are inactive and reside in the peer's graveyard
 */
struct rxrpc_connection
{
	atomic_t		usage;
	struct rxrpc_transport	*trans;		/* transport endpoint */
	struct rxrpc_peer	*peer;		/* peer from/to which connected */
	struct rxrpc_service	*service;	/* responsible service (inbound conns) */
	struct rxrpc_timer	timeout;	/* decaching timer */
	struct list_head	link;		/* link in peer's list */
	struct list_head	proc_link;	/* link in proc list */
	struct list_head	err_link;	/* link in ICMP error processing list */
	struct sockaddr_in	addr;		/* remote address */
	struct rxrpc_call	*channels[4];	/* channels (active calls) */
	wait_queue_head_t	chanwait;	/* wait for channel to become available */
	spinlock_t		lock;		/* access lock */
	struct timeval		atime;		/* last access time */
	size_t			mtu_size;	/* MTU size for outbound messages */
	unsigned		call_counter;	/* call ID counter */
	rxrpc_serial_t		serial_counter;	/* packet serial number counter */

	/* the following should all be in net order */
	u32			in_epoch;	/* peer's epoch */
	u32			out_epoch;	/* my epoch */
	u32			conn_id;	/* connection ID, appropriately shifted */
	u16			service_id;	/* service ID */
	u8			security_ix;	/* security ID */
	u8			in_clientflag;	/* RXRPC_CLIENT_INITIATED if we are server */
	u8			out_clientflag;	/* RXRPC_CLIENT_INITIATED if we are client */
};

extern int rxrpc_create_connection(struct rxrpc_transport *trans,
				   u16 port,
				   u32 addr,
				   unsigned short service_id,
				   void *security,
				   struct rxrpc_connection **_conn);

extern int rxrpc_connection_lookup(struct rxrpc_peer *peer,
				   struct rxrpc_message *msg,
				   struct rxrpc_connection **_conn);

static inline void rxrpc_get_connection(struct rxrpc_connection *conn)
{
	if (atomic_read(&conn->usage)<0)
		BUG();
	atomic_inc(&conn->usage);
	//printk("rxrpc_get_conn(%p{u=%d})\n",conn,atomic_read(&conn->usage));
}

extern void rxrpc_put_connection(struct rxrpc_connection *conn);

extern int rxrpc_conn_receive_call_packet(struct rxrpc_connection *conn,
					  struct rxrpc_call *call,
					  struct rxrpc_message *msg);

extern void rxrpc_conn_handle_error(struct rxrpc_connection *conn, int local, int errno);

#endif /* _LINUX_RXRPC_CONNECTION_H */
