/*
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: include/linux/netfilter_ipv4/ip_conntrack_tcp.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _IP6_CONNTRACK_TCP_H
#define _IP6_CONNTRACK_TCP_H
/* TCP tracking. */

enum tcp_conntrack {
	TCP_CONNTRACK_NONE,
	TCP_CONNTRACK_ESTABLISHED,
	TCP_CONNTRACK_SYN_SENT,
	TCP_CONNTRACK_SYN_RECV,
	TCP_CONNTRACK_FIN_WAIT,
	TCP_CONNTRACK_TIME_WAIT,
	TCP_CONNTRACK_CLOSE,
	TCP_CONNTRACK_CLOSE_WAIT,
	TCP_CONNTRACK_LAST_ACK,
	TCP_CONNTRACK_LISTEN,
	TCP_CONNTRACK_MAX
};

struct ip6_ct_tcp
{
	enum tcp_conntrack state;

	/* Poor man's window tracking: sequence number of valid ACK
           handshake completion packet */
	u_int32_t handshake_ack;
};

#endif /* _IP6_CONNTRACK_TCP_H */
