#ifndef _NET_NEIGHBOUR_H
#define _NET_NEIGHBOUR_H

/*
 *	Generic neighbour manipulation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Alexey Kuznetsov	<kuznet@ms2.inr.ac.ru>
 */

/* The following flags & states are exported to user space,
   so that they should be moved to include/linux/ directory.
 */

/*
 *	Neighbor Cache Entry Flags
 */

#define NTF_PROXY	0x08	/* == ATF_PUBL */
#define NTF_ROUTER	0x80

/*
 *	Neighbor Cache Entry States.
 */

#define NUD_INCOMPLETE	0x01
#define NUD_REACHABLE	0x02
#define NUD_STALE	0x04
#define NUD_DELAY	0x08
#define NUD_PROBE	0x10
#define NUD_FAILED	0x20

/* Dummy states */
#define NUD_NOARP	0x40
#define NUD_PERMANENT	0x80
#define NUD_NONE	0x00

/* NUD_NOARP & NUD_PERMANENT are pseudostates, they never change
   and make no address resolution or NUD.
   NUD_PERMANENT is also cannot be deleted by garbage collectors.
 */

#ifdef __KERNEL__

#include <asm/atomic.h>
#include <linux/skbuff.h>

#include <linux/config.h>

#include <linux/err.h>
#include <linux/sysctl.h>

#ifdef CONFIG_IPV6_NDISC_NEW
#define NUD_IN_TIMER	(NUD_INCOMPLETE|NUD_REACHABLE|NUD_DELAY|NUD_PROBE)
#else
#define NUD_IN_TIMER	(NUD_INCOMPLETE|NUD_DELAY|NUD_PROBE)
#endif
#define NUD_VALID	(NUD_PERMANENT|NUD_NOARP|NUD_REACHABLE|NUD_PROBE|NUD_STALE|NUD_DELAY)
#define NUD_CONNECTED	(NUD_PERMANENT|NUD_NOARP|NUD_REACHABLE)

struct neigh_parms
{
	struct neigh_parms *next;
	int	(*neigh_setup)(struct neighbour *);
	struct neigh_table *tbl;
	int	entries;
	void	*priv;

	void	*sysctl_table;

	int	base_reachable_time;
	int	retrans_time;
	int	gc_staletime;
	int	reachable_time;
	int	delay_probe_time;

	int	queue_len;
	int	ucast_probes;
	int	app_probes;
	int	mcast_probes;
	int	anycast_delay;
	int	proxy_delay;
	int	proxy_qlen;
	int	locktime;
};

struct neigh_statistics
{
	unsigned long allocs;
	unsigned long res_failed;
	unsigned long rcv_probes_mcast;
	unsigned long rcv_probes_ucast;
};

struct neighbour
{
	struct neighbour	*next;
	struct neigh_table	*tbl;
	struct neigh_parms	*parms;
	struct net_device		*dev;
	unsigned long		used;
	unsigned long		confirmed;
	unsigned long		updated;
	__u8			flags;
	__u8			nud_state;
	__u8			type;
	__u8			dead;
	atomic_t		probes;
	rwlock_t		lock;
	unsigned char		ha[(MAX_ADDR_LEN+sizeof(unsigned long)-1)&~(sizeof(unsigned long)-1)];
	struct hh_cache		*hh;
	atomic_t		refcnt;
	int			(*output)(struct sk_buff *skb);
	struct sk_buff_head	arp_queue;
	struct timer_list	timer;
	struct neigh_ops	*ops;
	u8			primary_key[0];
};

struct neigh_ops
{
	int			family;
	void			(*destructor)(struct neighbour *);
	void			(*solicit)(struct neighbour *, struct sk_buff*);
	void			(*error_report)(struct neighbour *, struct sk_buff*);
	int			(*output)(struct sk_buff*);
	int			(*connected_output)(struct sk_buff*);
	int			(*hh_output)(struct sk_buff*);
	int			(*queue_xmit)(struct sk_buff*);
};

struct pneigh_entry
{
	struct pneigh_entry	*next;
	struct net_device		*dev;
	u8			key[0];
};

#define NEIGH_HASHMASK		0x1F
#define PNEIGH_HASHMASK		0xF

/*
 *	neighbour table manipulation
 */


struct neigh_table
{
	struct neigh_table	*next;
	int			family;
	int			entry_size;
	int			key_len;
	__u32			(*hash)(const void *pkey, const struct net_device *);
	int			(*constructor)(struct neighbour *);
	int			(*pconstructor)(struct pneigh_entry *);
	void			(*pdestructor)(struct pneigh_entry *);
	void			(*proxy_redo)(struct sk_buff *skb);
	char			*id;
	struct neigh_parms	parms;
	/* HACK. gc_* shoul follow parms without a gap! */
	int			gc_interval;
	int			gc_thresh1;
	int			gc_thresh2;
	int			gc_thresh3;
	unsigned long		last_flush;
	struct timer_list 	gc_timer;
	struct timer_list 	proxy_timer;
	struct sk_buff_head	proxy_queue;
	int			entries;
	rwlock_t		lock;
	unsigned long		last_rand;
	struct neigh_parms	*parms_list;
	kmem_cache_t		*kmem_cachep;
	struct neigh_statistics	stats;
	struct neighbour	*hash_buckets[NEIGH_HASHMASK+1];
	struct pneigh_entry	*phash_buckets[PNEIGH_HASHMASK+1];
};

static __inline__ char * neigh_state(int state)
{
	switch (state) {
	case NUD_NONE:		return "NONE";
	case NUD_INCOMPLETE:	return "INCOMPLETE";
	case NUD_REACHABLE:	return "REACHABLE";
	case NUD_STALE:		return "STALE";
	case NUD_DELAY:		return "DELAY";
	case NUD_PROBE:		return "PROBE";
	case NUD_FAILED:	return "FAILED";
	case NUD_NOARP:		return "NOARP";
	case NUD_PERMANENT:	return "PERMANENT";
	default:		return "???";
	}
}

/* flags for __neigh_update() */
#define NEIGH_UPDATE_F_ADMIN			0x00000001
#define NEIGH_UPDATE_F_ISROUTER			0x00000002
#define NEIGH_UPDATE_F_OVERRIDE			0x00000004
#define NEIGH_UPDATE_F_OVERRIDE_VALID_ISROUTER	0x00000008
#define NEIGH_UPDATE_F_REUSEADDR		0x00000010
#define NEIGH_UPDATE_F_REUSESUSPECTSTATE	0x00000020
#define NEIGH_UPDATE_F_SETUP_ISROUTER		0x00000040
#define NEIGH_UPDATE_F_SUSPECT_CONNECTED	0x00000080

#define NEIGH_UPDATE_F_IP6NS		(NEIGH_UPDATE_F_SETUP_ISROUTER|\
					 NEIGH_UPDATE_F_REUSESUSPECTSTATE|\
					 NEIGH_UPDATE_F_OVERRIDE)
#define NEIGH_UPDATE_F_IP6NA		(NEIGH_UPDATE_F_SETUP_ISROUTER|\
					 NEIGH_UPDATE_F_REUSESUSPECTSTATE|\
					 NEIGH_UPDATE_F_SUSPECT_CONNECTED|\
					 NEIGH_UPDATE_F_OVERRIDE_VALID_ISROUTER)
#define NEIGH_UPDATE_F_IP6RS		(NEIGH_UPDATE_F_SETUP_ISROUTER|\
					 NEIGH_UPDATE_F_REUSESUSPECTSTATE|\
					 NEIGH_UPDATE_F_OVERRIDE|\
					 NEIGH_UPDATE_F_OVERRIDE_VALID_ISROUTER)
#define NEIGH_UPDATE_F_IP6RA		(NEIGH_UPDATE_F_SETUP_ISROUTER|\
					 NEIGH_UPDATE_F_REUSESUSPECTSTATE|\
					 NEIGH_UPDATE_F_OVERRIDE|\
					 NEIGH_UPDATE_F_OVERRIDE_VALID_ISROUTER|\
					 NEIGH_UPDATE_F_ISROUTER)
#define NEIGH_UPDATE_F_IP6REDIRECT	(NEIGH_UPDATE_F_REUSESUSPECTSTATE|\
					 NEIGH_UPDATE_F_OVERRIDE)

extern void			neigh_table_init(struct neigh_table *tbl);
extern int			neigh_table_clear(struct neigh_table *tbl);
extern struct neighbour *	neigh_lookup(struct neigh_table *tbl,
					     const void *pkey,
					     struct net_device *dev);
extern struct neighbour *	neigh_create(struct neigh_table *tbl,
					     const void *pkey,
					     struct net_device *dev);
extern void			neigh_destroy(struct neighbour *neigh);
extern int			__neigh_event_send(struct neighbour *neigh, struct sk_buff *skb);
extern int			__neigh_update(struct neighbour *neigh, const u8 *lladdr, u8 new, u32 flags);
extern int			neigh_update(struct neighbour *neigh, const u8 *lladdr, u8 new, int override, int arp);
extern void			neigh_changeaddr(struct neigh_table *tbl, struct net_device *dev);
extern int			neigh_ifdown(struct neigh_table *tbl, struct net_device *dev);
extern int			neigh_resolve_output(struct sk_buff *skb);
extern int			neigh_connected_output(struct sk_buff *skb);
extern int			neigh_compat_output(struct sk_buff *skb);
extern struct neighbour 	*neigh_event_ns(struct neigh_table *tbl,
						u8 *lladdr, void *saddr,
						struct net_device *dev);

extern struct neigh_parms	*neigh_parms_alloc(struct net_device *dev, struct neigh_table *tbl);
extern void			neigh_parms_release(struct neigh_table *tbl, struct neigh_parms *parms);
extern unsigned long		neigh_rand_reach_time(unsigned long base);

extern void			pneigh_enqueue(struct neigh_table *tbl, struct neigh_parms *p,
					       struct sk_buff *skb);
extern struct pneigh_entry	*pneigh_lookup(struct neigh_table *tbl, const void *key, struct net_device *dev, int creat);
extern int			pneigh_delete(struct neigh_table *tbl, const void *key, struct net_device *dev);

struct netlink_callback;
struct nlmsghdr;
extern int neigh_dump_info(struct sk_buff *skb, struct netlink_callback *cb);
extern int neigh_add(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg);
extern int neigh_delete(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg);
extern void neigh_app_ns(struct neighbour *n);
extern void neigh_app_notify(struct neighbour *n);

extern int			neigh_sysctl_register(struct net_device *dev, 
						      struct neigh_parms *p,
						      int p_id, int pdev_id,
						      char *p_name,
						      proc_handler *proc_handler);
extern void			neigh_sysctl_unregister(struct neigh_parms *p);

/*
 *	Neighbour references
 */

static inline void neigh_release(struct neighbour *neigh)
{
#ifdef CONFIG_IPV6_NDISC_DEBUG
	printk(KERN_DEBUG "%s(neigh=%p): refcnt=%d\n",
		__FUNCTION__, neigh, atomic_read(&neigh->refcnt)-1);
#endif
	if (atomic_dec_and_test(&neigh->refcnt))
		neigh_destroy(neigh);
}

static inline struct neighbour * neigh_clone(struct neighbour *neigh)
{
#ifdef CONFIG_IPV6_NDISC_DEBUG
	printk(KERN_DEBUG "%s(neigh=%p): refcnt=%d\n",
		__FUNCTION__, neigh, neigh ? atomic_read(&neigh->refcnt)+1 : 0);
#endif
	if (neigh)
		atomic_inc(&neigh->refcnt);
	return neigh;
}

#ifdef CONFIG_IPV6_NDISC_DEBUG
#define neigh_hold(n)	({	\
	struct neighbour *_n = (n);		\
	printk(KERN_DEBUG "%s(neigh=%p): refcnt=%d\n", \
		__FUNCTION__, _n, atomic_read(&_n->refcnt)+1);	\
	atomic_inc(&_n->refcnt);	\
})
#else
#define neigh_hold(n)	atomic_inc(&(n)->refcnt)
#endif

static inline void neigh_confirm(struct neighbour *neigh)
{
	if (neigh)
		neigh->confirmed = jiffies;
}

static inline int neigh_is_connected(struct neighbour *neigh)
{
	return neigh->nud_state&NUD_CONNECTED;
}

static inline int neigh_is_valid(struct neighbour *neigh)
{
	return neigh->nud_state&NUD_VALID;
}

static inline int neigh_event_send(struct neighbour *neigh, struct sk_buff *skb)
{
	int ret = 0;

#ifdef CONFIG_IPV6_NDISC_DEBUG
	printk(KERN_DEBUG
		"%s(neigh=%p, skb=%p): %s, refcnt=%d\n",
		__FUNCTION__, neigh, skb, neigh_state(neigh->nud_state), atomic_read(&neigh->refcnt));
#endif
	write_lock_bh(&neigh->lock);
	neigh->used = jiffies;
	if (!(neigh->nud_state&(NUD_CONNECTED|NUD_DELAY|NUD_PROBE)))
		ret = __neigh_event_send(neigh, skb);
	write_unlock_bh(&neigh->lock);
	if (ret < 0) {
		if (skb)
			kfree_skb(skb);
		ret = 1;
	}
	return ret;
}

static inline struct neighbour *
__neigh_lookup(struct neigh_table *tbl, const void *pkey, struct net_device *dev, int creat)
{
	struct neighbour *n = neigh_lookup(tbl, pkey, dev);

	if (n || !creat)
		return n;

	n = neigh_create(tbl, pkey, dev);
	return IS_ERR(n) ? NULL : n;
}

static inline struct neighbour *
__neigh_lookup_errno(struct neigh_table *tbl, const void *pkey,
  struct net_device *dev)
{
	struct neighbour *n = neigh_lookup(tbl, pkey, dev);

	if (n)
		return n;

	return neigh_create(tbl, pkey, dev);
}

#define LOCALLY_ENQUEUED -2

static inline struct pneigh_entry *pneigh_clone(struct pneigh_entry *pneigh)
{
	return pneigh;
}

static inline void pneigh_refcnt_init(struct pneigh_entry *pneigh) {}

static inline int pneigh_refcnt_dec_and_test(struct pneigh_entry *pneigh)
{
	return 1;
}

static inline int pneigh_alloc_flag(void)
{
	return GFP_KERNEL;
}

#endif
#endif


