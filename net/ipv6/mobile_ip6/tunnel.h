/*
 *      MIPL Mobile IPv6 IP6-IP6 tunneling header file
 *
 *      $Id: s.tunnel.h 1.18 03/09/22 16:45:04+03:00 vnuorval@amber.hut.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _TUNNEL_H
#define _TUNNEL_H

#include <linux/in6.h>
#include <linux/if_arp.h>
#include <net/ipv6_tunnel.h>

static __inline__ int is_mip6_tnl(struct ip6_tnl *t)
{
	return (t != NULL && 
		t->parms.flags & IP6_TNL_F_KERNEL_DEV &&
		t->parms.flags & IP6_TNL_F_MIP6_DEV);
			
}

static __inline__ int dev_is_mip6_tnl(struct net_device *dev)
{
	struct ip6_tnl *t = (struct ip6_tnl *)dev->priv;
	return (dev->type == ARPHRD_TUNNEL6 && is_mip6_tnl(t));
}


#endif

