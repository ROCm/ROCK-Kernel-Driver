#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>

#include <net/dst.h>
#include <net/route.h>

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
   8. ->resolved	If template uniquely resolves to a static xfrm_state,
                        the reference is stores here.

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

/* Structure to encapsulate addresses. I do not want to use
 * "standard" structure. My apologies. */

typedef union
{
	struct {
		u32	addr;
		u32	mask;	/* Use unused bits to cache mask. */
	} a4;
#define xfrm4_addr a4.addr
#define xfrm4_mask a4.mask
	u32		a6[4];
} xfrm_address_t;

/* Ident of a specific xfrm_state. It is used on input to lookup
 * the state by (spi,daddr,ah/esp) or to store information about
 * spi, protocol and tunnel address on output. */

struct xfrm_id
{
	xfrm_address_t	daddr;
	__u32		spi;
	__u8		proto;
};

/* Selector, used as selector both on policy rules (SPD) and SAs. */

struct xfrm_selector
{
	xfrm_address_t	daddr;
	xfrm_address_t	saddr;
	__u16	dport;
	__u16	dport_mask;
	__u16	sport;
	__u16	sport_mask;
	__u8	prefixlen_d;
	__u8	prefixlen_s;
	__u8	proto;
	int	ifindex;
	uid_t	user;
	void	*owner;
};


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
		int		state;
		u32		seq;
		u32		warn_bytes;
	} km;

	/* Parameters of this state. */
	struct {
		u8		mode;
		u8		algo;
		xfrm_address_t	saddr;
		int		header_len;
		int		trailer_len;
		u32		hard_byte_limit;
		u32		soft_byte_limit;
		u32		replay_window;
		/* More... */
	} props;

	/* State for replay detection */
	struct {
		u32		oseq;
		u32		seq;
		u32		bitmap;
	} replay;

	/* Statistics */
	struct {
		unsigned long	lastuse;
		unsigned long	expires;
		u32		bytes;
		u32		replay_window;
		u32		replay;
		u32		integrity_failed;
		/* More... */
	} stats;

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

#define XFRM_DST_HSIZE		1024

struct xfrm_type
{
	char			*description;
	atomic_t		refcnt;
	__u8			proto;
	__u8			algo;

	int			(*init_state)(struct xfrm_state *x, void *args);
	void			(*destructor)(struct xfrm_state *);
	int			(*input)(struct xfrm_state *, struct sk_buff *skb);
	int			(*output)(struct sk_buff *skb);
	/* Estimate maximal size of result of transformation of a dgram */
	u32			(*get_max_size)(struct xfrm_state *, int size);
};

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

/* Mode: transport/tunnel */
	__u8			mode;

/* Sharing mode: unique, this session only, this user only etc. */
	__u8			share;

/* Bit mask of algos allowed for acquisition */
	__u32			algos;

/* If template statically resolved, hold ref here */
	struct xfrm_state      *resolved;
};

#define XFRM_MAX_DEPTH		3

enum
{
	XFRM_SHARE_ANY,		/* No limitations */
	XFRM_SHARE_SESSION,	/* For this session only */
	XFRM_SHARE_USER,	/* For this user only */
	XFRM_SHARE_UNIQUE	/* Use once */
};

enum
{
	XFRM_POLICY_IN	= 0,
	XFRM_POLICY_FWD	= 1,
	XFRM_POLICY_OUT	= 2,
	XFRM_POLICY_MAX	= 3
};

struct xfrm_policy
{
	struct xfrm_policy	*next;

	/* This lock only affects elements except for entry. */
	rwlock_t		lock;
	atomic_t		refcnt;

	u32			priority;
	u32			index;
	struct xfrm_selector	selector;
	unsigned long		expires;
	unsigned long		lastuse;
	struct dst_entry       *bundles;
	__u8			action;
#define XFRM_POLICY_ALLOW	0
#define XFRM_POLICY_BLOCK	1
	__u8			flags;
#define XFRM_POLICY_LOCALOK	1	/* Allow user to override global policy */
	__u8			dead;
	__u8			xfrm_nr;
	struct xfrm_tmpl       	xfrm_vec[XFRM_MAX_DEPTH];
};

extern struct xfrm_policy *xfrm_policy_list[XFRM_POLICY_MAX];

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

extern int __xfrm_policy_check(int dir, struct sk_buff *skb);

static inline int xfrm_policy_check(int dir, struct sk_buff *skb)
{
	return	!xfrm_policy_list[dir] ||
		(skb->dst->flags & DST_NOPOLICY) ||
		__xfrm_policy_check(dir, skb);
}

extern int __xfrm_route_forward(struct sk_buff *skb);

static inline int xfrm_route_forward(struct sk_buff *skb)
{
	return	!xfrm_policy_list[XFRM_POLICY_OUT] ||
		(skb->dst->flags & DST_NOXFRM) ||
		__xfrm_route_forward(skb);
}

extern void xfrm_state_init(void);
extern void xfrm_input_init(void);
extern struct xfrm_state *xfrm_state_alloc(void);
extern struct xfrm_state *xfrm_state_find(u32 daddr, struct flowi *fl, struct xfrm_tmpl *tmpl);
extern int xfrm_state_check_expire(struct xfrm_state *x);
extern void xfrm_state_insert(struct xfrm_state *x);
extern int xfrm_state_check_space(struct xfrm_state *x, struct sk_buff *skb);
extern struct xfrm_state * xfrm_state_lookup(u32 daddr, u32 spi, u8 proto);
extern struct xfrm_policy *xfrm_policy_lookup(int dir, struct flowi *fl);
extern int xfrm_replay_check(struct xfrm_state *x, u32 seq);
extern void xfrm_replay_advance(struct xfrm_state *x, u32 seq);
extern int xfrm_check_selectors(struct xfrm_state **x, int n, struct flowi *fl);
extern int xfrm4_rcv(struct sk_buff *skb);


extern wait_queue_head_t *km_waitq;
extern void km_notify(struct xfrm_state *x, int event);
extern int km_query(struct xfrm_state *x);

extern int ah4_init(void);
