/*
 *      Mobile-node functionality
 *
 *      Authors:
 *      Sami Kivisaari          <skivisaa@cc.hut.fi>
 *
 *      $Id: s.mn.c 1.205 03/10/02 15:33:54+03:00 henkku@mart10.hut.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#include <linux/autoconf.h>
#include <linux/sched.h>
#include <linux/ipv6.h>
#include <linux/net.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/ipsec.h>
#include <linux/notifier.h>
#include <linux/list.h>
#include <linux/route.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>

#include <asm/uaccess.h>

#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/neighbour.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/mipglue.h>

#include "util.h"
#include "mdetect.h"
#include "bul.h"
#include "mobhdr.h"
#include "debug.h"
#include "mn.h"
#include "mipv6_icmp.h"
#include "multiaccess_ctl.h"
#include "prefix.h"
#include "tunnel_mn.h"
#include "stats.h"
#include "config.h"

#define MIPV6_BUL_SIZE 128

static LIST_HEAD(mn_info_list);

/* Lock for list of MN infos */
rwlock_t mn_info_lock = RW_LOCK_UNLOCKED;

static spinlock_t ifrh_lock = SPIN_LOCK_UNLOCKED;

struct ifr_holder {
	struct list_head list;
	struct in6_ifreq ifr;
	int old_ifi;
	struct handoff *ho;
};

LIST_HEAD(ifrh_list);

/* Determines whether manually configured home addresses are preferred as 
 * source addresses over dynamically configured ones
 */
int mipv6_use_preconfigured_hoaddr = 1; 

/* Determines whether home addresses, which are at home are preferred as 
 * source addresses over other home addresses
 */
int mipv6_use_topol_corr_hoaddr = 0;

/*  Defined in ndisc.c of IPv6 module */
extern void ndisc_send_na(struct net_device *dev, struct neighbour *neigh,
			  struct in6_addr *daddr,
			  struct in6_addr *solicited_addr, int router, 
			  int solicited, int override, int inc_opt);

static spinlock_t icmpv6_id_lock = SPIN_LOCK_UNLOCKED;
static __u16 icmpv6_id = 0;

static inline __u16 mipv6_get_dhaad_id(void)
{
	__u16 ret;
	spin_lock_bh(&icmpv6_id_lock);
	ret = ++icmpv6_id;
	spin_unlock_bh(&icmpv6_id_lock);
	return ret;
}

/** 
 * mipv6_mninfo_get_by_home - Returns mn_info for a home address
 * @haddr: home address of MN
 *
 * Returns mn_info on success %NULL otherwise.  Caller MUST hold
 * @mn_info_lock (read or write).
 **/
struct mn_info *mipv6_mninfo_get_by_home(struct in6_addr *haddr)
{
	struct list_head *lh;
	struct mn_info *minfo;

	DEBUG_FUNC();

	if (!haddr)
		return NULL;

	list_for_each(lh, &mn_info_list) {
		minfo = list_entry(lh, struct mn_info, list);
		spin_lock(&minfo->lock);
		if (!ipv6_addr_cmp(&minfo->home_addr, haddr)) {
			spin_unlock(&minfo->lock);
			return minfo;
		}
		spin_unlock(&minfo->lock);
	}
	return NULL;
}

/**
 * mipv6_mninfo_get_by_ha - Lookup mn_info with Home Agent address
 * @home_agent: Home Agent address
 *
 * Searches for a mn_info entry with @ha set to @home_agent.  You MUST
 * hold @mn_info_lock when calling this function.  Returns pointer to
 * mn_info entry or %NULL on failure.
 **/
struct mn_info *mipv6_mninfo_get_by_ha(struct in6_addr *home_agent)
{
	struct list_head *lh;
	struct mn_info *minfo;

	if (!home_agent)
		return NULL;

	list_for_each(lh, &mn_info_list) {
		minfo = list_entry(lh, struct mn_info, list);
		spin_lock(&minfo->lock);
		if (!ipv6_addr_cmp(&minfo->ha, home_agent)) {
			spin_unlock(&minfo->lock);
			return minfo;
		}
		spin_unlock(&minfo->lock);
	}
	return NULL;
}

/**
 * mipv6_mninfo_get_by_id - Lookup mn_info with id
 * @id: DHAAD identifier
 *
 * Searches for a mn_info entry with @dhaad_id set to @id.  You MUST
 * hold @mn_info_lock when calling this function.  Returns pointer to
 * mn_info entry or %NULL on failure.
 **/
struct mn_info *mipv6_mninfo_get_by_id(unsigned short id)
{
	struct list_head *lh;
	struct mn_info *minfo = 0;

	list_for_each(lh, &mn_info_list) {
		minfo = list_entry(lh, struct mn_info, list);
		spin_lock(&minfo->lock);
		if (minfo->dhaad_id == id) {
			spin_unlock(&minfo->lock);
			return minfo;
		}
		spin_unlock(&minfo->lock);
	}
	return NULL;
}

/** 
 * mipv6_mninfo_add - Adds a new home info for MN
 * @ifindex: Interface for home address
 * @home_addr:  Home address of MN, must be set
 * @plen: prefix length of the home address, must be set
 * @isathome : home address at home
 * @lifetime: lifetime of the home address, 0 is infinite
 * @ha: home agent for the home address
 * @ha_plen: prefix length of home agent's address, can be zero 
 * @ha_lifetime: Lifetime of the home address, 0 is infinite
 *
 * The function adds a new home info entry for MN, allowing it to
 * register the home address with the home agent.  Starts home
 * registration process.  If @ha is %ADDRANY, DHAAD is performed to
 * find a home agent.  Returns 0 on success, a negative value
 * otherwise.  Caller MUST NOT hold @mn_info_lock or
 * @addrconf_hash_lock.
 **/
void mipv6_mninfo_add(int ifindex, struct in6_addr *home_addr, int plen, 
		      int isathome, unsigned long lifetime, struct in6_addr *ha, 
		      int ha_plen, unsigned long ha_lifetime, int man_conf)
{
	struct mn_info *minfo;
	struct in6_addr coa;

	DEBUG_FUNC();

	write_lock_bh(&mn_info_lock);
	if ((minfo = mipv6_mninfo_get_by_home(home_addr)) != NULL){ 
	      DEBUG(1, "MN info already exists");
	      write_unlock_bh(&mn_info_lock);
	      return;
	}
	minfo = kmalloc(sizeof(struct mn_info), GFP_ATOMIC);
	if (!minfo) {
	       write_unlock_bh(&mn_info_lock);
	       return;
	}
	memset(minfo, 0, sizeof(struct mn_info));
	spin_lock_init(&minfo->lock);

	
	ipv6_addr_copy(&minfo->home_addr, home_addr);

	if (ha)
		ipv6_addr_copy(&minfo->ha, ha);
	if (ha_plen < 128 && ha_plen > 0)
		minfo->home_plen = ha_plen; 
	else minfo->home_plen = 64;

	minfo->ifindex_user = ifindex; /* Ifindex for tunnel interface */
	minfo->ifindex = ifindex; /* Interface on which home address is currently conf'd */
	/* TODO: we should get home address lifetime from somewhere */
	/* minfo->home_addr_expires = jiffies + lifetime * HZ; */

	/* manual configuration flag cannot be unset by dynamic updates 
	 *  from prefix advertisements
	 */
	if (!minfo->man_conf) minfo->man_conf = man_conf; 
	minfo->is_at_home = isathome;

	list_add(&minfo->list, &mn_info_list);
	write_unlock_bh(&mn_info_lock);

	if (mipv6_get_care_of_address(home_addr, &coa) == 0) 
		init_home_registration(home_addr, &coa);
}

/**
 * mipv6_mninfo_del - Delete home info for MN 
 * @home_addr : Home address or prefix 
 * @del_dyn_only : Delete only dynamically created home entries 
 *
 * Deletes every mn_info entry that matches the first plen bits of
 * @home_addr.  Returns number of deleted entries on success and a
 * negative value otherwise.  Caller MUST NOT hold @mn_info_lock.
 **/
int mipv6_mninfo_del(struct in6_addr *home_addr, int del_dyn_only)
{
	struct list_head *lh, *next;
	struct mn_info *minfo;
	int ret = -1;
	if (!home_addr)
		return -1;

	write_lock(&mn_info_lock);

	list_for_each_safe(lh, next, &mn_info_list) {
		minfo = list_entry(lh, struct mn_info, list);
		if (ipv6_addr_cmp(&minfo->home_addr, home_addr) == 0
		    && ((!minfo->man_conf && del_dyn_only) || !del_dyn_only)){
			list_del(&minfo->list);
			kfree(minfo);
			ret++;
		}
	}
	write_unlock(&mn_info_lock);
	return ret;
}

void mipv6_mn_set_home(int ifindex, struct in6_addr *homeaddr, int plen,
		       struct in6_addr *homeagent, int ha_plen)
{
	mipv6_mninfo_add(ifindex, homeaddr, plen, 0, 0, 
			 homeagent, ha_plen, 0, 1);
}
static int skip_dad(struct in6_addr *addr)
{
	struct mn_info *minfo;
	int ret = 0;

	if (addr == NULL) {
		DEBUG(DBG_CRITICAL, "Null argument");
		return 0;
	}
	read_lock_bh(&mn_info_lock);
	if ((minfo = mipv6_mninfo_get_by_home(addr)) != NULL) {
		if ((minfo->is_at_home != 0) && (minfo->has_home_reg))
			ret = 1;
		DEBUG(DBG_INFO, "minfo->is_at_home == 0: %d, minfo->has_home_reg %d",
		      (minfo->is_at_home == 0), minfo->has_home_reg);
	}
	read_unlock_bh(&mn_info_lock);
	
	return ret;
}
/**
 * mipv6_mn_is_home_addr - Determines if addr is node's home address
 * @addr: IPv6 address
 *
 * Returns 1 if addr is node's home address.  Otherwise returns zero.
 **/
int mipv6_mn_is_home_addr(struct in6_addr *addr)
{
	int ret = 0;

	if (addr == NULL) {
		DEBUG(DBG_CRITICAL, "Null argument");
		return -1;
	}
	read_lock_bh(&mn_info_lock);
	if (mipv6_mninfo_get_by_home(addr))
		ret = 1;
	read_unlock_bh(&mn_info_lock);

	return (ret);
}

/** 
 * mipv6_mn_is_at_home - determine if node is home for a home address
 * @home_addr : home address of MN
 *
 * Returns 1 if home address in question is in the home network, 0
 * otherwise.  Caller MUST NOT not hold @mn_info_lock.
 **/ 
int mipv6_mn_is_at_home(struct in6_addr *home_addr)
{
	struct mn_info *minfo;
	int ret = 0;
	read_lock_bh(&mn_info_lock);
	if ((minfo = mipv6_mninfo_get_by_home(home_addr)) != NULL) {
		spin_lock(&minfo->lock);
		ret = (minfo->is_at_home == MN_AT_HOME);
		spin_unlock(&minfo->lock);
	}
	read_unlock_bh(&mn_info_lock);
	return ret;
}	
void mipv6_mn_set_home_reg(struct in6_addr *home_addr, int has_home_reg)
{
	struct mn_info *minfo;
	read_lock_bh(&mn_info_lock);

	if ((minfo = mipv6_mninfo_get_by_home(home_addr)) != NULL) {
		spin_lock(&minfo->lock);
		minfo->has_home_reg = has_home_reg;
		spin_unlock(&minfo->lock);
	}
	read_unlock_bh(&mn_info_lock);
}	

static int mn_inet6addr_event(
	struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifp = (struct inet6_ifaddr *)ptr;

	switch (event) {
	case NETDEV_UP:
		/* Is address a valid coa ?*/
		if (!(ifp->flags & IFA_F_TENTATIVE))
			mipv6_mdet_finalize_ho(&ifp->addr, 
					       ifp->idev->dev->ifindex);
		else if(skip_dad(&ifp->addr))
			ifp->flags &= ~IFA_F_TENTATIVE;
		break;
	case NETDEV_DOWN:	
#if 0
		/* This is useless with manually configured home 
		   addresses, which will not expire
		*/
		mipv6_mninfo_del(&ifp->addr, 0);
#endif
	  break;

	}

	return NOTIFY_DONE;
}

struct notifier_block mipv6_mn_inet6addr_notifier = {
	mn_inet6addr_event,
	NULL,
	0 /* check if using zero is ok */
};

static void mipv6_get_saddr_hook(struct inet6_ifaddr *ifp,
				 struct in6_addr *homeaddr)
{
	int found = 0, reiter = 0;
	struct list_head *lh;
	struct mn_info *minfo = NULL;
	struct in6_addr coa;

	read_lock_bh(&mn_info_lock);
restart:
	list_for_each(lh, &mn_info_list) {
		minfo = list_entry(lh, struct mn_info, list);
		if ((ipv6_addr_scope(homeaddr) != ipv6_addr_scope(&minfo->home_addr)) 
		    || ipv6_chk_addr(&minfo->home_addr, NULL, 0) == 0)
			continue; 

		spin_lock(&minfo->lock);
		if (minfo->is_at_home == MN_AT_HOME || minfo->has_home_reg) {
			if ((mipv6_use_topol_corr_hoaddr && 
			     minfo->is_at_home == MN_AT_HOME) || 
			    (mipv6_use_preconfigured_hoaddr && 
			     minfo->man_conf) ||
			    (!(mipv6_use_preconfigured_hoaddr || 
			       mipv6_use_topol_corr_hoaddr) || reiter)) {
				spin_unlock(&minfo->lock);
				ipv6_addr_copy(homeaddr, &minfo->home_addr);
				found = 1;
				break;
			}
		}
		spin_unlock(&minfo->lock);
	}
	if (!found && !reiter) {
		reiter = 1;
		goto restart;
	}

	if (!found && minfo && 
	    !mipv6_get_care_of_address(&minfo->home_addr, &coa)) {
		ipv6_addr_copy(homeaddr, &coa); 
	}
	read_unlock_bh(&mn_info_lock);

	DEBUG(DBG_DATADUMP, "Source address selection:  %x:%x:%x:%x:%x:%x:%x:%x", 
	      NIPV6ADDR(homeaddr));
	return;
}

int addrconf_add_ifaddr_kernel(struct in6_ifreq *ireq);
int addrconf_del_ifaddr_kernel(struct in6_ifreq *ireq);

static void mv_home_addr(void *arg)
{
	mm_segment_t oldfs;
	int err = 0, new_if = 0;
	struct list_head *lh, *next;
	struct ifr_holder *ifrh;
	LIST_HEAD(list);
	
	DEBUG(DBG_INFO, "mipv6 move home address task");

	spin_lock_bh(&ifrh_lock);
	list_splice_init(&ifrh_list, &list);
	spin_unlock_bh(&ifrh_lock);
	
	oldfs = get_fs(); set_fs(KERNEL_DS);
	list_for_each_safe(lh, next, &list) {
		ifrh = list_entry(lh, struct ifr_holder, list);
		if (ifrh->old_ifi) {
			new_if = ifrh->ifr.ifr6_ifindex;
			ifrh->ifr.ifr6_ifindex = ifrh->old_ifi;
			err = addrconf_del_ifaddr_kernel(&ifrh->ifr); 
			ifrh->ifr.ifr6_ifindex = new_if;
			if (err < 0)
				DEBUG(DBG_WARNING, "removal of home address %x:%x:%x:%x:%x:%x:%x:%x from" 
				      " old interface %d failed with status %d", 
				      NIPV6ADDR(&ifrh->ifr.ifr6_addr), ifrh->old_ifi, err);		
		}
		if(!err) {
			err = addrconf_add_ifaddr_kernel(&ifrh->ifr);
		}
		if (ifrh->ho) {
			DEBUG(DBG_INFO, "Calling mobile_node moved after moving home address to new if");
			mipv6_mobile_node_moved(ifrh->ho);
		}
		list_del(&ifrh->list);
		kfree(ifrh);
	}
	set_fs(oldfs);

	if (err < 0)
		DEBUG(DBG_WARNING, "adding of home address to a new interface %d failed %d", new_if, err);
	else {
		DEBUG(DBG_WARNING, "adding of home address to a new interface OK");
	}
}

DECLARE_WORK(mv_homeaddr, mv_home_addr, NULL);

struct dhaad_halist {
	struct list_head list;
	struct in6_addr addr;
	int retry;
};

/* clear all has from candidate list.  do this when a new dhaad reply
 * is received. */
int mipv6_mn_flush_ha_candidate(struct list_head *ha)
{
	struct list_head *p, *tmp;
	struct dhaad_halist *e;

	list_for_each_safe(p, tmp, ha) {
		e = list_entry(p, struct dhaad_halist, list);
		list_del(p);
		kfree(e);
		e = NULL;
	}
	return 0;
}

/* add new ha to candidates. only done when dhaad reply is received. */
int mipv6_mn_add_ha_candidate(struct list_head *ha, struct in6_addr *addr)
{
	struct dhaad_halist *e;

	e = kmalloc(sizeof(*e), GFP_ATOMIC);
	memset(e, 0, sizeof(*e));
	ipv6_addr_copy(&e->addr, addr);

	list_add_tail(&e->list, ha);
	return 0;
}

#define MAX_RETRIES_PER_HA 3

/* get next ha candidate.  this is done when dhaad reply has been
 * received and we want to register with the best available ha. */
int mipv6_mn_get_ha_candidate(struct list_head *ha, struct in6_addr *addr)
{
	struct list_head *p;

	list_for_each(p, ha) {
		struct dhaad_halist *e;
		e = list_entry(p, typeof(*e), list);
		if (e->retry >= 0 && e->retry < MAX_RETRIES_PER_HA) {
			ipv6_addr_copy(addr, &e->addr);
			return 0;
		}
	}
	return -1;
}

/* change candidate status.  if registration with ha fails, we
 * increase retry for ha candidate.  if retry is >= 3 we set it to -1
 * (failed), do get_ha_candidate() again */
int mipv6_mn_try_ha_candidate(struct list_head *ha, struct in6_addr *addr)
{
	struct list_head *p;

	list_for_each(p, ha) {
		struct dhaad_halist *e;
		e = list_entry(p, typeof(*e), list);
		if (ipv6_addr_cmp(addr, &e->addr) == 0) {
			if (e->retry >= MAX_RETRIES_PER_HA) e->retry = -1;
			else if (e->retry >= 0) e->retry++;
			return 0;
		}
	}
	return -1;
}

/**
 * mipv6_mn_get_bulifetime - Get lifetime for a binding update
 * @home_addr: home address for BU 
 * @coa: care-of address for BU
 * @flags: flags used for BU 
 *
 * Returns maximum lifetime for BUs determined by the lifetime of
 * care-of address and the lifetime of home address.
 **/
__u32 mipv6_mn_get_bulifetime(struct in6_addr *home_addr, struct in6_addr *coa,
			      __u8 flags)
{
	struct inet6_ifaddr *ifp_hoa, *ifp_coa;
	__u32 lifetime = (flags & MIPV6_BU_F_HOME ? 
			  HA_BU_DEF_LIFETIME : CN_BU_DEF_LIFETIME); 

	ifp_hoa = ipv6_get_ifaddr(home_addr, NULL, 0);
	if(!ifp_hoa) {
		DEBUG(DBG_INFO, "home address missing");
		return 0;
	}
	if (!(ifp_hoa->flags & IFA_F_PERMANENT)){
		if (ifp_hoa->valid_lft)
			lifetime = min_t(__u32, lifetime, ifp_hoa->valid_lft);
		else
			DEBUG(DBG_ERROR, "Zero lifetime for home address");
	}
	in6_ifa_put(ifp_hoa);

	ifp_coa = ipv6_get_ifaddr(coa, NULL, 0);
	if (!ifp_coa) { 
		DEBUG(DBG_INFO, "care-of address missing");
		return 0;
	}
	if (!(ifp_coa->flags & IFA_F_PERMANENT)) {
		if(ifp_coa->valid_lft)
			lifetime = min_t(__u32, lifetime, ifp_coa->valid_lft);
		else
			DEBUG(DBG_ERROR, 
			      "Zero lifetime for care-of address");
	}
	in6_ifa_put(ifp_coa);


	DEBUG(DBG_INFO, "Lifetime for binding is %ld", lifetime);

lifetime = (flags & MIPV6_BU_F_HOME ?  HA_BU_DEF_LIFETIME : CN_BU_DEF_LIFETIME); 
	return lifetime;
}

static int 
mipv6_mn_tnl_rcv_send_bu_hook(struct ip6_tnl *t, struct sk_buff *skb)
{
	struct ipv6hdr *inner = (struct ipv6hdr *)skb->h.raw;
	struct ipv6hdr *outer = skb->nh.ipv6h; 
	struct mn_info *minfo = NULL;
	struct inet6_skb_parm *opt;
	__u32 lifetime;
	__u8 user_flags = 0;

	DEBUG_FUNC();

	if (!is_mip6_tnl(t))
		return IP6_TNL_ACCEPT;

	if (!mip6node_cnf.accept_ret_rout) {
		DEBUG(DBG_INFO, "Return routability administratively disabled" 
		      " not doing route optimization");
		return IP6_TNL_ACCEPT;
	}
	read_lock(&mn_info_lock);
	minfo = mipv6_mninfo_get_by_home(&inner->daddr);

	if (!minfo) {
		DEBUG(DBG_WARNING, "MN info missing");
		read_unlock(&mn_info_lock);
		return IP6_TNL_ACCEPT;
	}
	DEBUG(DBG_DATADUMP, "MIPV6 MN: Received a tunneled IPv6 packet"
	      " to %x:%x:%x:%x:%x:%x:%x:%x,"
	      " from %x:%x:%x:%x:%x:%x:%x:%x with\n tunnel header"
	      "daddr: %x:%x:%x:%x:%x:%x:%x:%x,"
	      "saddr: %x:%x:%x:%x:%x:%x:%x:%x", 
	       NIPV6ADDR(&inner->daddr), NIPV6ADDR(&inner->saddr),
	       NIPV6ADDR(&outer->daddr), NIPV6ADDR(&outer->saddr));
	
	spin_lock(&minfo->lock);

	/* We don't send bus in response to all tunneled packets */

        if (!ipv6_addr_cmp(&minfo->ha, &inner->saddr)) {
		spin_unlock(&minfo->lock);
		read_unlock(&mn_info_lock);
                DEBUG(DBG_ERROR, "HA BUG: Received a tunneled packet "
		      "originally sent by home agent, not sending BU");
		return IP6_TNL_ACCEPT;
        }
	if (ipv6_addr_cmp(&minfo->ha, &outer->saddr)) {
		spin_unlock(&minfo->lock);
		read_unlock(&mn_info_lock);
		DEBUG(DBG_WARNING, "MIPV6 MN: Received a tunneled IPv6 packet"
		      " that was not tunneled by HA %x:%x:%x:%x:%x:%x:%x:%x,"
		      " but by %x:%x:%x:%x:%x:%x:%x:%x", 
		      NIPV6ADDR(&minfo->ha), NIPV6ADDR(&outer->saddr));
		return IP6_TNL_ACCEPT;
        }
	spin_unlock(&minfo->lock);
	read_unlock(&mn_info_lock);

	DEBUG(DBG_DATADUMP, "Sending BU to correspondent node");

	user_flags |= mip6node_cnf.bu_cn_ack ? MIPV6_BU_F_ACK : 0;

	if (inner->nexthdr != IPPROTO_DSTOPTS && inner->nexthdr != IPPROTO_MOBILITY) {
		struct in6_addr coa;
		lifetime = mipv6_mn_get_bulifetime(&inner->daddr,
						   &outer->daddr, 0); 
		if (lifetime && 
		    !mipv6_get_care_of_address(&inner->daddr, &coa)) {
			write_lock(&bul_lock);
			mipv6_send_bu(&inner->daddr, &inner->saddr, &coa,
				      INITIAL_BINDACK_TIMEOUT,
				      MAX_BINDACK_TIMEOUT, 1, 
				      user_flags,
				      lifetime, NULL);
			write_unlock(&bul_lock);
		}
	}
	/* (Mis)use ipsec tunnel flag  */
	DEBUG(DBG_DATADUMP, "setting rcv_tunnel flag in skb");
	opt = (struct inet6_skb_parm *)skb->cb;
	opt->mipv6_flags |= MIPV6_RCV_TUNNEL;
	return IP6_TNL_ACCEPT;
}

static struct ip6_tnl_hook_ops mipv6_mn_tnl_rcv_send_bu_ops = {
	{NULL, NULL}, 
	IP6_TNL_PRE_DECAP,
	IP6_TNL_PRI_FIRST,
	mipv6_mn_tnl_rcv_send_bu_hook
};

static int
mipv6_mn_tnl_xmit_stats_hook(struct ip6_tnl *t, struct sk_buff *skb)
{
	DEBUG_FUNC();
	if (is_mip6_tnl(t))
		MIPV6_INC_STATS(n_encapsulations);
	return IP6_TNL_ACCEPT;
}

static struct ip6_tnl_hook_ops mipv6_mn_tnl_xmit_stats_ops = {
	{NULL, NULL},
	IP6_TNL_PRE_ENCAP,
	IP6_TNL_PRI_LAST,
	mipv6_mn_tnl_xmit_stats_hook
};

static int
mipv6_mn_tnl_rcv_stats_hook(struct ip6_tnl *t, struct sk_buff *skb)
{
	DEBUG_FUNC();	
	if (is_mip6_tnl(t))
		MIPV6_INC_STATS(n_decapsulations);
	return IP6_TNL_ACCEPT;
}

static struct ip6_tnl_hook_ops mipv6_mn_tnl_rcv_stats_ops = {
	{NULL, NULL},
	IP6_TNL_PRE_DECAP,
	IP6_TNL_PRI_LAST,
	mipv6_mn_tnl_rcv_stats_hook
};

static void mn_check_tunneled_packet(struct sk_buff *skb)
{
	struct inet6_skb_parm *opt = (struct inet6_skb_parm *)skb->cb;
	DEBUG_FUNC();
	/* If tunnel flag was set */
	if (opt->mipv6_flags & MIPV6_RCV_TUNNEL) {
		struct in6_addr coa; 
		__u32 lifetime;
		__u8 user_flags = 0;

		if (!mip6node_cnf.accept_ret_rout) {
			DEBUG(DBG_INFO, "Return routability administratively disabled");
			return;
		}
		if (skb->nh.ipv6h->nexthdr == IPPROTO_MOBILITY) {
			return;
		}
		user_flags |= mip6node_cnf.bu_cn_ack ? MIPV6_BU_F_ACK : 0;
		mipv6_get_care_of_address(&skb->nh.ipv6h->daddr, &coa);
		lifetime = mipv6_mn_get_bulifetime(&skb->nh.ipv6h->daddr,
 							 &coa, 0); 

		DEBUG(DBG_WARNING, "packet to address %x:%x:%x:%x:%x:%x:%x:%x"
		      "was tunneled. Sending BU to CN" 
		      "%x:%x:%x:%x:%x:%x:%x:%x", 
		      NIPV6ADDR(&skb->nh.ipv6h->daddr),
		      NIPV6ADDR(&skb->nh.ipv6h->saddr)); 
		/* This should work also with home address option */
		
		write_lock(&bul_lock);
		mipv6_send_bu(&skb->nh.ipv6h->daddr, &skb->nh.ipv6h->saddr, 
			      &coa, INITIAL_BINDACK_TIMEOUT,
			      MAX_BINDACK_TIMEOUT, 1, user_flags,
			      lifetime, NULL);
		write_unlock(&bul_lock);
	}
}

int sched_mv_home_addr_task(struct in6_addr *haddr, int plen_new, 
			    int newif, int oldif, struct handoff *ho)
{
	int alloc_size;
	struct ifr_holder *ifrh;

	alloc_size = sizeof(*ifrh) + (ho ? sizeof(*ho): 0);
	if ((ifrh = kmalloc(alloc_size, GFP_ATOMIC)) == NULL) {
		DEBUG(DBG_ERROR, "Out of memory");
		return -1;
	} 
	if (ho) {
		ifrh->ho = (struct handoff *)((struct ifr_holder *)(ifrh + 1));
		memcpy(ifrh->ho, ho, sizeof(*ho));
	} else 
		ifrh->ho = NULL;

	/* must queue task to avoid deadlock with rtnl */
	ifrh->ifr.ifr6_ifindex = newif;
	ifrh->ifr.ifr6_prefixlen = plen_new;
	ipv6_addr_copy(&ifrh->ifr.ifr6_addr, haddr);
	ifrh->old_ifi = oldif;
	
	spin_lock_bh(&ifrh_lock);
	list_add_tail(&ifrh->list, &ifrh_list);
	spin_unlock_bh(&ifrh_lock);

	schedule_work(&mv_homeaddr);

	return 0;
}

static void send_ret_home_ns(struct in6_addr *ha_addr, struct in6_addr *home_addr, int ifindex)
{
	struct in6_addr nil;
	struct in6_addr mcaddr;
	struct net_device *dev = dev_get_by_index(ifindex);
	if (!dev)
		return;
	memset(&nil, 0, sizeof(nil));
	addrconf_addr_solict_mult(home_addr, &mcaddr);
	ndisc_send_ns(dev, NULL,
		      home_addr,
		      &mcaddr, &nil); 
	dev_put(dev);
}
static int mn_ha_handoff(struct handoff *ho)
{
	struct list_head *lh;
	struct mn_info *minfo;
	struct in6_addr *coa= ho->coa;
	int wait_mv_home = 0; 

	DEBUG_FUNC();

	read_lock_bh(&mn_info_lock);
	list_for_each(lh, &mn_info_list) {
		__u8 has_home_reg;
		int ifindex;
		struct in6_addr ha;
		__u8 athome;
		__u32 lifetime;
		struct mipv6_bul_entry *entry = NULL;
		
		minfo = list_entry(lh, struct mn_info, list);
		spin_lock(&minfo->lock);
		has_home_reg = minfo->has_home_reg;
		ifindex = minfo->ifindex;
		ipv6_addr_copy(&ha, &minfo->ha);
		
		if (mipv6_prefix_compare(&ho->rtr_addr, &minfo->home_addr,
					 ho->plen)) {
			if (minfo->has_home_reg)
				athome = minfo->is_at_home = MN_RETURNING_HOME;
			else
				athome = minfo->is_at_home = MN_AT_HOME;
			coa = &minfo->home_addr;

			spin_unlock(&minfo->lock);
#if 0			
			/* Cancel prefix solicitation, rtr is our HA */
			mipv6_pfx_cancel_send(&ho->rtr_addr, ifindex);
#endif			
			minfo->ifindex = ho->ifindex;
			if (ifindex != ho->ifindex){
				send_ret_home_ns(&minfo->ha, &minfo->home_addr, ho->ifindex);
				wait_mv_home++;
				DEBUG(DBG_INFO, 
				      "Moving home address back to "
				      "the home interface");
				sched_mv_home_addr_task(&minfo->home_addr, 
							128, ho->ifindex,
							ifindex, ho);
			} 

			if (!has_home_reg || wait_mv_home)
				continue;
			
			lifetime = 0;

		} else {
			athome = minfo->is_at_home = MN_NOT_AT_HOME;
			if (minfo->ifindex_user != minfo->ifindex) {
				DEBUG(DBG_INFO, "Scheduling home address move to virtual interface");
				sched_mv_home_addr_task(&minfo->home_addr, 
							128,
							minfo->ifindex_user, 
							minfo->ifindex, ho); /* Is minfo->ifindex correct */
				
				wait_mv_home++;
			}
			minfo->ifindex = minfo->ifindex_user;
			spin_unlock(&minfo->lock);
			if (wait_mv_home)
				continue;
			if (!has_home_reg &&
			    init_home_registration(&minfo->home_addr, 
						   ho->coa)) {
				continue;
			}
			lifetime = mipv6_mn_get_bulifetime(&minfo->home_addr, 
							   ho->coa,
							   MIPV6_BU_F_HOME);
			
		}
		write_lock(&bul_lock);
		if (!(entry = mipv6_bul_get(&ha, &minfo->home_addr)) ||
		    !(entry->flags & MIPV6_BU_F_HOME)) {
			DEBUG(DBG_ERROR, 
			      "Unable to find home registration for "
			      "home address: %x:%x:%x:%x:%x:%x:%x:%x!\n",
			      NIPV6ADDR(&minfo->home_addr));
			write_unlock(&bul_lock);
			continue;
		}
		DEBUG(DBG_INFO, "Sending home de ? %d registration for "
		      "home address: %x:%x:%x:%x:%x:%x:%x:%x\n" 
		      "to home agent %x:%x:%x:%x:%x:%x:%x:%x, "
		      "with lifetime %ld", 
		      (athome != MN_NOT_AT_HOME),  
		      NIPV6ADDR(&entry->home_addr), 
		      NIPV6ADDR(&entry->cn_addr), lifetime);
		mipv6_send_bu(&entry->home_addr, &entry->cn_addr, 
			      coa, INITIAL_BINDACK_TIMEOUT, 
			      MAX_BINDACK_TIMEOUT, 1, entry->flags, 
			      lifetime, NULL);
		write_unlock(&bul_lock);

	}
	read_unlock_bh(&mn_info_lock);
	return wait_mv_home;
}
/**
 * mn_cn_handoff - called for every bul entry to send BU to CN
 * @rawentry: bul entry
 * @args: handoff event
 * @sortkey:
 *
 * Since MN can have many home addresses and home networks, every BUL
 * entry needs to be checked
 **/
int mn_cn_handoff(void *rawentry, void *args, unsigned long *sortkey)
{
	struct mipv6_bul_entry *entry = (struct mipv6_bul_entry *)rawentry;
	struct in6_addr *coa = (struct in6_addr *)args;

	DEBUG_FUNC();

	/* Home registrations already handled by mn_ha_handoff */
	if (entry->flags & MIPV6_BU_F_HOME)
		return ITERATOR_CONT;

	/* BUL is locked by mipv6_mobile_node_moved which calls us 
	   through mipv6_bul_iterate */

	if (mipv6_prefix_compare(coa, 
				 &entry->home_addr,
				 64)) {
		mipv6_send_bu(&entry->home_addr, &entry->cn_addr, 
			      &entry->home_addr, INITIAL_BINDACK_TIMEOUT, 
			      MAX_BINDACK_TIMEOUT, 1, entry->flags, 0, 
			      NULL);
	} else {
		u32 lifetime = mipv6_mn_get_bulifetime(&entry->home_addr, 
						       coa,
						       entry->flags);
		mipv6_send_bu(&entry->home_addr, &entry->cn_addr,
                              coa, INITIAL_BINDACK_TIMEOUT,
			      MAX_BINDACK_TIMEOUT, 1, entry->flags,
			      lifetime, NULL);
	}
	return ITERATOR_CONT;
}

/**
 * init_home_registration - start Home Registration process
 * @home_addr: home address
 * @coa: care-of address
 *
 * Checks whether we have a Home Agent address for this home address.
 * If not starts Dynamic Home Agent Address Discovery.  Otherwise
 * tries to register with home agent if not already registered.
 * Returns 1, if home registration process is started and 0 otherwise
 **/
int init_home_registration(struct in6_addr *home_addr, struct in6_addr *coa)
{
	struct mn_info *hinfo;
	struct in6_addr ha;
	__u8 man_conf;
	int ifindex;
	__u32 lifetime;
	__u8 user_flags = 0, flags;

	DEBUG_FUNC();

	read_lock_bh(&mn_info_lock);
        if ((hinfo = mipv6_mninfo_get_by_home(home_addr)) == NULL) {
                DEBUG(DBG_ERROR, "No mn_info found for address: "
		      "%x:%x:%x:%x:%x:%x:%x:%x",
		      NIPV6ADDR(home_addr));
		read_unlock_bh(&mn_info_lock);
                return -ENOENT;
        }
	spin_lock(&hinfo->lock);
	if (mipv6_prefix_compare(&hinfo->home_addr, coa, hinfo->home_plen)) { 
		spin_unlock(&hinfo->lock);
		read_unlock_bh(&mn_info_lock);
		DEBUG(DBG_INFO, "Adding home address, MN at home");
		return 1;
	}
        if (ipv6_addr_any(&hinfo->ha)) {
                int dhaad_id = mipv6_get_dhaad_id();
                hinfo->dhaad_id = dhaad_id;
		spin_unlock(&hinfo->lock);
                mipv6_icmpv6_send_dhaad_req(home_addr, hinfo->home_plen, dhaad_id);
		read_unlock_bh(&mn_info_lock);
                DEBUG(DBG_INFO,
		      "Home Agent address not set, initiating DHAAD");
                return 1;
        }
        ipv6_addr_copy(&ha, &hinfo->ha);
        man_conf = hinfo->man_conf;
        ifindex = hinfo->ifindex;
	spin_unlock(&hinfo->lock);
	read_unlock_bh(&mn_info_lock);
#if 0	
	if (man_conf)
		mipv6_pfx_add_ha(&ha, coa, ifindex);
#endif	
	if (mipv6_bul_exists(&ha, home_addr)) {
		DEBUG(DBG_INFO, "BU already sent to HA");
		return 0;
	}
	/* user flags received through sysctl */
	user_flags |= mip6node_cnf.bu_lladdr ? MIPV6_BU_F_LLADDR : 0;
	user_flags |= mip6node_cnf.bu_keymgm ? MIPV6_BU_F_KEYMGM : 0;

	flags = MIPV6_BU_F_HOME | MIPV6_BU_F_ACK | user_flags;

	lifetime = mipv6_mn_get_bulifetime(home_addr, coa, flags);

	DEBUG(DBG_INFO, "Sending initial home registration for "
	      "home address: %x:%x:%x:%x:%x:%x:%x:%x\n" 
	      "to home agent %x:%x:%x:%x:%x:%x:%x:%x, "
	      "with lifetime %ld, prefixlength %d",   
	      NIPV6ADDR(home_addr), NIPV6ADDR(&ha), lifetime, 0);

	write_lock_bh(&bul_lock);
	mipv6_send_bu(home_addr, &ha, coa, INITIAL_BINDACK_DAD_TIMEOUT,
		      MAX_BINDACK_TIMEOUT, 1, flags, lifetime, NULL);
	write_unlock_bh(&bul_lock);

	return 1;
}

/**
 * mipv6_mobile_node_moved - Send BUs to all HAs and CNs
 * @ho: handoff structure contains the new and previous routers
 *
 * Event for handoff.  Sends BUs everyone on Binding Update List.
 **/
int mipv6_mobile_node_moved(struct handoff *ho)
{
#if 0
	int bu_to_prev_router = 1;
#endif
	int dummy;

	DEBUG_FUNC();

	ma_ctl_upd_iface(ho->ifindex, 
			 MA_IFACE_CURRENT | MA_IFACE_HAS_ROUTER, &dummy);

	/* First send BU to HA, then to all other nodes that are on BU list */
	if (mn_ha_handoff(ho) != 0)
		return 0; /* Wait for move home address task */
#if 0	 
	/* Add current care-of address to mn_info list, if current router acts 
	   as a HA.*/ 

	if (ho->home_address && bu_to_prev_router) 
		mipv6_mninfo_add(ho->coa, ho->plen, 
				 MN_AT_HOME, 0, &ho->rtr_addr, 
				 ho->plen, ROUTER_BU_DEF_LIFETIME,
				 0);
				  
#endif
	return 0;		
}

/**
 * mipv6_mn_send_home_na - send NA when returning home
 * @haddr: home address to advertise
 *
 * After returning home, MN must advertise all its valid addresses in
 * home link to all nodes.
 **/
void mipv6_mn_send_home_na(struct in6_addr *haddr)
{
	struct net_device *dev = NULL;
	struct in6_addr mc_allnodes;
	struct mn_info *hinfo = NULL;
	struct in6_addr addr;
 
	read_lock(&mn_info_lock);
	hinfo = mipv6_mninfo_get_by_home(haddr);
	if (!hinfo) {
		read_unlock(&mn_info_lock);
		return;
	}
	spin_lock(&hinfo->lock);
	hinfo->is_at_home = MN_AT_HOME;
	dev = dev_get_by_index(hinfo->ifindex);
	spin_unlock(&hinfo->lock);
	read_unlock(&mn_info_lock);
	if (dev == NULL) {
		DEBUG(DBG_ERROR, "Send home_na: device not found.");
		return;
	}
	
	ipv6_addr_all_nodes(&mc_allnodes);
	if (ipv6_get_lladdr(dev, &addr) == 0)
		ndisc_send_na(dev, NULL, &mc_allnodes, &addr, 
			      0, 0, 1, 1);
	ndisc_send_na(dev, NULL, &mc_allnodes, haddr, 0, 0, 1, 1);
	dev_put(dev);
}

static int mn_use_hao(struct in6_addr *daddr, struct in6_addr *saddr)
{
	struct mipv6_bul_entry *entry;
	struct mn_info *minfo = NULL;
	int add_ha = 0;

	read_lock_bh(&mn_info_lock);
	minfo = mipv6_mninfo_get_by_home(saddr);
	if (minfo && minfo->is_at_home != MN_AT_HOME) {
		read_lock_bh(&bul_lock);
		if ((entry = mipv6_bul_get(daddr, saddr)) == NULL) {
			read_unlock_bh(&bul_lock);
			read_unlock_bh(&mn_info_lock);
			return add_ha;
		}
		add_ha = (!entry->rr || entry->rr->rr_state == RR_DONE || 
			  entry->flags & MIPV6_BU_F_HOME);
		read_unlock_bh(&bul_lock);
	}
	read_unlock_bh(&mn_info_lock);
	return add_ha;
}

static int 
mn_dev_event(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct list_head *lh;
	struct mn_info *minfo;
	int newif = 0;

	DEBUG_FUNC();

	/* here are probably the events we need to worry about */
	switch (event) {
	case NETDEV_UP:
		DEBUG(DBG_DATADUMP, "New netdevice %s registered.", dev->name);
		if (dev->type != ARPHRD_LOOPBACK && !dev_is_mip6_tnl(dev)) 
			ma_ctl_add_iface(dev->ifindex);
			
		break;
	case NETDEV_GOING_DOWN:
		DEBUG(DBG_DATADUMP, "Netdevice %s disappeared.", dev->name);
		/* 
		 * Go through mn_info list and move all home addresses on the
		 * netdev going down to a new device. This will make it 
                 * practically impossible for the home address to return home,
		 * but allow MN to retain its connections using the address.
		 */

		ma_ctl_upd_iface(dev->ifindex, MA_IFACE_NOT_PRESENT, &newif);

		// KK, need this ?
		// I think needed, otherwise router stays though not present.
		mipv6_mdet_del_if(dev->ifindex);

		read_lock_bh(&mn_info_lock);
		list_for_each(lh, &mn_info_list) {
			minfo = list_entry(lh, struct mn_info, list);
			spin_lock(&minfo->lock);
			if (minfo->ifindex == dev->ifindex) {
				if (sched_mv_home_addr_task(&minfo->home_addr, 128, 
							    minfo->ifindex_user, 
							    0, NULL) < 0) {
					minfo->ifindex = 0;
					spin_unlock(&minfo->lock);
					read_unlock_bh(&mn_info_lock);
					return NOTIFY_DONE;
				} else { 
					minfo->ifindex = minfo->ifindex_user;
					if (minfo->is_at_home) {
						minfo->is_at_home = 0;

					}
					newif = minfo->ifindex_user;
				}
			}
			spin_unlock(&minfo->lock);				
		}
		
		read_unlock_bh(&mn_info_lock);
	}

	return NOTIFY_DONE;
}

struct notifier_block mipv6_mn_dev_notifier = {
	mn_dev_event,
	NULL,
	0 /* check if using zero is ok */
};

static void deprecate_addr(struct mn_info *minfo)
{
	/*
	 * Lookup address from IPv6 address list and set deprecated flag
	 */
	
}

/*
 * Required because we can only modify addresses after the packet is
 * constructed.  We otherwise mess with higher level protocol
 * pseudoheaders. With strict protocol layering life would be SO much
 * easier!  
 */
unsigned int modify_xmit_addrs(unsigned int hooknum,
				      struct sk_buff **pskb,
				      const struct net_device *in,
				      const struct net_device *out,
				      int (*okfn) (struct sk_buff *))
{
	struct sk_buff *skb = *pskb;

	DEBUG_FUNC();
	
	if (skb) {
		struct ipv6hdr *hdr = skb->nh.ipv6h;
		struct inet6_skb_parm *opt = (struct inet6_skb_parm *)skb->cb;
		struct in6_addr coa;

		if ((opt->mipv6_flags & MIPV6_SND_HAO) &&
		    !mipv6_get_care_of_address(&hdr->saddr, &coa)) {
			DEBUG(DBG_DATADUMP, 
			      "Replace source address with CoA and reroute");
			ipv6_addr_copy(&hdr->saddr, &coa);
			skb->nfcache |= NFC_ALTERED;
		}
	}
	return NF_ACCEPT;
}

/* We set a netfilter hook so that we can modify outgoing packet's
 * source addresses 
 */
struct nf_hook_ops addr_modify_hook_ops = {
	{NULL, NULL},		/* List head, no predecessor, no successor */
	modify_xmit_addrs,
	THIS_MODULE,
	PF_INET6,
	NF_IP6_LOCAL_OUT,
	NF_IP6_PRI_FIRST       	/* Should be of EXTREMELY high priority since we
				 * do not want to mess with IPSec (possibly
				 * implemented as packet filter)
				 */
};

#define MN_INFO_LEN 77

static int mn_proc_info(char *buffer, char **start, off_t offset,
			int length)
{
	struct list_head *p;
	struct mn_info *minfo;
	int len = 0, skip = 0;

	DEBUG_FUNC();

	read_lock_bh(&mn_info_lock);
	list_for_each(p, &mn_info_list) {
		if (len < offset / MN_INFO_LEN) {
			skip++;
			continue;
		}
		if (len >= length)
			break;
		minfo = list_entry(p, struct mn_info, list);
		spin_lock(&minfo->lock);
		len += sprintf(buffer + len, "%02d %08x%08x%08x%08x %02x "
			       "%08x%08x%08x%08x %d %d\n",
			       minfo->ifindex,
			       ntohl(minfo->home_addr.s6_addr32[0]),
			       ntohl(minfo->home_addr.s6_addr32[1]),
			       ntohl(minfo->home_addr.s6_addr32[2]),
			       ntohl(minfo->home_addr.s6_addr32[3]),
			       minfo->home_plen,
			       ntohl(minfo->ha.s6_addr32[0]),
			       ntohl(minfo->ha.s6_addr32[1]),
			       ntohl(minfo->ha.s6_addr32[2]),
			       ntohl(minfo->ha.s6_addr32[3]),
			       minfo->is_at_home, minfo->has_home_reg);
		spin_unlock(&minfo->lock);
	}
	read_unlock_bh(&mn_info_lock);

	*start = buffer;
	if (offset)
		*start += offset % MN_INFO_LEN;

	len -= offset % MN_INFO_LEN;

	if (len > length)
		len = length;
	if (len < 0)
		len = 0;
	
	return len;
}

int mipv6_mn_ha_probe(struct inet6_ifaddr *ifp, u8 *lladdr)
{
	struct mn_info *minfo;
	struct neighbour *neigh;

	if (!(minfo = mipv6_mninfo_get_by_home(&ifp->addr)) ||
	    ipv6_addr_any(&minfo->ha))
		return 0;

	if ((neigh = ndisc_get_neigh(ifp->idev->dev, &minfo->ha))) {
		if (lladdr)
			neigh_update(neigh, lladdr, NUD_REACHABLE, 0, 1);
		neigh_release(neigh);
		spin_lock_bh(&ifp->lock);
		ifp->flags &= ~IFA_F_TENTATIVE;
		spin_unlock_bh(&ifp->lock);
	} else DEBUG(DBG_INFO, "Neigh entry for HA does not exist");
	return 1;
}

int __init mipv6_mn_init(void)
{
	struct net_device *dev;

	DEBUG_FUNC();

	if (mipv6_add_tnl_to_ha())
		return -ENODEV;

	mipv6_bul_init(MIPV6_BUL_SIZE);
	mip6_fn.mn_use_hao = mn_use_hao;
	mip6_fn.mn_check_tunneled_packet = mn_check_tunneled_packet;

	ma_ctl_init();
	for (dev = dev_base; dev; dev = dev->next) {
		if (dev->flags & IFF_UP && 
		    dev->type != ARPHRD_LOOPBACK && !dev_is_mip6_tnl(dev)) {
			ma_ctl_add_iface(dev->ifindex);
		}
	} 
	DEBUG(DBG_INFO, "Multiaccess support initialized");

	register_netdevice_notifier(&mipv6_mn_dev_notifier);
	register_inet6addr_notifier(&mipv6_mn_inet6addr_notifier);

	ip6ip6_tnl_register_hook(&mipv6_mn_tnl_rcv_send_bu_ops);
	ip6ip6_tnl_register_hook(&mipv6_mn_tnl_xmit_stats_ops);
	ip6ip6_tnl_register_hook(&mipv6_mn_tnl_rcv_stats_ops);

	MIPV6_SETCALL(mipv6_set_home, mipv6_mn_set_home);

	mipv6_initialize_mdetect();

	/* COA to home transformation hook */
	MIPV6_SETCALL(mipv6_get_home_address, mipv6_get_saddr_hook);
	MIPV6_SETCALL(mipv6_mn_ha_probe, mipv6_mn_ha_probe);
	MIPV6_SETCALL(mipv6_is_home_addr, mipv6_mn_is_home_addr);
	proc_net_create("mip6_mninfo", 0, mn_proc_info);
	/* Set packet modification hook (source addresses) */
	nf_register_hook(&addr_modify_hook_ops);

	return 0;
}

void __exit mipv6_mn_exit(void)
{
	struct list_head *lh, *tmp;
	struct mn_info *minfo;
	DEBUG_FUNC();
	
	mip6_fn.mn_use_hao = NULL;
	mip6_fn.mn_check_tunneled_packet = NULL;
	
	MIPV6_RESETCALL(mipv6_set_home);
	MIPV6_RESETCALL(mipv6_get_home_address);
	MIPV6_RESETCALL(mipv6_mn_ha_probe);
	MIPV6_RESETCALL(mipv6_is_home_addr);
	nf_unregister_hook(&addr_modify_hook_ops);
	proc_net_remove("mip6_mninfo");
	mipv6_shutdown_mdetect();
	ip6ip6_tnl_unregister_hook(&mipv6_mn_tnl_rcv_stats_ops);
	ip6ip6_tnl_unregister_hook(&mipv6_mn_tnl_xmit_stats_ops);
	ip6ip6_tnl_unregister_hook(&mipv6_mn_tnl_rcv_send_bu_ops);
	ma_ctl_clean();

	unregister_inet6addr_notifier(&mipv6_mn_inet6addr_notifier);
	unregister_netdevice_notifier(&mipv6_mn_dev_notifier);
	write_lock_bh(&mn_info_lock);

	list_for_each_safe(lh, tmp, &mn_info_list) {
		minfo = list_entry(lh, struct mn_info, list);
		if (minfo->is_at_home == MN_NOT_AT_HOME) 
			deprecate_addr(minfo);
		list_del(&minfo->list);
		kfree(minfo);
	}
	write_unlock_bh(&mn_info_lock);
	mipv6_bul_exit();
	flush_scheduled_work();
	mipv6_del_tnl_to_ha();
}
