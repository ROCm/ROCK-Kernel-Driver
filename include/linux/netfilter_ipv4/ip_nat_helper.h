#ifndef _IP_NAT_HELPER_H
#define _IP_NAT_HELPER_H
/* NAT protocol helper routines. */

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/module.h>

struct sk_buff;

/* Flags */
/* NAT helper must be called on every packet (for TCP) */
#define IP_NAT_HELPER_F_ALWAYS		0x01

struct ip_nat_helper
{
	struct list_head list;		/* Internal use */

	const char *name;		/* name of the module */
	unsigned char flags;		/* Flags (see above) */
	struct module *me;		/* pointer to self */
	
	/* Mask of things we will help: vs. tuple from server */
	struct ip_conntrack_tuple tuple;
	struct ip_conntrack_tuple mask;
	
	/* Helper function: returns verdict */
	unsigned int (*help)(struct ip_conntrack *ct,
			     struct ip_conntrack_expect *exp,
			     struct ip_nat_info *info,
			     enum ip_conntrack_info ctinfo,
			     unsigned int hooknum,
			     struct sk_buff **pskb);

	/* Returns verdict and sets up NAT for this connection */
	unsigned int (*expect)(struct sk_buff **pskb,
			       unsigned int hooknum,
			       struct ip_conntrack *ct,
			       struct ip_nat_info *info);
};

extern int ip_nat_helper_register(struct ip_nat_helper *me);
extern void ip_nat_helper_unregister(struct ip_nat_helper *me);

extern struct ip_nat_helper *
ip_nat_find_helper(const struct ip_conntrack_tuple *tuple);

extern struct ip_nat_helper *
__ip_nat_find_helper(const struct ip_conntrack_tuple *tuple);

/* These return true or false. */
extern int ip_nat_mangle_tcp_packet(struct sk_buff **skb,
				struct ip_conntrack *ct,
				enum ip_conntrack_info ctinfo,
				unsigned int match_offset,
				unsigned int match_len,
				const char *rep_buffer,
				unsigned int rep_len);
extern int ip_nat_mangle_udp_packet(struct sk_buff **skb,
				struct ip_conntrack *ct,
				enum ip_conntrack_info ctinfo,
				unsigned int match_offset,
				unsigned int match_len,
				const char *rep_buffer,
				unsigned int rep_len);
extern int ip_nat_seq_adjust(struct sk_buff **pskb, 
			     struct ip_conntrack *ct, 
			     enum ip_conntrack_info ctinfo);
#endif
