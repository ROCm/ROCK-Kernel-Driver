/* $Id: rawhdlc.h,v 1.3.6.2 2001/09/23 22:24:51 kai Exp $
 *
 * Author     Brent Baccala
 * Copyright  by Brent Baccala <baccala@FreeSoft.org>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef RAWHDLC_H
struct hdlc_state {
	char insane_mode;
	u8 state;
	u8 r_one;
	u8 r_val;
	u_int o_bitcnt;
	u_int i_bitcnt;
	u_int fcs;
};


int make_raw_hdlc_data(u8 *src, u_int slen, u8 *dst, u_int dsize);
void init_hdlc_state(struct hdlc_state *stateptr, int mode);
int read_raw_hdlc_data(struct hdlc_state *saved_state,
                       u8 *src, u_int slen, u8 *dst, u_int dsize);
#define RAWHDLC_H
#endif
