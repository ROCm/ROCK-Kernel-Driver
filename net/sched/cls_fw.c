/*
 * net/sched/cls_fw.c	Classifier mapping ipchains' fwmark to traffic class.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 * Karlis Peisenieks <karlis@mt.lv> : 990415 : fw_walk off by one
 * Karlis Peisenieks <karlis@mt.lv> : 990415 : fw_delete killed all the filter (and kernel).
 * Alex <alex@pilotsoft.com> : 2004xxyy: Added Action extension
 *
 * JHS: We should remove the CONFIG_NET_CLS_IND from here
 * eventually when the meta match extension is made available
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
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
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <linux/netfilter.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

struct fw_head
{
	struct fw_filter *ht[256];
};

struct fw_filter
{
	struct fw_filter	*next;
	u32			id;
	struct tcf_result	res;
#ifdef CONFIG_NET_CLS_ACT
       struct tc_action        *action;
#ifdef CONFIG_NET_CLS_IND
       char			indev[IFNAMSIZ];
#endif
#else
#ifdef CONFIG_NET_CLS_POLICE
	struct tcf_police	*police;
#endif
#endif
};

static __inline__ int fw_hash(u32 handle)
{
	return handle&0xFF;
}

static int fw_classify(struct sk_buff *skb, struct tcf_proto *tp,
			  struct tcf_result *res)
{
	struct fw_head *head = (struct fw_head*)tp->root;
	struct fw_filter *f;
#ifdef CONFIG_NETFILTER
	u32 id = skb->nfmark;
#else
	u32 id = 0;
#endif

	if (head == NULL)
		goto old_method;

	for (f=head->ht[fw_hash(id)]; f; f=f->next) {
		if (f->id == id) {
			*res = f->res;
#ifdef CONFIG_NET_CLS_ACT
#ifdef CONFIG_NET_CLS_IND
			if (0 != f->indev[0]) {
				if  (NULL == skb->input_dev) {
					continue;
				} else {
					if (0 != strcmp(f->indev, skb->input_dev->name)) {
						continue;
					}
				}
			}
#endif
                               if (f->action) {
                                       int pol_res = tcf_action_exec(skb, f->action);
                                       if (pol_res >= 0)
                                               return pol_res;
                               } else
#else
#ifdef CONFIG_NET_CLS_POLICE
			if (f->police)
				return tcf_police(skb, f->police);
#endif
#endif
			return 0;
		}
	}
	return -1;

old_method:
	if (id && (TC_H_MAJ(id) == 0 ||
		     !(TC_H_MAJ(id^tp->q->handle)))) {
		res->classid = id;
		res->class = 0;
		return 0;
	}
	return -1;
}

static unsigned long fw_get(struct tcf_proto *tp, u32 handle)
{
	struct fw_head *head = (struct fw_head*)tp->root;
	struct fw_filter *f;

	if (head == NULL)
		return 0;

	for (f=head->ht[fw_hash(handle)]; f; f=f->next) {
		if (f->id == handle)
			return (unsigned long)f;
	}
	return 0;
}

static void fw_put(struct tcf_proto *tp, unsigned long f)
{
}

static int fw_init(struct tcf_proto *tp)
{
	return 0;
}

static void fw_destroy(struct tcf_proto *tp)
{
	struct fw_head *head = (struct fw_head*)xchg(&tp->root, NULL);
	struct fw_filter *f;
	int h;

	if (head == NULL)
		return;

	for (h=0; h<256; h++) {
		while ((f=head->ht[h]) != NULL) {
			unsigned long cl;
			head->ht[h] = f->next;

			if ((cl = __cls_set_class(&f->res.class, 0)) != 0)
				tp->q->ops->cl_ops->unbind_tcf(tp->q, cl);
#ifdef CONFIG_NET_CLS_ACT
       if (f->action) {
               tcf_action_destroy(f->action,TCA_ACT_UNBIND);
       }
#else
#ifdef CONFIG_NET_CLS_POLICE
			tcf_police_release(f->police,TCA_ACT_UNBIND);
#endif
#endif

			kfree(f);
		}
	}
	kfree(head);
}

static int fw_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct fw_head *head = (struct fw_head*)tp->root;
	struct fw_filter *f = (struct fw_filter*)arg;
	struct fw_filter **fp;

	if (head == NULL || f == NULL)
		goto out;

	for (fp=&head->ht[fw_hash(f->id)]; *fp; fp = &(*fp)->next) {
		if (*fp == f) {
			unsigned long cl;

			tcf_tree_lock(tp);
			*fp = f->next;
			tcf_tree_unlock(tp);

			if ((cl = cls_set_class(tp, &f->res.class, 0)) != 0)
				tp->q->ops->cl_ops->unbind_tcf(tp->q, cl);
#ifdef CONFIG_NET_CLS_ACT
       if (f->action) {
               tcf_action_destroy(f->action,TCA_ACT_UNBIND);
       }
#else
#ifdef CONFIG_NET_CLS_POLICE
			tcf_police_release(f->police,TCA_ACT_UNBIND);
#endif
#endif
			kfree(f);
			return 0;
		}
	}
out:
	return -EINVAL;
}

static int fw_change(struct tcf_proto *tp, unsigned long base,
		     u32 handle,
		     struct rtattr **tca,
		     unsigned long *arg)
{
	struct fw_head *head = (struct fw_head*)tp->root;
	struct fw_filter *f;
	struct rtattr *opt = tca[TCA_OPTIONS-1];
	struct rtattr *tb[TCA_FW_MAX];
	int err;
#ifdef CONFIG_NET_CLS_ACT
       struct tc_action *act = NULL;
       int ret;
#endif


	if (!opt)
		return handle ? -EINVAL : 0;

	if (rtattr_parse(tb, TCA_FW_MAX, RTA_DATA(opt), RTA_PAYLOAD(opt)) < 0)
		return -EINVAL;

	if ((f = (struct fw_filter*)*arg) != NULL) {
		/* Node exists: adjust only classid */

		if (f->id != handle && handle)
			return -EINVAL;
		if (tb[TCA_FW_CLASSID-1]) {
			unsigned long cl;

			f->res.classid = *(u32*)RTA_DATA(tb[TCA_FW_CLASSID-1]);
			cl = tp->q->ops->cl_ops->bind_tcf(tp->q, base, f->res.classid);
			cl = cls_set_class(tp, &f->res.class, cl);
			if (cl)
				tp->q->ops->cl_ops->unbind_tcf(tp->q, cl);
		}
#ifdef CONFIG_NET_CLS_ACT
		if (tb[TCA_FW_POLICE-1]) {
			act = kmalloc(sizeof(*act),GFP_KERNEL);
			if (NULL == act)
				return -ENOMEM;

			memset(act,0,sizeof(*act));
			ret = tcf_action_init_1(tb[TCA_FW_POLICE-1], tca[TCA_RATE-1] ,act,"police",TCA_ACT_NOREPLACE,TCA_ACT_BIND);
			if (0 > ret){
				tcf_action_destroy(act,TCA_ACT_UNBIND);
				return ret;
			}
			act->type = TCA_OLD_COMPAT;

			sch_tree_lock(tp->q);
			act = xchg(&f->action, act);
			sch_tree_unlock(tp->q);

			tcf_action_destroy(act,TCA_ACT_UNBIND);

		}

		if(tb[TCA_FW_ACT-1]) {
			act = kmalloc(sizeof(*act),GFP_KERNEL);
			if (NULL == act)
				return -ENOMEM;
			memset(act,0,sizeof(*act));
			ret = tcf_action_init(tb[TCA_FW_ACT-1], tca[TCA_RATE-1],act,NULL, TCA_ACT_NOREPLACE,TCA_ACT_BIND);
			if (0 > ret) {
				tcf_action_destroy(act,TCA_ACT_UNBIND);
				return ret;
			}

			sch_tree_lock(tp->q);
			act = xchg(&f->action, act);
			sch_tree_unlock(tp->q);

			tcf_action_destroy(act,TCA_ACT_UNBIND);
		}
#ifdef CONFIG_NET_CLS_IND
		if(tb[TCA_FW_INDEV-1]) {
			struct rtattr *idev = tb[TCA_FW_INDEV-1];
			if (RTA_PAYLOAD(idev) >= IFNAMSIZ) {
				printk("cls_fw: bad indev name %s\n",(char*)RTA_DATA(idev));
				err = -EINVAL;
				goto errout;
			}
			memset(f->indev,0,IFNAMSIZ);
			sprintf(f->indev, "%s", (char*)RTA_DATA(idev));
		}
#endif
#else /* only POLICE defined */
#ifdef CONFIG_NET_CLS_POLICE
		if (tb[TCA_FW_POLICE-1]) {
			struct tcf_police *police = tcf_police_locate(tb[TCA_FW_POLICE-1], tca[TCA_RATE-1]);

			tcf_tree_lock(tp);
			police = xchg(&f->police, police);
			tcf_tree_unlock(tp);

			tcf_police_release(police,TCA_ACT_UNBIND);
		}
#endif
#endif
		return 0;
	}

	if (!handle)
		return -EINVAL;

	if (head == NULL) {
		head = kmalloc(sizeof(struct fw_head), GFP_KERNEL);
		if (head == NULL)
			return -ENOBUFS;
		memset(head, 0, sizeof(*head));

		tcf_tree_lock(tp);
		tp->root = head;
		tcf_tree_unlock(tp);
	}

	f = kmalloc(sizeof(struct fw_filter), GFP_KERNEL);
	if (f == NULL)
		return -ENOBUFS;
	memset(f, 0, sizeof(*f));

	f->id = handle;

	if (tb[TCA_FW_CLASSID-1]) {
		err = -EINVAL;
		if (RTA_PAYLOAD(tb[TCA_FW_CLASSID-1]) != 4)
			goto errout;
		f->res.classid = *(u32*)RTA_DATA(tb[TCA_FW_CLASSID-1]);
		cls_set_class(tp, &f->res.class, tp->q->ops->cl_ops->bind_tcf(tp->q, base, f->res.classid));
	}

#ifdef CONFIG_NET_CLS_ACT
	if(tb[TCA_FW_ACT-1]) {
		act = kmalloc(sizeof(*act),GFP_KERNEL);
		if (NULL == act)
			return -ENOMEM;
		memset(act,0,sizeof(*act));
		ret = tcf_action_init(tb[TCA_FW_ACT-1], tca[TCA_RATE-1],act,NULL,TCA_ACT_NOREPLACE,TCA_ACT_BIND);
		if (0 > ret) {
			tcf_action_destroy(act,TCA_ACT_UNBIND);
			return ret;
		}
		f->action= act;
	}
#ifdef CONFIG_NET_CLS_IND
		if(tb[TCA_FW_INDEV-1]) {
			struct rtattr *idev = tb[TCA_FW_INDEV-1];
			if (RTA_PAYLOAD(idev) >= IFNAMSIZ) {
				printk("cls_fw: bad indev name %s\n",(char*)RTA_DATA(idev));
				err = -EINVAL;
				goto errout;
			}
			memset(f->indev,0,IFNAMSIZ);
			sprintf(f->indev, "%s", (char*)RTA_DATA(idev));
		}
#endif
#else
#ifdef CONFIG_NET_CLS_POLICE
	if (tb[TCA_FW_POLICE-1])
		f->police = tcf_police_locate(tb[TCA_FW_POLICE-1], tca[TCA_RATE-1]);
#endif
#endif

	f->next = head->ht[fw_hash(handle)];
	tcf_tree_lock(tp);
	head->ht[fw_hash(handle)] = f;
	tcf_tree_unlock(tp);

	*arg = (unsigned long)f;
	return 0;

errout:
	if (f)
		kfree(f);
	return err;
}

static void fw_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct fw_head *head = (struct fw_head*)tp->root;
	int h;

	if (head == NULL)
		arg->stop = 1;

	if (arg->stop)
		return;

	for (h = 0; h < 256; h++) {
		struct fw_filter *f;

		for (f = head->ht[h]; f; f = f->next) {
			if (arg->count < arg->skip) {
				arg->count++;
				continue;
			}
			if (arg->fn(tp, (unsigned long)f, arg) < 0) {
				arg->stop = 1;
				break;
			}
			arg->count++;
		}
	}
}

static int fw_dump(struct tcf_proto *tp, unsigned long fh,
		   struct sk_buff *skb, struct tcmsg *t)
{
	struct fw_filter *f = (struct fw_filter*)fh;
	unsigned char	 *b = skb->tail;
	struct rtattr *rta;

	if (f == NULL)
		return skb->len;

	t->tcm_handle = f->id;

       if (!f->res.classid
#ifdef CONFIG_NET_CLS_ACT
           && !f->action
#else
#ifdef CONFIG_NET_CLS_POLICE
           && !f->police
#endif
#endif
           )
		return skb->len;

	rta = (struct rtattr*)b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);

	if (f->res.classid)
		RTA_PUT(skb, TCA_FW_CLASSID, 4, &f->res.classid);
#ifdef CONFIG_NET_CLS_ACT
               /* again for backward compatible mode - we want
               *  to work with both old and new modes of entering
               *  tc data even if iproute2  was newer - jhs
               */
	if (f->action) {
		struct rtattr * p_rta = (struct rtattr*)skb->tail;

		if (f->action->type != TCA_OLD_COMPAT) {
			RTA_PUT(skb, TCA_FW_ACT, 0, NULL);
			if (tcf_action_dump(skb,f->action,0,0) < 0) {
				goto rtattr_failure;
			}
		} else {
			RTA_PUT(skb, TCA_FW_POLICE, 0, NULL);
			if (tcf_action_dump_old(skb,f->action,0,0) < 0) {
				goto rtattr_failure;
			}
		}

		p_rta->rta_len = skb->tail - (u8*)p_rta;
	}
#ifdef CONFIG_NET_CLS_IND
	if(strlen(f->indev)) {
		struct rtattr * p_rta = (struct rtattr*)skb->tail;
		RTA_PUT(skb, TCA_FW_INDEV, IFNAMSIZ, f->indev);
		p_rta->rta_len = skb->tail - (u8*)p_rta;
	}
#endif
#else
#ifdef CONFIG_NET_CLS_POLICE
	if (f->police) {
		struct rtattr * p_rta = (struct rtattr*)skb->tail;

		RTA_PUT(skb, TCA_FW_POLICE, 0, NULL);

		if (tcf_police_dump(skb, f->police) < 0)
			goto rtattr_failure;

		p_rta->rta_len = skb->tail - (u8*)p_rta;
	}
#endif
#endif

	rta->rta_len = skb->tail - b;
#ifdef CONFIG_NET_CLS_ACT
       if (f->action && f->action->type == TCA_OLD_COMPAT) {
               if (tcf_action_copy_stats(skb,f->action))
                       goto rtattr_failure;
       }
#else
#ifdef CONFIG_NET_CLS_POLICE
	if (f->police) {
		if (qdisc_copy_stats(skb, &f->police->stats,
				     f->police->stats_lock))
			goto rtattr_failure;
	}
#endif
#endif
	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static struct tcf_proto_ops cls_fw_ops = {
	.next		=	NULL,
	.kind		=	"fw",
	.classify	=	fw_classify,
	.init		=	fw_init,
	.destroy	=	fw_destroy,
	.get		=	fw_get,
	.put		=	fw_put,
	.change		=	fw_change,
	.delete		=	fw_delete,
	.walk		=	fw_walk,
	.dump		=	fw_dump,
	.owner		=	THIS_MODULE,
};

static int __init init_fw(void)
{
	return register_tcf_proto_ops(&cls_fw_ops);
}

static void __exit exit_fw(void) 
{
	unregister_tcf_proto_ops(&cls_fw_ops);
}

module_init(init_fw)
module_exit(exit_fw)
MODULE_LICENSE("GPL");
