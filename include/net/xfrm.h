#ifndef _NET_XFRM_H
#define _NET_XFRM_H

#include <linux/xfrm.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <linux/in6.h>

#include <net/sock.h>
#include <net/dst.h>
#include <net/route.h>
#include <net/ip6_fib.h>

#define XFRM_ALIGN8(len)	(((len) + 7) & ~7)

extern struct semaphore xfrm_cfg_sem;

/* Organization of SPD aka "XFRM rules"
   ------------------------------------

   Basic objects:
   - policy rule, struct xfrm_policy (=SPD entry)
   - bundle of transformations, struct dst_entry == struct xfrm_dst (=SA bundle)
   - instance of a transformer, struct xfrm_state (=SA)
   - template to clone xfrm_state, struct xfrm_tmpl

   SPD is plain linear list of xfrm_policy rules, ordered by priority.
   (To be compatible with existing pfkeyv2 implementations,
   many rules with priority of 0x7fffffff are allowed to exist and
   such rules are ordered in an unpredictable way, thanks to bsd folks.)

   Lookup is plain linear search until the first match with selector.

   If "action" is "block", then we prohibit the flow, otherwise:
   if "xfrms_nr" is zero, the flow passes untransformed. Otherwise,
   policy entry has list of up to XFRM_MAX_DEPTH transformations,
   described by templates xfrm_tmpl. Each template is resolved
   to a complete xfrm_state (see below) and we pack bundle of transformations
   to a dst_entry returned to requestor.

   dst -. xfrm  .-> xfrm_state #1
    |---. child .-> dst -. xfrm .-> xfrm_state #2
                     |---. child .-> dst -. xfrm .-> xfrm_state #3
                                      |---. child .-> NULL

   Bundles are cached at xrfm_policy struct (field ->bundles).


   Resolution of xrfm_tmpl
   -----------------------
   Template contains:
   1. ->mode		Mode: transport or tunnel
   2. ->id.proto	Protocol: AH/ESP/IPCOMP
   3. ->id.daddr	Remote tunnel endpoint, ignored for transport mode.
      Q: allow to resolve security gateway?
   4. ->id.spi          If not zero, static SPI.
   5. ->saddr		Local tunnel endpoint, ignored for transport mode.
   6. ->algos		List of allowed algos. Plain bitmask now.
      Q: ealgos, aalgos, calgos. What a mess...
   7. ->share		Sharing mode.
      Q: how to implement private sharing mode? To add struct sock* to
      flow id?

   Having this template we search through SAD searching for entries
   with appropriate mode/proto/algo, permitted by selector.
   If no appropriate entry found, it is requested from key manager.

   PROBLEMS:
   Q: How to find all the bundles referring to a physical path for
      PMTU discovery? Seems, dst should contain list of all parents...
      and enter to infinite locking hierarchy disaster.
      No! It is easier, we will not search for them, let them find us.
      We add genid to each dst plus pointer to genid of raw IP route,
      pmtu disc will update pmtu on raw IP route and increase its genid.
      dst_check() will see this for top level and trigger resyncing
      metrics. Plus, it will be made via sk->dst_cache. Solved.
 */

/* Full description of state of transformer. */
struct xfrm_state
{
	struct list_head	bydst;
	struct list_head	byspi;

	atomic_t		refcnt;
	spinlock_t		lock;

	struct xfrm_id		id;
	struct xfrm_selector	sel;

	/* Key manger bits */
	struct {
		u8		state;
		u8		dying;
		u32		seq;
	} km;

	/* Parameters of this state. */
	struct {
		u8		mode;
		u8		replay_window;
		u8		aalgo, ealgo, calgo;
		u16		reqid;
		u16		family;
		xfrm_address_t	saddr;
		int		header_len;
		int		trailer_len;
	} props;

	struct xfrm_lifetime_cfg lft;

	/* Data for transformer */
	struct xfrm_algo	*aalg;
	struct xfrm_algo	*ealg;
	struct xfrm_algo	*calg;

	/* State for replay detection */
	struct xfrm_replay_state replay;

	/* Statistics */
	struct xfrm_stats	stats;

	struct xfrm_lifetime_cur curlft;
	struct timer_list	timer;

	/* Reference to data common to all the instances of this
	 * transformer. */
	struct xfrm_type	*type;

	/* Private data of this transformer, format is opaque,
	 * interpreted by xfrm_type methods. */
	void			*data;
};

enum {
	XFRM_STATE_VOID,
	XFRM_STATE_ACQ,
	XFRM_STATE_VALID,
	XFRM_STATE_ERROR,
	XFRM_STATE_EXPIRED,
	XFRM_STATE_DEAD
};


struct xfrm_type
{
	char			*description;
	struct module		*owner;
	__u8			proto;

	int			(*init_state)(struct xfrm_state *x, void *args);
	void			(*destructor)(struct xfrm_state *);
	int			(*input)(struct xfrm_state *, struct sk_buff *skb);
	int			(*output)(struct sk_buff *skb);
	/* Estimate maximal size of result of transformation of a dgram */
	u32			(*get_max_size)(struct xfrm_state *, int size);
};

extern int xfrm_register_type(struct xfrm_type *type);
extern int xfrm_unregister_type(struct xfrm_type *type);
extern struct xfrm_type *xfrm_get_type(u8 proto);
extern void xfrm_put_type(struct xfrm_type *type);

struct xfrm_tmpl
{
/* id in template is interpreted as:
 * daddr - destination of tunnel, may be zero for transport mode.
 * spi   - zero to acquire spi. Not zero if spi is static, then
 *	   daddr must be fixed too.
 * proto - AH/ESP/IPCOMP
 */
	struct xfrm_id		id;

/* Source address of tunnel. Ignored, if it is not a tunnel. */
	xfrm_address_t		saddr;

	__u16			reqid;

/* Mode: transport/tunnel */
	__u8			mode;

/* Sharing mode: unique, this session only, this user only etc. */
	__u8			share;

/* May skip this transfomration if no SA is found */
	__u8			optional;

/* Bit mask of algos allowed for acquisition */
	__u32			aalgos;
	__u32			ealgos;
	__u32			calgos;
};

#define XFRM_MAX_DEPTH		3

struct xfrm_policy
{
	struct xfrm_policy	*next;

	/* This lock only affects elements except for entry. */
	rwlock_t		lock;
	atomic_t		refcnt;
	struct timer_list	timer;

	u32			priority;
	u32			index;
	struct xfrm_selector	selector;
	struct xfrm_lifetime_cfg lft;
	struct xfrm_lifetime_cur curlft;
	struct dst_entry       *bundles;
	__u16			family;
	__u8			action;
	__u8			flags;
	__u8			dead;
	__u8			xfrm_nr;
	struct xfrm_tmpl       	xfrm_vec[XFRM_MAX_DEPTH];
};

struct xfrm_mgr
{
	struct list_head	list;
	char			*id;
	int			(*notify)(struct xfrm_state *x, int event);
	int			(*acquire)(struct xfrm_state *x, struct xfrm_tmpl *, struct xfrm_policy *xp, int dir);
	struct xfrm_policy	*(*compile_policy)(u16 family, int opt, u8 *data, int len, int *dir);
};

extern int xfrm_register_km(struct xfrm_mgr *km);
extern int xfrm_unregister_km(struct xfrm_mgr *km);


extern struct xfrm_policy *xfrm_policy_list[XFRM_POLICY_MAX*2];

static inline void xfrm_pol_hold(struct xfrm_policy *policy)
{
	if (policy)
		atomic_inc(&policy->refcnt);
}

extern void __xfrm_policy_destroy(struct xfrm_policy *policy);

static inline void xfrm_pol_put(struct xfrm_policy *policy)
{
	if (atomic_dec_and_test(&policy->refcnt))
		__xfrm_policy_destroy(policy);
}

extern void __xfrm_state_destroy(struct xfrm_state *);

static inline void xfrm_state_put(struct xfrm_state *x)
{
	if (atomic_dec_and_test(&x->refcnt))
		__xfrm_state_destroy(x);
}

static inline void xfrm_state_hold(struct xfrm_state *x)
{
	atomic_inc(&x->refcnt);
}

static inline int
xfrm4_selector_match(struct xfrm_selector *sel, struct flowi *fl)
{
	return	!((fl->fl4_dst^sel->daddr.xfrm4_addr)&sel->daddr.xfrm4_mask) &&
		!((fl->uli_u.ports.dport^sel->dport)&sel->dport_mask) &&
		!((fl->uli_u.ports.sport^sel->sport)&sel->sport_mask) &&
		(fl->proto == sel->proto || !sel->proto) &&
		(fl->oif == sel->ifindex || !sel->ifindex) &&
		!((fl->fl4_src^sel->saddr.xfrm4_addr)&sel->saddr.xfrm4_mask);
}

/* A struct encoding bundle of transformations to apply to some set of flow.
 *
 * dst->child points to the next element of bundle.
 * dst->xfrm  points to an instanse of transformer.
 *
 * Due to unfortunate limitations of current routing cache, which we
 * have no time to fix, it mirrors struct rtable and bound to the same
 * routing key, including saddr,daddr. However, we can have many of
 * bundles differing by session id. All the bundles grow from a parent
 * policy rule.
 */
struct xfrm_dst
{
	union {
		struct xfrm_dst		*next;
		struct dst_entry	dst;
		struct rtable		rt;
		struct rt6_info		rt6;
	} u;
};

struct sec_path
{
	atomic_t		refcnt;
	int			len;
	struct xfrm_state	*xvec[XFRM_MAX_DEPTH];
};

static inline struct sec_path *
secpath_get(struct sec_path *sp)
{
	if (sp)
		atomic_inc(&sp->refcnt);
	return sp;
}

extern void __secpath_destroy(struct sec_path *sp);

static inline void
secpath_put(struct sec_path *sp)
{
	if (sp && atomic_dec_and_test(&sp->refcnt))
		__secpath_destroy(sp);
}
extern int __xfrm_policy_check(struct sock *, int dir, struct sk_buff *skb, unsigned short family);

static inline int xfrm_policy_check(struct sock *sk, int dir, struct sk_buff *skb)
{
	if (sk && sk->policy[XFRM_POLICY_IN])
		return __xfrm_policy_check(sk, dir, skb, AF_INET);
		
	return	!xfrm_policy_list[dir] ||
		(skb->dst->flags & DST_NOPOLICY) ||
		__xfrm_policy_check(sk, dir, skb, AF_INET);
}

static inline int xfrm6_policy_check(struct sock *sk, int dir, struct sk_buff *skb)
{
	if (sk && sk->policy[XFRM_POLICY_IN])
		return __xfrm_policy_check(sk, dir, skb, AF_INET6);
		
	return	!xfrm_policy_list[dir] ||
		(skb->dst->flags & DST_NOPOLICY) ||
		__xfrm_policy_check(sk, dir, skb, AF_INET6);
}

extern int __xfrm_route_forward(struct sk_buff *skb, unsigned short family);

static inline int xfrm_route_forward(struct sk_buff *skb)
{
	return	!xfrm_policy_list[XFRM_POLICY_OUT] ||
		(skb->dst->flags & DST_NOXFRM) ||
		__xfrm_route_forward(skb, AF_INET);
}

static inline int xfrm6_route_forward(struct sk_buff *skb)
{
	return	!xfrm_policy_list[XFRM_POLICY_OUT] ||
		(skb->dst->flags & DST_NOXFRM) ||
		__xfrm_route_forward(skb, AF_INET6);
}

extern int __xfrm_sk_clone_policy(struct sock *sk);

static inline int xfrm_sk_clone_policy(struct sock *sk)
{
	if (unlikely(sk->policy[0] || sk->policy[1]))
		return __xfrm_sk_clone_policy(sk);
	return 0;
}

extern void __xfrm_sk_free_policy(struct xfrm_policy *, int dir);

static inline void xfrm_sk_free_policy(struct sock *sk)
{
	if (unlikely(sk->policy[0] != NULL)) {
		__xfrm_sk_free_policy(sk->policy[0], 0);
		sk->policy[0] = NULL;
	}
	if (unlikely(sk->policy[1] != NULL)) {
		__xfrm_sk_free_policy(sk->policy[1], 1);
		sk->policy[1] = NULL;
	}
}

/*
 * xfrm algorithm information
 */
struct xfrm_algo_auth_info {
	u16 icv_truncbits;
	u16 icv_fullbits;
};

struct xfrm_algo_encr_info {
	u16 blockbits;
	u16 defkeybits;
};

struct xfrm_algo_desc {
	char *name;
	u8 available:1;
	union {
		struct xfrm_algo_auth_info auth;
		struct xfrm_algo_encr_info encr;
	} uinfo;
	struct sadb_alg desc;
};

extern void xfrm_state_init(void);
extern void xfrm_input_init(void);
extern int xfrm_state_walk(u8 proto, int (*func)(struct xfrm_state *, int, void*), void *);
extern struct xfrm_state *xfrm_state_alloc(void);
extern struct xfrm_state *xfrm4_state_find(u32 daddr, u32 saddr, struct flowi *fl, struct xfrm_tmpl *tmpl,
					   struct xfrm_policy *pol, int *err);
extern struct xfrm_state *xfrm6_state_find(struct in6_addr *daddr, struct in6_addr *saddr,
					   struct flowi *fl, struct xfrm_tmpl *tmpl,
					   struct xfrm_policy *pol, int *err);
extern int xfrm_state_check_expire(struct xfrm_state *x);
extern void xfrm_state_insert(struct xfrm_state *x);
extern int xfrm_state_check_space(struct xfrm_state *x, struct sk_buff *skb);
extern struct xfrm_state *xfrm4_state_lookup(u32 daddr, u32 spi, u8 proto);
extern struct xfrm_state *xfrm6_state_lookup(struct in6_addr *daddr, u32 spi, u8 proto);
extern struct xfrm_state *xfrm_find_acq_byseq(u32 seq);
extern void xfrm_state_delete(struct xfrm_state *x);
extern void xfrm_state_flush(u8 proto);
extern int xfrm_replay_check(struct xfrm_state *x, u32 seq);
extern void xfrm_replay_advance(struct xfrm_state *x, u32 seq);
extern int xfrm_check_selectors(struct xfrm_state **x, int n, struct flowi *fl);
extern int xfrm4_rcv(struct sk_buff *skb);
extern int xfrm6_rcv(struct sk_buff *skb);
extern int xfrm6_clear_mutable_options(struct sk_buff *skb, u16 *nh_offset, int dir);
extern int xfrm_user_policy(struct sock *sk, int optname, u8 *optval, int optlen);

struct xfrm_policy *xfrm_policy_alloc(int gfp);
extern int xfrm_policy_walk(int (*func)(struct xfrm_policy *, int, int, void*), void *);
struct xfrm_policy *xfrm_policy_lookup(int dir, struct flowi *fl, unsigned short family);
int xfrm_policy_insert(int dir, struct xfrm_policy *policy, int excl);
struct xfrm_policy *xfrm_policy_delete(int dir, struct xfrm_selector *sel);
struct xfrm_policy *xfrm_policy_byid(int dir, u32 id, int delete);
void xfrm_policy_flush(void);
void xfrm_alloc_spi(struct xfrm_state *x, u32 minspi, u32 maxspi);
struct xfrm_state * xfrm_find_acq(u8 mode, u16 reqid, u8 proto, u32 daddr, u32 saddr, int create);
struct xfrm_state * xfrm6_find_acq(u8 mode, u16 reqid, u8 proto, struct in6_addr *daddr,
				   struct in6_addr *saddr, int create);
extern void xfrm_policy_flush(void);
extern void xfrm_policy_kill(struct xfrm_policy *);
extern int xfrm_sk_policy_insert(struct sock *sk, int dir, struct xfrm_policy *pol);
extern struct xfrm_policy *xfrm_sk_policy_lookup(struct sock *sk, int dir, struct flowi *fl);
extern int xfrm_flush_bundles(struct xfrm_state *x);

extern wait_queue_head_t km_waitq;
extern void km_warn_expired(struct xfrm_state *x);
extern void km_expired(struct xfrm_state *x);
extern int km_query(struct xfrm_state *x, struct xfrm_tmpl *, struct xfrm_policy *pol);

extern void xfrm_probe_algs(void);
extern int xfrm_count_auth_supported(void);
extern int xfrm_count_enc_supported(void);
extern struct xfrm_algo_desc *xfrm_aalg_get_byidx(unsigned int idx);
extern struct xfrm_algo_desc *xfrm_ealg_get_byidx(unsigned int idx);
extern struct xfrm_algo_desc *xfrm_aalg_get_byid(int alg_id);
extern struct xfrm_algo_desc *xfrm_ealg_get_byid(int alg_id);
extern struct xfrm_algo_desc *xfrm_aalg_get_byname(char *name);
extern struct xfrm_algo_desc *xfrm_ealg_get_byname(char *name);

static __inline__ int addr_match(void *token1, void *token2, int prefixlen)
{
	__u32 *a1 = token1;
	__u32 *a2 = token2;
	int pdw;
	int pbi;

	pdw = prefixlen >> 5;	  /* num of whole __u32 in prefix */
	pbi = prefixlen &  0x1f;  /* num of bits in incomplete u32 in prefix */

	if (pdw)
		if (memcmp(a1, a2, pdw << 2))
			return 0;

	if (pbi) {
		__u32 mask;

		mask = htonl((0xffffffff) << (32 - pbi));

		if ((a1[pdw] ^ a2[pdw]) & mask)
			return 0;
	}

	return 1;
}

static inline int
xfrm6_selector_match(struct xfrm_selector *sel, struct flowi *fl)
{
	return  addr_match(fl->fl6_dst, &sel->daddr, sel->prefixlen_d) &&
		addr_match(fl->fl6_src, &sel->saddr, sel->prefixlen_s) &&
		!((fl->uli_u.ports.dport^sel->dport)&sel->dport_mask) &&
		!((fl->uli_u.ports.sport^sel->sport)&sel->sport_mask) &&
		(fl->proto == sel->proto || !sel->proto) &&
		(fl->oif == sel->ifindex || !sel->ifindex);
}

extern int xfrm6_register_type(struct xfrm_type *type);
extern int xfrm6_unregister_type(struct xfrm_type *type);
extern struct xfrm_type *xfrm6_get_type(u8 proto);

struct crypto_tfm;
typedef void (icv_update_fn_t)(struct crypto_tfm *, struct scatterlist *, unsigned int);

typedef int (xfrm_dst_lookup_t)(struct xfrm_dst **dst, struct flowi *fl);
int xfrm_dst_lookup_register(xfrm_dst_lookup_t *dst_lookup, unsigned short family);
void xfrm_dst_lookup_unregister(unsigned short family);

#endif	/* _NET_XFRM_H */
