/*
 *      MIPL Mobile IPv6 Prefix solicitation and advertisement
 *
 *      $Id: s.prefix.h 1.10 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _PREFIX_H
#define _PREFIX_H

#include <net/addrconf.h>

struct pfx_list_entry {
	struct in6_addr daddr;
	struct in6_addr saddr;
	int retries;
	int ifindex;
};

extern struct list_head pfx_list;
extern rwlock_t pfx_list_lock;
extern struct timer_list pfx_timer;

int compare_pfx_list_entry(const void *data1, const void *data2,
			   int datalen);

/**
 * mipv6_pfx_cancel_send - cancel pending pfx_advs/sols to daddr
 * @daddr: destination address
 * @ifindex: pending items on this interface will be canceled
 *
 * if ifindex == -1, all items to daddr will be removed
 */
void mipv6_pfx_cancel_send(struct in6_addr *daddr, int ifindex);

/**
 * mipv6_pfx_add_ha - add a new HA to send prefix solicitations to
 * @daddr: address of HA
 * @saddr: our address to use as source address
 * @ifindex: interface index
 */
void mipv6_pfx_add_ha(struct in6_addr *daddr, struct in6_addr *saddr,
		      int ifindex);

void mipv6_pfxs_modified(struct prefix_info *pinfo, int ifindex);

int mipv6_pfx_add_home(int ifindex, struct in6_addr *daddr,
		       struct in6_addr *saddr, unsigned long min_expire);

int mipv6_initialize_pfx_icmpv6(void);
void mipv6_shutdown_pfx_icmpv6(void);

#endif
