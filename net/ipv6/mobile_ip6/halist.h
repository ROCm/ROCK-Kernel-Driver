/*
 *      MIPL Mobile IPv6 Home Agents List header file      
 *
 *      $Id: s.halist.h 1.13 03/09/26 00:30:23+03:00 vnuorval@cs78179138.pp.htv.fi $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _HALIST_H
#define _HALIST_H

int mipv6_halist_init(__u32 size);

void mipv6_halist_exit(void);

int mipv6_halist_add(int ifindex, struct in6_addr *glob_addr, int plen,
		     struct in6_addr *ll_addr, int pref, __u32 lifetime);

int mipv6_halist_delete(struct in6_addr *glob_addr);

int mipv6_ha_get_pref_list(int ifindex, struct in6_addr **addrs, int max);

int mipv6_ha_get_addr(int ifindex, struct in6_addr *addr);

#endif /* _HALIST_H */
