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
#define LLC_ACK_TIME		 1
#define LLC_REJ_TIME		 3
#define LLC_BUSY_TIME		 3
#define LLC_DEST_INVALID	 0	/* Invalid LLC PDU type */
#define LLC_DEST_SAP		 1	/* Type 1 goes here */
#define LLC_DEST_CONN		 2	/* Type 2 goes here */

/**
 * struct llc_station - LLC station component
 *
 * SAP and connection resource manager, one per adapter.
 *
 * @state - state of station
 * @xid_r_count - XID response PDU counter
 * @mac_sa - MAC source address
 * @sap_list - list of related SAPs
 * @ev_q - events entering state mach.
 * @mac_pdu_q - PDUs ready to send to MAC
 */
struct llc_station {
	u8			    state;
	u8			    xid_r_count;
	struct timer_list	    ack_timer;
	u8			    retry_count;
	u8			    maximum_retry;
	u8			    mac_sa[6];
	struct {
		rwlock_t	    lock;
		struct list_head    list;
	} sap_list;
	struct {
		struct sk_buff_head list;
		spinlock_t	    lock;
	} ev_q;
	struct sk_buff_head	    mac_pdu_q;
};

extern struct llc_sap *llc_sap_alloc(void);
extern void llc_sap_save(struct llc_sap *sap);
extern void llc_free_sap(struct llc_sap *sap);
extern struct llc_sap *llc_sap_find(u8 lsap);
extern void llc_station_state_process(struct llc_station *station,
				      struct sk_buff *skb);
extern void llc_station_send_pdu(struct llc_station *station,
				 struct sk_buff *skb);
extern struct sk_buff *llc_alloc_frame(void);

extern struct llc_station llc_main_station;
#endif /* LLC_MAIN_H */
