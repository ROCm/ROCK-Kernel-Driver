/* IRC extension for IP connection tracking.
 * (C) 2000 by Harald Welte <laforge@gnumonks.org>
 * based on RR's ip_conntrack_ftp.h
 *
 * ip_conntrack_irc.h,v 1.6 2000/11/07 18:26:42 laforge Exp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *
 */
#ifndef _IP_CONNTRACK_IRC_H
#define _IP_CONNTRACK_IRC_H

#ifndef __KERNEL__
#error Only in kernel.
#endif

#include <linux/netfilter_ipv4/lockhelp.h>

#define IP_CONNTR_IRC	2

struct dccproto {
	char* match;
	int matchlen;
};

/* Protects irc part of conntracks */
DECLARE_LOCK_EXTERN(ip_irc_lock);

/* We record seq number and length of irc ip/port text here: all in
   host order. */
struct ip_ct_irc
{
	/* This tells NAT that this is an IRC connection */
	int is_irc;
	/* sequence number where address part of DCC command begins */
	u_int32_t seq;
	/* 0 means not found yet */
	u_int32_t len;
	/* Port that was to be used */
	u_int16_t port;
};

#endif /* _IP_CONNTRACK_IRC_H */
