/* $Id: rawhdlc.h,v 1.3 2000/06/26 08:59:14 keil Exp $
 *
 * rawhdlc.h     support routines for cards that don't support HDLC
 *
 * Author     Brent Baccala <baccala@FreeSoft.org>
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#ifndef RAWHDLC_H
struct hdlc_state {
	char insane_mode;
	u_char state;
	u_char r_one;
	u_char r_val;
	u_int o_bitcnt;
	u_int i_bitcnt;
	u_int fcs;
};


int make_raw_hdlc_data(u_char *src, u_int slen, u_char *dst, u_int dsize);
void init_hdlc_state(struct hdlc_state *stateptr, int mode);
int read_raw_hdlc_data(struct hdlc_state *saved_state,
                       u_char *src, u_int slen, u_char *dst, u_int dsize);
#define RAWHDLC_H
#endif
