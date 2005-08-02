#ifndef _IP_CONNTRACK_H
#define _IP_CONNTRACK_H

#include <linux/netfilter/nf_conntrack_common.h>

#ifdef __KERNEL__
#include <linux/config.h>
#include <linux/netfilter_ipv4/ip_conntrack_tuple.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <asm/atomic.h>

#include <linux/netfilter_ipv4/ip_conntrack_tcp.h>
#include <linux/netfilter_ipv4/ip_conntrack_icmp.h>
#include <linux/netfilter_ipv4/ip_conntrack_sctp.h>

/* per conntrack: protocol private data */
union ip_conntrack_proto {
	/* insert conntrack proto private data here */
	struct ip_ct_sctp sctp;
	struct ip_ct_tcp tcp;
	struct ip_ct_icmp icmp;
};

union ip_conntrack_expect_proto {
	/* insert expect proto private data here */
};

/* Add protocol helper include file here */
#include <linux/netfilter_ipv4/ip_conntrack_amanda.h>
#include <linux/netfilter_ipv4/ip_conntrack_ftp.h>
#include <linux/netfilter_ipv4/ip_conntrack_irc.h>

/* per conntrack: application helper private data */
union ip_conntrack_help {
	/* insert conntrack helper private data (master) here */
	struct ip_ct_ftp_master ct_ftp_info;
	struct ip_ct_irc_master ct_irc_info;
};

#ifdef CONFIG_IP_NF_NAT_NEEDED
#include <linux/netfilter_ipv4/ip_nat.h>
#endif

#include <linux/types.h>
#include <linux/skbuff.h>

#ifdef CONFIG_NETFILTER_DEBUG
#define IP_NF_ASSERT(x)							\
do {									\
	if (!(x))							\
		/* Wooah!  I'm tripping my conntrack in a frenzy of	\
		   netplay... */					\
		printk("NF_IP_ASSERT: %s:%i(%s)\n",			\
		       __FILE__, __LINE__, __FUNCTION__);		\
} while(0)
#else
#define IP_NF_ASSERT(x)
#endif

struct ip_conntrack_helper;

struct ip_conntrack
{
	/* Usage count in here is 1 for hash table/destruct timer, 1 per skb,
           plus 1 for any connection(s) we are `master' for */
	struct nf_conntrack ct_general;

	/* Have we seen traffic both ways yet? (bitset) */
	unsigned long status;

	/* Timer function; drops refcnt when it goes off. */
	struct timer_list timeout;

#ifdef CONFIG_IP_NF_CT_ACCT
	/* Accounting Information (same cache line as other written members) */
	struct ip_conntrack_counter counters[IP_CT_DIR_MAX];
#endif
	/* If we were expected by an expectation, this will be it */
	struct ip_conntrack *master;

	/* Current number of expected connections */
	unsigned int expecting;

	/* Helper, if any. */
	struct ip_conntrack_helper *helper;

	/* Storage reserved for other modules: */
	union ip_conntrack_proto proto;

	union ip_conntrack_help help;

#ifdef CONFIG_IP_NF_NAT_NEEDED
	struct {
		struct ip_nat_info info;
#if defined(CONFIG_IP_NF_TARGET_MASQUERADE) || \
	defined(CONFIG_IP_NF_TARGET_MASQUERADE_MODULE)
		int masq_index;
#endif
	} nat;
#endif /* CONFIG_IP_NF_NAT_NEEDED */

#if defined(CONFIG_IP_NF_CONNTRACK_MARK)
	unsigned long mark;
#endif

	/* Traversed often, so hopefully in different cacheline to top */
	/* These are my tuples; original and reply */
	struct ip_conntrack_tuple_hash tuplehash[IP_CT_DIR_MAX];
};

struct ip_conntrack_expect
{
	/* Internal linked list (global expectation list) */
	struct list_head list;

	/* We expect this tuple, with the following mask */
	struct ip_conntrack_tuple tuple, mask;
 
	/* Function to call after setup and insertion */
	void (*expectfn)(struct ip_conntrack *new,
			 struct ip_conntrack_expect *this);

	/* The conntrack of the master connection */
	struct ip_conntrack *master;

	/* Timer function; deletes the expectation. */
	struct timer_list timeout;

	/* Usage count. */
	atomic_t use;

#ifdef CONFIG_IP_NF_NAT_NEEDED
	/* This is the original per-proto part, used to map the
	 * expected connection the way the recipient expects. */
	union ip_conntrack_manip_proto saved_proto;
	/* Direction relative to the master connection. */
	enum ip_conntrack_dir dir;
#endif
};

static inline struct ip_conntrack *
tuplehash_to_ctrack(const struct ip_conntrack_tuple_hash *hash)
{
	return container_of(hash, struct ip_conntrack,
			    tuplehash[hash->tuple.dst.dir]);
}

/* get master conntrack via master expectation */
#define master_ct(conntr) (conntr->master)

/* Alter reply tuple (maybe alter helper). */
extern void
ip_conntrack_alter_reply(struct ip_conntrack *conntrack,
			 const struct ip_conntrack_tuple *newreply);

/* Is this tuple taken? (ignoring any belonging to the given
   conntrack). */
extern int
ip_conntrack_tuple_taken(const struct ip_conntrack_tuple *tuple,
			 const struct ip_conntrack *ignored_conntrack);

/* Return conntrack_info and tuple hash for given skb. */
static inline struct ip_conntrack *
ip_conntrack_get(const struct sk_buff *skb, enum ip_conntrack_info *ctinfo)
{
	*ctinfo = skb->nfctinfo;
	return (struct ip_conntrack *)skb->nfct;
}

/* decrement reference count on a conntrack */
extern void ip_conntrack_put(struct ip_conntrack *ct);

/* call to create an explicit dependency on ip_conntrack. */
extern void need_ip_conntrack(void);

extern int invert_tuplepr(struct ip_conntrack_tuple *inverse,
			  const struct ip_conntrack_tuple *orig);

/* Refresh conntrack for this many jiffies */
extern void ip_ct_refresh_acct(struct ip_conntrack *ct,
			       enum ip_conntrack_info ctinfo,
			       const struct sk_buff *skb,
			       unsigned long extra_jiffies);

/* These are for NAT.  Icky. */
/* Update TCP window tracking data when NAT mangles the packet */
extern void ip_conntrack_tcp_update(struct sk_buff *skb,
				    struct ip_conntrack *conntrack,
				    enum ip_conntrack_dir dir);

/* Call me when a conntrack is destroyed. */
extern void (*ip_conntrack_destroyed)(struct ip_conntrack *conntrack);

/* Fake conntrack entry for untracked connections */
extern struct ip_conntrack ip_conntrack_untracked;

/* Returns new sk_buff, or NULL */
struct sk_buff *
ip_ct_gather_frags(struct sk_buff *skb, u_int32_t user);

/* Iterate over all conntracks: if iter returns true, it's deleted. */
extern void
ip_ct_iterate_cleanup(int (*iter)(struct ip_conntrack *i, void *data),
		      void *data);

/* It's confirmed if it is, or has been in the hash table. */
static inline int is_confirmed(struct ip_conntrack *ct)
{
	return test_bit(IPS_CONFIRMED_BIT, &ct->status);
}

extern unsigned int ip_conntrack_htable_size;
 
#define CONNTRACK_STAT_INC(count) (__get_cpu_var(ip_conntrack_stat).count++)

#ifdef CONFIG_IP_NF_NAT_NEEDED
static inline int ip_nat_initialized(struct ip_conntrack *conntrack,
				     enum ip_nat_manip_type manip)
{
	if (manip == IP_NAT_MANIP_SRC)
		return test_bit(IPS_SRC_NAT_DONE_BIT, &conntrack->status);
	return test_bit(IPS_DST_NAT_DONE_BIT, &conntrack->status);
}
#endif /* CONFIG_IP_NF_NAT_NEEDED */

#endif /* __KERNEL__ */
#endif /* _IP_CONNTRACK_H */
