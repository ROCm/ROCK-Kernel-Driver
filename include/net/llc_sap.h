#ifndef LLC_SAP_H
#define LLC_SAP_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
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
/* Defines the SAP component */
struct llc_sap {
	u8		    state;
	struct llc_station *parent_station;
	u8		    p_bit;		/* only lowest-order bit used */
	u8		    f_bit;		/* only lowest-order bit used */
	llc_prim_call_t	    req;		/* provided by LLC layer */
	llc_prim_call_t	    resp;		/* provided by LLC layer */
	llc_prim_call_t	    ind;		/* provided by network layer */
	llc_prim_call_t	    conf;		/* provided by network layer */
	struct llc_addr	    laddr;		/* SAP value in this 'lsap' */
	struct list_head    node;		/* entry in station sap_list */
	struct {
		spinlock_t	 lock;
		struct list_head list;
	} sk_list; /* LLC sockets this one manages */
	struct sk_buff_head mac_pdu_q;		/* PDUs ready to send to MAC */
};
struct llc_sap_state_ev;

extern void llc_sap_assign_sock(struct llc_sap *sap, struct sock *sk);
extern void llc_sap_unassign_sock(struct llc_sap *sap, struct sock *sk);
extern void llc_sap_send_ev(struct llc_sap *sap, struct llc_sap_state_ev *ev);
extern void llc_sap_rtn_pdu(struct llc_sap *sap, struct sk_buff *skb,
			    struct llc_sap_state_ev *ev);
extern void llc_sap_send_pdu(struct llc_sap *sap, struct sk_buff *skb);
extern struct llc_sap_state_ev *llc_sap_alloc_ev(struct llc_sap *sap);
#endif /* LLC_SAP_H */
