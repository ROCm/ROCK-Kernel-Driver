#ifndef _IP_CONNTRACK_H
#define _IP_CONNTRACK_H
/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */

#include <linux/config.h>
#include <linux/netfilter_ipv4/ip_conntrack_tuple.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <asm/atomic.h>

enum ip_conntrack_info
{
	/* Part of an established connection (either direction). */
	IP_CT_ESTABLISHED,

	/* Like NEW, but related to an existing connection, or ICMP error
	   (in either direction). */
	IP_CT_RELATED,

	/* Started a new connection to track (only
           IP_CT_DIR_ORIGINAL); may be a retransmission. */
	IP_CT_NEW,

	/* >= this indicates reply direction */
	IP_CT_IS_REPLY,

	/* Number of distinct IP_CT types (no NEW in reply dirn). */
	IP_CT_NUMBER = IP_CT_IS_REPLY * 2 - 1
};

/* Bitset representing status of connection. */
enum ip_conntrack_status {
	/* It's an expected connection: bit 0 set.  This bit never changed */
	IPS_EXPECTED_BIT = 0,
	IPS_EXPECTED = (1 << IPS_EXPECTED_BIT),

	/* We've seen packets both ways: bit 1 set.  Can be set, not unset. */
	IPS_SEEN_REPLY_BIT = 1,
	IPS_SEEN_REPLY = (1 << IPS_SEEN_REPLY_BIT),

	/* Conntrack should never be early-expired. */
	IPS_ASSURED_BIT = 2,
	IPS_ASSURED = (1 << IPS_ASSURED_BIT),

	/* Connection is confirmed: originating packet has left box */
	IPS_CONFIRMED_BIT = 3,
	IPS_CONFIRMED = (1 << IPS_CONFIRMED_BIT),
};

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

/* per expectation: application helper private data */
union ip_conntrack_expect_help {
	/* insert conntrack helper private data (expect) here */
	struct ip_ct_amanda_expect exp_amanda_info;
	struct ip_ct_ftp_expect exp_ftp_info;
	struct ip_ct_irc_expect exp_irc_info;

#ifdef CONFIG_IP_NF_NAT_NEEDED
	union {
		/* insert nat helper private data (expect) here */
	} nat;
#endif
};

/* per conntrack: application helper private data */
union ip_conntrack_help {
	/* insert conntrack helper private data (master) here */
	struct ip_ct_ftp_master ct_ftp_info;
	struct ip_ct_irc_master ct_irc_info;
};

#ifdef CONFIG_IP_NF_NAT_NEEDED
#include <linux/netfilter_ipv4/ip_nat.h>

/* per conntrack: nat application helper private data */
union ip_conntrack_nat_help {
	/* insert nat helper private data here */
};
#endif

#ifdef __KERNEL__

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

struct ip_conntrack_expect
{
	/* Internal linked list (global expectation list) */
	struct list_head list;

	/* reference count */
	atomic_t use;

	/* expectation list for this master */
	struct list_head expected_list;

	/* The conntrack of the master connection */
	struct ip_conntrack *expectant;

	/* The conntrack of the sibling connection, set after
	 * expectation arrived */
	struct ip_conntrack *sibling;

	/* Tuple saved for conntrack */
	struct ip_conntrack_tuple ct_tuple;

	/* Timer function; deletes the expectation. */
	struct timer_list timeout;

	/* Data filled out by the conntrack helpers follow: */

	/* We expect this tuple, with the following mask */
	struct ip_conntrack_tuple tuple, mask;

	/* Function to call after setup and insertion */
	int (*expectfn)(struct ip_conntrack *new);

	/* At which sequence number did this expectation occur */
	u_int32_t seq;
  
	union ip_conntrack_expect_proto proto;

	union ip_conntrack_expect_help help;
};

struct ip_conntrack_counter
{
	u_int64_t packets;
	u_int64_t bytes;
};

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

	/* If we're expecting another related connection, this will be
           in expected linked list */
	struct list_head sibling_list;
	
	/* Current number of expected connections */
	unsigned int expecting;

	/* If we were expected by an expectation, this will be it */
	struct ip_conntrack_expect *master;

	/* Helper, if any. */
	struct ip_conntrack_helper *helper;

	/* Storage reserved for other modules: */
	union ip_conntrack_proto proto;

	union ip_conntrack_help help;

#ifdef CONFIG_IP_NF_NAT_NEEDED
	struct {
		struct ip_nat_info info;
		union ip_conntrack_nat_help help;
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

/* get master conntrack via master expectation */
#define master_ct(conntr) (conntr->master ? conntr->master->expectant : NULL)

/* Alter reply tuple (maybe alter helper).  If it's already taken,
   return 0 and don't do alteration. */
extern int
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
extern inline void ip_conntrack_put(struct ip_conntrack *ct);

/* find unconfirmed expectation based on tuple */
struct ip_conntrack_expect *
ip_conntrack_expect_find_get(const struct ip_conntrack_tuple *tuple);

/* decrement reference count on an expectation */
void ip_conntrack_expect_put(struct ip_conntrack_expect *exp);

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
extern int ip_conntrack_tcp_update(struct sk_buff *skb,
				   struct ip_conntrack *conntrack,
				   int dir);

/* Call me when a conntrack is destroyed. */
extern void (*ip_conntrack_destroyed)(struct ip_conntrack *conntrack);

/* Fake conntrack entry for untracked connections */
extern struct ip_conntrack ip_conntrack_untracked;

extern int ip_ct_no_defrag;
/* Returns new sk_buff, or NULL */
struct sk_buff *
ip_ct_gather_frags(struct sk_buff *skb);

/* Delete all conntracks which match. */
extern void
ip_ct_selective_cleanup(int (*kill)(const struct ip_conntrack *i, void *data),
			void *data);

/* It's confirmed if it is, or has been in the hash table. */
static inline int is_confirmed(struct ip_conntrack *ct)
{
	return test_bit(IPS_CONFIRMED_BIT, &ct->status);
}

extern unsigned int ip_conntrack_htable_size;
 
struct ip_conntrack_stat
{
	unsigned int searched;
	unsigned int found;
	unsigned int new;
	unsigned int invalid;
	unsigned int ignore;
	unsigned int delete;
	unsigned int delete_list;
	unsigned int insert;
	unsigned int insert_failed;
	unsigned int drop;
	unsigned int early_drop;
	unsigned int error;
	unsigned int expect_new;
	unsigned int expect_create;
	unsigned int expect_delete;
};

#define CONNTRACK_STAT_INC(count) (__get_cpu_var(ip_conntrack_stat).count++)

/* eg. PROVIDES_CONNTRACK(ftp); */
#define PROVIDES_CONNTRACK(name)                        \
        int needs_ip_conntrack_##name;                  \
        EXPORT_SYMBOL(needs_ip_conntrack_##name)

/*. eg. NEEDS_CONNTRACK(ftp); */
#define NEEDS_CONNTRACK(name)                                           \
        extern int needs_ip_conntrack_##name;                           \
        static int *need_ip_conntrack_##name __attribute_used__ = &needs_ip_conntrack_##name

#endif /* __KERNEL__ */
#endif /* _IP_CONNTRACK_H */
