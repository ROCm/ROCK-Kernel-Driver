/*
 *	Mobile IPv6 Mobile Node Module
 *
 *	Authors:
 *	Sami Kivisaari          <skivisaa@cc.hut.fi>
 *	Antti Tuominen          <ajtuomin@tml.hut.fi>
 *
 *	$Id: s.module_mn.c 1.33 03/09/22 16:45:04+03:00 vnuorval@amber.hut.mediapoli.com $
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
#include <net/ipv6_tunnel.h>

extern int mipv6_debug;
int mipv6_use_auth = 0;

#if defined(MODULE)
// && LINUX_VERSION_CODE > 0
MODULE_AUTHOR("MIPL Team");
MODULE_DESCRIPTION("Mobile IPv6 Mobile Node");
MODULE_LICENSE("GPL");
//MODULE_PARM(mipv6_debug, "i");
#endif

#include "config.h"

#include "mobhdr.h"
#include "mn.h"
#include "mipv6_icmp.h"
#include "prefix.h"

/* TODO: These will go as soon as we get rid of the last two ioctls */
extern int mipv6_ioctl_mn_init(void);
extern void mipv6_ioctl_mn_exit(void);

/**********************************************************************
 *
 * MIPv6 Module Init / Cleanup
 *
 **********************************************************************/

#ifdef CONFIG_SYSCTL
/* Sysctl table */

extern int max_rtr_reach_time;
extern int eager_cell_switching;

static int max_reach = 1000;
static int min_reach = 1;
static int max_one = 1;
static int min_zero = 0;

extern int 
mipv6_mdetect_mech_sysctl(ctl_table *, int, struct file *, void *, size_t *);

extern int 
mipv6_router_reach_sysctl(ctl_table *, int, struct file *, void *, size_t *);


ctl_table mipv6_mobility_table[] = {
	{NET_IPV6_MOBILITY_BU_F_LLADDR, "bu_flag_lladdr",
	 &mip6node_cnf.bu_lladdr, sizeof(int), 0644, NULL,
	 &proc_dointvec_minmax, &sysctl_intvec, 0, &min_zero, &max_one},
	{NET_IPV6_MOBILITY_BU_F_KEYMGM, "bu_flag_keymgm",
	 &mip6node_cnf.bu_keymgm, sizeof(int), 0644, NULL,
	 &proc_dointvec_minmax, &sysctl_intvec, 0, &min_zero, &max_one},
	{NET_IPV6_MOBILITY_BU_F_CN_ACK, "bu_flag_cn_ack",
	 &mip6node_cnf.bu_cn_ack, sizeof(int), 0644, NULL,
	 &proc_dointvec_minmax, &sysctl_intvec, 0, &min_zero, &max_one},

	{NET_IPV6_MOBILITY_ROUTER_REACH, "max_router_reachable_time",
	 &max_rtr_reach_time, sizeof(int), 0644, NULL,
	 &proc_dointvec_minmax, &sysctl_intvec, 0, &min_reach, &max_reach},

	{NET_IPV6_MOBILITY_MDETECT_MECHANISM, "eager_cell_switching",
	 &eager_cell_switching, sizeof(int), 0644, NULL,
	 &proc_dointvec_minmax, &sysctl_intvec, 0, &min_zero, &max_one},

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

/*  Initialize the module  */
static int __init mip6_mn_init(void)
{
	int err = 0;
	extern int mipv6_initialize_pfx_icmpv6(void);

	printk(KERN_INFO "MIPL Mobile IPv6 for Linux Mobile Node %s (%s)\n",
	       MIPLVERSION, MIPV6VERSION);
	mip6node_cnf.capabilities = CAP_CN | CAP_MN;

#ifdef CONFIG_IPV6_MOBILITY_DEBUG
	printk(KERN_INFO "Debug-level: %d\n", mipv6_debug);
#endif

#ifdef CONFIG_SYSCTL
	mipv6_sysctl_header = register_sysctl_table(mipv6_root_table, 0);
#endif
	if ((err = mipv6_mn_init()) < 0)
		goto mn_fail;

	mipv6_mh_mn_init();

	mip6_fn.icmpv6_dhaad_rep_rcv = mipv6_icmpv6_rcv_dhaad_rep;
	mip6_fn.icmpv6_dhaad_req_rcv = mipv6_icmpv6_no_rcv;
	mip6_fn.icmpv6_pfxadv_rcv = mipv6_icmpv6_rcv_pfx_adv;
	mip6_fn.icmpv6_pfxsol_rcv = mipv6_icmpv6_no_rcv;
	mip6_fn.icmpv6_paramprob_rcv = mipv6_icmpv6_rcv_paramprob;

	mipv6_initialize_pfx_icmpv6();

	if ((err = mipv6_ioctl_mn_init()) < 0)
		goto ioctl_fail;

	return 0;

ioctl_fail:
	mipv6_shutdown_pfx_icmpv6();

	mip6_fn.icmpv6_dhaad_rep_rcv = NULL;
	mip6_fn.icmpv6_dhaad_req_rcv = NULL;
	mip6_fn.icmpv6_pfxadv_rcv = NULL;
	mip6_fn.icmpv6_pfxsol_rcv = NULL;
	mip6_fn.icmpv6_paramprob_rcv = NULL;

	mipv6_mh_mn_exit();
	mipv6_mn_exit();
mn_fail:
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(mipv6_sysctl_header);
#endif
	return err;
}
module_init(mip6_mn_init);

#ifdef MODULE
/*  Cleanup module  */
static void __exit mip6_mn_exit(void)
{
	printk(KERN_INFO "mip6_mn.o exiting.\n");
	mip6node_cnf.capabilities &= ~(int)CAP_MN;

	mipv6_ioctl_mn_exit();
	mipv6_shutdown_pfx_icmpv6();

	mip6_fn.icmpv6_dhaad_rep_rcv = NULL;
	mip6_fn.icmpv6_dhaad_req_rcv = NULL;
	mip6_fn.icmpv6_pfxadv_rcv = NULL;
	mip6_fn.icmpv6_pfxsol_rcv = NULL;
	mip6_fn.icmpv6_paramprob_rcv = NULL;

	mipv6_mn_exit();

/* common cleanup */
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(mipv6_sysctl_header);
#endif
}
module_exit(mip6_mn_exit);
#endif /* MODULE */
