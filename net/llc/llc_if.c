/*
 * llc_if.c - Defines LLC interface to upper layer
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/tcp.h>
#include <asm/errno.h>
#include <net/llc_if.h>
#include <net/llc_sap.h>
#include <net/llc_s_ev.h>
#include <net/llc_conn.h>
#include <net/sock.h>
#include <net/llc_c_ev.h>
#include <net/llc_c_ac.h>
#include <net/llc_c_st.h>
#include <net/llc_main.h>

/**
 *	llc_sap_open - open interface to the upper layers.
 *	@lsap: SAP number.
 *	@func: rcv func for datalink protos
 *
 *	Interface function to upper layer. Each one who wants to get a SAP
 *	(for example NetBEUI) should call this function. Returns the opened
 *	SAP for success, NULL for failure.
 */
struct llc_sap *llc_sap_open(u8 lsap, int (*func)(struct sk_buff *skb,
						  struct net_device *dev,
						  struct packet_type *pt))
{
	/* verify this SAP is not already open; if so, return error */
	struct llc_sap *sap;

	sap = llc_sap_find(lsap);
	if (sap) { /* SAP already exists */
		sap = NULL;
		goto out;
	}
	/* sap requested does not yet exist */
	sap = llc_sap_alloc();
	if (!sap)
		goto out;
	/* allocated a SAP; initialize it and clear out its memory pool */
	sap->laddr.lsap = lsap;
	sap->rcv_func	= func;
	sap->station	= &llc_main_station;
	/* initialized SAP; add it to list of SAPs this station manages */
	llc_sap_save(sap);
out:
	return sap;
}

/**
 *	llc_sap_close - close interface for upper layers.
 *	@sap: SAP to be closed.
 *
 *	Close interface function to upper layer. Each one who wants to
 *	close an open SAP (for example NetBEUI) should call this function.
 */
void llc_sap_close(struct llc_sap *sap)
{
	llc_free_sap(sap);
}

/**
 *	llc_build_and_send_ui_pkt - unitdata request interface for upper layers
 *	@sap: sap to use
 *	@skb: packet to send
 *	@dmac: destination mac address
 *	@dsap: destination sap
 *
 *	Upper layers calls this function when upper layer wants to send data
 *	using connection-less mode communication (UI pdu).
 *
 *	Accept data frame from network layer to be sent using connection-
 *	less mode communication; timeout/retries handled by network layer;
 *	package primitive as an event and send to SAP event handler
 */
void llc_build_and_send_ui_pkt(struct llc_sap *sap, struct sk_buff *skb,
			       u8 *dmac, u8 dsap)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	ev->saddr.lsap = sap->laddr.lsap;
	ev->daddr.lsap = dsap;
	memcpy(ev->saddr.mac, skb->dev->dev_addr, IFHWADDRLEN);
	memcpy(ev->daddr.mac, dmac, IFHWADDRLEN);

	ev->type      = LLC_SAP_EV_TYPE_PRIM;
	ev->prim      = LLC_DATAUNIT_PRIM;
	ev->prim_type = LLC_PRIM_TYPE_REQ;
	llc_sap_state_process(sap, skb);
}

/**
 *	llc_build_and_send_test_pkt - TEST interface for upper layers.
 *	@sap: sap to use
 *	@skb: packet to send
 *	@dmac: destination mac address
 *	@dsap: destination sap
 *
 *	This function is called when upper layer wants to send a TEST pdu.
 *	Returns 0 for success, 1 otherwise.
 */
void llc_build_and_send_test_pkt(struct llc_sap *sap, 
				 struct sk_buff *skb, u8 *dmac, u8 dsap)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	ev->saddr.lsap = sap->laddr.lsap;
	ev->daddr.lsap = dsap;
	memcpy(ev->saddr.mac, skb->dev->dev_addr, IFHWADDRLEN);
	memcpy(ev->daddr.mac, dmac, IFHWADDRLEN);
	
	ev->type      = LLC_SAP_EV_TYPE_PRIM;
	ev->prim      = LLC_TEST_PRIM;
	ev->prim_type = LLC_PRIM_TYPE_REQ;
	llc_sap_state_process(sap, skb);
}

/**
 *	llc_build_and_send_xid_pkt - XID interface for upper layers
 *	@sap: sap to use
 *	@skb: packet to send
 *	@dmac: destination mac address
 *	@dsap: destination sap
 *
 *	This function is called when upper layer wants to send a XID pdu.
 *	Returns 0 for success, 1 otherwise.
 */
void llc_build_and_send_xid_pkt(struct llc_sap *sap, struct sk_buff *skb,
				u8 *dmac, u8 dsap)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	ev->saddr.lsap = sap->laddr.lsap;
	ev->daddr.lsap = dsap;
	memcpy(ev->saddr.mac, skb->dev->dev_addr, IFHWADDRLEN);
	memcpy(ev->daddr.mac, dmac, IFHWADDRLEN);

	ev->type      = LLC_SAP_EV_TYPE_PRIM;
	ev->prim      = LLC_XID_PRIM;
	ev->prim_type = LLC_PRIM_TYPE_REQ;
	llc_sap_state_process(sap, skb);
}

/**
 *	llc_build_and_send_pkt - Connection data sending for upper layers.
 *	@sk: connection
 *	@skb: packet to send
 *
 *	This function is called when upper layer wants to send data using
 *	connection oriented communication mode. During sending data, connection
 *	will be locked and received frames and expired timers will be queued.
 *	Returns 0 for success, -ECONNABORTED when the connection already
 *	closed and -EBUSY when sending data is not permitted in this state or
 *	LLC has send an I pdu with p bit set to 1 and is waiting for it's
 *	response.
 */
int llc_build_and_send_pkt(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev;
	int rc = -ECONNABORTED;
	struct llc_opt *llc = llc_sk(sk);

	if (llc->state == LLC_CONN_STATE_ADM)
		goto out;
	rc = -EBUSY;
	if (llc_data_accept_state(llc->state)) { /* data_conn_refuse */
		llc->failed_data_req = 1;
		goto out;
	}
	if (llc->p_flag) {
		llc->failed_data_req = 1;
		goto out;
	}
	ev = llc_conn_ev(skb);
	ev->type      = LLC_CONN_EV_TYPE_PRIM;
	ev->prim      = LLC_DATA_PRIM;
	ev->prim_type = LLC_PRIM_TYPE_REQ;
	skb->dev      = llc->dev;
	rc = llc_conn_state_process(sk, skb);
out:
	return rc;
}

/**
 *	llc_establish_connection - Called by upper layer to establish a conn
 *	@sk: connection
 *	@lmac: local mac address
 *	@dmac: destination mac address
 *	@dsap: destination sap
 *
 *	Upper layer calls this to establish an LLC connection with a remote
 *	machine. This function packages a proper event and sends it connection
 *	component state machine. Success or failure of connection
 *	establishment will inform to upper layer via calling it's confirm
 *	function and passing proper information.
 */
int llc_establish_connection(struct sock *sk, u8 *lmac, u8 *dmac, u8 dsap)
{
	int rc = -EISCONN;
	struct llc_addr laddr, daddr;
	struct sk_buff *skb;
	struct llc_opt *llc = llc_sk(sk);
	struct sock *existing;

	laddr.lsap = llc->sap->laddr.lsap;
	daddr.lsap = dsap;
	memcpy(daddr.mac, dmac, sizeof(daddr.mac));
	memcpy(laddr.mac, lmac, sizeof(laddr.mac));
	existing = llc_lookup_established(llc->sap, &daddr, &laddr);
	if (existing) {
		if (existing->sk_state == TCP_ESTABLISHED) {
			sk = existing;
			goto out_put;
		} else
			sock_put(existing);
	}
	sock_hold(sk);
	rc = -ENOMEM;
	skb = alloc_skb(0, GFP_ATOMIC);
	if (skb) {
		struct llc_conn_state_ev *ev = llc_conn_ev(skb);

		ev->type      = LLC_CONN_EV_TYPE_PRIM;
		ev->prim      = LLC_CONN_PRIM;
		ev->prim_type = LLC_PRIM_TYPE_REQ;
		rc = llc_conn_state_process(sk, skb);
	}
out_put:
	sock_put(sk);
	return rc;
}

/**
 *	llc_send_disc - Called by upper layer to close a connection
 *	@sk: connection to be closed
 *
 *	Upper layer calls this when it wants to close an established LLC
 *	connection with a remote machine. This function packages a proper event
 *	and sends it to connection component state machine. Returns 0 for
 *	success, 1 otherwise.
 */
int llc_send_disc(struct sock *sk)
{
	u16 rc = 1;
	struct llc_conn_state_ev *ev;
	struct sk_buff *skb;

	sock_hold(sk);
	if (sk->sk_type != SOCK_STREAM || sk->sk_state != TCP_ESTABLISHED ||
	    llc_sk(sk)->state == LLC_CONN_STATE_ADM ||
	    llc_sk(sk)->state == LLC_CONN_OUT_OF_SVC)
		goto out;
	/*
	 * Postpone unassigning the connection from its SAP and returning the
	 * connection until all ACTIONs have been completely executed
	 */
	skb = alloc_skb(0, GFP_ATOMIC);
	if (!skb)
		goto out;
	sk->sk_state  = TCP_CLOSING;
	ev	      = llc_conn_ev(skb);
	ev->type      = LLC_CONN_EV_TYPE_PRIM;
	ev->prim      = LLC_DISC_PRIM;
	ev->prim_type = LLC_PRIM_TYPE_REQ;
	rc = llc_conn_state_process(sk, skb);
out:
	sock_put(sk);
	return rc;
}

/**
 *	llc_build_and_send_reset_pkt - Resets an established LLC connection
 *	@prim: pointer to structure that contains service parameters.
 *
 *	Called when upper layer wants to reset an established LLC connection
 *	with a remote machine. This function packages a proper event and sends
 *	it to connection component state machine. Returns 0 for success, 1
 *	otherwise.
 */
int llc_build_and_send_reset_pkt(struct sock *sk)
{
	int rc = 1;
	struct sk_buff *skb = alloc_skb(0, GFP_ATOMIC);

	if (skb) {
		struct llc_conn_state_ev *ev = llc_conn_ev(skb);

		ev->type      = LLC_CONN_EV_TYPE_PRIM;
		ev->prim      = LLC_RESET_PRIM;
		ev->prim_type = LLC_PRIM_TYPE_REQ;
		rc = llc_conn_state_process(sk, skb);
	}
	return rc;
}

EXPORT_SYMBOL(llc_sap_open);
EXPORT_SYMBOL(llc_sap_close);
EXPORT_SYMBOL(llc_build_and_send_ui_pkt);
