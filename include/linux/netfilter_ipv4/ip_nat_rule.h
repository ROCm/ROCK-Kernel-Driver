#ifndef _IP_NAT_RULE_H
#define _IP_NAT_RULE_H
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_nat.h>

#ifdef __KERNEL__
/* Want to be told when we first NAT an expected packet for a conntrack? */
struct ip_nat_expect
{
	struct list_head list;

	/* Returns 1 (and sets verdict) if it has setup NAT for this
           connection */
	int (*expect)(struct sk_buff **pskb,
		      unsigned int hooknum,
		      struct ip_conntrack *ct,
		      struct ip_nat_info *info,
		      struct ip_conntrack *master,
		      struct ip_nat_info *masterinfo,
		      unsigned int *verdict);
};

extern int ip_nat_expect_register(struct ip_nat_expect *expect);
extern void ip_nat_expect_unregister(struct ip_nat_expect *expect);
extern int ip_nat_rule_init(void) __init;
extern void ip_nat_rule_cleanup(void);
extern int ip_nat_rule_find(struct sk_buff **pskb,
			    unsigned int hooknum,
			    const struct net_device *in,
			    const struct net_device *out,
			    struct ip_conntrack *ct,
			    struct ip_nat_info *info);
#endif
#endif /* _IP_NAT_RULE_H */
