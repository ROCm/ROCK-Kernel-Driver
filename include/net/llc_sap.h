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
/**
 * struct llc_sap - Defines the SAP component
 *
 * @p_bit - only lowest-order bit used
 * @f_bit - only lowest-order bit used
 * @req - provided by LLC layer
 * @resp - provided by LLC layer
 * @ind - provided by network layer
 * @conf - provided by network layer
 * @laddr - SAP value in this 'lsap'
 * @node - entry in station sap_list
 * @sk_list - LLC sockets this one manages
 * @mac_pdu_q - PDUs ready to send to MAC
 */
struct llc_sap {
	struct llc_station	 *parent_station;
	u8			 state;
	u8			 p_bit;
	u8			 f_bit;
	llc_prim_call_t		 req;
	llc_prim_call_t		 resp;
	llc_prim_call_t		 ind;
	llc_prim_call_t		 conf;
	struct llc_prim_if_block llc_ind_prim, llc_cfm_prim;
	union llc_u_prim_data	 llc_ind_data_prim, llc_cfm_data_prim;
	struct llc_addr		 laddr;
	struct list_head	 node;
	struct {
		spinlock_t	 lock;
		struct list_head list;
	} sk_list;
	struct sk_buff_head	 mac_pdu_q;
};
struct llc_sap_state_ev;

extern void llc_sap_assign_sock(struct llc_sap *sap, struct sock *sk);
extern void llc_sap_unassign_sock(struct llc_sap *sap, struct sock *sk);
extern void llc_sap_send_ev(struct llc_sap *sap, struct sk_buff *skb);
extern void llc_sap_rtn_pdu(struct llc_sap *sap, struct sk_buff *skb);
extern void llc_sap_send_pdu(struct llc_sap *sap, struct sk_buff *skb);
#endif /* LLC_SAP_H */
