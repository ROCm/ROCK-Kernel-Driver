#ifndef __NET_PKT_CLS_H
#define __NET_PKT_CLS_H


#include <linux/pkt_cls.h>

struct rtattr;
struct tcmsg;

/* Basic packet classifier frontend definitions. */

struct tcf_result
{
	unsigned long	class;
	u32		classid;
};

struct tcf_proto
{
	/* Fast access part */
	struct tcf_proto	*next;
	void			*root;
	int			(*classify)(struct sk_buff*, struct tcf_proto*, struct tcf_result *);
	u32			protocol;

	/* All the rest */
	u32			prio;
	u32			classid;
	struct Qdisc		*q;
	void			*data;
	struct tcf_proto_ops	*ops;
};

struct tcf_walker
{
	int	stop;
	int	skip;
	int	count;
	int	(*fn)(struct tcf_proto *, unsigned long node, struct tcf_walker *);
};

struct module;

struct tcf_proto_ops
{
	struct tcf_proto_ops	*next;
	char			kind[IFNAMSIZ];

	int			(*classify)(struct sk_buff*, struct tcf_proto*, struct tcf_result *);
	int			(*init)(struct tcf_proto*);
	void			(*destroy)(struct tcf_proto*);

	unsigned long		(*get)(struct tcf_proto*, u32 handle);
	void			(*put)(struct tcf_proto*, unsigned long);
	int			(*change)(struct tcf_proto*, unsigned long, u32 handle, struct rtattr **, unsigned long *);
	int			(*delete)(struct tcf_proto*, unsigned long);
	void			(*walk)(struct tcf_proto*, struct tcf_walker *arg);

	/* rtnetlink specific */
	int			(*dump)(struct tcf_proto*, unsigned long, struct sk_buff *skb, struct tcmsg*);

	struct module		*owner;
};

/* Main classifier routine: scans classifier chain attached
   to this qdisc, (optionally) tests for protocol and asks
   specific classifiers.
 */

static inline int tc_classify(struct sk_buff *skb, struct tcf_proto *tp, struct tcf_result *res)
{
	int err = 0;
	u32 protocol = skb->protocol;
#ifdef CONFIG_NET_CLS_ACT
	struct tcf_proto *otp = tp;
reclassify:
#endif
	protocol = skb->protocol;

	for ( ; tp; tp = tp->next) {
		if ((tp->protocol == protocol ||
			tp->protocol == __constant_htons(ETH_P_ALL)) &&
			(err = tp->classify(skb, tp, res)) >= 0) {
#ifdef CONFIG_NET_CLS_ACT
			if ( TC_ACT_RECLASSIFY == err) {
				__u32 verd = (__u32) G_TC_VERD(skb->tc_verd);
				tp = otp;

				if (MAX_REC_LOOP < verd++) {
					printk("rule prio %d protocol %02x reclassify is buggy packet dropped\n",tp->prio&0xffff, ntohs(tp->protocol));
					return TC_ACT_SHOT;
				}
				skb->tc_verd = SET_TC_VERD(skb->tc_verd,verd);
				goto reclassify;
			} else {
				if (skb->tc_verd) 
					skb->tc_verd = SET_TC_VERD(skb->tc_verd,0);
				return err;
			}
#else

			return err;
#endif
            }

	}
	return -1;
}

static inline void tcf_destroy(struct tcf_proto *tp)
{
	tp->ops->destroy(tp);
	module_put(tp->ops->owner);
	kfree(tp);
}

extern int register_tcf_proto_ops(struct tcf_proto_ops *ops);
extern int unregister_tcf_proto_ops(struct tcf_proto_ops *ops);
extern int ing_filter(struct sk_buff *skb);




#endif
