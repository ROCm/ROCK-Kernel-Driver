/*
 *      MIPL Mobile IPv6 Binding Cache header file
 *
 *      $Id: s.bcache.h 1.28 03/09/16 01:50:08+03:00 vnuorval@dsl-hkigw1a8b.dial.inet.fi $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _BCACHE_H
#define _BCACHE_H

#include <linux/in6.h>
#include <linux/timer.h>
#include "hashlist.h"

#define CACHE_ENTRY 1 /* this and HOME_REGISTRATION are the entry types */
#define HOME_REGISTRATION 2
#define ANY_ENTRY 3

#define MIPV6_MAX_DESTUNREACH 5 /* Delete CN BCEs after 5 destination unreachables */
#define MIPV6_DEST_UNR_IVAL  10 /* What is the max interval of destination  
				   unreacahable error messages for them to be persistent*/

struct mipv6_bce {
	struct hashlist_entry e;
	int ifindex;				/* Interface identifier */
	struct in6_addr our_addr;		/* our address (as seen by the MN) */
	struct in6_addr home_addr;		/* MN home address */
	struct in6_addr coa;			/* MN care-of address */
	unsigned long callback_time;		/* time of expiration     (in jiffies) */
	unsigned long br_callback_time;		/* time for sending a BR  (in jiffies) */
	int (*callback_function)(struct mipv6_bce *entry);
	__u8 type;				/* home registration */
	__u8 router;				/* mn is router */
	__u8 flags;				/* flags received in BU */
	__u16 seq;				/* sequence number */
	unsigned long last_br;			/* time when last BR sent */
	unsigned long last_destunr;             /* time when last ICMP destination unreachable received */
	int br_count;				/* How many BRRs have sent */
	int destunr_count;                      /* Number of destination unreachables received */   
};

int mipv6_bcache_add(int ifindex, struct in6_addr *our_addr, 
		     struct in6_addr *home_addr, struct in6_addr *coa,
		     __u32 lifetime, __u16 seq, __u8 flags, __u8 type);

int mipv6_bcache_delete(struct in6_addr *home_addr, struct in6_addr *our_addr,
			__u8 type);

int mipv6_bcache_exists(struct in6_addr *home_addr,
			struct in6_addr *our_addr);

int mipv6_bcache_get(struct in6_addr *home_addr,
		     struct in6_addr *our_addr,
		     struct mipv6_bce *entry);

int mipv6_bcache_iterate(int (*func)(void *, void *, unsigned long *), void *args);

void mipv6_bcache_cleanup(int type);

int mipv6_bcache_init(__u32 size);

int mipv6_bcache_exit(void);

#endif /* _BCACHE_H */
