/* Linux ISDN subsystem, CISCO HDLC network interfaces
 *
 * Copyright 1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *           2001       by Bjoern A. Zeeb <i4l@zabbadoz.net>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#ifndef ISDN_CISCOHDLCK_H
#define ISDN_CISCOHDLCK_H

extern struct isdn_netif_ops isdn_ciscohdlck_ops;

struct inl_cisco {
	u32 myseq;             /* local keepalive seq. for Cisco */
	u32 mineseen;          /* returned keepalive seq. from remote */
	u32 yourseq;           /* remote keepalive seq. for Cisco  */
	int keepalive_period;  /* keepalive period */
	int last_slarp_in;     /* jiffie of last recvd keepalive pkt */
	char line_state;       /* state of line */
	char debserint;	       /* debugging flags */
	struct timer_list timer;
};

#endif
