#ifndef __NET_PKT_CLS_H
#define __NET_PKT_CLS_H

#include <linux/pkt_cls.h>
#include <net/sch_generic.h>
#include <net/act_api.h>

/* Basic packet classifier frontend definitions. */

struct tcf_walker
{
	int	stop;
	int	skip;
	int	count;
	int	(*fn)(struct tcf_proto *, unsigned long node, struct tcf_walker *);
};

extern int register_tcf_proto_ops(struct tcf_proto_ops *ops);
extern int unregister_tcf_proto_ops(struct tcf_proto_ops *ops);
extern int ing_filter(struct sk_buff *skb);

static inline unsigned long
__cls_set_class(unsigned long *clp, unsigned long cl)
{
	unsigned long old_cl;
 
	old_cl = *clp;
	*clp = cl;
	return old_cl;
}

static inline unsigned long
cls_set_class(struct tcf_proto *tp, unsigned long *clp, 
	unsigned long cl)
{
	unsigned long old_cl;
	
	tcf_tree_lock(tp);
	old_cl = __cls_set_class(clp, cl);
	tcf_tree_unlock(tp);
 
	return old_cl;
}

static inline void
tcf_bind_filter(struct tcf_proto *tp, struct tcf_result *r, unsigned long base)
{
	unsigned long cl;

	cl = tp->q->ops->cl_ops->bind_tcf(tp->q, base, r->classid);
	cl = cls_set_class(tp, &r->class, cl);
	if (cl)
		tp->q->ops->cl_ops->unbind_tcf(tp->q, cl);
}

static inline void
tcf_unbind_filter(struct tcf_proto *tp, struct tcf_result *r)
{
	unsigned long cl;

	if ((cl = __cls_set_class(&r->class, 0)) != 0)
		tp->q->ops->cl_ops->unbind_tcf(tp->q, cl);
}

#ifdef CONFIG_NET_CLS_ACT
static inline int
tcf_change_act_police(struct tcf_proto *tp, struct tc_action **action,
	struct rtattr *act_police_tlv, struct rtattr *rate_tlv)
{
	int ret;
	struct tc_action *act;

	act = kmalloc(sizeof(*act), GFP_KERNEL);
	if (NULL == act)
		return -ENOMEM;
	memset(act, 0, sizeof(*act));
	
	ret = tcf_action_init_1(act_police_tlv, rate_tlv, act, "police",
		TCA_ACT_NOREPLACE, TCA_ACT_BIND);
	if (ret < 0) {
		tcf_action_destroy(act, TCA_ACT_UNBIND);
		return ret;
	}

	act->type = TCA_OLD_COMPAT;

	if (*action) {
		tcf_tree_lock(tp);
		act = xchg(action, act);
		tcf_tree_unlock(tp);

		tcf_action_destroy(act, TCA_ACT_UNBIND);
	} else
		*action = act;

	return 0;
}

static inline int
tcf_change_act(struct tcf_proto *tp, struct tc_action **action,
	struct rtattr *act_tlv, struct rtattr *rate_tlv)
{
	int ret;
	struct tc_action *act;

	act = kmalloc(sizeof(*act), GFP_KERNEL);
	if (NULL == act)
		return -ENOMEM;
	memset(act, 0, sizeof(*act));

	ret = tcf_action_init(act_tlv, rate_tlv, act, NULL,
		TCA_ACT_NOREPLACE, TCA_ACT_BIND);
	if (ret < 0) {
		tcf_action_destroy(act, TCA_ACT_UNBIND);
		return ret;
	}

	if (*action) {
		tcf_tree_lock(tp);
		act = xchg(action, act);
		tcf_tree_unlock(tp);

		tcf_action_destroy(act, TCA_ACT_UNBIND);
	} else
		*action = act;

	return 0;
}

static inline int
tcf_dump_act(struct sk_buff *skb, struct tc_action *action,
	int act_type, int compat_type)
{
	/*
	 * again for backward compatible mode - we want
	 * to work with both old and new modes of entering
	 * tc data even if iproute2  was newer - jhs
	 */
	if (action) {
		struct rtattr * p_rta = (struct rtattr*) skb->tail;

		if (action->type != TCA_OLD_COMPAT) {
			RTA_PUT(skb, act_type, 0, NULL);
			if (tcf_action_dump(skb, action, 0, 0) < 0)
				goto rtattr_failure;
		} else {
			RTA_PUT(skb, compat_type, 0, NULL);
			if (tcf_action_dump_old(skb, action, 0, 0) < 0)
				goto rtattr_failure;
		}
		
		p_rta->rta_len = skb->tail - (u8*)p_rta;
	}
	return 0;

rtattr_failure:
	return -1;
}
#endif /* CONFIG_NET_CLS_ACT */

#ifdef CONFIG_NET_CLS_IND
static inline int
tcf_change_indev(struct tcf_proto *tp, char *indev, struct rtattr *indev_tlv)
{
	if (RTA_PAYLOAD(indev_tlv) >= IFNAMSIZ) {
		printk("cls: bad indev name %s\n", (char *) RTA_DATA(indev_tlv));
		return -EINVAL;
	}

	memset(indev, 0, IFNAMSIZ);
	sprintf(indev, "%s", (char *) RTA_DATA(indev_tlv));

	return 0;
}

static inline int
tcf_match_indev(struct sk_buff *skb, char *indev)
{
	if (0 != indev[0]) {
		if  (NULL == skb->input_dev)
			return 0;
		else if (0 != strcmp(indev, skb->input_dev->name))
			return 0;
	}

	return 1;
}
#endif /* CONFIG_NET_CLS_IND */

#ifdef CONFIG_NET_CLS_POLICE
static inline int
tcf_change_police(struct tcf_proto *tp, struct tcf_police **police,
	struct rtattr *police_tlv, struct rtattr *rate_tlv)
{
	struct tcf_police *p = tcf_police_locate(police_tlv, rate_tlv);

	if (*police) {
		tcf_tree_lock(tp);
		p = xchg(police, p);
		tcf_tree_unlock(tp);

		tcf_police_release(p, TCA_ACT_UNBIND);
	} else
		*police = p;

	return 0;
}

static inline int
tcf_dump_police(struct sk_buff *skb, struct tcf_police *police,
	int police_type)
{
	if (police) {
		struct rtattr * p_rta = (struct rtattr*) skb->tail;

		RTA_PUT(skb, police_type, 0, NULL);

		if (tcf_police_dump(skb, police) < 0)
			goto rtattr_failure;

		p_rta->rta_len = skb->tail - (u8*)p_rta;
	}
	return 0;

rtattr_failure:
	return -1;
}
#endif /* CONFIG_NET_CLS_POLICE */

#endif
