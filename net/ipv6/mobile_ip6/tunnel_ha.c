/*
 *	IPv6-IPv6 tunneling module
 *
 *	Authors:
 *	Sami Kivisaari		<skivisaa@cc.hut.fi>
 *	Ville Nuorvala          <vnuorval@tml.hut.fi>
 *
 *	$Id: s.tunnel_ha.c 1.39 03/09/22 16:45:04+03:00 vnuorval@amber.hut.mediapoli.com $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/route.h>
#include <linux/ipv6_route.h>

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif /* CONFIG_SYSCTL */

#include <net/protocol.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/dst.h>
#include <net/addrconf.h>
#include <net/ipv6_tunnel.h>

#include "tunnel.h"
#include "debug.h"
#include "stats.h"

#define MIPV6_TNL_MAX IP6_TNL_MAX
#define MIPV6_TNL_MIN 1

int mipv6_max_tnls = 3;
int mipv6_min_tnls = 1;

DECLARE_MUTEX(tnl_sem);

int mipv6_max_tnls_sysctl(ctl_table *ctl, int write, struct file *filp,
			  void *buffer, size_t *lenp, loff_t *ppos)
{
	int err;
	
	DEBUG_FUNC();

	down(&tnl_sem);
	if (write) {
		int diff;
		int old_max_tnls = mipv6_max_tnls;
		err = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);
		if (err < 0) 
			goto out;
		if (mipv6_max_tnls < mipv6_min_tnls || 
		    mipv6_max_tnls > MIPV6_TNL_MAX) {
			mipv6_max_tnls = old_max_tnls;
			goto out;
		}
		if (mipv6_max_tnls < old_max_tnls) {
			diff = old_max_tnls - mipv6_max_tnls;
			ip6ip6_tnl_dec_max_kdev_count(diff);
		} else if (mipv6_max_tnls > old_max_tnls) {
			diff = mipv6_max_tnls - old_max_tnls;
			ip6ip6_tnl_inc_max_kdev_count(diff);
		}
	} else {
		err = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);
	}
out:
	up(&tnl_sem);
	return err;
}

int mipv6_min_tnls_sysctl(ctl_table *ctl, int write, struct file *filp,
			  void *buffer, size_t *lenp, loff_t *ppos)
{
	int err;

	DEBUG_FUNC();

	down(&tnl_sem);
	if (write) {
		int diff;
		int old_min_tnls = mipv6_min_tnls;
		err = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);
		if (err < 0) 
			goto out;
		if (mipv6_min_tnls > mipv6_max_tnls || 
		    mipv6_min_tnls < MIPV6_TNL_MIN) {
			mipv6_min_tnls = old_min_tnls;
			goto out;
		}
		if (mipv6_min_tnls < old_min_tnls) {
			diff = old_min_tnls - mipv6_min_tnls;
			ip6ip6_tnl_dec_min_kdev_count(diff);
		} else if (mipv6_min_tnls > old_min_tnls) {
			diff = mipv6_min_tnls - old_min_tnls;
			ip6ip6_tnl_inc_min_kdev_count(diff);
		}
	} else {
		err = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);
	}
out:
	up(&tnl_sem);
	return err;
}

__init void mipv6_initialize_tunnel(void)
{
	down(&tnl_sem);
	ip6ip6_tnl_inc_max_kdev_count(mipv6_max_tnls);
	ip6ip6_tnl_inc_min_kdev_count(mipv6_min_tnls);
	up(&tnl_sem);
}

__exit void mipv6_shutdown_tunnel(void)
{
	down(&tnl_sem);
	ip6ip6_tnl_dec_min_kdev_count(mipv6_min_tnls);
	ip6ip6_tnl_dec_max_kdev_count(mipv6_max_tnls);
	up(&tnl_sem);
}

static __inline__ int mipv6_tnl_add(struct in6_addr *remote, 
				    struct in6_addr *local) 
{
	struct ip6_tnl_parm p;
	int ret;

	DEBUG_FUNC();

 	memset(&p, 0, sizeof(p));
	p.proto = IPPROTO_IPV6;
	ipv6_addr_copy(&p.laddr, local);
	ipv6_addr_copy(&p.raddr, remote);
	p.hop_limit = 255;
	p.flags = (IP6_TNL_F_KERNEL_DEV | IP6_TNL_F_MIP6_DEV |
		   IP6_TNL_F_IGN_ENCAP_LIMIT);

	ret = ip6ip6_kernel_tnl_add(&p);
	if (ret > 0) {
		DEBUG(DBG_INFO, "added tunnel from: "
		      "%x:%x:%x:%x:%x:%x:%x:%x to: %x:%x:%x:%x:%x:%x:%x:%x", 
		      NIPV6ADDR(local), NIPV6ADDR(remote));
	} else {
		DEBUG(DBG_WARNING, "unable to add tunnel from: "
		      "%x:%x:%x:%x:%x:%x:%x:%x to: %x:%x:%x:%x:%x:%x:%x:%x", 
		      NIPV6ADDR(local), NIPV6ADDR(remote));		
	}
	return ret;
}

static __inline__ int mipv6_tnl_del(struct in6_addr *remote, 
				    struct in6_addr *local) 
{
	struct ip6_tnl *t = ip6ip6_tnl_lookup(remote, local);
	
	DEBUG_FUNC();
	
	if (t != NULL && (t->parms.flags & IP6_TNL_F_MIP6_DEV)) {
		DEBUG(DBG_INFO, "deleting tunnel from: "
		      "%x:%x:%x:%x:%x:%x:%x:%x to: %x:%x:%x:%x:%x:%x:%x:%x", 
		      NIPV6ADDR(local), NIPV6ADDR(remote));

		return ip6ip6_kernel_tnl_del(t);
	}
	return 0;
}

static __inline__ int add_route_to_mn(struct in6_addr *coa, 
				      struct in6_addr *ha_addr, 
				      struct in6_addr *home_addr) 
{
	struct in6_rtmsg rtmsg;
	int err;
	struct ip6_tnl *t = ip6ip6_tnl_lookup(coa, ha_addr);
	
	if (!is_mip6_tnl(t)) {
		DEBUG(DBG_CRITICAL,"Tunnel missing");
		return -ENODEV;
	}
	
	DEBUG(DBG_INFO, "adding route to: %x:%x:%x:%x:%x:%x:%x:%x via "
	      "tunnel device", NIPV6ADDR(home_addr));

	memset(&rtmsg, 0, sizeof(rtmsg));
	ipv6_addr_copy(&rtmsg.rtmsg_dst, home_addr);
	rtmsg.rtmsg_dst_len = 128;
	rtmsg.rtmsg_type = RTMSG_NEWROUTE;
	rtmsg.rtmsg_flags = RTF_UP | RTF_NONEXTHOP | RTF_HOST | RTF_MOBILENODE;
	rtmsg.rtmsg_ifindex = t->dev->ifindex;
	rtmsg.rtmsg_metric = IP6_RT_PRIO_MIPV6;
	if ((err = ip6_route_add(&rtmsg, NULL, NULL)) == -EEXIST)
		err = 0;
	return err;
}

static __inline__ void del_route_to_mn(struct in6_addr *coa, 
				       struct in6_addr *ha_addr, 
				       struct in6_addr *home_addr) 
{
	struct ip6_tnl *t = ip6ip6_tnl_lookup(coa, ha_addr);

	DEBUG_FUNC();

	if (is_mip6_tnl(t)) {
		int err;
		struct in6_rtmsg rtmsg;

		DEBUG(DBG_INFO, "deleting route to: %x:%x:%x:%x:%x:%x:%x:%x "
		      " via tunnel device", NIPV6ADDR(home_addr));

		memset(&rtmsg, 0, sizeof(rtmsg));
		ipv6_addr_copy(&rtmsg.rtmsg_dst, home_addr);
		rtmsg.rtmsg_dst_len = 128;
		rtmsg.rtmsg_ifindex = t->dev->ifindex;
		rtmsg.rtmsg_metric = IP6_RT_PRIO_MIPV6;
		err = ip6_route_del(&rtmsg, NULL, NULL);
	}
}


int mipv6_add_tnl_to_mn(struct in6_addr *coa, 
			struct in6_addr *ha_addr,
			struct in6_addr *home_addr)
{
	int ret;

	DEBUG_FUNC();

	ret = mipv6_tnl_add(coa, ha_addr);

	if (ret > 0) {
		int err = add_route_to_mn(coa, ha_addr, home_addr);
		if (err) {
			if (err != -ENODEV) {
				mipv6_tnl_del(coa, ha_addr);
			}
			return err;
		}
	}
	return ret;
} 

int mipv6_del_tnl_to_mn(struct in6_addr *coa, 
			struct in6_addr *ha_addr,
			struct in6_addr *home_addr)
{
	DEBUG_FUNC();
	del_route_to_mn(coa, ha_addr, home_addr);
	return mipv6_tnl_del(coa, ha_addr);
} 
