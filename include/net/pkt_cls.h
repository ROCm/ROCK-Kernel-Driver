#ifndef __NET_PKT_CLS_H
#define __NET_PKT_CLS_H

#include <linux/pkt_cls.h>
#include <net/sch_generic.h>

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

#endif
