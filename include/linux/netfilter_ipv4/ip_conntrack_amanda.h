#ifndef _IP_CONNTRACK_AMANDA_H
#define _IP_CONNTRACK_AMANDA_H
/* AMANDA tracking. */

#ifdef __KERNEL__

#include <linux/netfilter_ipv4/lockhelp.h>

/* Protects amanda part of conntracks */
DECLARE_LOCK_EXTERN(ip_amanda_lock);

#endif

struct conn {
	char* match;
	int matchlen;
};

#define NUM_MSGS 	3


struct ip_ct_amanda_expect
{
	u_int16_t port;		/* port number of this expectation */
	u_int16_t offset;	/* offset of the port specification in ctrl packet */
	u_int16_t len;		/* the length of the port number specification */
};

#endif /* _IP_CONNTRACK_AMANDA_H */
