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
static int llc_conn_req_handler(struct llc_prim_if_block *prim);
static int llc_disc_req_handler(struct llc_prim_if_block *prim);
static int llc_rst_req_handler(struct llc_prim_if_block *prim);
static int llc_flowcontrol_req_handler(struct llc_prim_if_block *prim);
static int llc_sap_resp(struct llc_prim_if_block *prim);
static int llc_conn_rsp_handler(struct llc_prim_if_block *prim);
static int llc_rst_rsp_handler(struct llc_prim_if_block *prim);
static int llc_no_rsp_handler(struct llc_prim_if_block *prim);

/* table of request handler functions */
static llc_prim_call_t llc_req_prim[LLC_NBR_PRIMITIVES] = {
	[LLC_DATAUNIT_PRIM]	= llc_unitdata_req_handler,
	[LLC_CONN_PRIM]		= llc_conn_req_handler,
	[LLC_DATA_PRIM]		= NULL, /* replaced by llc_build_and_send_pkt */
	[LLC_DISC_PRIM]		= llc_disc_req_handler,
	[LLC_RESET_PRIM]	= llc_rst_req_handler,
	[LLC_FLOWCONTROL_PRIM]	= llc_flowcontrol_req_handler,
	[LLC_XID_PRIM]		= llc_xid_req_handler,
	[LLC_TEST_PRIM]		= llc_test_req_handler,
};

/* table of response handler functions */
static llc_prim_call_t llc_resp_prim[LLC_NBR_PRIMITIVES] = {
	[LLC_DATAUNIT_PRIM]	= llc_no_rsp_handler,
	[LLC_CONN_PRIM]		= llc_conn_rsp_handler,
	[LLC_DATA_PRIM]		= llc_no_rsp_handler,
	[LLC_DISC_PRIM]		= llc_no_rsp_handler,
	[LLC_RESET_PRIM]	= llc_rst_rsp_handler,
	[LLC_FLOWCONTROL_PRIM]	= llc_no_rsp_handler,
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
	sap->resp = llc_sap_resp;
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

	lock_sock(sk);
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
	release_sock(sk);
	return rc;
}

/**
 *	llc_confirm_impossible - Informs upper layer about failed connection
 *	@prim: pointer to structure that contains confirmation data.
 *
 *	Informs upper layer about failing in connection establishment. This
 *	function is called by llc_conn_req_handler.
 */
static void llc_confirm_impossible(struct llc_prim_if_block *prim)
{
	prim->data->conn.status = LLC_STATUS_IMPOSSIBLE;
	prim->sap->conf(prim);
}

/**
 *	llc_conn_req_handler - Called by upper layer to establish a conn
 *	@prim: pointer to structure that contains service parameters.
 *
 *	Upper layer calls this to establish an LLC connection with a remote
 *	machine. This function packages a proper event and sends it connection
 *	component state machine. Success or failure of connection
 *	establishment will inform to upper layer via calling it's confirm
 *	function and passing proper information.
 */
static int llc_conn_req_handler(struct llc_prim_if_block *prim)
{
	int rc = -EBUSY;
	struct llc_opt *llc;
	struct llc_sap *sap = prim->sap;
	struct sk_buff *skb;
	struct net_device *ddev = mac_dev_peer(prim->data->conn.dev,
					       prim->data->conn.dev->type,
					       prim->data->conn.daddr.mac),
			  *sdev = (ddev->flags & IFF_LOOPBACK) ?
				  ddev : prim->data->conn.dev;
	struct llc_addr laddr, daddr;
	/* network layer supplies addressing required to establish connection;
	 * package as an event and send it to the connection event handler
	 */
	struct sock *sk;

	memcpy(laddr.mac, sdev->dev_addr, sizeof(laddr.mac));
	laddr.lsap = prim->data->conn.saddr.lsap;
	memcpy(daddr.mac, prim->data->conn.daddr.mac, sizeof(daddr.mac));
	daddr.lsap = prim->data->conn.daddr.lsap;
	sk = llc_lookup_established(sap, &daddr, &laddr);
	if (sk) {
		llc_confirm_impossible(prim);
		goto out_put;
	}
	rc = -ENOMEM;
	if (prim->data->conn.sk) {
		sk = prim->data->conn.sk;
		if (llc_sock_init(sk))
			goto out;
	} else {
		/*
		 * FIXME: this one will die as soon as core and
		 * llc_sock starts sharing a struct sock.
		 */
		sk = llc_sock_alloc(PF_LLC);
		if (!sk) {
			llc_confirm_impossible(prim);
			goto out;
		}
		prim->data->conn.sk = sk;
	}
	sock_hold(sk);
	lock_sock(sk);
	/* assign new connection to it's SAP */
	llc_sap_assign_sock(sap, sk);
	llc = llc_sk(sk);
	memcpy(&llc->daddr, &daddr, sizeof(llc->daddr));
	memcpy(&llc->laddr, &laddr, sizeof(llc->laddr));
	llc->dev     = ddev;
	llc->link    = prim->data->conn.link;
	llc->handler = prim->data->conn.handler;
	skb = alloc_skb(1, GFP_ATOMIC);
	if (skb) {
		struct llc_conn_state_ev *ev = llc_conn_ev(skb);

		ev->type	   = LLC_CONN_EV_TYPE_PRIM;
		ev->data.prim.prim = LLC_CONN_PRIM;
		ev->data.prim.type = LLC_PRIM_TYPE_REQ;
		ev->data.prim.data = prim;
		rc = llc_conn_state_process(sk, skb);
	}
	if (rc) {
		llc_sap_unassign_sock(sap, sk);
		llc_sock_free(sk);
		llc_confirm_impossible(prim);
	}
	release_sock(sk);
out_put:
	sock_put(sk);
out:
	return rc;
}

/**
 *	llc_disc_req_handler - Called by upper layer to close a connection
 *	@prim: pointer to structure that contains service parameters.
 *
 *	Upper layer calls this when it wants to close an established LLC
 *	connection with a remote machine. This function packages a proper event
 *	and sends it to connection component state machine. Returns 0 for
 *	success, 1 otherwise.
 */
static int llc_disc_req_handler(struct llc_prim_if_block *prim)
{
	u16 rc = 1;
	struct llc_conn_state_ev *ev;
	struct sk_buff *skb;
	struct sock* sk = prim->data->disc.sk;

	sock_hold(sk);
	lock_sock(sk);
	if (llc_sk(sk)->state == LLC_CONN_STATE_ADM ||
	    llc_sk(sk)->state == LLC_CONN_OUT_OF_SVC)
		goto out;
	/*
	 * Postpone unassigning the connection from its SAP and returning the
	 * connection until all ACTIONs have been completely executed
	 */
	skb = alloc_skb(1, GFP_ATOMIC);
	if (!skb)
		goto out;
	ev = llc_conn_ev(skb);
	ev->type	   = LLC_CONN_EV_TYPE_PRIM;
	ev->data.prim.prim = LLC_DISC_PRIM;
	ev->data.prim.type = LLC_PRIM_TYPE_REQ;
	ev->data.prim.data = prim;
	rc = llc_conn_state_process(sk, skb);
out:
	release_sock(sk);
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

	lock_sock(sk);
	skb = alloc_skb(1, GFP_ATOMIC);
	if (skb) {
		struct llc_conn_state_ev *ev = llc_conn_ev(skb);

		ev->type = LLC_CONN_EV_TYPE_PRIM;
		ev->data.prim.prim = LLC_RESET_PRIM;
		ev->data.prim.type = LLC_PRIM_TYPE_REQ;
		ev->data.prim.data = prim;
		rc = llc_conn_state_process(sk, skb);
	}
	release_sock(sk);
	return rc;
}

/* We don't support flow control. The original code from procom has
 * some bits, but for now I'm cleaning this
 */
static int llc_flowcontrol_req_handler(struct llc_prim_if_block *prim)
{
	return 1;
}

/**
 *	llc_sap_resp - Sends response to peer
 *	@prim: pointer to structure that contains service parameters
 *
 *	This function is a interface function to upper layer. Each one who
 *	wants to response to an indicate can call this function via calling
 *	sap_resp with proper service parameters. Returns 0 for success, 1
 *	otherwise.
 */
static int llc_sap_resp(struct llc_prim_if_block *prim)
{
	u16 rc = 1;
	/* network layer RESPONSE primitive received; package primitive
	 * as an event and send it to the connection event handler
	 */
	if (prim->prim < LLC_NBR_PRIMITIVES)
	       /* valid primitive; call the function to handle it */
		rc = llc_resp_prim[prim->prim](prim);
	return rc;
}

/**
 *	llc_conn_rsp_handler - Response to connect indication
 *	@prim: pointer to structure that contains response info.
 *
 *	Response to connect indication.
 */
static int llc_conn_rsp_handler(struct llc_prim_if_block *prim)
{
	struct sock *sk = prim->data->conn.sk;

	llc_sk(sk)->link = prim->data->conn.link;
	return 0;
}

/**
 *	llc_rst_rsp_handler - Response to RESET indication
 *	@prim: pointer to structure that contains response info
 *
 *	Returns 0 for success, 1 otherwise
 */
static int llc_rst_rsp_handler(struct llc_prim_if_block *prim)
{
	int rc = 1;
	/*
	 * Network layer supplies connection handle; map it to a connection;
	 * package as event and send it to connection event handler
	 */
	struct sock *sk = prim->data->res.sk;
	struct sk_buff *skb = alloc_skb(1, GFP_ATOMIC);

	if (skb) {
		struct llc_conn_state_ev *ev = llc_conn_ev(skb);

		ev->type	   = LLC_CONN_EV_TYPE_PRIM;
		ev->data.prim.prim = LLC_RESET_PRIM;
		ev->data.prim.type = LLC_PRIM_TYPE_RESP;
		ev->data.prim.data = prim;
		rc = llc_conn_state_process(sk, skb);
	}
	return rc;
}

static int llc_no_rsp_handler(struct llc_prim_if_block *prim)
{
	return 0;
}

EXPORT_SYMBOL(llc_sap_open);
EXPORT_SYMBOL(llc_sap_close);
