#ifndef LLC_STAT_H
#define LLC_STAT_H
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
/* Station component state table */
/* Station component states */
#define LLC_STATION_STATE_DOWN		1	/* initial state */
#define LLC_STATION_STATE_DUP_ADDR_CHK	2
#define LLC_STATION_STATE_UP		3

#define LLC_NBR_STATION_STATES		3	/* size of state table */

/* Station component state table structure */
struct llc_station_state_trans {
	llc_station_ev_t ev;
	u8 next_state;
	llc_station_action_t *ev_actions;
};

struct llc_station_state {
	u8 curr_state;
	struct llc_station_state_trans **transitions;
};

extern struct llc_station_state llc_station_state_table[LLC_NBR_STATION_STATES];
#endif /* LLC_STAT_H */
