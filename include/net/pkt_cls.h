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

struct tcf_exts
{
#ifdef CONFIG_NET_CLS_ACT
	struct tc_action *action;
#elif defined CONFIG_NET_CLS_POLICE
	struct tcf_police *police;
#endif
};

/* Map to export classifier specific extension TLV types to the
 * generic extensions API. Unsupported extensions must be set to 0.
 */
struct tcf_ext_map
{
	int action;
	int police;
};

/**
 * tcf_exts_is_predicative - check if a predicative extension is present
 * @exts: tc filter extensions handle
 *
 * Returns 1 if a predicative extension is present, i.e. an extension which
 * might cause further actions and thus overrule the regular tcf_result.
 */
static inline int
tcf_exts_is_predicative(struct tcf_exts *exts)
{
#ifdef CONFIG_NET_CLS_ACT
	return !!exts->action;
#elif defined CONFIG_NET_CLS_POLICE
	return !!exts->police;
#else
	return 0;
#endif
}

/**
 * tcf_exts_is_available - check if at least one extension is present
 * @exts: tc filter extensions handle
 *
 * Returns 1 if at least one extension is present.
 */
static inline int
tcf_exts_is_available(struct tcf_exts *exts)
{
	/* All non-predicative extensions must be added here. */
	return tcf_exts_is_predicative(exts);
}

/**
 * tcf_exts_exec - execute tc filter extensions
 * @skb: socket buffer
 * @exts: tc filter extensions handle
 * @res: desired result
 *
 * Executes all configured extensions. Returns 0 on a normal execution,
 * a negative number if the filter must be considered unmatched or
 * a positive action code (TC_ACT_*) which must be returned to the
 * underlying layer.
 */
static inline int
tcf_exts_exec(struct sk_buff *skb, struct tcf_exts *exts,
	       struct tcf_result *res)
{
#ifdef CONFIG_NET_CLS_ACT
	if (exts->action)
		return tcf_action_exec(skb, exts->action, res);
#elif defined CONFIG_NET_CLS_POLICE
	if (exts->police)
		return tcf_police(skb, exts->police);
#endif

	return 0;
}

extern int tcf_exts_validate(struct tcf_proto *tp, struct rtattr **tb,
	                     struct rtattr *rate_tlv, struct tcf_exts *exts,
	                     struct tcf_ext_map *map);
extern void tcf_exts_destroy(struct tcf_proto *tp, struct tcf_exts *exts);
extern void tcf_exts_change(struct tcf_proto *tp, struct tcf_exts *dst,
	                     struct tcf_exts *src);
extern int tcf_exts_dump(struct sk_buff *skb, struct tcf_exts *exts,
	                 struct tcf_ext_map *map);
extern int tcf_exts_dump_stats(struct sk_buff *skb, struct tcf_exts *exts,
	                       struct tcf_ext_map *map);

#ifdef CONFIG_NET_CLS_ACT
static inline int
tcf_change_act_police(struct tcf_proto *tp, struct tc_action **action,
	struct rtattr *act_police_tlv, struct rtattr *rate_tlv)
{
	int ret;
	struct tc_action *act;

	act = tcf_action_init_1(act_police_tlv, rate_tlv, "police",
	                        TCA_ACT_NOREPLACE, TCA_ACT_BIND, &ret);
	if (act == NULL)
		return ret;

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

	act = tcf_action_init(act_tlv, rate_tlv, NULL,
	                      TCA_ACT_NOREPLACE, TCA_ACT_BIND, &ret);
	if (act == NULL)
		return ret;

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
