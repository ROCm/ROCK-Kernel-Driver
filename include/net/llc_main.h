#ifndef LLC_MAIN_H
#define LLC_MAIN_H
/*
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
#define LLC_EVENT		 1
#define LLC_PACKET		 2
#define LLC_TYPE_1		 1
#define LLC_TYPE_2		 2
#define LLC_P_TIME		 2
#define LLC_ACK_TIME		 3
#define LLC_REJ_TIME		 3
#define LLC_BUSY_TIME		 3
#define LLC_SENDACK_TIME	50
#define LLC_DEST_INVALID	 0	/* Invalid LLC PDU type */
#define LLC_DEST_SAP		 1	/* Type 1 goes here */
#define LLC_DEST_CONN		 2	/* Type 2 goes here */

/* LLC Layer global default parameters */

#define LLC_GLOBAL_DEFAULT_MAX_NBR_SAPS		4
#define LLC_GLOBAL_DEFAULT_MAX_NBR_CONNS	64

extern struct llc_prim_if_block llc_ind_prim, llc_cfm_prim;

/* LLC station component (SAP and connection resource manager) */
/* Station component; one per adapter */
struct llc_station {
	u8     state;			/* state of station */
	u8     xid_r_count;		/* XID response PDU counter */
	struct timer_list ack_timer;
	u8     ack_tmr_running;		/* 1 or 0 */
	u8     retry_count;
	u8     maximum_retry;
	u8     mac_sa[6];		/* MAC source address */
	struct {
		spinlock_t	 lock;
		struct list_head list;
	} sap_list;			/* list of related SAPs */
	struct {
		spinlock_t	 lock;
		struct list_head list;
	} ev_q;				/* events entering state mach. */
	struct sk_buff_head mac_pdu_q;	/* PDUs ready to send to MAC */
};
struct llc_station_state_ev;

extern struct llc_sap *llc_sap_alloc(void);
extern void llc_sap_save(struct llc_sap *sap);
extern void llc_free_sap(struct llc_sap *sap);
extern struct llc_sap *llc_sap_find(u8 lsap);
extern struct llc_station *llc_station_get(void);
extern struct llc_station_state_ev *
			     llc_station_alloc_ev(struct llc_station *station);
extern void llc_station_send_ev(struct llc_station *station,
				struct llc_station_state_ev *ev);
extern void llc_station_send_pdu(struct llc_station *station,
				 struct sk_buff *skb);
extern struct sk_buff *llc_alloc_frame(void);
#endif /* LLC_MAIN_H */
