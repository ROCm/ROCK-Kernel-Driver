/*
 * llc_if.c - Defines LLC interface to upper layer
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
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
#include <net/llc_mac.h>

static int llc_sap_req(struct llc_prim_if_block *prim);
static int llc_unitdata_req_handler(struct llc_prim_if_block *prim);
static int llc_test_req_handler(struct llc_prim_if_block *prim);
static int llc_xid_req_handler(struct llc_prim_if_block *prim);
static int llc_rst_req_handler(struct llc_prim_if_block *prim);
static int llc_flowcontrol_req_handler(struct llc_prim_if_block *prim);

/* table of request handler functions */
static llc_prim_call_t llc_req_prim[LLC_NBR_PRIMITIVES] = {
	[LLC_DATAUNIT_PRIM]	= llc_unitdata_req_handler,
	[LLC_CONN_PRIM]		= NULL, /* replaced by llc_establish_connection */
	[LLC_DATA_PRIM]		= NULL, /* replaced by llc_build_and_send_pkt */
	[LLC_DISC_PRIM]		= NULL, /* replaced by llc_send_disc */
	[LLC_RESET_PRIM]	= llc_rst_req_handler,
	[LLC_FLOWCONTROL_PRIM]	= llc_flowcontrol_req_handler,
	[LLC_XID_PRIM]		= llc_xid_req_handler,
	[LLC_TEST_PRIM]		= llc_test_req_handler,
};

/**
 *	llc_sap_open - open interface to the upper layers.
 *	@nw_indicate: pointer to indicate function of upper layer.
 *	@nw_confirm: pointer to confirm function of upper layer.
 *	@lsap: SAP number.
 *	@sap: pointer to allocated SAP (output argument).
 *
 *	Interface function to upper layer. Each one who wants to get a SAP
 *	(for example NetBEUI) should call this function. Returns the opened
 *	SAP for success, NULL for failure.
 */
struct llc_sap *llc_sap_open(llc_prim_call_t nw_indicate,
			     llc_prim_call_t nw_confirm, u8 lsap)
{
	/* verify this SAP is not already open; if so, return error */
	struct llc_sap *sap;

	MOD_INC_USE_COUNT;
	sap = llc_sap_find(lsap);
	if (sap) { /* SAP already exists */
		sap = NULL;
		goto err;
	}
	/* sap requested does not yet exist */
	sap = llc_sap_alloc();
	if (!sap)
		goto err;
	/* allocated a SAP; initialize it and clear out its memory pool */
	sap->laddr.lsap = lsap;
	sap->req = llc_sap_req;
	sap->ind = nw_indicate;
	sap->conf = nw_confirm;
	sap->parent_station = llc_station_get();
	/* initialized SAP; add it to list of SAPs this station manages */
	llc_sap_save(sap);
out:
	return sap;
err:
	MOD_DEC_USE_COUNT;
	goto out;
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
	MOD_DEC_USE_COUNT;
}

/**
 *	llc_sap_req - Request interface for upper layers
 *	@prim: pointer to structure that contains service parameters.
 *
 *	Request interface function to upper layer. Each one who wants to
 *	request a service from LLC, must call this function. Details of
 *	requested service is defined in input argument(prim). Returns 0 for
 *	success, 1 otherwise.
 */
static int llc_sap_req(struct llc_prim_if_block *prim)
{
	int rc = 1;

	if (prim->prim > 8 || prim->prim == 6) {
		printk(KERN_ERR "%s: invalid primitive %d\n", __FUNCTION__,
			prim->prim);
		goto out;
	}
	/* receive REQUEST primitive from network layer; call the appropriate
	 * primitive handler which then packages it up as an event and sends it
	 * to the SAP or CONNECTION event handler
	 */
	if (prim->prim < LLC_NBR_PRIMITIVES)
	       /* valid primitive; call the function to handle it */
		rc = llc_req_prim[prim->prim](prim);
out:
	return rc;
}

/**
 *	llc_unitdata_req_handler - unitdata request interface for upper layers
 *	@prim: pointer to structure that contains service parameters
 *
 *	Upper layers calls this function when upper layer wants to send data
 *	using connection-less mode communication (UI pdu). Returns 0 for
 *	success, 1 otherwise.
 */
static int llc_unitdata_req_handler(struct llc_prim_if_block *prim)
{
	int rc = 1;
	struct llc_sap_state_ev *ev;
	/* accept data frame from network layer to be sent using connection-
	 * less mode communication; timeout/retries handled by network layer;
	 * package primitive as an event and send to SAP event handler
	 */
	struct llc_sap *sap = llc_sap_find(prim->data->udata.saddr.lsap);

	if (!sap)
		goto out;
	ev = llc_sap_ev(prim->data->udata.skb);
	ev->type	   = LLC_SAP_EV_TYPE_PRIM;
	ev->data.prim.prim = LLC_DATAUNIT_PRIM;
	ev->data.prim.type = LLC_PRIM_TYPE_REQ;
	ev->data.prim.data = prim;
	rc = 0;
	llc_sap_state_process(sap, prim->data->udata.skb);
out:
	return rc;
}

/**
 *	llc_test_req_handler - TEST interface for upper layers.
 *	@prim: pointer to structure that contains service parameters.
 *
 *	This function is called when upper layer wants to send a TEST pdu.
 *	Returns 0 for success, 1 otherwise.
 */
static int llc_test_req_handler(struct llc_prim_if_block *prim)
{
	int rc = 1;
	struct llc_sap_state_ev *ev;
	/* package primitive as an event and send to SAP event handler */
	struct llc_sap *sap = llc_sap_find(prim->data->udata.saddr.lsap);
	if (!sap)
		goto out;
	ev = llc_sap_ev(prim->data->udata.skb);
	ev->type	   = LLC_SAP_EV_TYPE_PRIM;
	ev->data.prim.prim = LLC_TEST_PRIM;
	ev->data.prim.type = LLC_PRIM_TYPE_REQ;
	ev->data.prim.data = prim;
	rc = 0;
	llc_sap_state_process(sap, prim->data->udata.skb);
out:
	return rc;
}

/**
 *	llc_xid_req_handler - XID interface for upper layers
 *	@prim: pointer to structure that contains service parameters.
 *
 *	This function is called when upper layer wants to send a XID pdu.
 *	Returns 0 for success, 1 otherwise.
 */
static int llc_xid_req_handler(struct llc_prim_if_block *prim)
{
	int rc = 1;
	struct llc_sap_state_ev *ev;
	/* package primitive as an event and send to SAP event handler */
	struct llc_sap *sap = llc_sap_find(prim->data->udata.saddr.lsap);

	if (!sap)
		goto out;
	ev = llc_sap_ev(prim->data->udata.skb);
	ev->type	   = LLC_SAP_EV_TYPE_PRIM;
	ev->data.prim.prim = LLC_XID_PRIM;
	ev->data.prim.type = LLC_PRIM_TYPE_REQ;
	ev->data.prim.data = prim;
	rc = 0;
	llc_sap_state_process(sap, prim->data->udata.skb);
out:
	return rc;
}

/**
 *	llc_build_and_send_pkt - Connection data sending for upper layers.
 *	@prim: pointer to structure that contains service parameters
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
	ev->type	    = LLC_CONN_EV_TYPE_PRIM;
	ev->data.prim.prim  = LLC_DATA_PRIM;
	ev->data.prim.type  = LLC_PRIM_TYPE_REQ;
	ev->data.prim.data  = NULL;
	skb->dev	    = llc->dev;
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
		if (existing->state == TCP_ESTABLISHED) {
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

		ev->type	   = LLC_CONN_EV_TYPE_PRIM;
		ev->data.prim.prim = LLC_CONN_PRIM;
		ev->data.prim.type = LLC_PRIM_TYPE_REQ;
		ev->data.prim.data = NULL;
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
	if (sk->type != SOCK_STREAM || sk->state != TCP_ESTABLISHED ||
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
	sk->state	   = TCP_CLOSING;
	ev		   = llc_conn_ev(skb);
	ev->type	   = LLC_CONN_EV_TYPE_PRIM;
	ev->data.prim.prim = LLC_DISC_PRIM;
	ev->data.prim.type = LLC_PRIM_TYPE_REQ;
	ev->data.prim.data = NULL;
	rc = llc_conn_state_process(sk, skb);
out:
	sock_put(sk);
	return rc;
}

/**
 *	llc_rst_req_handler - Resets an established LLC connection
 *	@prim: pointer to structure that contains service parameters.
 *
 *	Called when upper layer wants to reset an established LLC connection
 *	with a remote machine. This function packages a proper event and sends
 *	it to connection component state machine. Returns 0 for success, 1
 *	otherwise.
 */
static int llc_rst_req_handler(struct llc_prim_if_block *prim)
{
	struct sk_buff *skb;
	int rc = 1;
	struct sock *sk = prim->data->res.sk;

	skb = alloc_skb(0, GFP_ATOMIC);
	if (skb) {
		struct llc_conn_state_ev *ev = llc_conn_ev(skb);

		ev->type = LLC_CONN_EV_TYPE_PRIM;
		ev->data.prim.prim = LLC_RESET_PRIM;
		ev->data.prim.type = LLC_PRIM_TYPE_REQ;
		ev->data.prim.data = prim;
		rc = llc_conn_state_process(sk, skb);
	}
	return rc;
}

/* We don't support flow control. The original code from procom has
 * some bits, but for now I'm cleaning this
 */
static int llc_flowcontrol_req_handler(struct llc_prim_if_block *prim)
{
	return 1;
}

EXPORT_SYMBOL(llc_sap_open);
EXPORT_SYMBOL(llc_sap_close);
