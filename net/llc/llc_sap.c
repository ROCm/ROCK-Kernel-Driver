/*
 * llc_sap.c - driver routines for SAP component.
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
#include <linux/skbuff.h>
#include <net/llc_conn.h>
#include <net/llc_sap.h>
#include <net/llc_s_ev.h>
#include <net/llc_s_ac.h>
#include <net/llc_s_st.h>
#include <net/sock.h>
#include <linux/tcp.h>
#include <net/llc_main.h>
#include <net/llc_mac.h>
#include <net/llc_pdu.h>
#include <linux/if_tr.h>

static int llc_sap_next_state(struct llc_sap *sap, struct sk_buff *skb);
static int llc_exec_sap_trans_actions(struct llc_sap *sap,
				      struct llc_sap_state_trans *trans,
				      struct sk_buff *skb);
static struct llc_sap_state_trans *llc_find_sap_trans(struct llc_sap *sap,
						      struct sk_buff *skb);

/**
 *	llc_sap_assign_sock - adds a connection to a SAP
 *	@sap: pointer to SAP.
 *	@conn: pointer to connection.
 *
 *	This function adds a connection to connection_list of a SAP.
 */
void llc_sap_assign_sock(struct llc_sap *sap, struct sock *sk)
{
	spin_lock_bh(&sap->sk_list.lock);
	llc_sk(sk)->sap = sap;
	list_add_tail(&llc_sk(sk)->node, &sap->sk_list.list);
	sock_hold(sk);
	spin_unlock_bh(&sap->sk_list.lock);
}

/**
 *	llc_sap_unassign_sock - removes a connection from SAP
 *	@sap: SAP
 *	@sk: pointer to connection
 *
 *	This function removes a connection from sk_list.list of a SAP.
 */
void llc_sap_unassign_sock(struct llc_sap *sap, struct sock *sk)
{
	spin_lock_bh(&sap->sk_list.lock);
	list_del(&llc_sk(sk)->node);
	sock_put(sk);
	spin_unlock_bh(&sap->sk_list.lock);
}

/**
 *	llc_sap_state_process - sends event to SAP state machine
 *	@sap: sap to use
 *	@skb: pointer to occurred event
 *	@pt: packet type, for datalink protos
 *
 *	After executing actions of the event, upper layer will be indicated
 *	if needed(on receiving an UI frame). sk can be null for the
 *	datalink_proto case.
 */
void llc_sap_state_process(struct llc_sap *sap, struct sk_buff *skb,
			   struct packet_type *pt)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	llc_sap_next_state(sap, skb);
	if (ev->ind_cfm_flag == LLC_IND) {
		if (sap->rcv_func) {
			/* FIXME:
			 * Ugly hack, still trying to figure it
			 * out if this is a bug in IPX or here
			 * in LLC Land... But hey, it even works,
			 * no leaks 8)
			 */
			if (skb->list)
				skb_get(skb);
			sap->rcv_func(skb, skb->dev, pt);
		} else {
			if (skb->sk->state == TCP_LISTEN)
				goto drop;

			llc_save_primitive(skb, ev->primitive);

			/* queue skb to the user. */
			if (sock_queue_rcv_skb(skb->sk, skb))
				kfree_skb(skb);
		}
	} else if (ev->type == LLC_SAP_EV_TYPE_PDU) {
drop:
		kfree_skb(skb);
	}
}

/**
 *	llc_sap_rtn_pdu - Informs upper layer on rx of an UI, XID or TEST pdu.
 *	@sap: pointer to SAP
 *	@skb: received pdu
 */
void llc_sap_rtn_pdu(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_pdu_un *pdu;
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	pdu = llc_pdu_un_hdr(skb);
	switch (LLC_U_PDU_RSP(pdu)) {
	case LLC_1_PDU_CMD_TEST:
		ev->primitive = LLC_TEST_PRIM;	   break;
	case LLC_1_PDU_CMD_XID:
		ev->primitive = LLC_XID_PRIM;	   break;
	case LLC_1_PDU_CMD_UI:
		ev->primitive = LLC_DATAUNIT_PRIM; break;
	}
	ev->ind_cfm_flag = LLC_IND;
	ev->prim	 = NULL;
}

/**
 *	llc_sap_send_pdu - Sends a frame to MAC layer for transmition
 *	@sap: pointer to SAP
 *	@skb: pdu that must be sent
 */
void llc_sap_send_pdu(struct llc_sap *sap, struct sk_buff *skb)
{
	mac_send_pdu(skb);
	kfree_skb(skb);
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

	if (sap->state <= LLC_NR_SAP_STATES) {
		trans = llc_find_sap_trans(sap, skb);
		if (trans) {
			/* got the state to which we next transition; perform
			 * the actions associated with this transition before
			 * actually transitioning to the next state
			 */
			rc = llc_exec_sap_trans_actions(sap, trans, skb);
			if (!rc)
				/* transition SAP to next state if all actions
				 * execute successfully
				 */
				sap->state = trans->next_state;
		}
	}
	return rc;
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
	/* search thru events for this state until list exhausted or until
	 * its obvious the event is not valid for the current state
	 */
	for (next_trans = curr_state->transitions; next_trans[i]->ev; i++)
		if (!next_trans[i]->ev(sap, skb)) {
			/* got event match; return it */
			rc = next_trans[i];
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
