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
#include <net/llc_main.h>
#include <net/llc_mac.h>
#include <net/llc_pdu.h>
#include <linux/if_tr.h>

static void llc_sap_free_ev(struct llc_sap *sap, struct llc_sap_state_ev *ev);
static int llc_sap_next_state(struct llc_sap *sap, struct llc_sap_state_ev *ev);
static int llc_exec_sap_trans_actions(struct llc_sap *sap,
				      struct llc_sap_state_trans *trans,
				      struct llc_sap_state_ev *ev);
static struct llc_sap_state_trans *llc_find_sap_trans(struct llc_sap *sap,
						  struct llc_sap_state_ev *ev);

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
 *	This function removes a connection from connection_list of a SAP.
 *	List locking is performed by caller (rtn_all_conns).
 */
void llc_sap_unassign_sock(struct llc_sap *sap, struct sock *sk)
{
	spin_lock_bh(&sap->sk_list.lock);
	list_del(&llc_sk(sk)->node);
	sock_put(sk);
	spin_unlock_bh(&sap->sk_list.lock);
}

/**
 *	llc_sap_alloc_ev - allocates sap event
 *	@sap: pointer to SAP
 *	@ev: allocated event (output argument)
 *
 *	Returns the allocated sap event or %NULL when out of memory.
 */
struct llc_sap_state_ev *llc_sap_alloc_ev(struct llc_sap *sap)
{
	struct llc_sap_state_ev *ev = kmalloc(sizeof(*ev), GFP_ATOMIC);

	if (ev)
		memset(ev, 0, sizeof(*ev));
	return ev;
}

/**
 *	llc_sap_send_ev - sends event to SAP state machine
 *	@sap: pointer to SAP
 *	@ev: pointer to occurred event
 *
 *	After executing actions of the event, upper layer will be indicated
 *	if needed(on receiving an UI frame).
 */
void llc_sap_send_ev(struct llc_sap *sap, struct llc_sap_state_ev *ev)
{
	struct llc_prim_if_block *prim;
	u8 flag;

	llc_sap_next_state(sap, ev);
	flag = ev->ind_cfm_flag;
	prim = ev->prim;
	if (flag == LLC_IND) {
		skb_get(ev->data.pdu.skb);
		sap->ind(prim);
	}
	llc_sap_free_ev(sap, ev);
}

/**
 *	llc_sap_rtn_pdu - Informs upper layer on rx of an UI, XID or TEST pdu.
 *	@sap: pointer to SAP
 *	@skb: received pdu
 *	@ev: pointer to occurred event
 */
void llc_sap_rtn_pdu(struct llc_sap *sap, struct sk_buff *skb,
		     struct llc_sap_state_ev *ev)
{
	struct llc_pdu_un *pdu;
	struct llc_prim_if_block *prim = &llc_ind_prim;
	union llc_u_prim_data *prim_data = llc_ind_prim.data;
	u8 lfb;

	llc_pdu_decode_sa(skb, prim_data->udata.saddr.mac);
	llc_pdu_decode_da(skb, prim_data->udata.daddr.mac);
	llc_pdu_decode_dsap(skb, &prim_data->udata.daddr.lsap);
	llc_pdu_decode_ssap(skb, &prim_data->udata.saddr.lsap);
	prim_data->udata.pri = 0;
	prim_data->udata.skb = skb;
	pdu = (struct llc_pdu_un *)skb->nh.raw;
	switch (LLC_U_PDU_RSP(pdu)) {
		case LLC_1_PDU_CMD_TEST:
			prim->prim = LLC_TEST_PRIM;
			break;
		case LLC_1_PDU_CMD_XID:
			prim->prim = LLC_XID_PRIM;
			break;
		case LLC_1_PDU_CMD_UI:
			if (skb->protocol == ntohs(ETH_P_TR_802_2)) {
				if (((struct trh_hdr *)skb->mac.raw)->rcf) {
					lfb = ntohs(((struct trh_hdr *)
						    skb->mac.raw)->rcf) &
						    0x0070;
					prim_data->udata.lfb = lfb >> 4;
				} else {
					lfb = 0xFF;
					prim_data->udata.lfb = 0xFF;
				}
			}
			prim->prim = LLC_DATAUNIT_PRIM;
			break;
	}
	prim->data = prim_data;
	prim->sap = sap;
	ev->ind_cfm_flag = LLC_IND;
	ev->prim = prim;
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
 *	llc_sap_free_ev - frees an sap event
 *	@sap: pointer to SAP
 *	@ev: released event
 */
static void llc_sap_free_ev(struct llc_sap *sap, struct llc_sap_state_ev *ev)
{
	if (ev->type == LLC_SAP_EV_TYPE_PDU) {
		struct llc_pdu_un *pdu =
				(struct llc_pdu_un *)ev->data.pdu.skb->nh.raw;

		if (LLC_U_PDU_CMD(pdu) != LLC_1_PDU_CMD_UI)
			kfree_skb(ev->data.pdu.skb);
	}
	kfree(ev);
}

/**
 *	llc_sap_next_state - finds transition, execs actions & change SAP state
 *	@sap: pointer to SAP
 *	@ev: happened event
 *
 *	This function finds transition that matches with happened event, then
 *	executes related actions and finally changes state of SAP. It returns
 *	0 on success and 1 for failure.
 */
static int llc_sap_next_state(struct llc_sap *sap, struct llc_sap_state_ev *ev)
{
	int rc = 1;
	struct llc_sap_state_trans *trans;

	if (sap->state <= LLC_NBR_SAP_STATES) {
		trans = llc_find_sap_trans(sap, ev);
		if (trans) {
			/* got the state to which we next transition; perform
			 * the actions associated with this transition before
			 * actually transitioning to the next state
			 */
			rc = llc_exec_sap_trans_actions(sap, trans, ev);
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
 *	@ev: happened event
 *
 *	This function finds transition that matches with happened event.
 *	Returns the pointer to found transition on success or %NULL for
 *	failure.
 */
static struct llc_sap_state_trans *llc_find_sap_trans(struct llc_sap *sap,
						    struct llc_sap_state_ev* ev)
{
	int i = 0;
	struct llc_sap_state_trans *rc = NULL;
	struct llc_sap_state_trans **next_trans;
	struct llc_sap_state *curr_state = &llc_sap_state_table[sap->state - 1];
	/* search thru events for this state until list exhausted or until
	 * its obvious the event is not valid for the current state
	 */
	for (next_trans = curr_state->transitions; next_trans [i]->ev; i++)
		if (!next_trans[i]->ev(sap, ev)) {
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
 *	@ev: happened event.
 *
 *	This function executes actions that is related to happened event.
 *	Returns 0 for success and 1 for failure of at least one action.
 */
static int llc_exec_sap_trans_actions(struct llc_sap *sap,
				      struct llc_sap_state_trans *trans,
				      struct llc_sap_state_ev *ev)
{
	int rc = 0;
	llc_sap_action_t *next_action;

	for (next_action = trans->ev_actions;
	     next_action && *next_action; next_action++)
		if ((*next_action)(sap, ev))
			rc = 1;
	return rc;
}
