/*
 * llc_stat.c - Implementation of LLC station component state machine
 * 		transitions
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
#include <linux/types.h>
#include <net/llc_if.h>
#include <net/llc_sap.h>
#include <net/llc_evnt.h>
#include <net/llc_actn.h>
#include <net/llc_stat.h>

/* COMMON STATION STATE transitions */

/* dummy last-transition indicator; common to all state transition groups
 * last entry for this state
 * all members are zeros, .bss zeroes it
 */
static struct llc_station_state_trans llc_stat_state_trans_end;

/* DOWN STATE transitions */

/* state transition for LLC_STATION_EV_ENABLE_WITH_DUP_ADDR_CHECK event */
static llc_station_action_t llc_stat_down_state_actions_1[] = {
	[0] = llc_station_ac_start_ack_timer,
	[1] = llc_station_ac_set_retry_cnt_0,
	[2] = llc_station_ac_set_xid_r_cnt_0,
	[3] = llc_station_ac_send_null_dsap_xid_c,
	[4] = NULL,
};

static struct llc_station_state_trans llc_stat_down_state_trans_1 = {
	.ev	    = llc_stat_ev_enable_with_dup_addr_check,
	.next_state = LLC_STATION_STATE_DUP_ADDR_CHK,
	.ev_actions = llc_stat_down_state_actions_1,
};

/* state transition for LLC_STATION_EV_ENABLE_WITHOUT_DUP_ADDR_CHECK event */
static llc_station_action_t llc_stat_down_state_actions_2[] = {
	[0] = llc_station_ac_report_status,	/* STATION UP */
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_down_state_trans_2 = {
	.ev	    = llc_stat_ev_enable_without_dup_addr_check,
	.next_state = LLC_STATION_STATE_UP,
	.ev_actions = llc_stat_down_state_actions_2,
};

/* array of pointers; one to each transition */
static struct llc_station_state_trans *llc_stat_dwn_state_trans[] = {
	[0] = &llc_stat_down_state_trans_1,
	[1] = &llc_stat_down_state_trans_2,
	[2] = &llc_stat_state_trans_end,
};

/* UP STATE transitions */
/* state transition for LLC_STATION_EV_DISABLE_REQ event */
static llc_station_action_t llc_stat_up_state_actions_1[] = {
	[0] = llc_station_ac_report_status,	/* STATION DOWN */
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_up_state_trans_1 = {
	.ev	    = llc_stat_ev_disable_req,
	.next_state = LLC_STATION_STATE_DOWN,
	.ev_actions = llc_stat_up_state_actions_1,
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_XID_C event */
static llc_station_action_t llc_stat_up_state_actions_2[] = {
	[0] = llc_station_ac_send_xid_r,
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_up_state_trans_2 = {
	.ev	    = llc_stat_ev_rx_null_dsap_xid_c,
	.next_state = LLC_STATION_STATE_UP,
	.ev_actions = llc_stat_up_state_actions_2,
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_TEST_C event */
static llc_station_action_t llc_stat_up_state_actions_3[] = {
	[0] = llc_station_ac_send_test_r,
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_up_state_trans_3 = {
	.ev	    = llc_stat_ev_rx_null_dsap_test_c,
	.next_state = LLC_STATION_STATE_UP,
	.ev_actions = llc_stat_up_state_actions_3,
};

/* array of pointers; one to each transition */
static struct llc_station_state_trans *llc_stat_up_state_trans [] = {
	[0] = &llc_stat_up_state_trans_1,
	[1] = &llc_stat_up_state_trans_2,
	[2] = &llc_stat_up_state_trans_3,
	[3] = &llc_stat_state_trans_end,
};

/* DUP ADDR CHK STATE transitions */
/* state transition for LLC_STATION_EV_RX_NULL_DSAP_0_XID_R_XID_R_CNT_EQ
 * event
 */
static llc_station_action_t llc_stat_dupaddr_state_actions_1[] = {
	[0] = llc_station_ac_inc_xid_r_cnt_by_1,
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_1 = {
	.ev	    = llc_stat_ev_rx_null_dsap_0_xid_r_xid_r_cnt_eq,
	.next_state = LLC_STATION_STATE_DUP_ADDR_CHK,
	.ev_actions = llc_stat_dupaddr_state_actions_1,
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_1_XID_R_XID_R_CNT_EQ
 * event
 */
static llc_station_action_t llc_stat_dupaddr_state_actions_2[] = {
	[0] = llc_station_ac_report_status,	/* DUPLICATE ADDRESS FOUND */
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_2 = {
	.ev	    = llc_stat_ev_rx_null_dsap_1_xid_r_xid_r_cnt_eq,
	.next_state = LLC_STATION_STATE_DOWN,
	.ev_actions = llc_stat_dupaddr_state_actions_2,
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_XID_C event */
static llc_station_action_t llc_stat_dupaddr_state_actions_3[] = {
	[0] = llc_station_ac_send_xid_r,
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_3 = {
	.ev	    = llc_stat_ev_rx_null_dsap_xid_c,
	.next_state = LLC_STATION_STATE_DUP_ADDR_CHK,
	.ev_actions = llc_stat_dupaddr_state_actions_3,
};

/* state transition for LLC_STATION_EV_ACK_TMR_EXP_LT_RETRY_CNT_MAX_RETRY
 * event
 */
static llc_station_action_t llc_stat_dupaddr_state_actions_4[] = {
	[0] = llc_station_ac_start_ack_timer,
	[1] = llc_station_ac_inc_retry_cnt_by_1,
	[2] = llc_station_ac_set_xid_r_cnt_0,
	[3] = llc_station_ac_send_null_dsap_xid_c,
	[4] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_4 = {
	.ev	    = llc_stat_ev_ack_tmr_exp_lt_retry_cnt_max_retry,
	.next_state = LLC_STATION_STATE_DUP_ADDR_CHK,
	.ev_actions = llc_stat_dupaddr_state_actions_4,
};

/* state transition for LLC_STATION_EV_ACK_TMR_EXP_EQ_RETRY_CNT_MAX_RETRY
 * event
 */
static llc_station_action_t llc_stat_dupaddr_state_actions_5[] = {
	[0] = llc_station_ac_report_status,	/* STATION UP */
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_5 = {
	.ev	    = llc_stat_ev_ack_tmr_exp_eq_retry_cnt_max_retry,
	.next_state = LLC_STATION_STATE_UP,
	.ev_actions = llc_stat_dupaddr_state_actions_5,
};

/* state transition for LLC_STATION_EV_DISABLE_REQ event */
static llc_station_action_t llc_stat_dupaddr_state_actions_6[] = {
	[0] = llc_station_ac_report_status,	/* STATION DOWN */
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_6 = {
	.ev	    = llc_stat_ev_disable_req,
	.next_state = LLC_STATION_STATE_DOWN,
	.ev_actions = llc_stat_dupaddr_state_actions_6,
};

/* array of pointers; one to each transition */
static struct llc_station_state_trans *llc_stat_dupaddr_state_trans[] = {
	[0] = &llc_stat_dupaddr_state_trans_6,	/* Request */
	[1] = &llc_stat_dupaddr_state_trans_4,	/* Timer */
	[2] = &llc_stat_dupaddr_state_trans_5,
	[3] = &llc_stat_dupaddr_state_trans_1,	/* Receive frame */
	[4] = &llc_stat_dupaddr_state_trans_2,
	[5] = &llc_stat_dupaddr_state_trans_3,
	[6] = &llc_stat_state_trans_end,
};

struct llc_station_state llc_station_state_table[LLC_NBR_STATION_STATES] = {
	[LLC_STATION_STATE_DOWN - 1] = {
		.curr_state  = LLC_STATION_STATE_DOWN,
		.transitions = llc_stat_dwn_state_trans,
	},
	[LLC_STATION_STATE_DUP_ADDR_CHK - 1] = {
		.curr_state  = LLC_STATION_STATE_DUP_ADDR_CHK,
		.transitions = llc_stat_dupaddr_state_trans,
	},
	[LLC_STATION_STATE_UP - 1] = {
		.curr_state  = LLC_STATION_STATE_UP,
		.transitions = llc_stat_up_state_trans,
	},
};
