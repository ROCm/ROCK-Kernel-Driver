/*
 * net/sched/gact.c	Generic actions
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * copyright 	Jamal Hadi Salim (2002-4)
 *
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_gact.h>
#include <net/tc_act/tc_gact.h>

/* use generic hash table */
#define MY_TAB_SIZE	16
#define MY_TAB_MASK	15
static u32 idx_gen;
static struct tcf_gact *tcf_gact_ht[MY_TAB_SIZE];
static rwlock_t gact_lock = RW_LOCK_UNLOCKED;

/* ovewrride the defaults */
#define tcf_st  tcf_gact
#define tc_st  tc_gact
#define tcf_t_lock   gact_lock
#define tcf_ht tcf_gact_ht

#define CONFIG_NET_ACT_INIT 1
#include <net/pkt_act.h>

#ifdef CONFIG_GACT_PROB
typedef int (*g_rand)(struct tcf_gact *p);
static int
gact_net_rand(struct tcf_gact *p) {
	if (net_random()%p->pval)
		return p->action;
	return p->paction;
}

static int
gact_determ(struct tcf_gact *p) {
	if (p->bstats.packets%p->pval)
		return p->action;
	return p->paction;
}


g_rand gact_rand[MAX_RAND]= { NULL,gact_net_rand, gact_determ};

#endif
static int
tcf_gact_init(struct rtattr *rta, struct rtattr *est, struct tc_action *a,int ovr,int bind)
{
	struct rtattr *tb[TCA_GACT_MAX];
	struct tc_gact *parm = NULL;
#ifdef CONFIG_GACT_PROB
	struct tc_gact_p *p_parm = NULL;
#endif
	struct tcf_gact *p = NULL;
	int ret = 0;
	int size = sizeof (*p);

	if (rtattr_parse(tb, TCA_GACT_MAX, RTA_DATA(rta), RTA_PAYLOAD(rta)) < 0)
		return -1;

	if (NULL == a || NULL == tb[TCA_GACT_PARMS - 1]) {
		printk("BUG: tcf_gact_init called with NULL params\n");
		return -1;
	}

	parm = RTA_DATA(tb[TCA_GACT_PARMS - 1]);
#ifdef CONFIG_GACT_PROB
	if (NULL != tb[TCA_GACT_PROB - 1]) {
		p_parm = RTA_DATA(tb[TCA_GACT_PROB - 1]);
	}
#endif

	p = tcf_hash_check(parm, a, ovr, bind);

	if (NULL == p) {
		p = tcf_hash_create(parm,est,a,size,ovr, bind);

		if (NULL == p) {
			return -1;
		} else {
			p->refcnt = 1;
			ret = 1;
			goto override;
		}
	}

	if (ovr) {
override:
		p->action = parm->action;
#ifdef CONFIG_GACT_PROB
		if (NULL != p_parm) {
			p->paction = p_parm->paction;
			p->pval = p_parm->pval;
			p->ptype = p_parm->ptype;
		} else {
			p->paction = p->pval = p->ptype = 0;
		}
#endif
	}

	return ret;
}

static int
tcf_gact_cleanup(struct tc_action *a, int bind)
{
	struct tcf_gact *p;
	p = PRIV(a,gact);
	if (NULL != p)
		return tcf_hash_release(p, bind);
	return 0;
}

static int
tcf_gact(struct sk_buff **pskb, struct tc_action *a)
{
	struct tcf_gact *p;
	struct sk_buff *skb = *pskb;
	int action = TC_ACT_SHOT;

	p = PRIV(a,gact);

	if (NULL == p) {
		if (net_ratelimit())
			printk("BUG: tcf_gact called with NULL params\n");
		return -1;
	}

	spin_lock(&p->lock);
#ifdef CONFIG_GACT_PROB
	if (p->ptype && NULL != gact_rand[p->ptype])
		action = gact_rand[p->ptype](p);
	else
		action = p->action;
#else
	action = p->action;
#endif
	p->bstats.bytes += skb->len;
	p->bstats.packets++;
	if (TC_ACT_SHOT == action)
		p->qstats.drops++;
	p->tm.lastuse = jiffies;
	spin_unlock(&p->lock);

	return action;
}

static int
tcf_gact_dump(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	unsigned char *b = skb->tail;
	struct tc_gact opt;
#ifdef CONFIG_GACT_PROB
	struct tc_gact_p p_opt;
#endif
	struct tcf_gact *p;
	struct tcf_t t;

	p = PRIV(a,gact);
	if (NULL == p) {
		printk("BUG: tcf_gact_dump called with NULL params\n");
		goto rtattr_failure;
	}

	opt.index = p->index;
	opt.refcnt = p->refcnt - ref;
	opt.bindcnt = p->bindcnt - bind;
	opt.action = p->action;
	RTA_PUT(skb, TCA_GACT_PARMS, sizeof (opt), &opt);
#ifdef CONFIG_GACT_PROB
	if (p->ptype) {
		p_opt.paction = p->paction;
		p_opt.pval = p->pval;
		p_opt.ptype = p->ptype;
		RTA_PUT(skb, TCA_GACT_PROB, sizeof (p_opt), &p_opt);
	} 
#endif
	t.install = jiffies_to_clock_t(jiffies - p->tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - p->tm.lastuse);
	t.expires = jiffies_to_clock_t(p->tm.expires);
	RTA_PUT(skb, TCA_GACT_TM, sizeof (t), &t);
	return skb->len;

      rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static struct tc_action_ops act_gact_ops = {
	.next		=	NULL,
	.kind		=	"gact",
	.type		=	TCA_ACT_GACT,
	.capab		=	TCA_CAP_NONE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_gact,
	.dump		=	tcf_gact_dump,
	.cleanup	=	tcf_gact_cleanup,
	.lookup		=	tcf_hash_search,
	.init		=	tcf_gact_init,
	.walk		=	tcf_generic_walker
};

MODULE_AUTHOR("Jamal Hadi Salim(2002-4)");
MODULE_DESCRIPTION("Generic Classifier actions");
MODULE_LICENSE("GPL");

static int __init
gact_init_module(void)
{
#ifdef CONFIG_GACT_PROB
	printk("GACT probability on\n");
#else
	printk("GACT probability NOT on\n");
#endif
	return tcf_register_action(&act_gact_ops);
}

static void __exit
gact_cleanup_module(void)
{
	tcf_unregister_action(&act_gact_ops);
}

module_init(gact_init_module);
module_exit(gact_cleanup_module);
