/*
 * llc_main.c - This module contains main functions to manage station, saps
 * 	and connections of the LLC.
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
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <net/llc_if.h>
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
#include <linux/llc.h>

/* static function prototypes */
static void llc_station_service_events(struct llc_station *station);
static void llc_station_free_ev(struct llc_station *station,
				struct llc_station_state_ev *ev);
static void llc_station_send_pdus(struct llc_station *station);
static u16 llc_station_next_state(struct llc_station *station,
			      struct llc_station_state_ev *ev);
static u16 llc_exec_station_trans_actions(struct llc_station *station,
					  struct llc_station_state_trans *trans,
					  struct llc_station_state_ev *ev);
static struct llc_station_state_trans *
			     llc_find_station_trans(struct llc_station *station,
					       struct llc_station_state_ev *ev);
static int llc_rtn_all_conns(struct llc_sap *sap);

extern void llc_register_sap(unsigned char sap,
			     int (*rcvfunc)(struct sk_buff *skb,
					    struct net_device *dev,
					    struct packet_type *pt));
extern void llc_unregister_sap(unsigned char sap);

static struct llc_station llc_main_station;	/* only one of its kind */
struct llc_prim_if_block llc_ind_prim, llc_cfm_prim;
static union llc_u_prim_data llc_ind_data_prim, llc_cfm_data_prim;

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
		spin_lock_init(&sap->sk_list.lock);
		INIT_LIST_HEAD(&sap->sk_list.list);
		skb_queue_head_init(&sap->mac_pdu_q);
	}
	return sap;
}

/**
 *	llc_free_sap - frees a sap
 *	@sap: Address of the sap
 *
 * 	Frees all associated connections (if any), removes this sap from
 * 	the list of saps in te station and them frees the memory for this sap.
 */
void llc_free_sap(struct llc_sap *sap)
{
	struct llc_station *station = sap->parent_station;

	llc_rtn_all_conns(sap);
	spin_lock_bh(&station->sap_list.lock);
	list_del(&sap->node);
	spin_unlock_bh(&station->sap_list.lock);
	kfree(sap);
}

/**
 *	llc_sap_save - add sap to station list
 *	@sap: Address of the sap
 *
 *	Adds a sap to the LLC's station sap list.
 */
void llc_sap_save(struct llc_sap *sap)
{
	spin_lock_bh(&llc_main_station.sap_list.lock);
	list_add_tail(&sap->node, &llc_main_station.sap_list.list);
	spin_unlock_bh(&llc_main_station.sap_list.lock);
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

	spin_lock_bh(&llc_main_station.sap_list.lock);
	list_for_each(entry, &llc_main_station.sap_list.list) {
		sap = list_entry(entry, struct llc_sap, node);
		if (sap->laddr.lsap == sap_value)
			break;
	}
	if (entry == &llc_main_station.sap_list.list) /* not found */
		sap = NULL;
	spin_unlock_bh(&llc_main_station.sap_list.lock);
	return sap;
}

/**
 *	llc_backlog_rcv - Processes rx frames and expired timers.
 *	@sk: LLC sock (p8022 connection)
 *	@skb: queued rx frame or event
 *
 *	This function processes frames that has received and timers that has
 *	expired during sending an I pdu (refer to data_req_handler).  frames
 *	queue by mac_indicate function (llc_mac.c) and timers queue by timer
 *	callback functions(llc_c_ac.c).
 */
static int llc_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	int rc = 0;
	struct llc_opt *llc = llc_sk(sk);

	if (skb->cb[0] == LLC_PACKET) {
		if (llc->state > 1) /* not closed */
			rc = llc_pdu_router(llc->sap, sk, skb, LLC_TYPE_2);
		else
			kfree_skb(skb);
	} else if (skb->cb[0] == LLC_EVENT) {
		struct llc_conn_state_ev *ev =
					(struct llc_conn_state_ev *)skb->data;
		/* timer expiration event */
		if (llc->state > 1)  /* not closed */
			rc = llc_conn_send_ev(sk, ev);
		else
			llc_conn_free_ev(ev);
		kfree_skb(skb);
	}
	return rc;
}

/**
 *     llc_sock_init - Initialize a socket with default llc values.
 *     @sk: socket to intiailize.
 */
int llc_sock_init(struct sock* sk)
{
	struct llc_opt *llc = kmalloc(sizeof(*llc), GFP_ATOMIC);
	int rc = -ENOMEM;

	if (!llc)
		goto out;
	memset(llc, 0, sizeof(*llc));
	rc = 0;
	llc->sk			     = sk;
	llc->state		     = LLC_CONN_STATE_ADM;
	llc->inc_cntr		     = llc->dec_cntr = 2;
	llc->dec_step		     = llc->connect_step = 1;
	llc->ack_timer.expire	     = LLC_ACK_TIME;
	llc->pf_cycle_timer.expire   = LLC_P_TIME;
	llc->rej_sent_timer.expire   = LLC_REJ_TIME;
	llc->busy_state_timer.expire = LLC_BUSY_TIME;
	llc->n2			     = 2;   /* max retransmit */
	llc->k			     = 2;   /* tx win size, will adjust dynam */
	llc->rw			     = 128; /* rx win size (opt and equal to
					     * tx_win of remote LLC)
					     */
	skb_queue_head_init(&llc->pdu_unack_q);
	sk->backlog_rcv = llc_backlog_rcv;
	llc_sk(sk) = llc;
out:
	return rc;
}

/**
 *	__llc_sock_alloc - Allocates LLC sock
 *
 *	Allocates a LLC sock and initializes it. Returns the new LLC sock
 *	or %NULL if there's no memory available for one
 */
struct sock *__llc_sock_alloc(void)
{
	struct sock *sk = sk_alloc(PF_LLC, GFP_ATOMIC, 1, NULL);

	if (!sk)
		goto out;
	if (llc_sock_init(sk))
		goto outsk;
	sock_init_data(NULL, sk);
out:
	return sk;
outsk:
	sk_free(sk);
	sk = NULL;
	goto out;
}

/**
 *	__llc_sock_free - Frees a LLC socket
 *	@sk - socket to free
 *
 *	Frees a LLC socket
 */
void __llc_sock_free(struct sock *sk, u8 free)
{
	struct llc_opt *llc = llc_sk(sk);

	llc->state = LLC_CONN_OUT_OF_SVC;
	/* stop all (possibly) running timers */
	llc_conn_ac_stop_all_timers(sk, NULL);
	/* handle return of frames on lists */
	printk(KERN_INFO __FUNCTION__ ": unackq=%d, txq=%d\n",
		skb_queue_len(&llc->pdu_unack_q),
		skb_queue_len(&sk->write_queue));
	skb_queue_purge(&sk->write_queue);
	skb_queue_purge(&llc->pdu_unack_q);
	if (free)
		sock_put(sk);
}

/**
 *	llc_sock_reset - resets a connection
 *	@sk: LLC socket to reset
 *
 *	Resets a connection to the out of service state. Stops its timers
 *	and frees any frames in the queues of the connection.
 */
void llc_sock_reset(struct sock *sk)
{
	struct llc_opt *llc = llc_sk(sk);

	llc_conn_ac_stop_all_timers(sk, NULL);
	skb_queue_purge(&sk->write_queue);
	skb_queue_purge(&llc->pdu_unack_q);
	llc->remote_busy_flag	= 0;
	llc->cause_flag		= 0;
	llc->retry_count	= 0;
	llc->p_flag		= 0;
	llc->f_flag		= 0;
	llc->s_flag		= 0;
	llc->ack_pf		= 0;
	llc->first_pdu_Ns	= 0;
	llc->ack_must_be_send	= 0;
	llc->dec_step		= 1;
	llc->inc_cntr		= 2;
	llc->dec_cntr		= 2;
	llc->X			= 0;
	llc->failed_data_req	= 0 ;
	llc->last_nr		= 0;
}

/**
 *	llc_rtn_all_conns - Closes all connections of a sap
 *	@sap: sap to close its connections
 *
 *	Closes all connections of a sap. Returns 0 if all actions complete
 *	successfully, nonzero otherwise
 */
static int llc_rtn_all_conns(struct llc_sap *sap)
{
	int rc = 0;
	union llc_u_prim_data prim_data;
	struct llc_prim_if_block prim;
	struct list_head *entry, *tmp;

	spin_lock_bh(&sap->sk_list.lock);
	if (list_empty(&sap->sk_list.list))
		goto out;
	list_for_each_safe(entry, tmp, &sap->sk_list.list) {
		struct llc_opt *llc = list_entry(entry, struct llc_opt, node);

		prim.sap = sap;
		prim_data.disc.sk = llc->sk;
		prim.prim = LLC_DISC_PRIM;
		prim.data = &prim_data;
		llc->state = LLC_CONN_STATE_TEMP;
		if (sap->req(&prim))
			rc = 1;
	}
out:
	spin_unlock_bh(&sap->sk_list.lock);
	return rc;
}

/**
 *	llc_station_get - get addr of global station.
 *
 *	Returns address of a place to copy the global station to it.
 */
struct llc_station *llc_station_get(void)
{
	return &llc_main_station;
}

/**
 *	llc_station_alloc_ev - allocates an event
 *	@station: Address of the station
 *
 *	Allocates an event in this station. Returns the allocated event on
 *	success, %NULL otherwise.
 */
struct llc_station_state_ev *llc_station_alloc_ev(struct llc_station *station)
{
	struct llc_station_state_ev *ev = kmalloc(sizeof(*ev), GFP_ATOMIC);

	if (ev)
		memset(ev, 0, sizeof(*ev));
	return ev;
}

/**
 *	llc_station_send_ev: queue event and try to process queue.
 *	@station: Address of the station
 *	@ev: Address of the event
 *
 *	Queues an event (on the station event queue) for handling by the
 *	station state machine and attempts to process any queued-up events.
 */
void llc_station_send_ev(struct llc_station *station,
			 struct llc_station_state_ev *ev)
{
	spin_lock_bh(&station->ev_q.lock);
	list_add_tail(&ev->node, &station->ev_q.list);
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

	while ((skb = skb_dequeue(&station->mac_pdu_q)) != NULL) {
		int rc = mac_send_pdu(skb);

		kfree_skb(skb);
		if (rc)
			break;
	}
}

/**
 *	llc_station_free_ev - frees an event
 *	@station: Address of the station
 *	@event: Address of the event
 *
 *	Frees an event.
 */
static void llc_station_free_ev(struct llc_station *station,
				struct llc_station_state_ev *ev)
{
	struct sk_buff *skb = ev->data.pdu.skb;

	if (ev->type == LLC_STATION_EV_TYPE_PDU)
		kfree_skb(skb);
	kfree(ev);
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
	struct llc_station_state_ev *ev;
	struct list_head *entry, *tmp;

	list_for_each_safe(entry, tmp, &station->ev_q.list) {
		ev = list_entry(entry, struct llc_station_state_ev, node);
		list_del(&ev->node);
		llc_station_next_state(station, ev);
	}
}

/**
 *	llc_station_next_state - processes event and goes to the next state
 *	@station: Address of the station
 *	@ev: Address of the event
 *
 *	Processes an event, executes any transitions related to that event and
 *	updates the state of the station.
 */
static u16 llc_station_next_state(struct llc_station *station,
			      struct llc_station_state_ev *ev)
{
	u16 rc = 1;
	struct llc_station_state_trans *trans;

	if (station->state > LLC_NBR_STATION_STATES)
		goto out;
	trans = llc_find_station_trans(station, ev);
	if (trans) {
		/* got the state to which we next transition; perform the
		 * actions associated with this transition before actually
		 * transitioning to the next state
		 */
		rc = llc_exec_station_trans_actions(station, trans, ev);
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
	llc_station_free_ev(station, ev);
	return rc;
}

/**
 *	llc_find_station_trans - finds transition for this event
 *	@station: Address of the station
 *	@ev: Address of the event
 *
 *	Search thru events of the current state of the station until list
 *	exhausted or it's obvious that the event is not valid for the current
 *	state. Returns the address of the transition if cound, %NULL otherwise.
 */
static struct llc_station_state_trans *
	llc_find_station_trans(struct llc_station *station,
				struct llc_station_state_ev *ev)
{
	int i = 0;
	struct llc_station_state_trans *rc = NULL;
	struct llc_station_state_trans **next_trans;
	struct llc_station_state *curr_state =
				&llc_station_state_table[station->state - 1];

	for (next_trans = curr_state->transitions; next_trans[i]->ev; i++)
		if (!next_trans[i]->ev(station, ev)) {
			rc = next_trans[i];
			break;
		}
	return rc;
}

/**
 *	llc_exec_station_trans_actions - executes actions for transition
 *	@station: Address of the station
 *	@trans: Address of the transition
 *	@ev: Address of the event that caused the transition
 *
 *	Executes actions of a transition of the station state machine. Returns
 *	0 if all actions complete successfully, nonzero otherwise.
 */
static u16 llc_exec_station_trans_actions(struct llc_station *station,
					  struct llc_station_state_trans *trans,
					  struct llc_station_state_ev *ev)
{
	u16 rc = 0;
	llc_station_action_t *next_action;

	for (next_action = trans->ev_actions;
	     next_action && *next_action; next_action++)
		if ((*next_action)(station, ev))
			rc = 1;
	return rc;
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

static int llc_proc_get_info(char *bf, char **start, off_t offset, int length)
{
	struct llc_opt *llc;
	struct list_head *sap_entry, *llc_entry;
	off_t begin = 0, pos = 0;
	int len = 0;

	spin_lock_bh(&llc_main_station.sap_list.lock);
	list_for_each(sap_entry, &llc_main_station.sap_list.list) {
		struct llc_sap *sap = list_entry(sap_entry, struct llc_sap,
						 node);

		len += sprintf(bf + len, "lsap=%d\n", sap->laddr.lsap);
		spin_lock_bh(&sap->sk_list.lock);
		if (list_empty(&sap->sk_list.list)) {
			len += sprintf(bf + len, "no connections\n");
			goto unlock;
		}
		len += sprintf(bf + len,
				"connection list:\nstate  retr txwin rxwin\n");
		list_for_each(llc_entry, &sap->sk_list.list) {
			llc = list_entry(llc_entry, struct llc_opt, node);
			len += sprintf(bf + len, "  %-5d%-5d%-6d%-5d\n",
					llc->state, llc->retry_count, llc->k,
					llc->rw);
		}
unlock:	
		spin_unlock_bh(&sap->sk_list.lock);
		pos = begin + len;
		if (pos < offset) {
			len = 0; /* Keep dumping into the buffer start */
			begin = pos;
		}
		if (pos > offset + length) /* We have dumped enough */
			break;
	}
	spin_unlock_bh(&llc_main_station.sap_list.lock);

	/* The data in question runs from begin to begin + len */
	*start = bf + (offset - begin); /* Start of wanted data */
	len -= (offset - begin); /* Remove unwanted header data from length */
	return len;
}

static char llc_banner[] __initdata =
		KERN_INFO "LLC 2.0 by Procom, 1997, Arnaldo C. Melo, 2001\n"
		KERN_INFO "NET4.0 IEEE 802.2 extended support\n";
static char llc_error_msg[] __initdata =
		KERN_ERR "LLC install NOT successful.\n";

static int __init llc_init(void)
{
	u16 rc = 0;
	struct llc_station_state_ev *ev;

	printk(llc_banner);
	INIT_LIST_HEAD(&llc_main_station.ev_q.list);
	spin_lock_init(&llc_main_station.ev_q.lock);
	INIT_LIST_HEAD(&llc_main_station.sap_list.list);
	spin_lock_init(&llc_main_station.sap_list.lock);
	skb_queue_head_init(&llc_main_station.mac_pdu_q);
	ev = kmalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev)
		goto err;
	llc_build_offset_table();
	memset(ev, 0, sizeof(*ev));
	if(dev_base->next)
		memcpy(llc_main_station.mac_sa, dev_base->next->dev_addr, ETH_ALEN);
	else
		memset(llc_main_station.mac_sa, 0, ETH_ALEN);
	llc_main_station.ack_timer.expires = jiffies + 3 * HZ;
	llc_main_station.maximum_retry	= 1;
	llc_main_station.state		= LLC_STATION_STATE_DOWN;
	ev->type	= LLC_STATION_EV_TYPE_SIMPLE;
	ev->data.a.ev	= LLC_STATION_EV_ENABLE_WITHOUT_DUP_ADDR_CHECK;
	rc = llc_station_next_state(&llc_main_station, ev);
	llc_ind_prim.data = &llc_ind_data_prim;
	llc_cfm_prim.data = &llc_cfm_data_prim;
	proc_net_create("802.2", 0, llc_proc_get_info);
	/* initialize the station component */
	llc_register_sap(0, mac_indicate);
	llc_ui_init();
out:
	return rc;
err:
	printk(llc_error_msg);
	rc = 1;
	goto out;
}

static void __exit llc_exit(void)
{
	llc_ui_exit();
	llc_unregister_sap(0);
	proc_net_remove("802.2");
}

module_init(llc_init);
module_exit(llc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Procom, 1997, Arnaldo C. Melo, Jay Schullist, 2001");
MODULE_DESCRIPTION("LLC 2.0, NET4.0 IEEE 802.2 extended support");
