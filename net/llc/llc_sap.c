/*
 * llc_sap.c - driver routines for SAP component.
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
#include <linux/skbuff.h>
#include <net/llc_conn.h>
#include <net/llc_sap.h>
#include <net/llc_s_ev.h>
#include <net/llc_s_ac.h>
#include <net/llc_s_st.h>
#include <net/sock.h>
#include <linux/tcp.h>
#include <net/llc_main.h>
#include <net/llc_pdu.h>
#include <linux/if_tr.h>

void llc_save_primitive(struct sk_buff* skb, u8 prim)
{
	struct sockaddr_llc *addr = llc_ui_skb_cb(skb);

       /* save primitive for use by the user. */
	addr->sllc_family = skb->sk->sk_family;
	addr->sllc_arphrd = skb->dev->type;
	addr->sllc_test   = prim == LLC_TEST_PRIM;
	addr->sllc_xid    = prim == LLC_XID_PRIM;
	addr->sllc_ua     = prim == LLC_DATAUNIT_PRIM;
	llc_pdu_decode_sa(skb, addr->sllc_smac);
	llc_pdu_decode_da(skb, addr->sllc_dmac);
	llc_pdu_decode_dsap(skb, &addr->sllc_dsap);
	llc_pdu_decode_ssap(skb, &addr->sllc_ssap);
}

/**
 *	llc_sap_assign_sock - adds a connection to a SAP
 *	@sap: pointer to SAP.
 *	@conn: pointer to connection.
 *
 *	This function adds a connection to connection_list of a SAP.
 */
void llc_sap_assign_sock(struct llc_sap *sap, struct sock *sk)
{
	write_lock_bh(&sap->sk_list.lock);
	llc_sk(sk)->sap = sap;
	sk_add_node(sk, &sap->sk_list.list);
	write_unlock_bh(&sap->sk_list.lock);
}

/**
 *	llc_sap_unassign_sock - removes a connection from SAP
 *	@sap: SAP
 *	@sk: pointer to connection
 *
 *	This function removes a connection from sk_list.list of a SAP if
 *	the connection was in this list.
 */
void llc_sap_unassign_sock(struct llc_sap *sap, struct sock *sk)
{
	write_lock_bh(&sap->sk_list.lock);
	sk_del_node_init(sk);
	write_unlock_bh(&sap->sk_list.lock);
}

/**
 *	llc_sap_rtn_pdu - Informs upper layer on rx of an UI, XID or TEST pdu.
 *	@sap: pointer to SAP
 *	@skb: received pdu
 */
void llc_sap_rtn_pdu(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	switch (LLC_U_PDU_RSP(pdu)) {
	case LLC_1_PDU_CMD_TEST:
		ev->prim = LLC_TEST_PRIM;	break;
	case LLC_1_PDU_CMD_XID:
		ev->prim = LLC_XID_PRIM;	break;
	case LLC_1_PDU_CMD_UI:
		ev->prim = LLC_DATAUNIT_PRIM;	break;
	}
	ev->ind_cfm_flag = LLC_IND;
}

/**
 *	llc_find_sap_trans - finds transition for event
 *	@sap: pointer to SAP
 *	@skb: happened event
 *
 *	This function finds transition that matches with happened event.
 *	Returns the pointer to found transition on success or %NULL for
 *	failure.
 */
static struct llc_sap_state_trans *llc_find_sap_trans(struct llc_sap *sap,
						      struct sk_buff* skb)
{
	int i = 0;
	struct llc_sap_state_trans *rc = NULL;
	struct llc_sap_state_trans **next_trans;
	struct llc_sap_state *curr_state = &llc_sap_state_table[sap->state - 1];
	/*
	 * Search thru events for this state until list exhausted or until
	 * its obvious the event is not valid for the current state
	 */
	for (next_trans = curr_state->transitions; next_trans[i]->ev; i++)
		if (!next_trans[i]->ev(sap, skb)) {
			rc = next_trans[i]; /* got event match; return it */
			break;
		}
	return rc;
}

/**
 *	llc_exec_sap_trans_actions - execute actions related to event
 *	@sap: pointer to SAP
 *	@trans: pointer to transition that it's actions must be performed
 *	@skb: happened event.
 *
 *	This function executes actions that is related to happened event.
 *	Returns 0 for success and 1 for failure of at least one action.
 */
static int llc_exec_sap_trans_actions(struct llc_sap *sap,
				      struct llc_sap_state_trans *trans,
				      struct sk_buff *skb)
{
	int rc = 0;
	llc_sap_action_t *next_action = trans->ev_actions;

	for (; next_action && *next_action; next_action++)
		if ((*next_action)(sap, skb))
			rc = 1;
	return rc;
}

/**
 *	llc_sap_next_state - finds transition, execs actions & change SAP state
 *	@sap: pointer to SAP
 *	@skb: happened event
 *
 *	This function finds transition that matches with happened event, then
 *	executes related actions and finally changes state of SAP. It returns
 *	0 on success and 1 for failure.
 */
static int llc_sap_next_state(struct llc_sap *sap, struct sk_buff *skb)
{
	int rc = 1;
	struct llc_sap_state_trans *trans;

	if (sap->state > LLC_NR_SAP_STATES)
		goto out;
	trans = llc_find_sap_trans(sap, skb);
	if (!trans)
		goto out;
	/*
	 * Got the state to which we next transition; perform the actions
	 * associated with this transition before actually transitioning to the
	 * next state
	 */
	rc = llc_exec_sap_trans_actions(sap, trans, skb);
	if (rc)
		goto out;
	/*
	 * Transition SAP to next state if all actions execute successfully
	 */
	sap->state = trans->next_state;
out:
	return rc;
}

/**
 *	llc_sap_state_process - sends event to SAP state machine
 *	@sap: sap to use
 *	@skb: pointer to occurred event
 *
 *	After executing actions of the event, upper layer will be indicated
 *	if needed(on receiving an UI frame). sk can be null for the
 *	datalink_proto case.
 */
void llc_sap_state_process(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	/*
	 * We have to hold the skb, because llc_sap_next_state
	 * will kfree it in the sending path and we need to
	 * look at the skb->cb, where we encode llc_sap_state_ev.
	 */
	skb_get(skb);
	ev->ind_cfm_flag = 0;
	llc_sap_next_state(sap, skb);
	if (ev->ind_cfm_flag == LLC_IND) {
		if (skb->sk->sk_state == TCP_LISTEN)
			kfree_skb(skb);
		else {
			llc_save_primitive(skb, ev->prim);

			/* queue skb to the user. */
			if (sock_queue_rcv_skb(skb->sk, skb))
				kfree_skb(skb);
		}
	} 
	kfree_skb(skb);
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
 *	llc_sap_rcv - sends received pdus to the sap state machine
 *	@sap: current sap component structure.
 *	@skb: received frame.
 *
 *	Sends received pdus to the sap state machine.
 */
static void llc_sap_rcv(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	ev->type   = LLC_SAP_EV_TYPE_PDU;
	ev->reason = 0;
	llc_sap_state_process(sap, skb);
}

/**
 *	llc_lookup_dgram - Finds dgram socket for the local sap/mac
 *	@sap: SAP
 *	@laddr: address of local LLC (MAC + SAP)
 *
 *	Search socket list of the SAP and finds connection using the local
 *	mac, and local sap. Returns pointer for socket found, %NULL otherwise.
 */
struct sock *llc_lookup_dgram(struct llc_sap *sap, struct llc_addr *laddr)
{
	struct sock *rc;
	struct hlist_node *node;

	read_lock_bh(&sap->sk_list.lock);
	sk_for_each(rc, node, &sap->sk_list.list) {
		struct llc_opt *llc = llc_sk(rc);

		if (rc->sk_type == SOCK_DGRAM &&
		    llc->laddr.lsap == laddr->lsap &&
		    llc_mac_match(llc->laddr.mac, laddr->mac)) {
			sock_hold(rc);
			goto found;
		}
	}
	rc = NULL;
found:
	read_unlock_bh(&sap->sk_list.lock);
	return rc;
}

void llc_sap_handler(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_addr laddr;
	struct sock *sk;

	llc_pdu_decode_da(skb, laddr.mac);
	llc_pdu_decode_dsap(skb, &laddr.lsap);

	sk = llc_lookup_dgram(sap, &laddr);
	if (sk) {
		skb->sk = sk;
		llc_sap_rcv(sap, skb);
		sock_put(sk);
	} else
		kfree_skb(skb);
}

/**
 *	llc_sap_alloc - allocates and initializes sap.
 *
 *	Allocates and initializes sap.
 */
struct llc_sap *llc_sap_alloc(void)
{
	struct llc_sap *sap = kmalloc(sizeof(*sap), GFP_ATOMIC);

	if (sap) {
		memset(sap, 0, sizeof(*sap));
		sap->state = LLC_SAP_STATE_ACTIVE;
		memcpy(sap->laddr.mac, llc_main_station.mac_sa, ETH_ALEN);
		rwlock_init(&sap->sk_list.lock);
	}
	return sap;
}

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
 * 	Removes this sap from the list of saps in the station and then
 * 	frees the memory for this sap.
 */
void llc_sap_close(struct llc_sap *sap)
{
	WARN_ON(!hlist_empty(&sap->sk_list.list));
	write_lock_bh(&sap->station->sap_list.lock);
	list_del(&sap->node);
	write_unlock_bh(&sap->station->sap_list.lock);
	kfree(sap);
}

EXPORT_SYMBOL(llc_sap_open);
EXPORT_SYMBOL(llc_sap_close);
EXPORT_SYMBOL(llc_build_and_send_ui_pkt);
