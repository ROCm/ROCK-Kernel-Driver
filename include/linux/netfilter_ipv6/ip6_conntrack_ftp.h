/*
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: include/linux/netfilter_ipv4/ip_conntrack_ftp.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _IP6_CONNTRACK_FTP_H
#define _IP6_CONNTRACK_FTP_H
/* FTP tracking. */

#ifdef __KERNEL__

#include <linux/netfilter_ipv4/lockhelp.h>

/* Protects ftp part of conntracks */
DECLARE_LOCK_EXTERN(ip6_ftp_lock);

#define FTP_PORT	21

#endif /* __KERNEL__ */

enum ip6_ct_ftp_type
{
	/* EPRT command from client */
	IP6_CT_FTP_EPRT,
	/* EPSV response from server */
	IP6_CT_FTP_EPSV,
};

/* This structure is per expected connection */
struct ip6_ct_ftp_expect
{
	/* We record seq number and length of ftp ip/port text here: all in
	 * host order. */

	/* sequence number of IP address in packet is in ip_conntrack_expect */
	u_int32_t len;			/* length of IPv6 address */
	enum ip6_ct_ftp_type ftptype;	/* EPRT or EPSV ? */
	u_int16_t port;		/* Port that was to be used */
};

/* This structure exists only once per master */
struct ip6_ct_ftp_master {
	/* Next valid seq position for cmd matching after newline */
	u_int32_t seq_aft_nl[IP6_CT_DIR_MAX];
	/* 0 means seq_match_aft_nl not set */
	int seq_aft_nl_set[IP6_CT_DIR_MAX];
};

#endif /* _IP6_CONNTRACK_FTP_H */
