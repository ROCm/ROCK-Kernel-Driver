/*
 * llc_evnt.c - LLC station component event match functions
 * Description :
 *   Functions in this module are implementation of station component events.
 *   Details of events can be found in IEEE-802.2 standard document.
 *   All functions have one station and one event as input argument. All of
 *   them return 0 On success and 1 otherwise.
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
#include <linux/socket.h>
#include <net/sock.h>
#include <net/llc_if.h>
#include <net/llc_main.h>
#include <net/llc_evnt.h>
#include <net/llc_pdu.h>

int llc_stat_ev_enable_with_dup_addr_check(struct llc_station *station,
					   struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);	
	
	return ev->type == LLC_STATION_EV_TYPE_SIMPLE &&
	       ev->prim_type ==
	       		      LLC_STATION_EV_ENABLE_WITH_DUP_ADDR_CHECK ? 0 : 1;
}

int llc_stat_ev_enable_without_dup_addr_check(struct llc_station *station,
					      struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);	
	
	return ev->type == LLC_STATION_EV_TYPE_SIMPLE &&
	       ev->prim_type ==
			LLC_STATION_EV_ENABLE_WITHOUT_DUP_ADDR_CHECK ? 0 : 1;
}

int llc_stat_ev_ack_tmr_exp_lt_retry_cnt_max_retry(struct llc_station *station,
						   struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);	
	
	return ev->type == LLC_STATION_EV_TYPE_ACK_TMR &&
	       station->retry_count < station->maximum_retry ? 0 : 1;
}

int llc_stat_ev_ack_tmr_exp_eq_retry_cnt_max_retry(struct llc_station *station,
						   struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);	
	
	return ev->type == LLC_STATION_EV_TYPE_ACK_TMR &&
		station->retry_count == station->maximum_retry ? 0 : 1;
}

int llc_stat_ev_rx_null_dsap_xid_c(struct llc_station *station,
				   struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);	
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return ev->type == LLC_STATION_EV_TYPE_PDU &&
	       !LLC_PDU_IS_CMD(pdu) &&			/* command PDU */
	       !LLC_PDU_TYPE_IS_U(pdu) &&		/* U type PDU */
	       LLC_U_PDU_CMD(pdu) == LLC_1_PDU_CMD_XID &&
	       !pdu->dsap ? 0 : 1;			/* NULL DSAP value */
}

int llc_stat_ev_rx_null_dsap_0_xid_r_xid_r_cnt_eq(struct llc_station *station,
						  struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return ev->type == LLC_STATION_EV_TYPE_PDU &&
	       !LLC_PDU_IS_RSP(pdu) &&			/* response PDU */
	       !LLC_PDU_TYPE_IS_U(pdu) &&		/* U type PDU */
	       LLC_U_PDU_RSP(pdu) == LLC_1_PDU_CMD_XID &&
	       !pdu->dsap &&				/* NULL DSAP value */
	       !station->xid_r_count ? 0 : 1;
}

int llc_stat_ev_rx_null_dsap_1_xid_r_xid_r_cnt_eq(struct llc_station *station,
						  struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return ev->type == LLC_STATION_EV_TYPE_PDU &&
	       !LLC_PDU_IS_RSP(pdu) &&			/* response PDU */
	       !LLC_PDU_TYPE_IS_U(pdu) &&		/* U type PDU */
	       LLC_U_PDU_RSP(pdu) == LLC_1_PDU_CMD_XID &&
	       !pdu->dsap &&				/* NULL DSAP value */
	       station->xid_r_count == 1 ? 0 : 1;
}

int llc_stat_ev_rx_null_dsap_test_c(struct llc_station *station,
				    struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return ev->type == LLC_STATION_EV_TYPE_PDU &&
	       !LLC_PDU_IS_CMD(pdu) &&			/* command PDU */
	       !LLC_PDU_TYPE_IS_U(pdu) &&		/* U type PDU */
	       LLC_U_PDU_CMD(pdu) == LLC_1_PDU_CMD_TEST &&
	       !pdu->dsap ? 0 : 1;			/* NULL DSAP */
}

int llc_stat_ev_disable_req(struct llc_station *station, struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);

	return ev->type == LLC_STATION_EV_TYPE_PRIM &&
	       ev->prim == LLC_DISABLE_PRIM &&
	       ev->prim_type == LLC_PRIM_TYPE_REQ ? 0 : 1;
}
