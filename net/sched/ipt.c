/*
 * net/sched/ipt.c	iptables target interface
 *
 *TODO: Add other tables. For now we only support the ipv4 table targets
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Copyright:	Jamal Hadi Salim (2002-4)
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
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
#include <linux/tc_act/tc_ipt.h>
#include <net/tc_act/tc_ipt.h>

#include <linux/netfilter_ipv4/ip_tables.h>

/* use generic hash table */
#define MY_TAB_SIZE     16
#define MY_TAB_MASK     15

static u32 idx_gen;
static struct tcf_ipt *tcf_ipt_ht[MY_TAB_SIZE];
/* ipt hash table lock */
static rwlock_t ipt_lock = RW_LOCK_UNLOCKED;

/* ovewrride the defaults */
#define tcf_st  tcf_ipt
#define tcf_t_lock   ipt_lock
#define tcf_ht tcf_ipt_ht

#include <net/pkt_act.h>

static inline int
init_targ(struct tcf_ipt *p)
{
	struct ipt_target *target;
	int ret = 0;
	struct ipt_entry_target *t = p->t;
	target = __ipt_find_target_lock(t->u.user.name, &ret);

	if (!target) {
		printk("init_targ: Failed to find %s\n", t->u.user.name);
		return -1;
	}

	DPRINTK("init_targ: found %s\n", target->name);
	/* we really need proper ref counting
	 seems to be only needed for modules?? Talk to laforge */
/*      if (target->me)
              __MOD_INC_USE_COUNT(target->me);
*/
	t->u.kernel.target = target;

	__ipt_mutex_up();

	if (t->u.kernel.target->checkentry
	    && !t->u.kernel.target->checkentry(p->tname, NULL, t->data,
					       t->u.target_size
					       - sizeof (*t), p->hook)) {
/*              if (t->u.kernel.target->me)
	      __MOD_DEC_USE_COUNT(t->u.kernel.target->me);
*/
		DPRINTK("ip_tables: check failed for `%s'.\n",
			t->u.kernel.target->name);
		ret = -EINVAL;
	}

	return ret;
}

static int
tcf_ipt_init(struct rtattr *rta, struct rtattr *est, struct tc_action *a, int ovr, int bind)
{
	struct ipt_entry_target *t;
	unsigned h;
	struct rtattr *tb[TCA_IPT_MAX];
	struct tcf_ipt *p;
	int ret = 0;
	u32 index = 0;
	u32 hook = 0;

	if (NULL == a || NULL == rta ||
	    (rtattr_parse(tb, TCA_IPT_MAX, RTA_DATA(rta), RTA_PAYLOAD(rta)) <
	     0)) {
		return -1;
	}


	if (tb[TCA_IPT_INDEX - 1]) {
		index = *(u32 *) RTA_DATA(tb[TCA_IPT_INDEX - 1]);
		DPRINTK("ipt index %d\n", index);
	}

	if (index && (p = tcf_hash_lookup(index)) != NULL) {
		a->priv = (void *) p;
		spin_lock(&p->lock);
		if (bind) {
			p->bindcnt += 1;
			p->refcnt += 1;
		}
		if (ovr) {
			goto override;
		}
		spin_unlock(&p->lock);
		return ret;
	}

	if (NULL == tb[TCA_IPT_TARG - 1] || NULL == tb[TCA_IPT_HOOK - 1]) {
		return -1;
	}

	p = kmalloc(sizeof (*p), GFP_KERNEL);
	if (p == NULL)
		return -1;

	memset(p, 0, sizeof (*p));
	p->refcnt = 1;
	ret = 1;
	spin_lock_init(&p->lock);
	p->stats_lock = &p->lock;
	if (bind)
		p->bindcnt = 1;

override:
	hook = *(u32 *) RTA_DATA(tb[TCA_IPT_HOOK - 1]);

	t = (struct ipt_entry_target *) RTA_DATA(tb[TCA_IPT_TARG - 1]);

	p->t = kmalloc(t->u.target_size, GFP_KERNEL);
	if (p->t == NULL) {
		if (ovr) {
			printk("ipt policy messed up \n");
			spin_unlock(&p->lock);
			return -1;
		}
		kfree(p);
		return -1;
	}

	memcpy(p->t, RTA_DATA(tb[TCA_IPT_TARG - 1]), t->u.target_size);
	DPRINTK(" target NAME %s size %d data[0] %x data[1] %x\n",
		t->u.user.name, t->u.target_size, t->data[0], t->data[1]);

	p->tname = kmalloc(IFNAMSIZ, GFP_KERNEL);

	if (p->tname == NULL) {
		if (ovr) {
			printk("ipt policy messed up 2 \n");
			spin_unlock(&p->lock);
			return -1;
		}
		kfree(p->t);
		kfree(p);
		return -1;
	} else {
		int csize = IFNAMSIZ - 1;

		memset(p->tname, 0, IFNAMSIZ);
		if (tb[TCA_IPT_TABLE - 1]) {
			if (strlen((char *) RTA_DATA(tb[TCA_IPT_TABLE - 1])) <
			    csize)
				csize = strlen(RTA_DATA(tb[TCA_IPT_TABLE - 1]));
			strncpy(p->tname, RTA_DATA(tb[TCA_IPT_TABLE - 1]),
				csize);
			DPRINTK("table name %s\n", p->tname);
		} else {
			strncpy(p->tname, "mangle", 1 + strlen("mangle"));
		}
	}

	if (0 > init_targ(p)) {
		if (ovr) {
			printk("ipt policy messed up 2 \n");
			spin_unlock(&p->lock);
			return -1;
		}
		kfree(p->tname);
		kfree(p->t);
		kfree(p);
		return -1;
	}

	if (ovr) {
		spin_unlock(&p->lock);
		return -1;
	}

	p->index = index ? : tcf_hash_new_index();

	p->tm.lastuse = jiffies;
	/*
	p->tm.expires = jiffies;
	*/
	p->tm.install = jiffies;
#ifdef CONFIG_NET_ESTIMATOR
	if (est)
		gen_new_estimator(&p->bstats, &p->rate_est, p->stats_lock, est);
#endif
	h = tcf_hash(p->index);
	write_lock_bh(&ipt_lock);
	p->next = tcf_ipt_ht[h];
	tcf_ipt_ht[h] = p;
	write_unlock_bh(&ipt_lock);
	a->priv = (void *) p;
	return ret;

}

static int
tcf_ipt_cleanup(struct tc_action *a, int bind)
{
	struct tcf_ipt *p;
	p = PRIV(a,ipt);
	if (NULL != p)
		return tcf_hash_release(p, bind);
	return 0;
}

static int
tcf_ipt(struct sk_buff **pskb, struct tc_action *a)
{
	int ret = 0, result = 0;
	struct tcf_ipt *p;
	struct sk_buff *skb = *pskb;

	p = PRIV(a,ipt);

	if (NULL == p || NULL == skb) {
		return -1;
	}

	spin_lock(&p->lock);

	p->tm.lastuse = jiffies;
	p->bstats.bytes += skb->len;
	p->bstats.packets++;

	if (skb_cloned(skb) ) {
		if (pskb_expand_head(skb, 0, 0, GFP_ATOMIC)) {
			return -1;
		}
	}
	/* yes, we have to worry about both in and out dev
	 worry later - danger - this API seems to have changed
	 from earlier kernels */

	ret = p->t->u.kernel.target->target(&skb, skb->dev, NULL,
					    p->hook, p->t->data, (void *)NULL);
	switch (ret) {
	case NF_ACCEPT:
		result = TC_ACT_OK;
		break;
	case NF_DROP:
		result = TC_ACT_SHOT;
		p->qstats.drops++;
		break;
	case IPT_CONTINUE:
		result = TC_ACT_PIPE;
		break;
	default:
		if (net_ratelimit())
			printk("Bogus netfilter code %d assume ACCEPT\n", ret);
		result = TC_POLICE_OK;
		break;
	}
	spin_unlock(&p->lock);
	return result;

}

static int
tcf_ipt_dump(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	struct ipt_entry_target *t;
	struct tcf_t tm;
	struct tc_cnt c;
	unsigned char *b = skb->tail;

	struct tcf_ipt *p;

	p = PRIV(a,ipt);
	if (NULL == p) {
		printk("BUG: tcf_ipt_dump called with NULL params\n");
		goto rtattr_failure;
	}
	/* for simple targets kernel size == user size
	** user name = target name
	** for foolproof you need to not assume this
	*/

	t = kmalloc(p->t->u.user.target_size, GFP_ATOMIC);

	if (NULL == t)
		goto rtattr_failure;

	c.bindcnt = p->bindcnt - bind;
	c.refcnt = p->refcnt - ref;
	memcpy(t, p->t, p->t->u.user.target_size);
	strcpy(t->u.user.name, p->t->u.kernel.target->name);

	DPRINTK("\ttcf_ipt_dump tablename %s length %d\n", p->tname,
		strlen(p->tname));
	DPRINTK
	    ("\tdump target name %s size %d size user %d data[0] %x data[1] %x\n",
	     p->t->u.kernel.target->name, p->t->u.target_size, p->t->u.user.target_size,
	     p->t->data[0], p->t->data[1]);
	RTA_PUT(skb, TCA_IPT_TARG, p->t->u.user.target_size, t);
	RTA_PUT(skb, TCA_IPT_INDEX, 4, &p->index);
	RTA_PUT(skb, TCA_IPT_HOOK, 4, &p->hook);
	RTA_PUT(skb, TCA_IPT_CNT, sizeof(struct tc_cnt), &c);
	RTA_PUT(skb, TCA_IPT_TABLE, IFNAMSIZ, p->tname);
	tm.install = jiffies_to_clock_t(jiffies - p->tm.install);
	tm.lastuse = jiffies_to_clock_t(jiffies - p->tm.lastuse);
	tm.expires = jiffies_to_clock_t(p->tm.expires);
	RTA_PUT(skb, TCA_IPT_TM, sizeof (tm), &tm);
	return skb->len;

      rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static struct tc_action_ops act_ipt_ops = {
	.next		=	NULL,
	.kind		=	"ipt",
	.type		=	TCA_ACT_IPT,
	.capab		=	TCA_CAP_NONE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_ipt,
	.dump		=	tcf_ipt_dump,
	.cleanup	=	tcf_ipt_cleanup,
	.lookup		=	tcf_hash_search,
	.init		=	tcf_ipt_init,
	.walk		=	tcf_generic_walker
};

MODULE_AUTHOR("Jamal Hadi Salim(2002-4)");
MODULE_DESCRIPTION("Iptables target actions");
MODULE_LICENSE("GPL");

static int __init
ipt_init_module(void)
{
	return tcf_register_action(&act_ipt_ops);
}

static void __exit
ipt_cleanup_module(void)
{
	tcf_unregister_action(&act_ipt_ops);
}

module_init(ipt_init_module);
module_exit(ipt_cleanup_module);
