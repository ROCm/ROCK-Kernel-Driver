/*
 * llc_actn.c - Implementation of actions of station component of LLC
 *
 * Description :
 *   Functions in this module are implementation of station component actions.
 *   Details of actions can be found in IEEE-802.2 standard document.
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
#include <linux/netdevice.h>
#include <net/llc_if.h>
#include <net/llc_main.h>
#include <net/llc_evnt.h>
#include <net/llc_pdu.h>
#include <net/llc_mac.h>

int llc_station_ac_start_ack_timer(struct llc_station *station,
				   struct sk_buff *skb)
{
	mod_timer(&station->ack_timer, jiffies + LLC_ACK_TIME * HZ);
	return 0;
}

int llc_station_ac_set_retry_cnt_0(struct llc_station *station,
				   struct sk_buff *skb)
{
	station->retry_count = 0;
	return 0;
}

int llc_station_ac_inc_retry_cnt_by_1(struct llc_station *station,
				      struct sk_buff *skb)
{
	station->retry_count++;
	return 0;
}

int llc_station_ac_set_xid_r_cnt_0(struct llc_station *station,
				   struct sk_buff *skb)
{
	station->xid_r_count = 0;
	return 0;
}

int llc_station_ac_inc_xid_r_cnt_by_1(struct llc_station *station,
				      struct sk_buff *skb)
{
	station->xid_r_count++;
	return 0;
}

int llc_station_ac_send_null_dsap_xid_c(struct llc_station *station,
					struct sk_buff *skb)
{
	int rc = 1;
	struct sk_buff *nskb = llc_alloc_frame();

	if (!nskb)
		goto out;
	rc = 0;
	llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, 0, 0, LLC_PDU_CMD);
	llc_pdu_init_as_xid_cmd(nskb, LLC_XID_NULL_CLASS_2, 127);
	lan_hdrs_init(nskb, station->mac_sa, station->mac_sa);
	llc_station_send_pdu(station, nskb);
out:
	return rc;
}

int llc_station_ac_send_xid_r(struct llc_station *station,
			      struct sk_buff *skb)
{
	u8 mac_da[ETH_ALEN], dsap;
	int rc = 1;
	struct sk_buff* nskb = llc_alloc_frame();

	if (!nskb)
		goto out;
	rc = 0;
	nskb->dev = skb->dev;
	llc_pdu_decode_sa(skb, mac_da);
	llc_pdu_decode_ssap(skb, &dsap);
	llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, 0, dsap, LLC_PDU_RSP);
	llc_pdu_init_as_xid_rsp(nskb, LLC_XID_NULL_CLASS_2, 127);
	lan_hdrs_init(nskb, station->mac_sa, mac_da);
	llc_station_send_pdu(station, nskb);
out:
	return rc;
}

int llc_station_ac_send_test_r(struct llc_station *station,
			       struct sk_buff *skb)
{
	u8 mac_da[ETH_ALEN], dsap;
	int rc = 1;
	struct sk_buff *nskb = llc_alloc_frame();

	if (!nskb)
		goto out;
	rc = 0;
	nskb->dev = skb->dev;
	llc_pdu_decode_sa(skb, mac_da);
	llc_pdu_decode_ssap(skb, &dsap);
	llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, 0, dsap, LLC_PDU_RSP);
       	llc_pdu_init_as_test_rsp(nskb, skb);
	lan_hdrs_init(nskb, station->mac_sa, mac_da);
	llc_station_send_pdu(station, nskb);
out:
	return rc;
}

int llc_station_ac_report_status(struct llc_station *station,
				 struct sk_buff *skb)
{
	return 0;
}

void llc_station_ack_tmr_cb(unsigned long timeout_data)
{
	struct llc_station *station = (struct llc_station *)timeout_data;
	struct sk_buff *skb = alloc_skb(0, GFP_ATOMIC);

	if (skb) {
		struct llc_station_state_ev *ev = llc_station_ev(skb);

		ev->type = LLC_STATION_EV_TYPE_ACK_TMR;
		llc_station_state_process(station, skb);
	}
}
