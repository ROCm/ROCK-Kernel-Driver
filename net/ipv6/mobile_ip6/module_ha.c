/*
 *	Mobile IPv6 Home Agent Module
 *
 *	Authors:
 *	Sami Kivisaari          <skivisaa@cc.hut.fi>
 *	Antti Tuominen          <ajtuomin@tml.hut.fi>
 *
 *	$Id$
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif /* CONFIG_SYSCTL */

#include <net/mipglue.h>

#include "mobhdr.h"
#include "tunnel_ha.h"
#include "ha.h"
#include "halist.h"
#include "mipv6_icmp.h"
#include "prefix.h"
#include "bcache.h"
#include "debug.h"
//#include <net/ipv6_tunnel.h>

extern int mipv6_debug;

int mipv6_use_auth = 0;

#if defined(MODULE) 
//&& LINUX_VERSION_CODE > 0
MODULE_AUTHOR("MIPL Team");
MODULE_DESCRIPTION("Mobile IPv6 Home Agent");
MODULE_LICENSE("GPL");
#endif

#include "config.h"

#define MIPV6_HALIST_SIZE 128

/*
 * Called from ndisc.c's router_discovery.
 */
static int mipv6_ha_ra_rcv(struct sk_buff *skb)
{
	int optlen, ha_info_pref = 0, ha_info_lifetime;
	int ifi = ((struct inet6_skb_parm *)skb->cb)->iif;
	struct ra_msg *ra = (struct ra_msg *) skb->h.raw;
	struct in6_addr *saddr = &skb->nh.ipv6h->saddr;
	struct in6_addr ll_addr;
	struct hal {
		struct in6_addr prefix;
		int plen;
		struct hal *next;
	};
	struct hal *ha_queue = NULL;
	
	__u8 * opt = (__u8 *)(ra + 1);

	DEBUG_FUNC();

	ha_info_lifetime = ntohs(ra->icmph.icmp6_rt_lifetime);
	ipv6_addr_copy(&ll_addr, saddr);

	optlen = (skb->tail - (unsigned char *)ra) - sizeof(struct ra_msg);

	while (optlen > 0) {
		int len = (opt[1] << 3);
		if (len == 0) 
			return MIPV6_IGN_RTR;
		
		if (opt[0] == ND_OPT_PREFIX_INFO) {
			struct prefix_info *pinfo;

			if (len < sizeof(struct prefix_info)) 
				return MIPV6_IGN_RTR;

			pinfo = (struct prefix_info *) opt;
			if ((ra->icmph.icmp6_home_agent) && pinfo->router_address) {
				/* If RA has H bit set and Prefix Info
				 * Option R bit set, queue this
				 * address to be added to Home Agents
				 * List.  
				 */
				struct hal *tmp;
				if (ipv6_addr_type(&pinfo->prefix) & IPV6_ADDR_LINKLOCAL)
					goto nextopt;
				tmp = kmalloc(sizeof(struct hal), GFP_ATOMIC);
				if (tmp == NULL) {
					DEBUG(DBG_ERROR, "Out of memory");
					return MIPV6_IGN_RTR;
				}
				ipv6_addr_copy(&tmp->prefix, &pinfo->prefix);
				tmp->plen = pinfo->prefix_len;
				tmp->next = ha_queue;
				ha_queue = tmp;
			}
			goto nextopt;
		}
		if (opt[0] == ND_OPT_HOME_AGENT_INFO) {
			__u16 tmp;
			tmp = ntohs(*(__u16 *)(opt + 4));
			ha_info_pref = (tmp & 0x8000) ? -(int)((u16)(~0))^(tmp + 1) : tmp;
			ha_info_lifetime = ntohs(*(__u16 *)(opt + 6));
			DEBUG(DBG_DATADUMP,
			      "received home agent info with preference : %d and lifetime : %d",
			      ha_info_pref, ha_info_lifetime);
		}
	nextopt:
		optlen -= len;
		opt += len;
	}
	while (ha_queue) {
		struct hal *tmp = ha_queue->next;
		if (ha_info_lifetime) {
			mipv6_halist_add(ifi, &ha_queue->prefix, 
					 ha_queue->plen, &ll_addr, 
					 ha_info_pref, ha_info_lifetime);
		} else {
			if (mipv6_halist_delete(&ha_queue->prefix) < 0) {
				DEBUG(DBG_INFO, "Not able to delete %x:%x:%x:%x:%x:%x:%x:%x",
				      NIPV6ADDR(&ha_queue->prefix));
			}
		}
		kfree(ha_queue);
		ha_queue = tmp;
	}
	return MIPV6_ADD_RTR;
}

/**********************************************************************
 *
 * MIPv6 Module Init / Cleanup
 *
 **********************************************************************/

#ifdef CONFIG_SYSCTL
/* Sysctl table */
extern int 
mipv6_max_tnls_sysctl(ctl_table *, int, struct file *, void *, size_t *, loff_t *);

extern int 
mipv6_min_tnls_sysctl(ctl_table *, int, struct file *, void *, size_t *, loff_t *);

int max_adv = ~(u16)0;
int min_zero = 0;
ctl_table mipv6_mobility_table[] = {
	{NET_IPV6_MOBILITY_BINDING_REFRESH, "binding_refresh_advice",
	 &mip6node_cnf.binding_refresh_advice, sizeof(int), 0644, NULL,
	 &proc_dointvec_minmax, &sysctl_intvec, 0, &min_zero, &max_adv},

	{NET_IPV6_MOBILITY_MAX_TNLS, "max_tnls", &mipv6_max_tnls, sizeof(int),
	 0644, NULL, &mipv6_max_tnls_sysctl},
	{NET_IPV6_MOBILITY_MIN_TNLS, "min_tnls", &mipv6_min_tnls, sizeof(int),
	 0644, NULL, &mipv6_min_tnls_sysctl},
	{0}
};
ctl_table mipv6_table[] = {
	{NET_IPV6_MOBILITY, "mobility", NULL, 0, 0555, mipv6_mobility_table},
	{0}
};

static struct ctl_table_header *mipv6_sysctl_header;
static struct ctl_table mipv6_net_table[];
static struct ctl_table mipv6_root_table[];

ctl_table mipv6_net_table[] = {
	{NET_IPV6, "ipv6", NULL, 0, 0555, mipv6_table},
	{0}
};

ctl_table mipv6_root_table[] = {
	{CTL_NET, "net", NULL, 0, 0555, mipv6_net_table},
	{0}
};
#endif /* CONFIG_SYSCTL */

extern void mipv6_check_dad(struct in6_addr *haddr);
extern void mipv6_dad_init(void);
extern void mipv6_dad_exit(void);
extern int mipv6_forward(struct sk_buff *);
extern int mipv6_initialize_pfx_icmpv6(void);

/*  Initialize the module  */
static int __init mip6_ha_init(void)
{
	int err = 0;

	printk(KERN_INFO "MIPL Mobile IPv6 for Linux Home Agent %s (%s)\n",
	       MIPLVERSION, MIPV6VERSION);
	mip6node_cnf.capabilities = CAP_CN | CAP_HA;

	mip6_fn.icmpv6_dhaad_rep_rcv = mipv6_icmpv6_no_rcv;
	mip6_fn.icmpv6_dhaad_req_rcv = mipv6_icmpv6_rcv_dhaad_req;
	mip6_fn.icmpv6_pfxadv_rcv = mipv6_icmpv6_no_rcv;
	mip6_fn.icmpv6_pfxsol_rcv = mipv6_icmpv6_rcv_pfx_sol;
	mip6_fn.icmpv6_paramprob_rcv = mipv6_icmpv6_no_rcv;

#ifdef CONFIG_IPV6_MOBILITY_DEBUG
	printk(KERN_INFO "Debug-level: %d\n", mipv6_debug);
#endif

#ifdef CONFIG_SYSCTL
	mipv6_sysctl_header = register_sysctl_table(mipv6_root_table, 0);
#endif
	mipv6_initialize_tunnel();

	if ((err = mipv6_ha_init()) < 0)
		goto ha_fail;

	MIPV6_SETCALL(mipv6_ra_rcv, mipv6_ha_ra_rcv);
	MIPV6_SETCALL(mipv6_forward, mipv6_forward);
	mipv6_dad_init();
	MIPV6_SETCALL(mipv6_check_dad, mipv6_check_dad);

	if ((err = mipv6_halist_init(MIPV6_HALIST_SIZE)) < 0)
		goto halist_fail;

	mipv6_initialize_pfx_icmpv6();

	return 0;

halist_fail:
	mipv6_dad_exit();
	mipv6_ha_exit();
ha_fail:
	mipv6_shutdown_tunnel();

	mip6_fn.icmpv6_dhaad_rep_rcv = NULL;
	mip6_fn.icmpv6_dhaad_req_rcv = NULL;
	mip6_fn.icmpv6_pfxadv_rcv = NULL;
	mip6_fn.icmpv6_pfxsol_rcv = NULL;
	mip6_fn.icmpv6_paramprob_rcv = NULL;

	MIPV6_RESETCALL(mipv6_ra_rcv);
	MIPV6_RESETCALL(mipv6_forward);
	MIPV6_RESETCALL(mipv6_check_dad);

#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(mipv6_sysctl_header);
#endif
	return err;
}
module_init(mip6_ha_init);

#ifdef MODULE
/*  Cleanup module  */
static void __exit mip6_ha_exit(void)
{
	printk(KERN_INFO "mip6_ha.o exiting.\n");
	mip6node_cnf.capabilities &= ~(int)CAP_HA;

	mipv6_bcache_cleanup(HOME_REGISTRATION);

	MIPV6_RESETCALL(mipv6_ra_rcv);
	MIPV6_RESETCALL(mipv6_forward);
	MIPV6_RESETCALL(mipv6_check_dad);

	mipv6_halist_exit();
	mipv6_shutdown_pfx_icmpv6();

	mip6_fn.icmpv6_dhaad_rep_rcv = NULL;
	mip6_fn.icmpv6_dhaad_req_rcv = NULL;
	mip6_fn.icmpv6_pfxadv_rcv = NULL;
	mip6_fn.icmpv6_pfxsol_rcv = NULL;
	mip6_fn.icmpv6_paramprob_rcv = NULL;

	mipv6_dad_exit();
	mipv6_ha_exit();
	mipv6_shutdown_tunnel();
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(mipv6_sysctl_header);
#endif
}
module_exit(mip6_ha_exit);
#endif /* MODULE */
