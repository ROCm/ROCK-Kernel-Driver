/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 International Business Machines Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * This module provides the abstraction for an SCTP association.
 * 
 * The SCTP reference implementation is free software; 
 * you can redistribute it and/or modify it under the terms of 
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * The SCTP reference implementation is distributed in the hope that it 
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 * 
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 * 
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by: 
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Hui Huang             <hui.huang@nokia.com>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/sched.h>

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <net/ipv6.h>
#include <net/sctp/sctp.h>

/* Forward declarations for internal functions. */
static void sctp_assoc_bh_rcv(sctp_association_t *asoc);


/* 1st Level Abstractions. */

/* Allocate and initialize a new association */
sctp_association_t *sctp_association_new(const sctp_endpoint_t *ep,
					 const struct sock *sk, 
					 sctp_scope_t scope, int priority)
{
	sctp_association_t *asoc;

	asoc = t_new(sctp_association_t, priority);
	if (!asoc)
		goto fail;

	if (!sctp_association_init(asoc, ep, sk, scope, priority))
		goto fail_init;

	asoc->base.malloced = 1;
	SCTP_DBG_OBJCNT_INC(assoc);

	return asoc;

fail_init:
	kfree(asoc);
fail:
	return NULL;
}

/* Intialize a new association from provided memory. */
sctp_association_t *sctp_association_init(sctp_association_t *asoc,
					  const sctp_endpoint_t *ep, 
					  const struct sock *sk,
					  sctp_scope_t scope, 
					  int priority)
{
	sctp_opt_t *sp;
	int i;

	/* Retrieve the SCTP per socket area.  */
	sp = sctp_sk((struct sock *)sk);

	/* Init all variables to a known value.  */
	memset(asoc, 0, sizeof(sctp_association_t));

	/* Discarding const is appropriate here.  */
	asoc->ep = (sctp_endpoint_t *)ep;
	sctp_endpoint_hold(asoc->ep);

	/* Hold the sock. */
	asoc->base.sk = (struct sock *)sk;
	sock_hold(asoc->base.sk);

	/* Initialize the common base substructure. */
	asoc->base.type = SCTP_EP_TYPE_ASSOCIATION;

	/* Initialize the object handling fields. */
	atomic_set(&asoc->base.refcnt, 1);
	asoc->base.dead = 0;
	asoc->base.malloced = 0;

	/* Initialize the bind addr area. */
	sctp_bind_addr_init(&asoc->base.bind_addr, ep->base.bind_addr.port);
	asoc->base.addr_lock = RW_LOCK_UNLOCKED;

	asoc->state = SCTP_STATE_CLOSED;
	asoc->state_timestamp = jiffies;

	/* Set things that have constant value.  */
	asoc->cookie_life.tv_sec = SCTP_DEFAULT_COOKIE_LIFE_SEC;
	asoc->cookie_life.tv_usec = SCTP_DEFAULT_COOKIE_LIFE_USEC;

	asoc->pmtu = 0;
	asoc->frag_point = 0;

	/* Initialize the default association max_retrans and RTO values. */
	asoc->max_retrans = ep->proto->max_retrans_association;
	asoc->rto_initial = ep->proto->rto_initial;
	asoc->rto_max = ep->proto->rto_max;
	asoc->rto_min = ep->proto->rto_min;

	asoc->overall_error_threshold = 0;
	asoc->overall_error_count = 0;

	/* Initialize the maximum mumber of new data packets that can be sent
	 * in a burst.
	 */
	asoc->max_burst = ep->proto->max_burst;

	/* Copy things from the endpoint.  */
	for (i = SCTP_EVENT_TIMEOUT_NONE; i < SCTP_NUM_TIMEOUT_TYPES; ++i) {
		asoc->timeouts[i] = ep->timeouts[i];
		init_timer(&asoc->timers[i]);
		asoc->timers[i].function = sctp_timer_events[i];
		asoc->timers[i].data = (unsigned long) asoc;
	}

	/* Pull default initialization values from the sock options.
	 * Note: This assumes that the values have already been
	 * validated in the sock.
	 */
	asoc->c.sinit_max_instreams = sp->initmsg.sinit_max_instreams;
	asoc->c.sinit_num_ostreams  = sp->initmsg.sinit_num_ostreams;
	asoc->max_init_attempts	= sp->initmsg.sinit_max_attempts;
	asoc->max_init_timeo    = sp->initmsg.sinit_max_init_timeo * HZ;

	/* RFC 2960 6.5 Stream Identifier and Stream Sequence Number
	 *
	 * The stream sequence number in all the streams shall start
	 * from 0 when the association is established.  Also, when the
	 * stream sequence number reaches the value 65535 the next
	 * stream sequence number shall be set to 0.
	 */
	for (i = 0; i < SCTP_MAX_STREAM; i++)
		asoc->ssn[i] = 0;

	/* Set the local window size for receive.
	 * This is also the rcvbuf space per association.
	 * RFC 6 - A SCTP receiver MUST be able to receive a minimum of
	 * 1500 bytes in one SCTP packet.
	 */
	if (sk->rcvbuf < SCTP_DEFAULT_MINWINDOW)
		asoc->rwnd = SCTP_DEFAULT_MINWINDOW;
	else
		asoc->rwnd = sk->rcvbuf;

	asoc->rwnd_over = 0;

	/* Use my own max window until I learn something better.  */
	asoc->peer.rwnd = SCTP_DEFAULT_MAXWINDOW;

	/* Set the sndbuf size for transmit.  */
	asoc->sndbuf_used = 0;

	init_waitqueue_head(&asoc->wait);

	asoc->c.my_vtag = sctp_generate_tag(ep);
	asoc->peer.i.init_tag = 0;     /* INIT needs a vtag of 0. */
	asoc->c.peer_vtag = 0;
	asoc->c.my_ttag   = 0;
	asoc->c.peer_ttag = 0;

	asoc->c.initial_tsn = sctp_generate_tsn(ep);

	asoc->next_tsn = asoc->c.initial_tsn;

	asoc->ctsn_ack_point = asoc->next_tsn - 1;

	asoc->unack_data = 0;

	SCTP_DEBUG_PRINTK("myctsnap for %s INIT as 0x%x.\n",
			  asoc->ep->debug_name,
			  asoc->ctsn_ack_point);

	/* ADDIP Section 4.1 Asconf Chunk Procedures
	 *
	 * When an endpoint has an ASCONF signaled change to be sent to the
	 * remote endpoint it should do the following:
	 * ...
	 * A2) a serial number should be assigned to the chunk. The serial
	 * number should be a monotonically increasing number. All serial
	 * numbers are defined to be initialized at the start of the
	 * association to the same value as the initial TSN.
	 */
	asoc->addip_serial = asoc->c.initial_tsn;

	/* Make an empty list of remote transport addresses.  */
	INIT_LIST_HEAD(&asoc->peer.transport_addr_list);

	/* RFC 2960 5.1 Normal Establishment of an Association
	 *
	 * After the reception of the first data chunk in an
	 * association the endpoint must immediately respond with a
	 * sack to acknowledge the data chunk.  Subsequent
	 * acknowledgements should be done as described in Section
	 * 6.2.
	 *
	 * [We implement this by telling a new association that it
	 * already received one packet.]
	 */
	asoc->peer.sack_needed = 1;

	/* Create an input queue.  */
	sctp_inqueue_init(&asoc->base.inqueue);
	sctp_inqueue_set_th_handler(&asoc->base.inqueue,
				    (void (*)(void *))sctp_assoc_bh_rcv,
				    asoc);

	/* Create an output queue.  */
	sctp_outqueue_init(asoc, &asoc->outqueue);
	sctp_outqueue_set_output_handlers(&asoc->outqueue,
					  sctp_packet_init,
					  sctp_packet_config,
					  sctp_packet_append_chunk,
					  sctp_packet_transmit_chunk,
					  sctp_packet_transmit);

	if (NULL == sctp_ulpqueue_init(&asoc->ulpq, asoc, SCTP_MAX_STREAM))
		goto fail_init;

	/* Set up the tsn tracking. */
	sctp_tsnmap_init(&asoc->peer.tsn_map, SCTP_TSN_MAP_SIZE, 0);
	asoc->peer.next_dup_tsn = 0;

	skb_queue_head_init(&asoc->addip_chunks);

	asoc->need_ecne = 0;

	asoc->debug_name = "unnamedasoc";
	asoc->eyecatcher = SCTP_ASSOC_EYECATCHER;

	/* Assume that peer would support both address types unless we are
	 * told otherwise.
	 */
	asoc->peer.ipv4_address = 1;
	asoc->peer.ipv6_address = 1;
	INIT_LIST_HEAD(&asoc->asocs);

	asoc->autoclose = sp->autoclose;

	return asoc;

fail_init:
	sctp_endpoint_put(asoc->ep);
	sock_put(asoc->base.sk);
	return NULL;
}


/* Free this association if possible.  There may still be users, so
 * the actual deallocation may be delayed.
 */
void sctp_association_free(sctp_association_t *asoc)
{
	sctp_transport_t *transport;
	sctp_endpoint_t *ep;
	struct list_head *pos, *temp;
	int i;

	ep = asoc->ep;
	list_del(&asoc->asocs);

	/* Mark as dead, so other users can know this structure is
	 * going away.
	 */
	asoc->base.dead = 1;

	/* Dispose of any data lying around in the outqueue. */
	sctp_outqueue_free(&asoc->outqueue);

	/* Dispose of any pending messages for the upper layer. */
	sctp_ulpqueue_free(&asoc->ulpq);

	/* Dispose of any pending chunks on the inqueue. */
	sctp_inqueue_free(&asoc->base.inqueue);

	/* Clean up the bound address list. */
	sctp_bind_addr_free(&asoc->base.bind_addr);

	/* Do we need to go through all of our timers and
	 * delete them?   To be safe we will try to delete all, but we
	 * should be able to go through and make a guess based
	 * on our state.
	 */
	for (i = SCTP_EVENT_TIMEOUT_NONE; i < SCTP_NUM_TIMEOUT_TYPES; ++i) {
		if (timer_pending(&asoc->timers[i]) &&
		    del_timer(&asoc->timers[i]))
			sctp_association_put(asoc);
	}

	/* Release the transport structures. */
	list_for_each_safe(pos, temp, &asoc->peer.transport_addr_list) {
		transport = list_entry(pos, sctp_transport_t, transports);
		list_del(pos);
		sctp_transport_free(transport);
	}

	asoc->eyecatcher = 0;

	sctp_association_put(asoc);
}


/* Cleanup and free up an association. */
static void sctp_association_destroy(sctp_association_t *asoc)
{
	SCTP_ASSERT(asoc->base.dead, "Assoc is not dead", return);

	sctp_endpoint_put(asoc->ep);
	sock_put(asoc->base.sk);

	if (asoc->base.malloced) {
		kfree(asoc);
		SCTP_DBG_OBJCNT_DEC(assoc);
	}
}


/* Add a transport address to an association.  */
sctp_transport_t *sctp_assoc_add_peer(sctp_association_t *asoc,
				      const sockaddr_storage_t *addr,
				      int priority)
{
	sctp_transport_t *peer;
	sctp_opt_t *sp;
	const __u16 *port;

	switch (addr->sa.sa_family) {
	case AF_INET:
		port = &addr->v4.sin_port;
		break;

	case AF_INET6:
		SCTP_V6(
			port = &addr->v6.sin6_port;
			break;
		);

	default:
		return NULL;
	};

	/* Set the port if it has not been set yet.  */
        if (0 == asoc->peer.port) {
                asoc->peer.port = *port;
        }

	SCTP_ASSERT(*port == asoc->peer.port, ":Invalid port\n",
		    return NULL);

	/* Check to see if this is a duplicate. */
	peer = sctp_assoc_lookup_paddr(asoc, addr);
	if (peer)
		return peer;

	peer = sctp_transport_new(addr, priority);
	if (NULL == peer)
		return NULL;

	sctp_transport_set_owner(peer, asoc);

	/* If this is the first transport addr on this association,
	 * initialize the association PMTU to the peer's PMTU.
	 * If not and the current association PMTU is higher than the new
	 * peer's PMTU, reset the association PMTU to the new peer's PMTU.
	 */
	if (asoc->pmtu) {
		asoc->pmtu = min_t(int, peer->pmtu, asoc->pmtu);
	} else {
		asoc->pmtu = peer->pmtu;
	}

	SCTP_DEBUG_PRINTK("sctp_assoc_add_peer:association %p PMTU set to "
			  "%d\n", asoc, asoc->pmtu);

	asoc->frag_point = asoc->pmtu -
		(SCTP_IP_OVERHEAD + sizeof(sctp_data_chunk_t));

	/* The asoc->peer.port might not be meaningful as of now, but
	 * initialize the packet structure anyway.
	 */
	(asoc->outqueue.init_output)(&peer->packet,
				     peer,
				     asoc->base.bind_addr.port,
				     asoc->peer.port);

	/* 7.2.1 Slow-Start
	 *
	 * o The initial cwnd before data transmission or after a
	 *   sufficiently long idle period MUST be <= 2*MTU.
	 *
	 * o The initial value of ssthresh MAY be arbitrarily high
	 *   (for example, implementations MAY use the size of the
	 *   receiver advertised window).
	 */
	peer->cwnd = asoc->pmtu * 2;

	/* At this point, we may not have the receiver's advertised window,
	 * so initialize ssthresh to the default value and it will be set
	 * later when we process the INIT.
	 */
	peer->ssthresh = SCTP_DEFAULT_MAXWINDOW;

	peer->partial_bytes_acked = 0;
	peer->flight_size = 0;

	peer->error_threshold = peer->max_retrans;

	/* Update the overall error threshold value of the association
	 * taking the new peer's error threshold into account.
	 */
	asoc->overall_error_threshold =
		min(asoc->overall_error_threshold + peer->error_threshold,
		    asoc->max_retrans);

	/* Initialize the peer's heartbeat interval based on the
	 * sock configured value.
	 */
	sp = sctp_sk(asoc->base.sk);
	peer->hb_interval = sp->paddrparam.spp_hbinterval * HZ;

        /* Attach the remote transport to our asoc.  */
	list_add_tail(&peer->transports, &asoc->peer.transport_addr_list);

	/* If we do not yet have a primary path, set one.  */
        if (NULL == asoc->peer.primary_path) {
		asoc->peer.primary_path = peer;
		asoc->peer.active_path = peer;
		asoc->peer.retran_path = peer;
	}

	if (asoc->peer.active_path == asoc->peer.retran_path)
		asoc->peer.retran_path = peer;

	return peer;
}

/* Lookup a transport by address. */
sctp_transport_t *sctp_assoc_lookup_paddr(const sctp_association_t *asoc,
					  const sockaddr_storage_t *address)
{
	sctp_transport_t *t;
	struct list_head *pos;

	/* Cycle through all transports searching for a peer address. */

	list_for_each(pos, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, sctp_transport_t, transports);
		if (sctp_cmp_addr_exact(address, &t->ipaddr))
			return t;
	}

	return NULL;
}

/* Engage in transport control operations.
 * Mark the transport up or down and send a notification to the user.
 * Select and update the new active and retran paths.
 */
void sctp_assoc_control_transport(sctp_association_t *asoc,
				  sctp_transport_t *transport,
				  sctp_transport_cmd_t command,
				  sctp_sn_error_t error)
{
	sctp_transport_t *t = NULL;
	sctp_transport_t *first;
	sctp_transport_t *second;
	sctp_ulpevent_t *event;
	struct list_head *pos;
	int spc_state = 0;

	/* Record the transition on the transport.  */
	switch (command) {
	case SCTP_TRANSPORT_UP:
		transport->state.active = 1;
		spc_state = ADDRESS_AVAILABLE;
		break;

	case SCTP_TRANSPORT_DOWN:
		transport->state.active = 0;
		spc_state = ADDRESS_UNREACHABLE;
		break;

	default:
		BUG();
	};

	/* Generate and send a SCTP_PEER_ADDR_CHANGE notification to the
	 * user.
	 */
	event = sctp_ulpevent_make_peer_addr_change(asoc,
				(struct sockaddr_storage *) &transport->ipaddr,
				0, spc_state, error, GFP_ATOMIC);
	if (event)
		sctp_ulpqueue_tail_event(&asoc->ulpq, event);

	/* Select new active and retran paths. */

	/* Look for the two most recently used active transports.
	 *
	 * This code produces the wrong ordering whenever jiffies
	 * rolls over, but we still get usable transports, so we don't
	 * worry about it.
	 */
	first = NULL; second = NULL;

	list_for_each(pos, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, sctp_transport_t, transports);

		if (!t->state.active)
			continue;
		if (!first || t->last_time_heard > first->last_time_heard) {
			second = first;
			first = t;
		}
		if (!second || t->last_time_heard > second->last_time_heard)
			second = t;
	}

	/* RFC 2960 6.4 Multi-Homed SCTP Endpoints
	 *
	 * By default, an endpoint should always transmit to the
	 * primary path, unless the SCTP user explicitly specifies the
	 * destination transport address (and possibly source
	 * transport address) to use.
	 *
	 * [If the primary is active but not most recent, bump the most
	 * recently used transport.]
	 */
	if (asoc->peer.primary_path->state.active &&
	    first != asoc->peer.primary_path) {
		second = first;
		first = asoc->peer.primary_path;
	}

	/* If we failed to find a usable transport, just camp on the
	 * primary, even if it is inactive.
	 */
	if (NULL == first) {
		first = asoc->peer.primary_path;
		second = asoc->peer.primary_path;
	}

	/* Set the active and retran transports.  */
	asoc->peer.active_path = first;
	asoc->peer.retran_path = second;
}

/* Hold a reference to an association. */
void sctp_association_hold(sctp_association_t *asoc)
{
	atomic_inc(&asoc->base.refcnt);
}

/* Release a reference to an association and cleanup
 * if there are no more references.
 */
void sctp_association_put(sctp_association_t *asoc)
{
	if (atomic_dec_and_test(&asoc->base.refcnt))
		sctp_association_destroy(asoc);
}

/* Allocate the next TSN, Transmission Sequence Number, for the given
 * association.
 */
__u32 __sctp_association_get_next_tsn(sctp_association_t *asoc)
{
	/* From Section 1.6 Serial Number Arithmetic:
	 * Transmission Sequence Numbers wrap around when they reach
	 * 2**32 - 1.  That is, the next TSN a DATA chunk MUST use
	 * after transmitting TSN = 2*32 - 1 is TSN = 0.
	 */
	__u32 retval = asoc->next_tsn;
	asoc->next_tsn++;
	asoc->unack_data++;

        return retval;
}

/* Allocate 'num' TSNs by incrementing the association's TSN by num. */
__u32 __sctp_association_get_tsn_block(sctp_association_t *asoc, int num)
{
	__u32 retval = asoc->next_tsn;

	asoc->next_tsn += num;
	asoc->unack_data += num;

	return retval;
}

/* Fetch the next Stream Sequence Number for stream number 'sid'.  */
__u16 __sctp_association_get_next_ssn(sctp_association_t *asoc, __u16 sid)
{
	return asoc->ssn[sid]++;
}

/* Compare two addresses to see if they match.  Wildcard addresses
 * always match within their address family.
 *
 * FIXME: We do not match address scopes correctly.
 */
int sctp_cmp_addr(const sockaddr_storage_t *ss1, const sockaddr_storage_t *ss2)
{
	int len;
	const void *base1;
	const void *base2;

	if (ss1->sa.sa_family != ss2->sa.sa_family)
		return 0;
	if (ss1->v4.sin_port != ss2->v4.sin_port)
		return 0;

	switch (ss1->sa.sa_family) {
	case AF_INET:
		if (INADDR_ANY == ss1->v4.sin_addr.s_addr ||
		    INADDR_ANY == ss2->v4.sin_addr.s_addr)
			goto match;

		len = sizeof(struct in_addr);
		base1 = &ss1->v4.sin_addr;
		base2 = &ss2->v4.sin_addr;
		break;

	case AF_INET6:
		SCTP_V6(
			if (IPV6_ADDR_ANY ==
			    sctp_ipv6_addr_type(&ss1->v6.sin6_addr))
				    goto match;

			if (IPV6_ADDR_ANY ==
			    sctp_ipv6_addr_type(&ss2->v6.sin6_addr))
				goto match;

			len = sizeof(struct in6_addr);
			base1 = &ss1->v6.sin6_addr;
			base2 = &ss2->v6.sin6_addr;
			break;
		)

	default:
		printk(KERN_WARNING
		       "WARNING, bogus socket address family %d\n",
		       ss1->sa.sa_family);
		return 0;
	};

	return (0 == memcmp(base1, base2, len));

match:
	return 1;
}

/* Compare two addresses to see if they match.  Wildcard addresses
 * only match themselves.
 *
 * FIXME: We do not match address scopes correctly.
 */
int sctp_cmp_addr_exact(const sockaddr_storage_t *ss1,
			const sockaddr_storage_t *ss2)
{
	int len;
	const void *base1;
	const void *base2;

	if (ss1->sa.sa_family != ss2->sa.sa_family)
		return 0;
	if (ss1->v4.sin_port != ss2->v4.sin_port)
		return 0;

	switch (ss1->sa.sa_family) {
	case AF_INET:
		len = sizeof(struct in_addr);
		base1 = &ss1->v4.sin_addr;
		base2 = &ss2->v4.sin_addr;
		break;

	case AF_INET6:
		SCTP_V6(
			len = sizeof(struct in6_addr);
			base1 = &ss1->v6.sin6_addr;
			base2 = &ss2->v6.sin6_addr;
			break;
		)

	default:
		printk(KERN_WARNING
		       "WARNING, bogus socket address family %d\n",
		       ss1->sa.sa_family);
		return 0;
	};

	return (0 == memcmp(base1, base2, len));
}

/* Return an ecne chunk to get prepended to a packet.
 * Note:  We are sly and return a shared, prealloced chunk.
 */
sctp_chunk_t *sctp_get_ecne_prepend(sctp_association_t *asoc)
{ 
	sctp_chunk_t *chunk;
	int need_ecne;
	__u32 lowest_tsn;

	/* Can be called from task or bh.   Both need_ecne and
	 * last_ecne_tsn are written during bh.
	 */
	need_ecne = asoc->need_ecne;
	lowest_tsn = asoc->last_ecne_tsn;

	if (need_ecne) {
		chunk = sctp_make_ecne(asoc, lowest_tsn);

		/* ECNE is not mandatory to the flow.  Being unable to
		 * alloc mem is not deadly.  We are just unable to help
		 * out the network.  If we run out of memory, just return
		 * NULL.
		 */
	} else {
		chunk = NULL;
	}

	return chunk;
}

/* Use this function for the packet prepend callback when no ECNE
 * packet is desired (e.g. some packets don't like to be bundled).
 */
sctp_chunk_t *sctp_get_no_prepend(sctp_association_t *asoc)
{
	return NULL;
}

/*
 * Find which transport this TSN was sent on.
 */
sctp_transport_t *sctp_assoc_lookup_tsn(sctp_association_t *asoc, __u32 tsn)
{
	sctp_transport_t *active;
	sctp_transport_t *match;
	struct list_head *entry, *pos;
	sctp_transport_t *transport;
	sctp_chunk_t *chunk;
	__u32 key = htonl(tsn);

	match = NULL;

	/*
	 * FIXME: In general, find a more efficient data structure for
	 * searching.
	 */

	/*
	 * The general strategy is to search each transport's transmitted
	 * list.   Return which transport this TSN lives on.
	 *
	 * Let's be hopeful and check the active_path first.
	 * Another optimization would be to know if there is only one
	 * outbound path and not have to look for the TSN at all.
	 *
	 */

	active = asoc->peer.active_path;

	list_for_each(entry, &active->transmitted) {
		chunk = list_entry(entry, sctp_chunk_t, transmitted_list);

		if (key == chunk->subh.data_hdr->tsn) {
			match = active;
			goto out;
		}
	}

	/* If not found, go search all the other transports. */
	list_for_each(pos, &asoc->peer.transport_addr_list) {
		transport = list_entry(pos, sctp_transport_t, transports);

		if (transport == active)
			break;
		list_for_each(entry, &transport->transmitted) {
			chunk = list_entry(entry, sctp_chunk_t,
					   transmitted_list);
			if (key == chunk->subh.data_hdr->tsn) {
				match = transport;
				goto out;
			}
		}
	}
out:
	return match;
}

/* Is this the association we are looking for? */
sctp_transport_t *sctp_assoc_is_match(sctp_association_t *asoc,
				      const sockaddr_storage_t *laddr,
				      const sockaddr_storage_t *paddr)
{
	sctp_transport_t *transport;

	sctp_read_lock(&asoc->base.addr_lock);

	if ((asoc->base.bind_addr.port == laddr->v4.sin_port) &&
	    (asoc->peer.port == paddr->v4.sin_port)) {
		transport = sctp_assoc_lookup_paddr(asoc, paddr);
		if (!transport)
			goto out;

		if (sctp_bind_addr_has_addr(&asoc->base.bind_addr, laddr))
			goto out;
	}
	transport = NULL;

out:
	sctp_read_unlock(&asoc->base.addr_lock);
	return transport;
}

/* Do delayed input processing.  This is scheduled by sctp_rcv(). */
static void sctp_assoc_bh_rcv(sctp_association_t *asoc)
{
	sctp_endpoint_t *ep;
	sctp_chunk_t *chunk;
	struct sock *sk;
	sctp_inqueue_t *inqueue;
	int state, subtype;
	sctp_assoc_t associd = sctp_assoc2id(asoc);
	int error = 0;

	/* The association should be held so we should be safe. */
	ep = asoc->ep;
	sk = asoc->base.sk;

	inqueue = &asoc->base.inqueue;
	while (NULL != (chunk = sctp_pop_inqueue(inqueue))) {
		state = asoc->state;
		subtype = chunk->chunk_hdr->type;

		/* Remember where the last DATA chunk came from so we
		 * know where to send the SACK.
		 */
		if (sctp_chunk_is_data(chunk))
			asoc->peer.last_data_from = chunk->transport;

		if (chunk->transport)
			chunk->transport->last_time_heard = jiffies;

		/* Run through the state machine. */
		error = sctp_do_sm(SCTP_EVENT_T_CHUNK, SCTP_ST_CHUNK(subtype),
				   state, ep, asoc, chunk, GFP_ATOMIC);

		/* Check to see if the association is freed in response to 
		 * the incoming chunk.  If so, get out of the while loop.
		 */ 
		if (!sctp_id2assoc(sk, associd))
			goto out;

		if (error != 0)
			goto err_out;
	}

err_out:
	/* Is this the right way to pass errors up to the ULP?  */
	if (error)
		sk->err = -error;
out:
}

/* This routine moves an association from its old sk to a new sk.  */
void sctp_assoc_migrate(sctp_association_t *assoc, struct sock *newsk)
{
	sctp_opt_t *newsp = sctp_sk(newsk);

	/* Delete the association from the old endpoint's list of
	 * associations.
	 */
	list_del(&assoc->asocs);

	/* Release references to the old endpoint and the sock.  */
	sctp_endpoint_put(assoc->ep);
	sock_put(assoc->base.sk);

	/* Get a reference to the new endpoint.  */
	assoc->ep = newsp->ep;
	sctp_endpoint_hold(assoc->ep);

	/* Get a reference to the new sock.  */
	assoc->base.sk = newsk;
	sock_hold(assoc->base.sk);

	/* Add the association to the new endpoint's list of associations.  */
	sctp_endpoint_add_asoc(newsp->ep, assoc);
}

/* Update an association (possibly from unexpected COOKIE-ECHO processing).  */
void sctp_assoc_update(sctp_association_t *asoc, sctp_association_t *new)
{
	int i;

	/* Copy in new parameters of peer. */
	asoc->c = new->c;
	asoc->peer.rwnd = new->peer.rwnd;
	asoc->peer.next_dup_tsn = new->peer.next_dup_tsn;
	asoc->peer.sack_needed = new->peer.sack_needed;
	asoc->peer.i = new->peer.i;
	sctp_tsnmap_init(&asoc->peer.tsn_map, SCTP_TSN_MAP_SIZE,
			 asoc->peer.i.initial_tsn);

	/* FIXME:
	 *    Do we need to copy primary_path etc?
	 *
	 *    More explicitly, addresses may have been removed and
	 *    this needs accounting for.
	 */

	/* If the case is A (association restart), use
	 * initial_tsn as next_tsn. If the case is B, use
	 * current next_tsn in case there is data sent to peer
	 * has been discarded and needs retransmission.
	 */
	if (SCTP_STATE_ESTABLISHED == asoc->state) {
		asoc->next_tsn = new->next_tsn;
		asoc->ctsn_ack_point = new->ctsn_ack_point;

		/* Reinitialize SSN for both local streams
		 * and peer's streams.
		 */
		for (i = 0; i < SCTP_MAX_STREAM; i++) {
			asoc->ssn[i]      = 0;
			asoc->ulpq.ssn[i] = 0;
		}
	} else {
		asoc->ctsn_ack_point = asoc->next_tsn - 1;
	}
}

/* Choose the transport for sending a shutdown packet.
 * Round-robin through the active transports, else round-robin
 * through the inactive transports as this is the next best thing
 * we can try.
 */
sctp_transport_t *sctp_assoc_choose_shutdown_transport(sctp_association_t *asoc)
{
	sctp_transport_t *t, *next;
	struct list_head *head = &asoc->peer.transport_addr_list;
	struct list_head *pos;

	/* If this is the first time SHUTDOWN is sent, use the active
	 * path.
	 */
	if (!asoc->shutdown_last_sent_to)
		return asoc->peer.active_path;

	/* Otherwise, find the next transport in a round-robin fashion. */

	t = asoc->shutdown_last_sent_to;
	pos = &t->transports;
	next = NULL;

	while (1) {
		/* Skip the head. */
		if (pos->next == head)
			pos = head->next;
		else
			pos = pos->next;

		t = list_entry(pos, sctp_transport_t, transports);

		/* Try to find an active transport. */

		if (t->state.active) {
			break;
		} else {
			/* Keep track of the next transport in case
			 * we don't find any active transport.
			 */
			if (!next)
				next = t;
		}

		/* We have exhausted the list, but didn't find any
		 * other active transports.  If so, use the next
		 * transport.
		 */
		if (t == asoc->shutdown_last_sent_to) {
			t = next;
			break;
		}
	}

	return t;
}
