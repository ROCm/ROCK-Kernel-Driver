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
#include <net/llc_mac.h>
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
struct llc_station llc_main_station;	/* only one of its kind */

/**
 *	llc_sap_save - add sap to station list
 *	@sap: Address of the sap
 *
 *	Adds a sap to the LLC's station sap list.
 */
void llc_sap_save(struct llc_sap *sap)
{
	write_lock_bh(&llc_main_station.sap_list.lock);
	list_add_tail(&sap->node, &llc_main_station.sap_list.list);
	write_unlock_bh(&llc_main_station.sap_list.lock);
}

/**
 *	llc_sap_find - searchs a SAP in station
 *	@sap_value: sap to be found
 *
 *	Searchs for a sap in the sap list of the LLC's station upon the sap ID.
 *	Returns the sap or %NULL if not found.
 */
struct llc_sap *llc_sap_find(u8 sap_value)
{
	struct llc_sap* sap = NULL;
	struct list_head *entry;

	read_lock_bh(&llc_main_station.sap_list.lock);
	list_for_each(entry, &llc_main_station.sap_list.list) {
		sap = list_entry(entry, struct llc_sap, node);
		if (sap->laddr.lsap == sap_value)
			break;
	}
	if (entry == &llc_main_station.sap_list.list) /* not found */
		sap = NULL;
	read_unlock_bh(&llc_main_station.sap_list.lock);
	return sap;
}

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

/**
 *	llc_alloc_frame - allocates sk_buff for frame
 *
 *	Allocates an sk_buff for frame and initializes sk_buff fields.
 *	Returns allocated skb or %NULL when out of memory.
 */
struct sk_buff *llc_alloc_frame(void)
{
	struct sk_buff *skb = alloc_skb(128, GFP_ATOMIC);

	if (skb) {
		skb_reserve(skb, 50);
		skb->nh.raw   = skb->h.raw = skb->data;
		skb->protocol = htons(ETH_P_802_2);
		skb->dev      = dev_base->next;
		skb->mac.raw  = skb->head;
	}
	return skb;
}

static struct packet_type llc_packet_type = {
	.type = __constant_htons(ETH_P_802_2),
	.func = llc_rcv,
	.data = (void *)1,
};

static struct packet_type llc_tr_packet_type = {
	.type = __constant_htons(ETH_P_TR_802_2),
	.func = llc_rcv,
	.data = (void *)1,
};

static char llc_error_msg[] __initdata =
		KERN_ERR "LLC install NOT successful.\n";

static int __init llc_init(void)
{
	u16 rc = 0;
	struct sk_buff *skb;
	struct llc_station_state_ev *ev;

	INIT_LIST_HEAD(&llc_main_station.sap_list.list);
	rwlock_init(&llc_main_station.sap_list.lock);
	skb_queue_head_init(&llc_main_station.mac_pdu_q);
	skb_queue_head_init(&llc_main_station.ev_q.list);
	spin_lock_init(&llc_main_station.ev_q.lock);
	init_timer(&llc_main_station.ack_timer);
	llc_main_station.ack_timer.data     = (unsigned long)&llc_main_station;
	llc_main_station.ack_timer.function = llc_station_ack_tmr_cb;

	skb = alloc_skb(0, GFP_ATOMIC);
	if (!skb)
		goto err;
	llc_set_station_handler(llc_station_rcv);
	ev = llc_station_ev(skb);
	memset(ev, 0, sizeof(*ev));
	if (dev_base->next)
		memcpy(llc_main_station.mac_sa,
		       dev_base->next->dev_addr, ETH_ALEN);
	else
		memset(llc_main_station.mac_sa, 0, ETH_ALEN);
	llc_main_station.ack_timer.expires = jiffies + 3 * HZ;
	llc_main_station.maximum_retry	= 1;
	llc_main_station.state		= LLC_STATION_STATE_DOWN;
	ev->type	= LLC_STATION_EV_TYPE_SIMPLE;
	ev->prim_type	= LLC_STATION_EV_ENABLE_WITHOUT_DUP_ADDR_CHECK;
	rc = llc_station_next_state(&llc_main_station, skb);
	dev_add_pack(&llc_packet_type);
	dev_add_pack(&llc_tr_packet_type);
out:
	return rc;
err:
	printk(llc_error_msg);
	rc = 1;
	goto out;
}

static void __exit llc_exit(void)
{
	dev_remove_pack(&llc_packet_type);
	dev_remove_pack(&llc_tr_packet_type);
	llc_set_station_handler(NULL);
}

module_init(llc_init);
module_exit(llc_exit);

EXPORT_SYMBOL(llc_sap_find);
EXPORT_SYMBOL(llc_alloc_frame);
EXPORT_SYMBOL(llc_main_station);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Procom 1997, Jay Schullist 2001, Arnaldo C. Melo 2001-2003");
MODULE_DESCRIPTION("LLC IEEE 802.2 extended support");
MODULE_ALIAS_NETPROTO(PF_LLC);
