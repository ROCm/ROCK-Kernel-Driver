/*
 *	Mobile IPv6 Common Module
 *
 *	Authors:
 *	Sami Kivisaari          <skivisaa@cc.hut.fi>
 *	Antti Tuominen          <ajtuomin@tml.hut.fi>
 *
 *	$Id: s.module_cn.c 1.15 03/08/26 12:07:40+03:00 henkku@tcs-pc-5.tcs.hut.fi $
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

#include "bcache.h"
#include "mipv6_icmp.h"
#include "stats.h"
#include "mobhdr.h"
#include "exthdrs.h"
#include <net/ipv6_tunnel.h>

int mipv6_debug = 1;

#if defined(MODULE)
//&& LINUX_VERSION_CODE > 0
MODULE_AUTHOR("MIPL Team");
MODULE_DESCRIPTION("Mobile IPv6");
MODULE_LICENSE("GPL");
MODULE_PARM(mipv6_debug, "i");
#endif

#include "config.h"

struct mip6_func mip6_fn;
struct mip6_conf mip6node_cnf = {
	capabilities:		CAP_CN,
	accept_ret_rout:	1,
	max_rtr_reachable_time:	0,
	eager_cell_switching:	0,
	max_num_tunnels:	0,
	min_num_tunnels:	0,
	binding_refresh_advice:	0,
	bu_lladdr:		0,
	bu_keymgm:		0,
	bu_cn_ack:		0
};

#define MIPV6_BCACHE_SIZE 128

/**********************************************************************
 *
 * MIPv6 CN Module Init / Cleanup
 *
 **********************************************************************/

#ifdef CONFIG_SYSCTL
/* Sysctl table */
ctl_table mipv6_mobility_table[] = {
	{NET_IPV6_MOBILITY_DEBUG, "debuglevel",
	 &mipv6_debug, sizeof(int), 0644, NULL,
	 &proc_dointvec},
	{NET_IPV6_MOBILITY_RETROUT, "accept_return_routability",
	 &mip6node_cnf.accept_ret_rout, sizeof(int), 0644, NULL,
	 &proc_dointvec},
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

extern void mipv6_rr_init(void);

/*  Initialize the module  */
static int __init mip6_init(void)
{
	int err = 0;

	printk(KERN_INFO "MIPL Mobile IPv6 for Linux Correspondent Node %s (%s)\n",
	       MIPLVERSION, MIPV6VERSION);

#ifdef CONFIG_IPV6_MOBILITY_DEBUG
	printk(KERN_INFO "Debug-level: %d\n", mipv6_debug);
#endif

	if ((err = mipv6_bcache_init(MIPV6_BCACHE_SIZE)) < 0)
		goto bcache_fail;

	if ((err = mipv6_icmpv6_init()) < 0)
		goto icmp_fail;

	if ((err = mipv6_stats_init()) < 0)
		goto stats_fail;
	mipv6_rr_init();

#ifdef CONFIG_SYSCTL
	mipv6_sysctl_header = register_sysctl_table(mipv6_root_table, 0);
#endif

	if ((err = mipv6_mh_common_init()) < 0)
		goto mh_fail;

	MIPV6_SETCALL(mipv6_modify_txoptions, mipv6_modify_txoptions);
		
	MIPV6_SETCALL(mipv6_handle_homeaddr, mipv6_handle_homeaddr);
	MIPV6_SETCALL(mipv6_icmp_handle_homeaddr, mipv6_icmp_handle_homeaddr);

	return 0;

mh_fail:
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(mipv6_sysctl_header);
#endif
	mipv6_stats_exit();
stats_fail:
	mipv6_icmpv6_exit();
icmp_fail:
	mipv6_bcache_exit();
bcache_fail:
	return err;
}
module_init(mip6_init);

#ifdef MODULE
/*  Cleanup module  */
static void __exit mip6_exit(void)
{
	printk(KERN_INFO "mip6_base.o exiting.\n");
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(mipv6_sysctl_header);
#endif

	/* Invalidate all custom kernel hooks.  No need to do this
           separately for all hooks. */
	mipv6_invalidate_calls();

	mipv6_mh_common_exit();
	mipv6_stats_exit();
	mipv6_icmpv6_exit();
	mipv6_bcache_exit();
}
module_exit(mip6_exit);
#endif /* MODULE */

EXPORT_SYMBOL(mipv6_debug);
EXPORT_SYMBOL(mip6node_cnf);
EXPORT_SYMBOL(mip6_fn);
