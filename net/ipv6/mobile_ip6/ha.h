/*
 *      MIPL Mobile IPv6 Home Agent header file
 *
 *      $Id: s.ha.h 1.24 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _HA_H
#define _HA_H

int mipv6_ha_init(void);
void mipv6_ha_exit(void);

int mipv6_dad_start(struct inet6_ifaddr *ifp, int ifindex,
		    struct in6_addr *saddr, struct in6_addr *daddr,
		    struct in6_addr *haddr, struct in6_addr *coa,
		    __u32 ba_lifetime, __u16 sequence, __u8 flags);

static __inline__ void mipv6_generate_ll_addr(struct in6_addr *ll_addr,
					      struct in6_addr *addr)
{
	ll_addr->s6_addr32[0] = htonl(0xfe800000);
	ll_addr->s6_addr32[1] = 0;
	ll_addr->s6_addr32[2] = addr->s6_addr32[2];
	ll_addr->s6_addr32[3] = addr->s6_addr32[3];
}

#endif
