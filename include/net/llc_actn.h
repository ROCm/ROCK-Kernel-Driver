#ifndef LLC_ACTN_H
#define LLC_ACTN_H
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
/* Station component state transition actions */
#define LLC_STATION_AC_START_ACK_TMR		1
#define LLC_STATION_AC_SET_RETRY_CNT_0		2
#define LLC_STATION_AC_INC_RETRY_CNT_BY_1	3
#define LLC_STATION_AC_SET_XID_R_CNT_0		4
#define LLC_STATION_AC_INC_XID_R_CNT_BY_1	5
#define LLC_STATION_AC_SEND_NULL_DSAP_XID_C	6
#define LLC_STATION_AC_SEND_XID_R		7
#define LLC_STATION_AC_SEND_TEST_R		8
#define LLC_STATION_AC_REPORT_STATUS		9

/* All station state event action functions look like this */
typedef int (*llc_station_action_t)(struct llc_station *station,
				    struct llc_station_state_ev *ev);
extern int llc_station_ac_start_ack_timer(struct llc_station *station,
					  struct llc_station_state_ev *ev);
extern int llc_station_ac_set_retry_cnt_0(struct llc_station *station,
					  struct llc_station_state_ev *ev);
extern int llc_station_ac_inc_retry_cnt_by_1(struct llc_station *station,
					     struct llc_station_state_ev *ev);
extern int llc_station_ac_set_xid_r_cnt_0(struct llc_station *station,
					  struct llc_station_state_ev *ev);
extern int llc_station_ac_inc_xid_r_cnt_by_1(struct llc_station *station,
					     struct llc_station_state_ev *ev);
extern int llc_station_ac_send_null_dsap_xid_c(struct llc_station *station,
					       struct llc_station_state_ev *ev);
extern int llc_station_ac_send_xid_r(struct llc_station *station,
				     struct llc_station_state_ev *ev);
extern int llc_station_ac_send_test_r(struct llc_station *station,
				      struct llc_station_state_ev *ev);
extern int llc_station_ac_report_status(struct llc_station *station,
					struct llc_station_state_ev *ev);
extern int llc_station_ac_report_status(struct llc_station *station,
					struct llc_station_state_ev *ev);
#endif /* LLC_ACTN_H */
