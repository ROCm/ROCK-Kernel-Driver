/*
 *      Home-agent functionality
 *
 *      Authors:
 *      Sami Kivisaari           <skivisaa@cc.hut.fi>
 *      Henrik Petander          <lpetande@cc.hut.fi>
 *
 *      $Id: s.ha.c 1.89 03/09/29 19:43:11+03:00 vnuorval@amber.hut.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *   
 *      Changes: Venkata Jagana,
 *               Krishna Kumar     : Statistics fix
 *               Masahide Nakamura : Use of mipv6_forward  
 *     
 */

#include <linux/autoconf.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/init.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#include <net/neighbour.h>
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/neighbour.h>

#include "tunnel_ha.h"
#include "bcache.h"
#include "stats.h"
#include "debug.h"
#include "util.h"
#include "ha.h"
#include "config.h"
#include "mobhdr.h"

static int mipv6_ha_tunnel_sitelocal = 0;

#ifdef CONFIG_SYSCTL

static struct ctl_table_header *mipv6_ha_sysctl_header;

static struct mipv6_ha_sysctl_table
{
	struct ctl_table_header *sysctl_header;
	ctl_table mipv6_vars[3];
	ctl_table mipv6_mobility_table[2];
	ctl_table mipv6_proto_table[2];
	ctl_table mipv6_root_table[2];
} mipv6_ha_sysctl = {
	NULL,

        {{NET_IPV6_MOBILITY_TUNNEL_SITELOCAL, "tunnel_sitelocal",
	  &mipv6_ha_tunnel_sitelocal, sizeof(int), 0644, NULL, 
	  &proc_dointvec},
	 {0}},

	{{NET_IPV6_MOBILITY, "mobility", NULL, 0, 0555, 
	  mipv6_ha_sysctl.mipv6_vars}, {0}},
	{{NET_IPV6, "ipv6", NULL, 0, 0555, 
	  mipv6_ha_sysctl.mipv6_mobility_table}, {0}},
	{{CTL_NET, "net", NULL, 0, 0555, 
	  mipv6_ha_sysctl.mipv6_proto_table}, {0}}
};

#endif /* CONFIG_SYSCTL */

/*  this should be in some header file but it isn't  */
extern void ndisc_send_na(struct net_device *dev, struct neighbour *neigh,
			  struct in6_addr *daddr, struct in6_addr *solicited_addr, 
			  int router, int solicited, int override, int inc_opt);

/*  this is defined in kernel IPv6 module (sockglue.c)  */
extern struct packet_type ipv6_packet_type;

/* mipv6_forward: Intercept NS packets destined to home address of MN */
int mipv6_forward(struct sk_buff *skb)
{
	struct ipv6hdr *ipv6h;
	struct in6_addr *daddr, *saddr;
	__u8 nexthdr;
	int nhoff;
	
	if (skb == NULL) return  0;
	
	ipv6h = skb->nh.ipv6h;
	daddr = &ipv6h->daddr;
	saddr = &ipv6h->saddr;

	nexthdr = ipv6h->nexthdr;
	nhoff = sizeof(*ipv6h);
   
	if (ipv6_ext_hdr(nexthdr))
		nhoff = ipv6_skip_exthdr(skb, nhoff, &nexthdr,
					 skb->len - sizeof(*ipv6h));
	
	/* Do not to forward Neighbor Solicitation to Home Address of MN */
	if (nexthdr == IPPROTO_ICMPV6) {
		struct icmp6hdr *icmp6h;
		int dest_type;
		
		if (nhoff < 0 || !pskb_may_pull(skb, nhoff + 
						sizeof(struct icmp6hdr))) {
			kfree_skb(skb);
			return 0;
                   }
		
		dest_type = ipv6_addr_type(daddr);
		icmp6h = (struct icmp6hdr *)&skb->nh.raw[nhoff];
		
		/* Intercepts NS to HoA of MN */

		if ((icmp6h->icmp6_type == NDISC_NEIGHBOUR_SOLICITATION) ||
		    ((dest_type & IPV6_ADDR_MULTICAST) &&
		     (icmp6h->icmp6_type == NDISC_ROUTER_ADVERTISEMENT))) {
			ip6_input(skb);
		} else {
			ip6_forward(skb);
		}
	} else {
		ip6_forward(skb);
	}
	return 0;
}


/**
 * mipv6_proxy_nd_rem - stop acting as a proxy for @home_address
 * @home_addr: address to remove
 * @ha_addr: home agent's address on home link
 * @linklocal: link-local compatibility bit
 *
 * When Home Agent acts as a proxy for an address it must leave the
 * solicited node multicast group for that address and stop responding 
 * to neighbour solicitations.  
 **/
static int mipv6_proxy_nd_rem(struct in6_addr *home_addr,
			      int ifindex, int linklocal)
{
        /* When MN returns home HA leaves the solicited mcast groups
         * for MNs home addresses 
	 */
	int err;
	struct net_device *dev;
	
	DEBUG_FUNC();
	
        if ((dev = dev_get_by_index(ifindex)) == NULL) {
		DEBUG(DBG_ERROR, "couldn't get dev");
		return -ENODEV;
	}
#if 1	/* TEST */
	/* Remove link-local entry */
	if (linklocal) {
		struct in6_addr ll_addr;
		mipv6_generate_ll_addr(&ll_addr, home_addr);
		if ((err = pneigh_delete(&nd_tbl, &ll_addr, dev)) < 0) {
			DEBUG(DBG_INFO,
			      "peigh_delete failed for "
			      "%x:%x:%x:%x:%x:%x:%x:%x",
			      NIPV6ADDR(&ll_addr));	
		}
	}
#endif
	/* Remove global (or site-local) entry */
	if ((err = pneigh_delete(&nd_tbl, home_addr, dev)) < 0) {
		DEBUG(DBG_INFO,
		      "peigh_delete failed for " 
		      "%x:%x:%x:%x:%x:%x:%x:%x",
		      NIPV6ADDR(home_addr));
	}
	dev_put(dev);
	return err;
}

/**
 * mipv6_proxy_nd - join multicast group for this address
 * @home_addr: address to defend
 * @ha_addr: home agent's address on home link
 * @linklocal: link-local compatibility bit
 *
 * While Mobile Node is away from home, Home Agent acts as a proxy for
 * @home_address. HA responds to neighbour solicitations for  @home_address 
 * thus getting all packets destined to home address of MN. 
 **/
static int mipv6_proxy_nd(struct in6_addr *home_addr, 
			  int ifindex, int linklocal)
{  
	/* The HA sends a proxy ndisc_na message to all hosts on MN's
	 * home subnet by sending a neighbor advertisement with the
	 * home address or all addresses of the mobile node if the
	 * prefix is not 0. The addresses are formed by combining the
	 * suffix or the host part of the address with each subnet
	 * prefix that exists in the home subnet 
	 */
	
        /* Since no previous entry for MN exists a proxy_nd advertisement
	 * is sent to all nodes link local multicast address
	 */	
	int err = -1;

	struct net_device *dev;
	struct in6_addr na_saddr;
	struct in6_addr ll_addr;
	struct pneigh_entry *ll_pneigh;
	struct in6_addr mcdest;
	int send_ll_na = 0;
	int inc_opt = 1;
	int solicited = 0;
	int override = 1;
	
	DEBUG_FUNC();
	
	if ((dev = dev_get_by_index(ifindex)) == NULL) {
		DEBUG(DBG_ERROR, "couldn't get dev");
		return -ENODEV;
	}
	
	if (!pneigh_lookup(&nd_tbl, home_addr, dev, 1)) {
		DEBUG(DBG_INFO,
		      "peigh_lookup failed for "
		      "%x:%x:%x:%x:%x:%x:%x:%x",
		      NIPV6ADDR(home_addr));
		goto free_dev;
	}
#if 1 /* TEST */
	if (linklocal) {
		mipv6_generate_ll_addr(&ll_addr, home_addr);
		
		if ((ll_pneigh = pneigh_lookup(&nd_tbl, &ll_addr, 
					       dev, 1)) == NULL) {
			DEBUG(DBG_INFO,
			      "peigh_lookup failed for "
			      "%x:%x:%x:%x:%x:%x:%x:%x",
			      NIPV6ADDR(&ll_addr));
			pneigh_delete(&nd_tbl, home_addr, dev);
			goto free_dev;
		} else {
			send_ll_na = 1;
		}
	} else {
		ll_pneigh = NULL;
	}
#endif	
	/* Proxy neighbor advertisement of MN's home address 
	 * to all nodes solicited multicast address 
	 */
	if (!ipv6_get_lladdr(dev, &na_saddr)) {	
		ipv6_addr_all_nodes(&mcdest); 
		ndisc_send_na(dev, NULL, &mcdest, home_addr, 0, 
			      solicited, override, inc_opt);
#if 1 /* TEST */
		if (send_ll_na) {
			ndisc_send_na(dev, NULL, &mcdest, &ll_addr, 
				      0, solicited, override, inc_opt);
		}
#endif
		err = 0;
	} else {
		DEBUG(DBG_ERROR, "failed to get link local address for sending proxy NA");
	}
free_dev:
	dev_put(dev);
	return err;
	
}

struct inet6_ifaddr *is_on_link_ipv6_address(struct in6_addr *mn_haddr,
					     struct in6_addr *ha_addr)
{
	struct inet6_ifaddr *ifp;
	struct inet6_dev *in6_dev;
	struct inet6_ifaddr *oifp = NULL;

	if ((ifp = ipv6_get_ifaddr(ha_addr, 0, 0)) == NULL)
		return NULL;

	if ((in6_dev = ifp->idev) != NULL) {
		in6_dev_hold(in6_dev);
		oifp = in6_dev->addr_list;
		while (oifp != NULL) {
			spin_lock(&oifp->lock);
			if (mipv6_prefix_compare(&oifp->addr, mn_haddr,
						 oifp->prefix_len) &&
			    !(oifp->flags & IFA_F_TENTATIVE)) {
				spin_unlock(&oifp->lock);
				DEBUG(DBG_INFO, "Home Addr Opt: on-link");
				in6_ifa_hold(oifp);
				break;
			}
			spin_unlock(&oifp->lock);
			oifp = oifp->if_next;
		}
		in6_dev_put(in6_dev);
	}
	in6_ifa_put(ifp);
/*      DEBUG(DBG_WARNING, "Home Addr Opt NOT on-link"); */
	return oifp;

}

/*
 * Lifetime checks. ifp->valid_lft >= ifp->prefered_lft always (see addrconf.c)
 * Returned value is in seconds.
 */

static __u32 get_min_lifetime(struct inet6_ifaddr *ifp, __u32 lifetime)
{
	__u32 rem_lifetime = 0;
	unsigned long now = jiffies;

	if (ifp->valid_lft == 0) {
		rem_lifetime = lifetime;
	} else {
		__u32 valid_lft_left =
		    ifp->valid_lft - ((now - ifp->tstamp) / HZ);
		rem_lifetime =
		    min_t(unsigned long, valid_lft_left, lifetime);
	}

	return rem_lifetime;
}

#define MAX_LIFETIME 1000

/**
 * mipv6_lifetime_check - check maximum lifetime is not exceeded
 * @lifetime: lifetime to check
 *
 * Checks @lifetime does not exceed %MAX_LIFETIME.  Returns @lifetime
 * if not exceeded, otherwise returns %MAX_LIFETIME.
 **/
static __u32 mipv6_lifetime_check(__u32 lifetime)
{
	return (lifetime > MAX_LIFETIME) ? MAX_LIFETIME : lifetime;
}

/* Generic routine handling finish of BU processing */
void mipv6_bu_finish(struct inet6_ifaddr *ifp, int ifindex, __u8 ba_status,
		     struct in6_addr *saddr, struct in6_addr *daddr,
		     struct in6_addr *haddr, struct in6_addr *coa,
		     __u32 ba_lifetime, __u16 sequence, __u8 flags, __u8 *k_bu)
{
	struct in6_addr *reply_addr;
	int err;

	if (ba_status >= REASON_UNSPECIFIED) {
		/* DAD failed */
		reply_addr = saddr;
		goto out;
	}
	
	reply_addr = haddr;
	ba_lifetime = get_min_lifetime(ifp, ba_lifetime);
	ba_lifetime = mipv6_lifetime_check(ba_lifetime);

	if ((err = mipv6_bcache_add(ifindex, daddr, haddr, coa, 
				    ba_lifetime, sequence, flags,
				    HOME_REGISTRATION)) != 0 ) {
		DEBUG(DBG_WARNING, "home reg failed.");

		if (err == -ENOMEDIUM)
			return;

		ba_status = INSUFFICIENT_RESOURCES;
		reply_addr = saddr;
	} else {
		DEBUG(DBG_INFO, "home reg succeeded.");
	}

	DEBUG(DBG_DATADUMP, "home_addr: %x:%x:%x:%x:%x:%x:%x:%x",
	      NIPV6ADDR(haddr));
	DEBUG(DBG_DATADUMP, "coa: %x:%x:%x:%x:%x:%x:%x:%x",
	      NIPV6ADDR(coa));
	DEBUG(DBG_DATADUMP, "lifet:%d, seq:%d", ba_lifetime, sequence);
out:
	mipv6_send_ba(daddr, haddr, coa, ba_status, sequence,
		      ba_lifetime, k_bu);
}

static int ha_proxy_create(int flags, int ifindex, struct in6_addr *coa,
			   struct in6_addr *our_addr, struct in6_addr *home_addr)
{
	int ret;

	if ((ret = mipv6_add_tnl_to_mn(coa, our_addr, home_addr)) <= 0) {
		if (ret != -ENOMEDIUM) {
			DEBUG(DBG_ERROR, "unable to configure tunnel to MN!");
		}
		return -1;
	}
	if (mipv6_proxy_nd(home_addr, ifindex, 
			   flags & MIPV6_BU_F_LLADDR) != 0) {
		DEBUG(DBG_ERROR, "mipv6_proxy_nd failed!");
		mipv6_del_tnl_to_mn(coa, our_addr, home_addr);
		return -2;
	}
	return 0;
}

static void ha_proxy_del(struct in6_addr *home_addr, struct mipv6_bce *entry)
{
	if (mipv6_proxy_nd_rem(&entry->home_addr, entry->ifindex,
			       entry->flags & MIPV6_BU_F_LLADDR) == 0) {
		DEBUG(DBG_INFO, "proxy_nd succ");
	} else {
		DEBUG(DBG_INFO, "proxy_nd fail");
	}
	mipv6_del_tnl_to_mn(&entry->coa, &entry->our_addr, home_addr);
}

static void bc_home_add(int ifindex, struct in6_addr *saddr, 
			struct in6_addr *daddr, struct in6_addr *haddr, 
			struct in6_addr *coa, __u32 lifetime, 
			__u16 sequence, __u8 flags, __u8 *k_bu)
{
	struct inet6_ifaddr *ifp = NULL;
	__u8 ba_status = SUCCESS;

	DEBUG_FUNC();

	ifp = is_on_link_ipv6_address(haddr, daddr);

	if (ifp == NULL) {
		ba_status = NOT_HOME_SUBNET;
	} else if (((ipv6_addr_type(haddr) & IPV6_ADDR_SITELOCAL) ||
		    (ipv6_addr_type(coa) & IPV6_ADDR_SITELOCAL))
		   && !mipv6_ha_tunnel_sitelocal) {
		/* Site-local home or care-of addresses are not 
		   accepted by default */
		ba_status = ADMINISTRATIVELY_PROHIBITED;
	} else {
		int ret;

		ifindex = ifp->idev->dev->ifindex;

		if ((ret = mipv6_dad_start(ifp, ifindex, saddr, daddr, 
					   haddr, coa, lifetime,
					   sequence, flags)) < 0) {
			/* An error occurred */
			ba_status = -ret;
		} else if (ret) {
			/* DAD is needed to be performed. */
			in6_ifa_put(ifp);
			return;
		}
	}

	mipv6_bu_finish(ifp, ifindex, ba_status, saddr, daddr, haddr, coa,
			lifetime, sequence, flags, k_bu);
	if (ifp)
		in6_ifa_put(ifp);
}

static void bc_home_delete(struct in6_addr *daddr, struct in6_addr *haddr, 
			   struct in6_addr *coa, __u16 sequence, __u8 flags,
			   __u8 *k_bu)
{
	__u8 status = SUCCESS;
	struct mipv6_bce bce;

	/* Primary Care-of Address Deregistration */
	if (mipv6_bcache_get(haddr, daddr, &bce) < 0) {
		DEBUG(DBG_INFO, "entry is not in cache");
		status = NOT_HA_FOR_MN;
	} else {
		if (flags != bce.flags) {
			DEBUG(DBG_INFO, "entry/BU flag mismatch");
			return;
		}
		ha_proxy_del(&bce.home_addr, &bce);
		mipv6_bcache_delete(haddr, daddr, HOME_REGISTRATION);
	}
	mipv6_send_ba(daddr, haddr, coa, status, sequence, 0, k_bu);
}

extern int mipv6_ra_rcv_ptr(struct sk_buff *skb, struct icmp6hdr *msg);


static int
mipv6_ha_tnl_xmit_stats_hook(struct ip6_tnl *t, struct sk_buff *skb)
{
	DEBUG_FUNC();
	if (is_mip6_tnl(t))
		MIPV6_INC_STATS(n_encapsulations);
	return IP6_TNL_ACCEPT;
}

static struct ip6_tnl_hook_ops mipv6_ha_tnl_xmit_stats_ops = {
	{NULL, NULL},
	IP6_TNL_PRE_ENCAP,
	IP6_TNL_PRI_LAST,
	mipv6_ha_tnl_xmit_stats_hook
};

static int
mipv6_ha_tnl_rcv_stats_hook(struct ip6_tnl *t, struct sk_buff *skb)
{
	DEBUG_FUNC();
	if (is_mip6_tnl(t))
		MIPV6_INC_STATS(n_decapsulations);
	return IP6_TNL_ACCEPT;
}

static struct ip6_tnl_hook_ops mipv6_ha_tnl_rcv_stats_ops = {
	{NULL, NULL},
	IP6_TNL_PRE_DECAP,
	IP6_TNL_PRI_LAST,
	mipv6_ha_tnl_rcv_stats_hook
};

static struct mip6_func old;

int __init mipv6_ha_init(void)
{
	DEBUG_FUNC();
	
#ifdef CONFIG_SYSCTL
	if (!(mipv6_ha_sysctl_header = 
	      register_sysctl_table(mipv6_ha_sysctl.mipv6_root_table, 0)))
		printk(KERN_ERR "Failed to register sysctl handlers!");
#endif
	memcpy(&old, &mip6_fn, sizeof(struct mip6_func));
	mip6_fn.bce_home_add = bc_home_add;
	mip6_fn.bce_home_del = bc_home_delete;
	mip6_fn.proxy_del = ha_proxy_del;
	mip6_fn.proxy_create = ha_proxy_create;
	/*  register packet interception hooks  */
	ip6ip6_tnl_register_hook(&mipv6_ha_tnl_xmit_stats_ops);
	ip6ip6_tnl_register_hook(&mipv6_ha_tnl_rcv_stats_ops);
	return 0;
}

void __exit mipv6_ha_exit(void)
{
	DEBUG_FUNC();

#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(mipv6_ha_sysctl_header);
#endif

	/*  remove packet interception hooks  */
	ip6ip6_tnl_unregister_hook(&mipv6_ha_tnl_rcv_stats_ops);
	ip6ip6_tnl_unregister_hook(&mipv6_ha_tnl_xmit_stats_ops);

	mip6_fn.bce_home_add = old.bce_home_add;
	mip6_fn.bce_home_del = old.bce_home_del;
	mip6_fn.proxy_del = old.proxy_del;
	mip6_fn.proxy_create = old.proxy_create;
}
