#ifndef _IP_NAT_HELPER_H
#define _IP_NAT_HELPER_H
/* NAT protocol helper routines. */

#include <linux/netfilter_ipv4/ip_conntrack.h>

struct sk_buff;

struct ip_nat_helper
{
	/* Internal use */
	struct list_head list;

	/* Mask of things we will help: vs. tuple from server */
	struct ip_conntrack_tuple tuple;
	struct ip_conntrack_tuple mask;
	
	/* Helper function: returns verdict */
	unsigned int (*help)(struct ip_conntrack *ct,
			     struct ip_nat_info *info,
			     enum ip_conntrack_info ctinfo,
			     unsigned int hooknum,
			     struct sk_buff **pskb);

	const char *name;
};

extern struct list_head helpers;

extern int ip_nat_helper_register(struct ip_nat_helper *me);
extern void ip_nat_helper_unregister(struct ip_nat_helper *me);
extern int ip_nat_mangle_tcp_packet(struct sk_buff **skb,
				struct ip_conntrack *ct,
				enum ip_conntrack_info ctinfo,
				unsigned int match_offset,
				unsigned int match_len,
				char *rep_buffer,
				unsigned int rep_len);
extern int ip_nat_seq_adjust(struct sk_buff *skb,
				struct ip_conntrack *ct,
				enum ip_conntrack_info ctinfo);
extern void ip_nat_delete_sack(struct sk_buff *skb, struct tcphdr *tcph);
#endif
