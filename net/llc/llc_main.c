/*
 * llc_main.c - This module contains main functions to manage station, saps
 * 	and connections of the LLC.
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
#include <linux/init.h>
#include <linux/module.h>
#include <net/llc.h>
#include <net/llc_sap.h>
#include <net/llc_conn.h>
#include <net/llc_main.h>
#include <net/llc_evnt.h>
#include <net/llc_actn.h>
#include <net/llc_stat.h>
#include <net/llc_c_ac.h>
#include <net/llc_s_ac.h>
#include <net/llc_c_ev.h>
#include <net/llc_c_st.h>
#include <net/llc_s_ev.h>
#include <net/llc_s_st.h>
#include <net/llc_proc.h>

/* static function prototypes */
static void llc_station_service_events(struct llc_station *station);
static void llc_station_free_ev(struct llc_station *station,
				struct sk_buff *skb);
static void llc_station_send_pdus(struct llc_station *station);
static u16 llc_station_next_state(struct llc_station *station,
				  struct sk_buff *skb);
static u16 llc_exec_station_trans_actions(struct llc_station *station,
					  struct llc_station_state_trans *trans,
					  struct sk_buff *skb);
static struct llc_station_state_trans *
			     llc_find_station_trans(struct llc_station *station,
						    struct sk_buff *skb);

static struct llc_station llc_main_station;

/**
 *	llc_station_state_process: queue event and try to process queue.
 *	@station: Address of the station
 *	@skb: Address of the event
 *
 *	Queues an event (on the station event queue) for handling by the
 *	station state machine and attempts to process any queued-up events.
 */
void llc_station_state_process(struct llc_station *station, struct sk_buff *skb)
{
	spin_lock_bh(&station->ev_q.lock);
	skb_queue_tail(&station->ev_q.list, skb);
	llc_station_service_events(station);
	spin_unlock_bh(&station->ev_q.lock);
}

/**
 *	llc_station_send_pdu - queues PDU to send
 *	@station: Address of the station
 *	@skb: Address of the PDU
 *
 *	Queues a PDU to send to the MAC layer.
 */
void llc_station_send_pdu(struct llc_station *station, struct sk_buff *skb)
{
	skb_queue_tail(&station->mac_pdu_q, skb);
	llc_station_send_pdus(station);
}

/**
 *	llc_station_send_pdus - tries to send queued PDUs
 *	@station: Address of the station
 *
 *	Tries to send any PDUs queued in the station mac_pdu_q to the MAC
 *	layer.
 */
static void llc_station_send_pdus(struct llc_station *station)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&station->mac_pdu_q)) != NULL)
		if (dev_queue_xmit(skb))
			break;
}

/**
 *	llc_station_free_ev - frees an event
 *	@station: Address of the station
 *	@skb: Address of the event
 *
 *	Frees an event.
 */
static void llc_station_free_ev(struct llc_station *station,
				struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);

	if (ev->type == LLC_STATION_EV_TYPE_PDU)
		kfree_skb(skb);
}

/**
 *	llc_station_service_events - service events in the queue
 *	@station: Address of the station
 *
 *	Get an event from the station event queue (if any); attempt to service
 *	the event; if event serviced, get the next event (if any) on the event
 *	queue; if event not service, re-queue the event on the event queue and
 *	attempt to service the next event; when serviced all events in queue,
 *	finished; if don't transition to different state, just service all
 *	events once; if transition to new state, service all events again.
 *	Caller must hold station->ev_q.lock.
 */
static void llc_station_service_events(struct llc_station *station)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&station->ev_q.list)) != NULL)
		llc_station_next_state(station, skb);
}

/**
 *	llc_station_next_state - processes event and goes to the next state
 *	@station: Address of the station
 *	@skb: Address of the event
 *
 *	Processes an event, executes any transitions related to that event and
 *	updates the state of the station.
 */
static u16 llc_station_next_state(struct llc_station *station,
				  struct sk_buff *skb)
{
	u16 rc = 1;
	struct llc_station_state_trans *trans;

	if (station->state > LLC_NBR_STATION_STATES)
		goto out;
	trans = llc_find_station_trans(station, skb);
	if (trans) {
		/* got the state to which we next transition; perform the
		 * actions associated with this transition before actually
		 * transitioning to the next state
		 */
		rc = llc_exec_station_trans_actions(station, trans, skb);
		if (!rc)
			/* transition station to next state if all actions
			 * execute successfully; done; wait for next event
			 */
			station->state = trans->next_state;
	} else
		/* event not recognized in current state; re-queue it for
		 * processing again at a later time; return failure
		 */
		rc = 0;
out:
	llc_station_free_ev(station, skb);
	return rc;
}

/**
 *	llc_find_station_trans - finds transition for this event
 *	@station: Address of the station
 *	@skb: Address of the event
 *
 *	Search thru events of the current state of the station until list
 *	exhausted or it's obvious that the event is not valid for the current
 *	state. Returns the address of the transition if cound, %NULL otherwise.
 */
static struct llc_station_state_trans *
			llc_find_station_trans(struct llc_station *station,
					       struct sk_buff *skb)
{
	int i = 0;
	struct llc_station_state_trans *rc = NULL;
	struct llc_station_state_trans **next_trans;
	struct llc_station_state *curr_state =
				&llc_station_state_table[station->state - 1];

	for (next_trans = curr_state->transitions; next_trans[i]->ev; i++)
		if (!next_trans[i]->ev(station, skb)) {
			rc = next_trans[i];
			break;
		}
	return rc;
}

/**
 *	llc_exec_station_trans_actions - executes actions for transition
 *	@station: Address of the station
 *	@trans: Address of the transition
 *	@skb: Address of the event that caused the transition
 *
 *	Executes actions of a transition of the station state machine. Returns
 *	0 if all actions complete successfully, nonzero otherwise.
 */
static u16 llc_exec_station_trans_actions(struct llc_station *station,
					  struct llc_station_state_trans *trans,
					  struct sk_buff *skb)
{
	u16 rc = 0;
	llc_station_action_t *next_action = trans->ev_actions;

	for (; next_action && *next_action; next_action++)
		if ((*next_action)(station, skb))
			rc = 1;
	return rc;
}

/*
 *	llc_station_rcv - send received pdu to the station state machine
 *	@skb: received frame.
 *
 *	Sends data unit to station state machine.
 */
static void llc_station_rcv(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);

	ev->type   = LLC_STATION_EV_TYPE_PDU;
	ev->reason = 0;
	llc_station_state_process(&llc_main_station, skb);
}

int __init llc_station_init(void)
{
	u16 rc = -ENOBUFS;
	struct sk_buff *skb;
	struct llc_station_state_ev *ev;

	skb_queue_head_init(&llc_main_station.mac_pdu_q);
	skb_queue_head_init(&llc_main_station.ev_q.list);
	spin_lock_init(&llc_main_station.ev_q.lock);
	init_timer(&llc_main_station.ack_timer);
	llc_main_station.ack_timer.data     = (unsigned long)&llc_main_station;
	llc_main_station.ack_timer.function = llc_station_ack_tmr_cb;

	skb = alloc_skb(0, GFP_ATOMIC);
	if (!skb)
		goto out;
	rc = 0;
	llc_set_station_handler(llc_station_rcv);
	ev = llc_station_ev(skb);
	memset(ev, 0, sizeof(*ev));
	llc_main_station.ack_timer.expires = jiffies + 3 * HZ;
	llc_main_station.maximum_retry	= 1;
	llc_main_station.state		= LLC_STATION_STATE_DOWN;
	ev->type	= LLC_STATION_EV_TYPE_SIMPLE;
	ev->prim_type	= LLC_STATION_EV_ENABLE_WITHOUT_DUP_ADDR_CHECK;
	rc = llc_station_next_state(&llc_main_station, skb);
out:
	return rc;
}

void __exit llc_station_exit(void)
{
	llc_set_station_handler(NULL);
}
