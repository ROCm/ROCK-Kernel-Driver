#ifndef _IP_CONNTRACK_FTP_H
#define _IP_CONNTRACK_FTP_H
/* FTP tracking. */

#ifndef __KERNEL__
#error Only in kernel.
#endif

#include <linux/netfilter_ipv4/lockhelp.h>

/* Protects ftp part of conntracks */
DECLARE_LOCK_EXTERN(ip_ftp_lock);

enum ip_ct_ftp_type
{
	/* PORT command from client */
	IP_CT_FTP_PORT = IP_CT_DIR_ORIGINAL,
	/* PASV response from server */
	IP_CT_FTP_PASV = IP_CT_DIR_REPLY
};

/* We record seq number and length of ftp ip/port text here: all in
   host order. */
struct ip_ct_ftp
{
	/* This tells NAT that this is an ftp connection */
	int is_ftp;
	u_int32_t seq;
	/* 0 means not found yet */
	u_int32_t len;
	enum ip_ct_ftp_type ftptype;
	/* Port that was to be used */
	u_int16_t port;
	/* Next valid seq position for cmd matching after newline */
	u_int32_t seq_aft_nl[IP_CT_DIR_MAX];
	/* 0 means seq_match_aft_nl not set */
	int seq_aft_nl_set[IP_CT_DIR_MAX];
};

#endif /* _IP_CONNTRACK_FTP_H */
