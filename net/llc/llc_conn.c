/*
 * llc_conn.c - Driver routines for connection component.
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 *		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <net/llc_if.h>
#include <net/llc_sap.h>
#include <net/llc_conn.h>
#include <net/sock.h>
#include <net/llc_main.h>
#include <net/llc_c_ev.h>
#include <net/llc_c_ac.h>
#include <net/llc_c_st.h>
#include <net/llc_mac.h>
#include <net/llc_pdu.h>
#include <net/llc_s_ev.h>

static int llc_find_offset(int state, int ev_type);
static void llc_conn_send_pdus(struct sock *sk);
static int llc_conn_service(struct sock *sk, struct sk_buff *skb);
static int llc_exec_conn_trans_actions(struct sock *sk,
				       struct llc_conn_state_trans *trans,
				       struct sk_buff *ev);
static struct llc_conn_state_trans *llc_qualify_conn_ev(struct sock *sk,
							struct sk_buff *skb);

/* Offset table on connection states transition diagram */
static int llc_offset_table[NBR_CONN_STATES][NBR_CONN_EV];

/**
 *	llc_conn_state_process - sends event to connection state machine
 *	@sk: connection
 *	@skb: occurred event
 *
 *	Sends an event to connection state machine. after processing event
 *	(executing it's actions and changing state), upper layer will be
 *	indicated or confirmed, if needed. Returns 0 for success, 1 for
 *	failure. The socket lock has to be held before calling this function.
 */
int llc_conn_state_process(struct sock *sk, struct sk_buff *skb)
{
	/* sending event to state machine */
	int rc = llc_conn_service(sk, skb);
	struct llc_opt *llc = llc_sk(sk);
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);
	u8 flag = ev->flag;
	struct llc_prim_if_block *ind_prim = ev->ind_prim;
	struct llc_prim_if_block *cfm_prim = ev->cfm_prim;

	/*
	 * FIXME: this will vanish as soon I get rid of the double sock crap
	 */
	if (flag != LLC_DATA_PRIM + 1)
		llc_conn_free_ev(skb);
	else if (ind_prim && cfm_prim)
		skb_get(skb);
#ifdef THIS_BREAKS_DISCONNECT_NOTIFICATION_BADLY
	/* check if the connection was freed by the state machine by
	 * means of llc_conn_disc */
	if (rc == 2) {
		printk(KERN_INFO "%s: rc == 2\n", __FUNCTION__);
		rc = -ECONNABORTED;
		goto out;
	}
#endif	/* THIS_BREAKS_DISCONNECT_NOTIFICATION_BADLY */
	if (!flag)   /* indicate or confirm not required */
		goto out;
	rc = 0;
	if (ind_prim) { /* indication required */
		/*
		 * FIXME: this will be saner as soon I get rid of the double
		 *	  sock crap
		 */
		if (flag == LLC_DATA_PRIM + 1) {
			struct sock *upper = llc_sk(skb->sk)->handler;

			skb->sk = upper;
			if (sock_queue_rcv_skb(upper, skb)) {
				printk(KERN_ERR
				       "%s: sock_queue_rcv_skb failed!\n",
				       __FUNCTION__);
				kfree_skb(skb);
			}
		} else
			llc->sap->ind(ind_prim);
	}
	if (!cfm_prim)  /* confirmation not required */
		goto out;
	/* data confirm has preconditions */
	/* FIXME: see FIXMEs above */
	if (flag != LLC_DATA_PRIM + 1) {
		llc->sap->conf(cfm_prim);
		goto out;
	}
	if (!llc_data_accept_state(llc->state)) {
		/* In this state, we can send I pdu */
		struct sock* upper = llc_sk(skb->sk)->handler;

		if (upper)
			wake_up(upper->sleep);
	} else
		rc = llc->failed_data_req = 1;
	kfree_skb(skb);
out:
	return rc;
}

void llc_conn_send_pdu(struct sock *sk, struct sk_buff *skb)
{
	llc_sock_assert(sk);
	/* queue PDU to send to MAC layer */
	skb_queue_tail(&sk->write_queue, skb);
	llc_conn_send_pdus(sk);
}

/**
 *	llc_conn_rtn_pdu - sends received data pdu to upper layer
 *	@sk: Active connection
 *	@skb: Received data frame
 *
 *	Sends received data pdu to upper layer (by using indicate function).
 *	Prepares service parameters (prim and prim_data). calling indication
 *	function will be done in llc_conn_state_process.
 */
void llc_conn_rtn_pdu(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	/* FIXME: indicate that we should send this to the upper layer */
	ev->flag     = LLC_DATA_PRIM + 1;
	ev->ind_prim = (void *)1;
}

/**
 *	llc_conn_resend_i_pdu_as_cmd - resend all all unacknowledged I PDUs
 *	@sk: active connection
 *	@nr: NR
 *	@first_p_bit: p_bit value of first pdu
 *
 *	Resend all unacknowledged I PDUs, starting with the NR; send first as
 *	command PDU with P bit equal first_p_bit; if more than one send
 *	subsequent as command PDUs with P bit equal zero (0).
 */
void llc_conn_resend_i_pdu_as_cmd(struct sock *sk, u8 nr, u8 first_p_bit)
{
	struct sk_buff *skb;
	struct llc_pdu_sn *pdu;
	u16 nbr_unack_pdus;
	u8 howmany_resend = 0;

	llc_conn_remove_acked_pdus(sk, nr, &nbr_unack_pdus);
	if (!nbr_unack_pdus)
		goto out;
	/* process unack PDUs only if unack queue is not empty; remove
	 * appropriate PDUs, fix them up, and put them on mac_pdu_q.
	 */
	while ((skb = skb_dequeue(&llc_sk(sk)->pdu_unack_q)) != NULL) {
		pdu = (struct llc_pdu_sn *)skb->nh.raw;
		llc_pdu_set_cmd_rsp(skb, LLC_PDU_CMD);
		llc_pdu_set_pf_bit(skb, first_p_bit);
		skb_queue_tail(&sk->write_queue, skb);
		first_p_bit = 0;
		llc_sk(sk)->vS = LLC_I_GET_NS(pdu);
		howmany_resend++;
	}
	if (howmany_resend > 0)
		llc_sk(sk)->vS = (llc_sk(sk)->vS + 1) % LLC_2_SEQ_NBR_MODULO;
	/* any PDUs to re-send are queued up; start sending to MAC */
	llc_conn_send_pdus(sk);
out:;
}

/**
 *	llc_conn_resend_i_pdu_as_rsp - Resend all unacknowledged I PDUs
 *	@sk: active connection.
 *	@nr: NR
 *	@first_f_bit: f_bit value of first pdu.
 *
 *	Resend all unacknowledged I PDUs, starting with the NR; send first as
 *	response PDU with F bit equal first_f_bit; if more than one send
 *	subsequent as response PDUs with F bit equal zero (0).
 */
void llc_conn_resend_i_pdu_as_rsp(struct sock *sk, u8 nr, u8 first_f_bit)
{
	struct sk_buff *skb;
	struct llc_pdu_sn *pdu;
	u16 nbr_unack_pdus;
	u8 howmany_resend = 0;

	llc_conn_remove_acked_pdus(sk, nr, &nbr_unack_pdus);
	if (!nbr_unack_pdus)
		goto out;
	/* process unack PDUs only if unack queue is not empty; remove
	 * appropriate PDUs, fix them up, and put them on mac_pdu_q
	 */
	while ((skb = skb_dequeue(&llc_sk(sk)->pdu_unack_q)) != NULL) {
		pdu = (struct llc_pdu_sn *)skb->nh.raw;
		llc_pdu_set_cmd_rsp(skb, LLC_PDU_RSP);
		llc_pdu_set_pf_bit(skb, first_f_bit);
		skb_queue_tail(&sk->write_queue, skb);
		first_f_bit = 0;
		llc_sk(sk)->vS = LLC_I_GET_NS(pdu);
		howmany_resend++;
	}
	if (howmany_resend > 0)
		llc_sk(sk)->vS = (llc_sk(sk)->vS + 1) % LLC_2_SEQ_NBR_MODULO;
	/* any PDUs to re-send are queued up; start sending to MAC */
	llc_conn_send_pdus(sk);
out:;
}

/**
 *	llc_conn_remove_acked_pdus - Removes acknowledged pdus from tx queue
 *	@sk: active connection
 *	nr: NR
 *	how_many_unacked: size of pdu_unack_q after removing acked pdus
 *
 *	Removes acknowledged pdus from transmit queue (pdu_unack_q). Returns
 *	the number of pdus that removed from queue.
 */
int llc_conn_remove_acked_pdus(struct sock *sk, u8 nr, u16 *how_many_unacked)
{
	int pdu_pos, i;
	struct sk_buff *skb;
	struct llc_pdu_sn *pdu;
	int nbr_acked = 0;
	int q_len = skb_queue_len(&llc_sk(sk)->pdu_unack_q);

	if (!q_len)
		goto out;
	skb = skb_peek(&llc_sk(sk)->pdu_unack_q);
	pdu = (struct llc_pdu_sn *)skb->nh.raw;

	/* finding position of last acked pdu in queue */
	pdu_pos = ((int)LLC_2_SEQ_NBR_MODULO + (int)nr -
			(int)LLC_I_GET_NS(pdu)) % LLC_2_SEQ_NBR_MODULO;

	for (i = 0; i < pdu_pos && i < q_len; i++) {
		skb = skb_dequeue(&llc_sk(sk)->pdu_unack_q);
		if (skb)
			kfree_skb(skb);
		nbr_acked++;
	}
out:
	*how_many_unacked = skb_queue_len(&llc_sk(sk)->pdu_unack_q);
	return nbr_acked;
}

/**
 *	llc_conn_send_pdus - Sends queued PDUs
 *	@sk: active connection
 *
 *	Sends queued pdus to MAC layer for transmition.
 */
static void llc_conn_send_pdus(struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&sk->write_queue)) != NULL) {
		struct llc_pdu_sn *pdu = (struct llc_pdu_sn *)skb->nh.raw;

		if (!LLC_PDU_TYPE_IS_I(pdu) &&
		    !(skb->dev->flags & IFF_LOOPBACK))
			skb_queue_tail(&llc_sk(sk)->pdu_unack_q, skb);
		mac_send_pdu(skb);
		if (LLC_PDU_TYPE_IS_I(pdu) ||
		    (skb->dev && skb->dev->flags & IFF_LOOPBACK))
			kfree_skb(skb);
	}
}

/**
 *	llc_conn_free_ev - free event
 *	@skb: event to free
 *
 *	Free allocated event.
 */
void llc_conn_free_ev(struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	if (ev->type == LLC_CONN_EV_TYPE_PDU) {
		/* free the frame that is bound to this event */
		struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

		if (LLC_PDU_TYPE_IS_I(pdu) || !ev->flag || !ev->ind_prim)
			kfree_skb(skb);
	} else if (ev->type == LLC_CONN_EV_TYPE_PRIM &&
		   ev->data.prim.prim != LLC_DATA_PRIM)
		kfree_skb(skb);
	else if (ev->type == LLC_CONN_EV_TYPE_P_TMR)
		kfree_skb(skb);
}

/**
 *	llc_conn_service - finds transition and changes state of connection
 *	@sk: connection
 *	@skb: happened event
 *
 *	This function finds transition that matches with happened event, then
 *	executes related actions and finally changes state of connection.
 *	Returns 0 for success, 1 for failure.
 */
static int llc_conn_service(struct sock *sk, struct sk_buff *skb)
{
	int rc = 1;
	struct llc_conn_state_trans *trans;

	if (llc_sk(sk)->state > NBR_CONN_STATES)
		goto out;
	rc = 0;
	trans = llc_qualify_conn_ev(sk, skb);
	if (trans) {
		rc = llc_exec_conn_trans_actions(sk, trans, skb);
		if (!rc && trans->next_state != NO_STATE_CHANGE)
			llc_sk(sk)->state = trans->next_state;
	}
out:
	return rc;
}

/**
 *	llc_qualify_conn_ev - finds transition for event
 *	@sk: connection
 *	@skb: happened event
 *
 *	This function finds transition that matches with happened event.
 *	Returns pointer to found transition on success, %NULL otherwise.
 */
static struct llc_conn_state_trans *llc_qualify_conn_ev(struct sock *sk,
							struct sk_buff *skb)
{
	struct llc_conn_state_trans **next_trans;
	llc_conn_ev_qfyr_t *next_qualifier;
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);
	struct llc_opt *llc = llc_sk(sk);
	struct llc_conn_state *curr_state =
					&llc_conn_state_table[llc->state - 1];

	/* search thru events for this state until
	 * list exhausted or until no more
	 */
	for (next_trans = curr_state->transitions +
		llc_find_offset(llc->state - 1, ev->type);
	     (*next_trans)->ev; next_trans++) {
		if (!((*next_trans)->ev)(sk, skb)) {
			/* got POSSIBLE event match; the event may require
			 * qualification based on the values of a number of
			 * state flags; if all qualifications are met (i.e.,
			 * if all qualifying functions return success, or 0,
			 * then this is THE event we're looking for
			 */
			for (next_qualifier = (*next_trans)->ev_qualifiers;
			     next_qualifier && *next_qualifier &&
			     !(*next_qualifier)(sk, skb); next_qualifier++)
				/* nothing */;
			if (!next_qualifier || !*next_qualifier)
				/* all qualifiers executed successfully; this is
				 * our transition; return it so we can perform
				 * the associated actions & change the state
				 */
				return *next_trans;
		}
	}
	return NULL;
}

/**
 *	llc_exec_conn_trans_actions - executes related actions
 *	@sk: connection
 *	@trans: transition that it's actions must be performed
 *	@skb: happened event
 *
 *	Executes actions that is related to happened event. Returns 0 for
 *	success, 1 to indicate failure of at least one action or 2 if the
 *	connection was freed (llc_conn_disc was called)
 */
static int llc_exec_conn_trans_actions(struct sock *sk,
				       struct llc_conn_state_trans *trans,
				       struct sk_buff *skb)
{
	int rc = 0;
	llc_conn_action_t *next_action;

	for (next_action = trans->ev_actions;
	     next_action && *next_action; next_action++) {
		int rc2 = (*next_action)(sk, skb);

		if (rc2 == 2) {
			rc = rc2;
			break;
		} else if (rc2)
			rc = 1;
	}
	return rc;
}

/**
 *	llc_lookup_established - Finds connection for the remote/local sap/mac
 *	@sap: SAP
 *	@daddr: address of remote LLC (MAC + SAP)
 *	@laddr: address of local LLC (MAC + SAP)
 *
 *	Search connection list of the SAP and finds connection using the remote
 *	mac, remote sap, local mac, and local sap. Returns pointer for
 *	connection found, %NULL otherwise.
 */
struct sock *llc_lookup_established(struct llc_sap *sap, struct llc_addr *daddr,
				    struct llc_addr *laddr)
{
	struct sock *rc = NULL;
	struct list_head *entry;

	spin_lock_bh(&sap->sk_list.lock);
	if (list_empty(&sap->sk_list.list))
		goto out;
	list_for_each(entry, &sap->sk_list.list) {
		struct llc_opt *llc = list_entry(entry, struct llc_opt, node);

		if (llc->laddr.lsap == laddr->lsap &&
		    llc->daddr.lsap == daddr->lsap &&
		    llc_mac_match(llc->laddr.mac, laddr->mac) &&
		    llc_mac_match(llc->daddr.mac, daddr->mac)) {
			rc = llc->sk;
			break;
		}
	}
	if (rc)
		sock_hold(rc);
out:
	spin_unlock_bh(&sap->sk_list.lock);
	return rc;
}

/**
 *	llc_data_accept_state - designates if in this state data can be sent.
 *	@state: state of connection.
 *
 *	Returns 0 if data can be sent, 1 otherwise.
 */
u8 llc_data_accept_state(u8 state)
{
	if (state != LLC_CONN_STATE_NORMAL && state != LLC_CONN_STATE_BUSY &&
	    state != LLC_CONN_STATE_REJ)
		return 1; /* data_conn_refuse */
	return 0;
}

/**
 *	find_next_offset - finds offset for next category of transitions
 *	@state: state table.
 *	@offset: start offset.
 *
 *	Finds offset of next category of transitions in transition table.
 *	Returns the start index of next category.
 */
u16 find_next_offset(struct llc_conn_state *state, u16 offset)
{
	u16 cnt = 0;
	struct llc_conn_state_trans **next_trans;

	for (next_trans = state->transitions + offset;
	     (*next_trans)->ev; next_trans++)
		++cnt;
	return cnt;
}

/**
 *	llc_build_offset_table - builds offset table of connection
 *
 *	Fills offset table of connection state transition table
 *	(llc_offset_table).
 */
void __init llc_build_offset_table(void)
{
	struct llc_conn_state *curr_state;
	int state, ev_type, next_offset;

	for (state = 0; state < NBR_CONN_STATES; state++) {
		curr_state = &llc_conn_state_table[state];
		next_offset = 0;
		for (ev_type = 0; ev_type < NBR_CONN_EV; ev_type++) {
			llc_offset_table[state][ev_type] = next_offset;
			next_offset += find_next_offset(curr_state,
							next_offset) + 1;
		}
	}
}

/**
 *	llc_find_offset - finds start offset of category of transitions
 *	@state: state of connection
 *	@ev_type: type of happened event
 *
 *	Finds start offset of desired category of transitions. Returns the
 *	desired start offset.
 */
static int llc_find_offset(int state, int ev_type)
{
	int rc = 0;
	/* at this stage, llc_offset_table[..][2] is not important. it is for
	 * init_pf_cycle and I don't know what is it.
	 */
	switch (ev_type) {
		case LLC_CONN_EV_TYPE_PRIM:
			rc = llc_offset_table[state][0]; break;
		case LLC_CONN_EV_TYPE_PDU:
			rc = llc_offset_table[state][4]; break;
		case LLC_CONN_EV_TYPE_SIMPLE:
			rc = llc_offset_table[state][1]; break;
		case LLC_CONN_EV_TYPE_P_TMR:
		case LLC_CONN_EV_TYPE_ACK_TMR:
		case LLC_CONN_EV_TYPE_REJ_TMR:
		case LLC_CONN_EV_TYPE_BUSY_TMR:
			rc = llc_offset_table[state][3]; break;
	}
	return rc;
}
