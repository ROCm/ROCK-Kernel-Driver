/*
 *	IPv6-IPv6 tunneling module
 *
 *	Authors:
 *	Sami Kivisaari		<skivisaa@cc.hut.fi>
 *	Ville Nuorvala          <vnuorval@tml.hut.fi>
 *
 *	$Id$
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

#include "tunnel.h"
#include "debug.h"
#include "stats.h"

static struct net_device *mn_ha_tdev;

static spinlock_t mn_ha_lock = SPIN_LOCK_UNLOCKED;

static __inline__ int add_reverse_route(struct in6_addr *ha_addr,
					struct in6_addr *home_addr, 
					struct net_device *tdev) 
{
	struct in6_rtmsg rtmsg;
	int err;

	DEBUG_FUNC();

	memset(&rtmsg, 0, sizeof(rtmsg));
	rtmsg.rtmsg_type = RTMSG_NEWROUTE;
	ipv6_addr_copy(&rtmsg.rtmsg_src, home_addr);
	rtmsg.rtmsg_src_len = 128;
	rtmsg.rtmsg_flags = RTF_UP | RTF_DEFAULT;
	rtmsg.rtmsg_ifindex = tdev->ifindex;
	rtmsg.rtmsg_metric = IP6_RT_PRIO_MIPV6;
	if ((err = ip6_route_add(&rtmsg, NULL, NULL)) == -EEXIST)
		return 0;
	return err;	
}

static __inline__ void del_reverse_route(struct in6_addr *ha_addr, 
					 struct in6_addr *home_addr,
					 struct net_device *tdev) 
{
int err;
	struct in6_rtmsg rtmsg;

	DEBUG(DBG_INFO, "removing reverse route via tunnel device");

	memset(&rtmsg, 0, sizeof(rtmsg));
	ipv6_addr_copy(&rtmsg.rtmsg_src, home_addr);
	rtmsg.rtmsg_src_len = 128;
	rtmsg.rtmsg_ifindex = tdev->ifindex;
	rtmsg.rtmsg_metric = IP6_RT_PRIO_MIPV6;
	err = ip6_route_del(&rtmsg, NULL, NULL);
}

int mipv6_add_tnl_to_ha(void)
{
	struct ip6_tnl_parm p;
	struct ip6_tnl *t;
	int err;

	DEBUG_FUNC();

 	memset(&p, 0, sizeof(p));
	p.proto = IPPROTO_IPV6;
	p.hop_limit = 255;
	p.flags = (IP6_TNL_F_KERNEL_DEV | IP6_TNL_F_MIP6_DEV |
		   IP6_TNL_F_IGN_ENCAP_LIMIT);
	strcpy(p.name, "mip6mnha1");

	rtnl_lock();
	if ((err = ip6ip6_tnl_create(&p, &t))) {
		rtnl_unlock();
		return err;
	}
	spin_lock_bh(&mn_ha_lock);

	if (!mn_ha_tdev) {
		mn_ha_tdev = t->dev;
		dev_hold(mn_ha_tdev);
	}
	spin_unlock_bh(&mn_ha_lock);
	dev_open(t->dev);
	rtnl_unlock();
	return 0;
} 

int mipv6_mv_tnl_to_ha(struct in6_addr *ha_addr,
		       struct in6_addr *coa,
		       struct in6_addr *home_addr, int add)
{
	int err = -ENODEV;

	DEBUG_FUNC();

	spin_lock_bh(&mn_ha_lock);
	if (mn_ha_tdev) {
		struct ip6_tnl_parm p;
		memset(&p, 0, sizeof(p));
		p.proto = IPPROTO_IPV6;
		ipv6_addr_copy(&p.laddr, coa);
		ipv6_addr_copy(&p.raddr, ha_addr);
		p.hop_limit = 255;
		p.flags = (IP6_TNL_F_KERNEL_DEV | IP6_TNL_F_MIP6_DEV |
			   IP6_TNL_F_IGN_ENCAP_LIMIT);

		ip6ip6_tnl_change((struct ip6_tnl *) mn_ha_tdev->priv, &p);
		if (add && ipv6_addr_cmp(coa, home_addr)) {
			err = add_reverse_route(ha_addr, home_addr, 
						mn_ha_tdev);
		} else {
			del_reverse_route(ha_addr, home_addr, mn_ha_tdev);
			err = 0;
		}
	}
	spin_unlock_bh(&mn_ha_lock);
	return err;
}

void mipv6_del_tnl_to_ha(void)
{
	struct net_device *dev;

	DEBUG_FUNC();

	rtnl_lock();
	spin_lock_bh(&mn_ha_lock);
	dev = mn_ha_tdev;
	mn_ha_tdev = NULL;
	spin_unlock_bh(&mn_ha_lock);
	dev_put(dev);
	unregister_netdevice(dev);
	rtnl_unlock();
}
