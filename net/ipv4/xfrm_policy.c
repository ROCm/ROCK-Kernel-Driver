/* Changes
 *
 *	Mitsuru KANDA @USAGI       : IPv6 Support 
 * 	Kazunori MIYAZAWA @USAGI   :
 * 	Kunihiro Ishiguro          :
 * 	
 */

#include <linux/config.h>
#include <net/xfrm.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>

DECLARE_MUTEX(xfrm_cfg_sem);

static u32      xfrm_policy_genid;
static rwlock_t xfrm_policy_lock = RW_LOCK_UNLOCKED;

struct xfrm_policy *xfrm_policy_list[XFRM_POLICY_MAX*2];

extern struct dst_ops xfrm4_dst_ops;
#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
extern struct dst_ops xfrm6_dst_ops;
#endif

static inline int xfrm_dst_lookup(struct xfrm_dst **dst, struct flowi *fl, unsigned short family);

/* Limited flow cache. Its function now is to accelerate search for
 * policy rules.
 *
 * Flow cache is private to cpus, at the moment this is important
 * mostly for flows which do not match any rule, so that flow lookups
 * are absolultely cpu-local. When a rule exists we do some updates
 * to rule (refcnt, stats), so that locality is broken. Later this
 * can be repaired.
 */

struct flow_entry
{
	struct flow_entry	*next;
	struct flowi		fl;
	u8			dir;
	u32			genid;
	struct xfrm_policy	*pol;
};

static kmem_cache_t *flow_cachep;

struct flow_entry **flow_table;

#define FLOWCACHE_HASH_SIZE	1024

static inline u32 flow_hash(struct flowi *fl)
{
	u32 hash = fl->fl4_src ^ fl->uli_u.ports.sport;

	hash = ((hash & 0xF0F0F0F0) >> 4) | ((hash & 0x0F0F0F0F) << 4);

	hash ^= fl->fl4_dst ^ fl->uli_u.ports.dport;
	hash ^= (hash >> 10);
	hash ^= (hash >> 20);
	return hash & (FLOWCACHE_HASH_SIZE-1);
}

#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
static inline u32 flow_hash6(struct flowi *fl)
{
	u32 hash = fl->fl6_src->s6_addr32[2] ^
		   fl->fl6_src->s6_addr32[3] ^ 
		   fl->uli_u.ports.sport;

	hash = ((hash & 0xF0F0F0F0) >> 4) | ((hash & 0x0F0F0F0F) << 4);

	hash ^= fl->fl6_dst->s6_addr32[2] ^
		fl->fl6_dst->s6_addr32[3] ^ 
		fl->uli_u.ports.dport;
	hash ^= (hash >> 10);
	hash ^= (hash >> 20);
	return hash & (FLOWCACHE_HASH_SIZE-1);
}
#endif

static int flow_lwm = 2*FLOWCACHE_HASH_SIZE;
static int flow_hwm = 4*FLOWCACHE_HASH_SIZE;

static int flow_number[NR_CPUS] __cacheline_aligned;

#define flow_count(cpu)		(flow_number[cpu])

static void flow_cache_shrink(int cpu)
{
	int i;
	struct flow_entry *fle, **flp;
	int shrink_to = flow_lwm/FLOWCACHE_HASH_SIZE;

	for (i=0; i<FLOWCACHE_HASH_SIZE; i++) {
		int k = 0;
		flp = &flow_table[cpu*FLOWCACHE_HASH_SIZE+i];
		while ((fle=*flp) != NULL && k<shrink_to) {
			k++;
			flp = &fle->next;
		}
		while ((fle=*flp) != NULL) {
			*flp = fle->next;
			if (fle->pol)
				xfrm_pol_put(fle->pol);
			kmem_cache_free(flow_cachep, fle);
		}
	}
}

struct xfrm_policy *flow_lookup(int dir, struct flowi *fl, 
				unsigned short family)
{
	struct xfrm_policy *pol = NULL;
	struct flow_entry *fle;
	u32 hash;
	int cpu;

	switch (family) {
	case AF_INET:
		hash = flow_hash(fl);
		break;
#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	case AF_INET6:
		hash = flow_hash6(fl);
		break;
#endif
	default:
		return NULL;
	}

	local_bh_disable();
	cpu = smp_processor_id();

	for (fle = flow_table[cpu*FLOWCACHE_HASH_SIZE+hash];
	     fle; fle = fle->next) {
		if (memcmp(fl, &fle->fl, sizeof(fle->fl)) == 0 &&
		    fle->dir == dir) {
			if (fle->genid == xfrm_policy_genid) {
				if ((pol = fle->pol) != NULL)
					atomic_inc(&pol->refcnt);
				local_bh_enable();
				return pol;
			}
			break;
		}
	}

	pol = xfrm_policy_lookup(dir, fl, family);

	if (fle) {
		/* Stale flow entry found. Update it. */
		fle->genid = xfrm_policy_genid;

		if (fle->pol)
			xfrm_pol_put(fle->pol);
		fle->pol = pol;
		if (pol)
			atomic_inc(&pol->refcnt);
	} else {
		if (flow_count(cpu) > flow_hwm)
			flow_cache_shrink(cpu);

		fle = kmem_cache_alloc(flow_cachep, SLAB_ATOMIC);
		if (fle) {
			flow_count(cpu)++;
			fle->fl = *fl;
			fle->genid = xfrm_policy_genid;
			fle->dir = dir;
			fle->pol = pol;
			if (pol)
				atomic_inc(&pol->refcnt);
			fle->next = flow_table[cpu*FLOWCACHE_HASH_SIZE+hash];
			flow_table[cpu*FLOWCACHE_HASH_SIZE+hash] = fle;
		}
	}
	local_bh_enable();
	return pol;
}

void __init flow_cache_init(void)
{
	int order;

	flow_cachep = kmem_cache_create("flow_cache",
					sizeof(struct flow_entry),
					0, SLAB_HWCACHE_ALIGN,
					NULL, NULL);

	if (!flow_cachep)
		panic("NET: failed to allocate flow cache slab\n");

	for (order = 0;
	     (PAGE_SIZE<<order) < (NR_CPUS*sizeof(struct flow_entry *)*FLOWCACHE_HASH_SIZE);
	     order++)
		/* NOTHING */;

	flow_table = (struct flow_entry **)__get_free_pages(GFP_ATOMIC, order);

	if (!flow_table)
		panic("Failed to allocate flow cache hash table\n");

	memset(flow_table, 0, PAGE_SIZE<<order);
}

static struct xfrm_type *xfrm_type_map[256];
static rwlock_t xfrm_type_lock = RW_LOCK_UNLOCKED;

int xfrm_register_type(struct xfrm_type *type)
{
	int err = 0;

	write_lock(&xfrm_type_lock);
	if (xfrm_type_map[type->proto] == NULL)
		xfrm_type_map[type->proto] = type;
	else
		err = -EEXIST;
	write_unlock(&xfrm_type_lock);
	return err;
}

int xfrm_unregister_type(struct xfrm_type *type)
{
	int err = 0;

	write_lock(&xfrm_type_lock);
	if (xfrm_type_map[type->proto] != type)
		err = -ENOENT;
	else
		xfrm_type_map[type->proto] = NULL;
	write_unlock(&xfrm_type_lock);
	return err;
}

struct xfrm_type *xfrm_get_type(u8 proto)
{
	struct xfrm_type *type;

	read_lock(&xfrm_type_lock);
	type = xfrm_type_map[proto];
	if (type && !try_module_get(type->owner))
		type = NULL;
	read_unlock(&xfrm_type_lock);
	return type;
}

static  xfrm_dst_lookup_t *__xfrm_dst_lookup[AF_MAX];
rwlock_t xdl_lock = RW_LOCK_UNLOCKED;

int xfrm_dst_lookup_register(xfrm_dst_lookup_t *dst_lookup, 
			     unsigned short family)
{
	int err = 0;

	write_lock(&xdl_lock);
	if (__xfrm_dst_lookup[family])
		err = -ENOBUFS;
	else { 
		__xfrm_dst_lookup[family] = dst_lookup;
	}
	write_unlock(&xdl_lock);

	return err;
}

void xfrm_dst_lookup_unregister(unsigned short family)
{
	write_lock(&xdl_lock);
	if (__xfrm_dst_lookup[family])
		__xfrm_dst_lookup[family] = 0;
	write_unlock(&xdl_lock);
}

static inline int xfrm_dst_lookup(struct xfrm_dst **dst, struct flowi *fl, 
				  unsigned short family)
{
	int err = 0;
	read_lock(&xdl_lock);
	if (__xfrm_dst_lookup[family])
		err = __xfrm_dst_lookup[family](dst, fl);
	else
		err = -EINVAL;
	read_unlock(&xdl_lock);
	return err;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static struct xfrm_type *xfrm6_type_map[256];
static rwlock_t xfrm6_type_lock = RW_LOCK_UNLOCKED;

int xfrm6_register_type(struct xfrm_type *type)
{
	int err = 0;

	write_lock(&xfrm6_type_lock);
	if (xfrm6_type_map[type->proto] == NULL)
		xfrm6_type_map[type->proto] = type;
	else
		err = -EEXIST;
	write_unlock(&xfrm6_type_lock);
	return err;
}

int xfrm6_unregister_type(struct xfrm_type *type)
{
	int err = 0;

	write_lock(&xfrm6_type_lock);
	if (xfrm6_type_map[type->proto] != type)
		err = -ENOENT;
	else
		xfrm6_type_map[type->proto] = NULL;
	write_unlock(&xfrm6_type_lock);
	return err;
}

struct xfrm_type *xfrm6_get_type(u8 proto)
{
	struct xfrm_type *type;

	read_lock(&xfrm6_type_lock);
	type = xfrm6_type_map[proto];
	if (type && !try_module_get(type->owner))
		type = NULL;
	read_unlock(&xfrm6_type_lock);
	return type;
}
#endif /* CONFIG_IPV6 || CONFIG_IPV6_MODULE */

void xfrm_put_type(struct xfrm_type *type)
{
	module_put(type->owner);
}

static inline unsigned long make_jiffies(long secs)
{
	if (secs >= (MAX_SCHEDULE_TIMEOUT-1)/HZ)
		return MAX_SCHEDULE_TIMEOUT-1;
	else
	        return secs*HZ;
}

static void xfrm_policy_timer(unsigned long data)
{
	struct xfrm_policy *xp = (struct xfrm_policy*)data;
	unsigned long now = (unsigned long)xtime.tv_sec;
	long next = LONG_MAX;
	u32 index;

	if (xp->dead)
		goto out;

	if (xp->lft.hard_add_expires_seconds) {
		long tmo = xp->lft.hard_add_expires_seconds +
			xp->curlft.add_time - now;
		if (tmo <= 0)
			goto expired;
		if (tmo < next)
			next = tmo;
	}
	if (next != LONG_MAX &&
	    !mod_timer(&xp->timer, jiffies + make_jiffies(next)))
		atomic_inc(&xp->refcnt);

out:
	xfrm_pol_put(xp);
	return;

expired:
	index = xp->index;
	xfrm_pol_put(xp);

	/* Not 100% correct. id can be recycled in theory */
	xp = xfrm_policy_byid(0, index, 1);
	if (xp) {
		xfrm_policy_kill(xp);
		xfrm_pol_put(xp);
	}
}


/* Allocate xfrm_policy. Not used here, it is supposed to be used by pfkeyv2
 * SPD calls.
 */

struct xfrm_policy *xfrm_policy_alloc(int gfp)
{
	struct xfrm_policy *policy;

	policy = kmalloc(sizeof(struct xfrm_policy), gfp);

	if (policy) {
		memset(policy, 0, sizeof(struct xfrm_policy));
		atomic_set(&policy->refcnt, 1);
		policy->lock = RW_LOCK_UNLOCKED;
		init_timer(&policy->timer);
		policy->timer.data = (unsigned long)policy;
		policy->timer.function = xfrm_policy_timer;
	}
	return policy;
}

/* Destroy xfrm_policy: descendant resources must be released to this moment. */

void __xfrm_policy_destroy(struct xfrm_policy *policy)
{
	if (!policy->dead)
		BUG();

	if (policy->bundles)
		BUG();

	if (del_timer(&policy->timer))
		BUG();

	kfree(policy);
}

/* Rule must be locked. Release descentant resources, announce
 * entry dead. The rule must be unlinked from lists to the moment.
 */

void xfrm_policy_kill(struct xfrm_policy *policy)
{
	struct dst_entry *dst;

	write_lock_bh(&policy->lock);
	if (policy->dead)
		goto out;

	policy->dead = 1;

	while ((dst = policy->bundles) != NULL) {
		policy->bundles = dst->next;
		dst_free(dst);
	}

	if (del_timer(&policy->timer))
		atomic_dec(&policy->refcnt);

out:
	write_unlock_bh(&policy->lock);
}

/* Generate new index... KAME seems to generate them ordered by cost
 * of an absolute inpredictability of ordering of rules. This will not pass. */
static u32 xfrm_gen_index(int dir)
{
	u32 idx;
	struct xfrm_policy *p;
	static u32 idx_generator;

	for (;;) {
		idx = (idx_generator | dir);
		idx_generator += 8;
		if (idx == 0)
			idx = 8;
		for (p = xfrm_policy_list[dir]; p; p = p->next) {
			if (p->index == idx)
				break;
		}
		if (!p)
			return idx;
	}
}

int xfrm_policy_insert(int dir, struct xfrm_policy *policy, int excl)
{
	struct xfrm_policy *pol, **p;

	write_lock_bh(&xfrm_policy_lock);
	for (p = &xfrm_policy_list[dir]; (pol=*p)!=NULL; p = &pol->next) {
		if (memcmp(&policy->selector, &pol->selector, sizeof(pol->selector)) == 0) {
			if (excl) {
				write_unlock_bh(&xfrm_policy_lock);
				return -EEXIST;
			}
			break;
		}
	}
	atomic_inc(&policy->refcnt);
	policy->next = pol ? pol->next : NULL;
	*p = policy;
	xfrm_policy_genid++;
	policy->index = pol ? pol->index : xfrm_gen_index(dir);
	policy->curlft.add_time = (unsigned long)xtime.tv_sec;
	policy->curlft.use_time = 0;
	if (policy->lft.hard_add_expires_seconds &&
	    !mod_timer(&policy->timer, jiffies + HZ))
		atomic_inc(&policy->refcnt);
	write_unlock_bh(&xfrm_policy_lock);

	if (pol) {
		atomic_dec(&pol->refcnt);
		xfrm_policy_kill(pol);
		xfrm_pol_put(pol);
	}
	return 0;
}

struct xfrm_policy *xfrm_policy_delete(int dir, struct xfrm_selector *sel)
{
	struct xfrm_policy *pol, **p;

	write_lock_bh(&xfrm_policy_lock);
	for (p = &xfrm_policy_list[dir]; (pol=*p)!=NULL; p = &pol->next) {
		if (memcmp(sel, &pol->selector, sizeof(*sel)) == 0) {
			*p = pol->next;
			break;
		}
	}
	if (pol)
		xfrm_policy_genid++;
	write_unlock_bh(&xfrm_policy_lock);
	return pol;
}

struct xfrm_policy *xfrm_policy_byid(int dir, u32 id, int delete)
{
	struct xfrm_policy *pol, **p;

	write_lock_bh(&xfrm_policy_lock);
	for (p = &xfrm_policy_list[id & 7]; (pol=*p)!=NULL; p = &pol->next) {
		if (pol->index == id) {
			if (delete)
				*p = pol->next;
			break;
		}
	}
	if (pol) {
		if (delete)
			xfrm_policy_genid++;
		else
			atomic_inc(&pol->refcnt);
	}
	write_unlock_bh(&xfrm_policy_lock);
	return pol;
}

void xfrm_policy_flush()
{
	struct xfrm_policy *xp;
	int dir;

	write_lock_bh(&xfrm_policy_lock);
	for (dir = 0; dir < XFRM_POLICY_MAX; dir++) {
		while ((xp = xfrm_policy_list[dir]) != NULL) {
			xfrm_policy_list[dir] = xp->next;
			write_unlock_bh(&xfrm_policy_lock);

			xfrm_policy_kill(xp);
			xfrm_pol_put(xp);

			write_lock_bh(&xfrm_policy_lock);
		}
	}
	xfrm_policy_genid++;
	write_unlock_bh(&xfrm_policy_lock);
}

int xfrm_policy_walk(int (*func)(struct xfrm_policy *, int, int, void*),
		     void *data)
{
	struct xfrm_policy *xp;
	int dir;
	int count = 0;
	int error = 0;

	read_lock_bh(&xfrm_policy_lock);
	for (dir = 0; dir < 2*XFRM_POLICY_MAX; dir++) {
		for (xp = xfrm_policy_list[dir]; xp; xp = xp->next)
			count++;
	}

	if (count == 0) {
		error = -ENOENT;
		goto out;
	}

	for (dir = 0; dir < 2*XFRM_POLICY_MAX; dir++) {
		for (xp = xfrm_policy_list[dir]; xp; xp = xp->next) {
			error = func(xp, dir%XFRM_POLICY_MAX, --count, data);
			if (error)
				goto out;
		}
	}

out:
	read_unlock_bh(&xfrm_policy_lock);
	return error;
}


/* Find policy to apply to this flow. */

struct xfrm_policy *xfrm_policy_lookup(int dir, struct flowi *fl, 
				       unsigned short family)
{
	struct xfrm_policy *pol;

	read_lock_bh(&xfrm_policy_lock);
	for (pol = xfrm_policy_list[dir]; pol; pol = pol->next) {
		struct xfrm_selector *sel = &pol->selector;
		int match;

		if (pol->family != family)
			continue;

		switch (family) {
		case AF_INET:
			match = xfrm4_selector_match(sel, fl);
			break;
#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		case AF_INET6:
			match = xfrm6_selector_match(sel, fl);
			break;
#endif
		default:
			match = 0;
		}
		if (match) {
			atomic_inc(&pol->refcnt);
			break;
		}
	}
	read_unlock_bh(&xfrm_policy_lock);
	return pol;
}

struct xfrm_policy *xfrm_sk_policy_lookup(struct sock *sk, int dir, struct flowi *fl)
{
	struct xfrm_policy *pol;

	read_lock_bh(&xfrm_policy_lock);
	if ((pol = sk->policy[dir]) != NULL) {
		int match;

		switch (sk->family) {
		case AF_INET:
			match = xfrm4_selector_match(&pol->selector, fl);
			break;
#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		case AF_INET6:
			match = xfrm6_selector_match(&pol->selector, fl);
			break;
#endif
		default:
			match = 0;
		}
		if (match)
			atomic_inc(&pol->refcnt);
		else
			pol = NULL;
	}
	read_unlock_bh(&xfrm_policy_lock);
	return pol;
}

void xfrm_sk_policy_link(struct xfrm_policy *pol, int dir)
{
	pol->next = xfrm_policy_list[XFRM_POLICY_MAX+dir];
	xfrm_policy_list[XFRM_POLICY_MAX+dir] = pol;
	atomic_inc(&pol->refcnt);
}

void xfrm_sk_policy_unlink(struct xfrm_policy *pol, int dir)
{
	struct xfrm_policy **polp;

	for (polp = &xfrm_policy_list[XFRM_POLICY_MAX+dir];
	     *polp != NULL; polp = &(*polp)->next) {
		if (*polp == pol) {
			*polp = pol->next;
			atomic_dec(&pol->refcnt);
			return;
		}
	}
}

int xfrm_sk_policy_insert(struct sock *sk, int dir, struct xfrm_policy *pol)
{
	struct xfrm_policy *old_pol;

	write_lock_bh(&xfrm_policy_lock);
	old_pol = sk->policy[dir];
	sk->policy[dir] = pol;
	if (pol) {
		pol->curlft.add_time = (unsigned long)xtime.tv_sec;
		pol->index = xfrm_gen_index(XFRM_POLICY_MAX+dir);
		xfrm_sk_policy_link(pol, dir);
	}
	if (old_pol)
		xfrm_sk_policy_unlink(old_pol, dir);
	write_unlock_bh(&xfrm_policy_lock);

	if (old_pol) {
		xfrm_policy_kill(old_pol);
		xfrm_pol_put(old_pol);
	}
	return 0;
}

static struct xfrm_policy *clone_policy(struct xfrm_policy *old, int dir)
{
	struct xfrm_policy *newp = xfrm_policy_alloc(GFP_ATOMIC);

	if (newp) {
		newp->selector = old->selector;
		newp->lft = old->lft;
		newp->curlft = old->curlft;
		newp->action = old->action;
		newp->flags = old->flags;
		newp->xfrm_nr = old->xfrm_nr;
		newp->index = old->index;
		memcpy(newp->xfrm_vec, old->xfrm_vec,
		       newp->xfrm_nr*sizeof(struct xfrm_tmpl));
		write_lock_bh(&xfrm_policy_lock);
		xfrm_sk_policy_link(newp, dir);
		write_unlock_bh(&xfrm_policy_lock);
	}
	return newp;
}

int __xfrm_sk_clone_policy(struct sock *sk)
{
	struct xfrm_policy *p0, *p1;
	p0 = sk->policy[0];
	p1 = sk->policy[1];
	sk->policy[0] = NULL;
	sk->policy[1] = NULL;
	if (p0 && (sk->policy[0] = clone_policy(p0, 0)) == NULL)
		return -ENOMEM;
	if (p1 && (sk->policy[1] = clone_policy(p1, 1)) == NULL)
		return -ENOMEM;
	return 0;
}

void __xfrm_sk_free_policy(struct xfrm_policy *pol, int dir)
{
	write_lock_bh(&xfrm_policy_lock);
	xfrm_sk_policy_unlink(pol, dir);
	write_unlock_bh(&xfrm_policy_lock);

	xfrm_policy_kill(pol);
	xfrm_pol_put(pol);
}

/* Resolve list of templates for the flow, given policy. */

static int
xfrm4_tmpl_resolve(struct xfrm_policy *policy, struct flowi *fl,
		   struct xfrm_state **xfrm)
{
	int nx;
	int i, error;
	u32 daddr = fl->fl4_dst;
	u32 saddr = fl->fl4_src;

	for (nx=0, i = 0; i < policy->xfrm_nr; i++) {
		struct xfrm_state *x;
		u32 remote = daddr;
		u32 local = saddr;
		struct xfrm_tmpl *tmpl = &policy->xfrm_vec[i];

		if (tmpl->mode) {
			remote = tmpl->id.daddr.xfrm4_addr;
			local = tmpl->saddr.xfrm4_addr;
		}

		x = xfrm4_state_find(remote, local, fl, tmpl, policy, &error);

		if (x && x->km.state == XFRM_STATE_VALID) {
			xfrm[nx++] = x;
			daddr = remote;
			saddr = local;
			continue;
		}
		if (x) {
			error = (x->km.state == XFRM_STATE_ERROR ?
				 -EINVAL : -EAGAIN);
			xfrm_state_put(x);
		}

		if (!tmpl->optional)
			goto fail;
	}
	return nx;

fail:
	for (nx--; nx>=0; nx--)
		xfrm_state_put(xfrm[nx]);
	return error;
}

#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
static int
xfrm6_tmpl_resolve(struct xfrm_policy *policy, struct flowi *fl,
		  struct xfrm_state **xfrm)
{
	int nx;
	int i, error;
	struct in6_addr *daddr = fl->fl6_dst;
	struct in6_addr *saddr = fl->fl6_src;

	for (nx=0, i = 0; i < policy->xfrm_nr; i++) {
		struct xfrm_state *x=NULL;
		struct in6_addr *remote = daddr;
		struct in6_addr *local = saddr;
		struct xfrm_tmpl *tmpl = &policy->xfrm_vec[i];

		if (tmpl->mode) {
			remote = (struct in6_addr*)&tmpl->id.daddr;
			local = (struct in6_addr*)&tmpl->saddr;
		}

		x = xfrm6_state_find(remote, local, fl, tmpl, policy, &error);

		if (x && x->km.state == XFRM_STATE_VALID) {
			xfrm[nx++] = x;
			daddr = remote;
			saddr = local;
			continue;
		}
		if (x) {
			error = (x->km.state == XFRM_STATE_ERROR ?
				 -EINVAL : -EAGAIN);
			xfrm_state_put(x);
		}

		if (!tmpl->optional)
			goto fail;
	}
	return nx;

fail:
	for (nx--; nx>=0; nx--)
		xfrm_state_put(xfrm[nx]);
	return error;
}
#endif

/* Check that the bundle accepts the flow and its components are
 * still valid.
 */

static int xfrm_bundle_ok(struct xfrm_dst *xdst, struct flowi *fl)
{
	do {
		if (xdst->u.dst.ops != &xfrm4_dst_ops)
			return 1;

		if (!xfrm4_selector_match(&xdst->u.dst.xfrm->sel, fl))
			return 0;
		if (xdst->u.dst.xfrm->km.state != XFRM_STATE_VALID ||
		    xdst->u.dst.path->obsolete > 0)
			return 0;
		xdst = (struct xfrm_dst*)xdst->u.dst.child;
	} while (xdst);
	return 0;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static int xfrm6_bundle_ok(struct xfrm_dst *xdst, struct flowi *fl)
{
	do {
		if (xdst->u.dst.ops != &xfrm6_dst_ops)
			return 1;

		if (!xfrm6_selector_match(&xdst->u.dst.xfrm->sel, fl))
			return 0;
		if (xdst->u.dst.xfrm->km.state != XFRM_STATE_VALID ||
		    xdst->u.dst.path->obsolete > 0)
			return 0;
		xdst = (struct xfrm_dst*)xdst->u.dst.child;
	} while (xdst);
	return 0;
}
#endif


/* Allocate chain of dst_entry's, attach known xfrm's, calculate
 * all the metrics... Shortly, bundle a bundle.
 */

static int
xfrm_bundle_create(struct xfrm_policy *policy, struct xfrm_state **xfrm, int nx,
		   struct flowi *fl, struct dst_entry **dst_p)
{
	struct dst_entry *dst, *dst_prev;
	struct rtable *rt0 = (struct rtable*)(*dst_p);
	struct rtable *rt = rt0;
	u32 remote = fl->fl4_dst;
	u32 local  = fl->fl4_src;
	int i;
	int err;
	int header_len = 0;
	int trailer_len = 0;

	dst = dst_prev = NULL;

	for (i = 0; i < nx; i++) {
		struct dst_entry *dst1 = dst_alloc(&xfrm4_dst_ops);

		if (unlikely(dst1 == NULL)) {
			err = -ENOBUFS;
			goto error;
		}

		dst1->xfrm = xfrm[i];
		if (!dst)
			dst = dst1;
		else {
			dst_prev->child = dst1;
			dst1->flags |= DST_NOHASH;
			dst_hold(dst1);
		}
		dst_prev = dst1;
		if (xfrm[i]->props.mode) {
			remote = xfrm[i]->id.daddr.xfrm4_addr;
			local  = xfrm[i]->props.saddr.xfrm4_addr;
		}
		header_len += xfrm[i]->props.header_len;
		trailer_len += xfrm[i]->props.trailer_len;
	}

	if (remote != fl->fl4_dst) {
		struct flowi fl_tunnel = { .nl_u = { .ip4_u =
						     { .daddr = remote,
						       .saddr = local }
					           }
				         };
		err = xfrm_dst_lookup((struct xfrm_dst**)&rt, &fl_tunnel, AF_INET);
		if (err)
			goto error;
	} else {
		dst_hold(&rt->u.dst);
	}
	dst_prev->child = &rt->u.dst;
	for (dst_prev = dst; dst_prev != &rt->u.dst; dst_prev = dst_prev->child) {
		struct xfrm_dst *x = (struct xfrm_dst*)dst_prev;
		x->u.rt.fl = *fl;

		dst_prev->dev = rt->u.dst.dev;
		if (rt->u.dst.dev)
			dev_hold(rt->u.dst.dev);
		dst_prev->obsolete	= -1;
		dst_prev->flags	       |= DST_HOST;
		dst_prev->lastuse	= jiffies;
		dst_prev->header_len	= header_len;
		dst_prev->trailer_len	= trailer_len;
		memcpy(&dst_prev->metrics, &rt->u.dst.metrics, sizeof(dst_prev->metrics));
		dst_prev->path		= &rt->u.dst;

		/* Copy neighbout for reachability confirmation */
		dst_prev->neighbour	= neigh_clone(rt->u.dst.neighbour);
		dst_prev->input		= rt->u.dst.input;
		dst_prev->output	= dst_prev->xfrm->type->output;
		if (rt->peer)
			atomic_inc(&rt->peer->refcnt);
		x->u.rt.peer = rt->peer;
		/* Sheit... I remember I did this right. Apparently,
		 * it was magically lost, so this code needs audit */
		x->u.rt.rt_flags = rt0->rt_flags&(RTCF_BROADCAST|RTCF_MULTICAST|RTCF_LOCAL);
		x->u.rt.rt_type = rt->rt_type;
		x->u.rt.rt_src = rt0->rt_src;
		x->u.rt.rt_dst = rt0->rt_dst;
		x->u.rt.rt_gateway = rt->rt_gateway;
		x->u.rt.rt_spec_dst = rt0->rt_spec_dst;
		header_len -= x->u.dst.xfrm->props.header_len;
		trailer_len -= x->u.dst.xfrm->props.trailer_len;
	}
	*dst_p = dst;
	return 0;

error:
	if (dst)
		dst_free(dst);
	return err;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static int
xfrm6_bundle_create(struct xfrm_policy *policy, struct xfrm_state **xfrm, int nx,
		   struct flowi *fl, struct dst_entry **dst_p)
{
	struct dst_entry *dst, *dst_prev;
	struct rt6_info *rt0 = (struct rt6_info*)(*dst_p);
	struct rt6_info *rt  = rt0;
	struct in6_addr *remote = fl->fl6_dst;
	struct in6_addr *local  = fl->fl6_src;
	int i;
	int err = 0;
	int header_len = 0;
	int trailer_len = 0;

	dst = dst_prev = NULL;

	for (i = 0; i < nx; i++) {
		struct dst_entry *dst1 = dst_alloc(&xfrm6_dst_ops);

		if (unlikely(dst1 == NULL)) {
			err = -ENOBUFS;
			goto error;
		}

		dst1->xfrm = xfrm[i];
		if (!dst)
			dst = dst1;
		else {
			dst_prev->child = dst1;
			dst1->flags |= DST_NOHASH;
			dst_clone(dst1);
		}
		dst_prev = dst1;
		if (xfrm[i]->props.mode) {
			remote = (struct in6_addr*)&xfrm[i]->id.daddr;
			local  = (struct in6_addr*)&xfrm[i]->props.saddr;
		}
		header_len += xfrm[i]->props.header_len;
		trailer_len += xfrm[i]->props.trailer_len;
	}

	if (ipv6_addr_cmp(remote, fl->fl6_dst)) {
		struct flowi fl_tunnel = { .nl_u = { .ip6_u =
						     { .daddr = remote,
						       .saddr = local }
					           }
				         };
		err = xfrm_dst_lookup((struct xfrm_dst**)&dst, &fl_tunnel, AF_INET6);
		if (err)
			goto error;
	} else {
		dst_clone(&rt->u.dst);
	}
	dst_prev->child = &rt->u.dst;
	for (dst_prev = dst; dst_prev != &rt->u.dst; dst_prev = dst_prev->child) {
		struct xfrm_dst *x = (struct xfrm_dst*)dst_prev;
		x->u.rt.fl = *fl;

		dst_prev->dev = rt->u.dst.dev;
		if (rt->u.dst.dev)
			dev_hold(rt->u.dst.dev);
		dst_prev->obsolete	= -1;
		dst_prev->flags	       |= DST_HOST;
		dst_prev->lastuse	= jiffies;
		dst_prev->header_len	= header_len;
		dst_prev->trailer_len	= trailer_len;
		memcpy(&dst_prev->metrics, &rt->u.dst.metrics, sizeof(dst_prev->metrics));
		dst_prev->path		= &rt->u.dst;

		/* Copy neighbout for reachability confirmation */
		dst_prev->neighbour	= neigh_clone(rt->u.dst.neighbour);
		dst_prev->input		= rt->u.dst.input;
		dst_prev->output	= dst_prev->xfrm->type->output;
		/* Sheit... I remember I did this right. Apparently,
		 * it was magically lost, so this code needs audit */
		x->u.rt6.rt6i_flags    = rt0->rt6i_flags&(RTCF_BROADCAST|RTCF_MULTICAST|RTCF_LOCAL);
		x->u.rt6.rt6i_metric   = rt0->rt6i_metric;
		x->u.rt6.rt6i_node     = rt0->rt6i_node;
		x->u.rt6.rt6i_hoplimit = rt0->rt6i_hoplimit;
		x->u.rt6.rt6i_gateway  = rt0->rt6i_gateway;
		memcpy(&x->u.rt6.rt6i_gateway, &rt0->rt6i_gateway, sizeof(x->u.rt6.rt6i_gateway)); 
		header_len -= x->u.dst.xfrm->props.header_len;
		trailer_len -= x->u.dst.xfrm->props.trailer_len;
	}
	*dst_p = dst;
	return 0;

error:
	if (dst)
		dst_free(dst);
	return err;
}
#endif

/* Main function: finds/creates a bundle for given flow.
 *
 * At the moment we eat a raw IP route. Mostly to speed up lookups
 * on interfaces with disabled IPsec.
 */
int xfrm_lookup(struct dst_entry **dst_p, struct flowi *fl,
		struct sock *sk, int flags)
{
	struct xfrm_policy *policy;
	struct xfrm_state *xfrm[XFRM_MAX_DEPTH];
	struct rtable *rt = (struct rtable*)*dst_p;
	struct dst_entry *dst;
	int nx = 0;
	int err;
	u32 genid;
	u16 family = (*dst_p)->ops->family;

	switch (family) {
	case AF_INET:
		if (!fl->fl4_src)
			fl->fl4_src = rt->rt_src;
		if (!fl->fl4_dst)
			fl->fl4_dst = rt->rt_dst;
	case AF_INET6:
		/* Still not clear... */
	default:
	}

restart:
	genid = xfrm_policy_genid;
	policy = NULL;
	if (sk && sk->policy[1])
		policy = xfrm_sk_policy_lookup(sk, XFRM_POLICY_OUT, fl);

	if (!policy) {
		/* To accelerate a bit...  */
		if ((rt->u.dst.flags & DST_NOXFRM) || !xfrm_policy_list[XFRM_POLICY_OUT])
			return 0;

		policy = flow_lookup(XFRM_POLICY_OUT, fl, family);
	}

	if (!policy)
		return 0;

	policy->curlft.use_time = (unsigned long)xtime.tv_sec;

	switch (policy->action) {
	case XFRM_POLICY_BLOCK:
		/* Prohibit the flow */
		xfrm_pol_put(policy);
		return -EPERM;

	case XFRM_POLICY_ALLOW:
		if (policy->xfrm_nr == 0) {
			/* Flow passes not transformed. */
			xfrm_pol_put(policy);
			return 0;
		}

		/* Try to find matching bundle.
		 *
		 * LATER: help from flow cache. It is optional, this
		 * is required only for output policy.
		 */
		if (family == AF_INET) {
			read_lock_bh(&policy->lock);
			for (dst = policy->bundles; dst; dst = dst->next) {
				struct xfrm_dst *xdst = (struct xfrm_dst*)dst;
				if (xdst->u.rt.fl.fl4_dst == fl->fl4_dst &&
				    xdst->u.rt.fl.fl4_src == fl->fl4_src &&
				    xdst->u.rt.fl.oif == fl->oif &&
				    xfrm_bundle_ok(xdst, fl)) {
					dst_clone(dst);
					break;
				}
			}
			read_unlock_bh(&policy->lock);
			if (dst)
				break;
			nx = xfrm4_tmpl_resolve(policy, fl, xfrm);
#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		} else if (family == AF_INET6) {
			read_lock_bh(&policy->lock);
			for (dst = policy->bundles; dst; dst = dst->next) {
				struct xfrm_dst *xdst = (struct xfrm_dst*)dst;
				if (!ipv6_addr_cmp(&xdst->u.rt6.rt6i_dst.addr, fl->fl6_dst) &&
				    !ipv6_addr_cmp(&xdst->u.rt6.rt6i_src.addr, fl->fl6_src) &&
				    xfrm6_bundle_ok(xdst, fl)) {
					dst_clone(dst);
					break;
				}
			}
			read_unlock_bh(&policy->lock);
			if (dst)
				break;
			nx = xfrm6_tmpl_resolve(policy, fl, xfrm);
#endif
		} else {
			return -EINVAL;
		}

		if (dst)
			break;

		if (unlikely(nx<0)) {
			err = nx;
			if (err == -EAGAIN) {
				struct task_struct *tsk = current;
				DECLARE_WAITQUEUE(wait, tsk);
				if (!flags)
					goto error;

				__set_task_state(tsk, TASK_INTERRUPTIBLE);
				add_wait_queue(&km_waitq, &wait);
				switch (family) {
				case AF_INET:
					err = xfrm4_tmpl_resolve(policy, fl, xfrm);
					break;
#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
				case AF_INET6:
					err = xfrm6_tmpl_resolve(policy, fl, xfrm);
					break;
#endif
				default:
					err = -EINVAL;
				}
				if (err == -EAGAIN)
					schedule();
				__set_task_state(tsk, TASK_RUNNING);
				remove_wait_queue(&km_waitq, &wait);

				if (err == -EAGAIN && signal_pending(current)) {
					err = -ERESTART;
					goto error;
				}
				if (err == -EAGAIN ||
				    genid != xfrm_policy_genid)
					goto restart;
			}
			if (err)
				goto error;
		} else if (nx == 0) {
			/* Flow passes not transformed. */
			xfrm_pol_put(policy);
			return 0;
		}

		dst = &rt->u.dst;
		switch (family) {
		case AF_INET:
			err = xfrm_bundle_create(policy, xfrm, nx, fl, &dst);
			break;
#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		case AF_INET6:
			err = xfrm6_bundle_create(policy, xfrm, nx, fl, &dst);
			break;
#endif
		default:
			err = -EINVAL;
		}
			
		if (unlikely(err)) {
			int i;
			for (i=0; i<nx; i++)
				xfrm_state_put(xfrm[i]);
			goto error;
		}

		write_lock_bh(&policy->lock);
		if (unlikely(policy->dead)) {
			/* Wow! While we worked on resolving, this
			 * policy has gone. Retry. It is not paranoia,
			 * we just cannot enlist new bundle to dead object.
			 */
			write_unlock_bh(&policy->lock);

			xfrm_pol_put(policy);
			if (dst)
				dst_free(dst);
			goto restart;
		}
		dst->next = policy->bundles;
		policy->bundles = dst;
		dst_hold(dst);
		write_unlock_bh(&policy->lock);
	}
	*dst_p = dst;
	ip_rt_put(rt);
	xfrm_pol_put(policy);
	return 0;

error:
	ip_rt_put(rt);
	xfrm_pol_put(policy);
	*dst_p = NULL;
	return err;
}

/* When skb is transformed back to its "native" form, we have to
 * check policy restrictions. At the moment we make this in maximally
 * stupid way. Shame on me. :-) Of course, connected sockets must
 * have policy cached at them.
 */

static inline int
xfrm_state_ok(struct xfrm_tmpl *tmpl, struct xfrm_state *x)
{
	return	x->id.proto == tmpl->id.proto &&
		(x->id.spi == tmpl->id.spi || !tmpl->id.spi) &&
		x->props.mode == tmpl->mode &&
		(tmpl->aalgos & (1<<x->props.aalgo)) &&
		(!x->props.mode || !tmpl->saddr.xfrm4_addr ||
		 tmpl->saddr.xfrm4_addr == x->props.saddr.xfrm4_addr);
}

static inline int
xfrm_policy_ok(struct xfrm_tmpl *tmpl, struct sec_path *sp, int idx)
{
	for (; idx < sp->len; idx++) {
		if (xfrm_state_ok(tmpl, sp->xvec[idx]))
			return ++idx;
	}
	return -1;
}

static void
_decode_session4(struct sk_buff *skb, struct flowi *fl)
{
	struct iphdr *iph = skb->nh.iph;
	u8 *xprth = skb->nh.raw + iph->ihl*4;

	if (!(iph->frag_off & htons(IP_MF | IP_OFFSET))) {
		switch (iph->protocol) {
		case IPPROTO_UDP:
		case IPPROTO_TCP:
		case IPPROTO_SCTP:
			if (pskb_may_pull(skb, xprth + 4 - skb->data)) {
				u16 *ports = (u16 *)xprth;

				fl->uli_u.ports.sport = ports[0];
				fl->uli_u.ports.dport = ports[1];
			}
			break;

		case IPPROTO_ESP:
			if (pskb_may_pull(skb, xprth + 4 - skb->data)) {
				u32 *ehdr = (u32 *)xprth;

				fl->uli_u.spi = ehdr[0];
			}
			break;

		case IPPROTO_AH:
			if (pskb_may_pull(skb, xprth + 8 - skb->data)) {
				u32 *ah_hdr = (u32*)xprth;

				fl->uli_u.spi = ah_hdr[1];
			}
			break;

		default:
			fl->uli_u.spi = 0;
			break;
		};
	} else {
		memset(fl, 0, sizeof(struct flowi));
	}
	fl->proto = iph->protocol;
	fl->fl4_dst = iph->daddr;
	fl->fl4_src = iph->saddr;
}

#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
static inline int
xfrm6_state_ok(struct xfrm_tmpl *tmpl, struct xfrm_state *x)
{
	return	x->id.proto == tmpl->id.proto &&
		(x->id.spi == tmpl->id.spi || !tmpl->id.spi) &&
		x->props.mode == tmpl->mode &&
		(tmpl->aalgos & (1<<x->props.aalgo)) &&
		(!x->props.mode || !ipv6_addr_any((struct in6_addr*)&x->props.saddr) ||
		 !ipv6_addr_cmp((struct in6_addr *)&tmpl->saddr, (struct in6_addr*)&x->props.saddr));
}

static inline int
xfrm6_policy_ok(struct xfrm_tmpl *tmpl, struct sec_path *sp, int idx)
{
	for (; idx < sp->len; idx++) {
		if (xfrm6_state_ok(tmpl, sp->xvec[idx]))
			return ++idx;
	}
	return -1;
}

static inline void
_decode_session6(struct sk_buff *skb, struct flowi *fl)
{
	u16 offset = sizeof(struct ipv6hdr);
	struct ipv6hdr *hdr = skb->nh.ipv6h;
	struct ipv6_opt_hdr *exthdr = (struct ipv6_opt_hdr*)(skb->nh.raw + offset);
	u8 nexthdr = skb->nh.ipv6h->nexthdr;

	fl->fl6_dst = &hdr->daddr;
	fl->fl6_src = &hdr->saddr;

	while (pskb_may_pull(skb, skb->nh.raw + offset + 1 - skb->data)) {
		switch (nexthdr) {
		case NEXTHDR_ROUTING:
		case NEXTHDR_HOP:
		case NEXTHDR_DEST:
			offset += ipv6_optlen(exthdr);
			nexthdr = exthdr->nexthdr;
			exthdr = (struct ipv6_opt_hdr*)(skb->nh.raw + offset);
			break;

		case IPPROTO_UDP:
		case IPPROTO_TCP:
		case IPPROTO_SCTP:
			if (pskb_may_pull(skb, skb->nh.raw + offset + 4 - skb->data)) {
				u16 *ports = (u16 *)exthdr;

				fl->uli_u.ports.sport = ports[0];
				fl->uli_u.ports.dport = ports[1];
			}
			return;

		/* XXX Why are there these headers? */
		case IPPROTO_AH:
		case IPPROTO_ESP:
		default:
			fl->uli_u.spi = 0;
			return;
		};
	}
}
#endif

int __xfrm_policy_check(struct sock *sk, int dir, struct sk_buff *skb, 
			unsigned short family)
{
	struct xfrm_policy *pol;
	struct flowi fl;

	switch (family) {
	case AF_INET:
		_decode_session4(skb, &fl);
		break;
#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	case AF_INET6:
		_decode_session6(skb, &fl);
		break;
#endif
	default :
		return 0;
	}

	/* First, check used SA against their selectors. */
	if (skb->sp) {
		int i;

		for (i=skb->sp->len-1; i>=0; i--) {
			int match;
			switch (family) {
			case AF_INET:
				match = xfrm4_selector_match(&skb->sp->xvec[i]->sel, &fl);
				break;
#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
			case AF_INET6:
				match = xfrm6_selector_match(&skb->sp->xvec[i]->sel, &fl);
				break;
#endif
			default:
				match = 0;
			}
			if (!match)
				return 0;
		}
	}

	pol = NULL;
	if (sk && sk->policy[dir])
		pol = xfrm_sk_policy_lookup(sk, dir, &fl);

	if (!pol)
		pol = flow_lookup(dir, &fl, family);

	if (!pol)
		return 1;

	pol->curlft.use_time = (unsigned long)xtime.tv_sec;

	if (pol->action == XFRM_POLICY_ALLOW) {
		if (pol->xfrm_nr != 0) {
			struct sec_path *sp;
			static struct sec_path dummy;
			int i, k;

			if ((sp = skb->sp) == NULL)
				sp = &dummy;

			/* For each tmpl search corresponding xfrm.
			 * Order is _important_. Later we will implement
			 * some barriers, but at the moment barriers
			 * are implied between each two transformations.
			 */
			for (i = pol->xfrm_nr-1, k = 0; i >= 0; i--) {
				if (pol->xfrm_vec[i].optional)
					continue;
				switch (family) {
				case AF_INET:
					k = xfrm_policy_ok(pol->xfrm_vec+i, sp, k);
					break;
#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
				case AF_INET6:
					k = xfrm6_policy_ok(pol->xfrm_vec+i, sp, k);
					break;
#endif
				default:
					k = -1;
				}
				if (k < 0)
					goto reject;
			}
		}
		xfrm_pol_put(pol);
		return 1;
	}

reject:
	xfrm_pol_put(pol);
	return 0;
}

int __xfrm_route_forward(struct sk_buff *skb, unsigned short family)
{
	struct flowi fl;

	switch (family) {
	case AF_INET:
		_decode_session4(skb, &fl);
		break;
#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	case AF_INET6:
		_decode_session6(skb, &fl);
		break;
#endif
	default:
		return 0;
	}

	return xfrm_lookup(&skb->dst, &fl, NULL, 0) == 0;
}

/* Optimize later using cookies and generation ids. */

static struct dst_entry *xfrm_dst_check(struct dst_entry *dst, u32 cookie)
{
	struct dst_entry *child = dst;

	while (child) {
		if (child->obsolete > 0 ||
		    (child->xfrm && child->xfrm->km.state != XFRM_STATE_VALID)) {
			dst_release(dst);
			return NULL;
		}
		child = child->child;
	}

	return dst;
}

static void xfrm_dst_destroy(struct dst_entry *dst)
{
	xfrm_state_put(dst->xfrm);
	dst->xfrm = NULL;
}

static void xfrm_link_failure(struct sk_buff *skb)
{
	/* Impossible. Such dst must be popped before reaches point of failure. */
	return;
}

static struct dst_entry *xfrm_negative_advice(struct dst_entry *dst)
{
	if (dst) {
		if (dst->obsolete) {
			dst_release(dst);
			dst = NULL;
		}
	}
	return dst;
}

static void __xfrm_garbage_collect(void)
{
	int i;
	struct xfrm_policy *pol;
	struct dst_entry *dst, **dstp, *gc_list = NULL;

	read_lock_bh(&xfrm_policy_lock);
	for (i=0; i<2*XFRM_POLICY_MAX; i++) {
		for (pol = xfrm_policy_list[i]; pol; pol = pol->next) {
			write_lock(&pol->lock);
			dstp = &pol->bundles;
			while ((dst=*dstp) != NULL) {
				if (atomic_read(&dst->__refcnt) == 0) {
					*dstp = dst->next;
					dst->next = gc_list;
					gc_list = dst;
				} else {
					dstp = &dst->next;
				}
			}
			write_unlock(&pol->lock);
		}
	}
	read_unlock_bh(&xfrm_policy_lock);

	while (gc_list) {
		dst = gc_list;
		gc_list = dst->next;
		dst_free(dst);
	}
}

static inline int xfrm4_garbage_collect(void)
{
	__xfrm_garbage_collect();
	return (atomic_read(&xfrm4_dst_ops.entries) > xfrm4_dst_ops.gc_thresh*2);
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static inline int xfrm6_garbage_collect(void)
{
	__xfrm_garbage_collect();
	return (atomic_read(&xfrm6_dst_ops.entries) > xfrm6_dst_ops.gc_thresh*2);
}
#endif

static int bundle_depends_on(struct dst_entry *dst, struct xfrm_state *x)
{
	do {
		if (dst->xfrm == x)
			return 1;
	} while ((dst = dst->child) != NULL);
	return 0;
}

int xfrm_flush_bundles(struct xfrm_state *x)
{
	int i;
	struct xfrm_policy *pol;
	struct dst_entry *dst, **dstp, *gc_list = NULL;

	read_lock_bh(&xfrm_policy_lock);
	for (i=0; i<2*XFRM_POLICY_MAX; i++) {
		for (pol = xfrm_policy_list[i]; pol; pol = pol->next) {
			write_lock(&pol->lock);
			dstp = &pol->bundles;
			while ((dst=*dstp) != NULL) {
				if (bundle_depends_on(dst, x)) {
					*dstp = dst->next;
					dst->next = gc_list;
					gc_list = dst;
				} else {
					dstp = &dst->next;
				}
			}
			write_unlock(&pol->lock);
		}
	}
	read_unlock_bh(&xfrm_policy_lock);

	while (gc_list) {
		dst = gc_list;
		gc_list = dst->next;
		dst_free(dst);
	}

	return 0;
}

 
static void xfrm4_update_pmtu(struct dst_entry *dst, u32 mtu)
{
	struct dst_entry *path = dst->path;

	if (mtu < 68 + dst->header_len)
		return;

	path->ops->update_pmtu(path, mtu);
}

#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
static void xfrm6_update_pmtu(struct dst_entry *dst, u32 mtu)
{
	struct dst_entry *path = dst->path;

	if (mtu >= 1280 && mtu < dst_pmtu(dst))
		return;

	path->ops->update_pmtu(path, mtu);
}
#endif

/* Well... that's _TASK_. We need to scan through transformation
 * list and figure out what mss tcp should generate in order to
 * final datagram fit to mtu. Mama mia... :-)
 *
 * Apparently, some easy way exists, but we used to choose the most
 * bizarre ones. :-) So, raising Kalashnikov... tra-ta-ta.
 *
 * Consider this function as something like dark humour. :-)
 */
static int xfrm_get_mss(struct dst_entry *dst, u32 mtu)
{
	int res = mtu - dst->header_len;

	for (;;) {
		struct dst_entry *d = dst;
		int m = res;

		do {
			struct xfrm_state *x = d->xfrm;
			if (x) {
				spin_lock_bh(&x->lock);
				if (x->km.state == XFRM_STATE_VALID &&
				    x->type && x->type->get_max_size)
					m = x->type->get_max_size(d->xfrm, m);
				else
					m += x->props.header_len;
				spin_unlock_bh(&x->lock);
			}
		} while ((d = d->child) != NULL);

		if (m <= mtu)
			break;
		res -= (m - mtu);
		if (res < 88)
			return mtu;
	}

	return res + dst->header_len;
}

struct dst_ops xfrm4_dst_ops = {
	.family =		AF_INET,
	.protocol =		__constant_htons(ETH_P_IP),
	.gc =			xfrm4_garbage_collect,
	.check =		xfrm_dst_check,
	.destroy =		xfrm_dst_destroy,
	.negative_advice =	xfrm_negative_advice,
	.link_failure =		xfrm_link_failure,
	.update_pmtu =		xfrm4_update_pmtu,
	.get_mss =		xfrm_get_mss,
	.gc_thresh =		1024,
	.entry_size =		sizeof(struct xfrm_dst),
};

#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
struct dst_ops xfrm6_dst_ops = {
	.family =		AF_INET6,
	.protocol =		__constant_htons(ETH_P_IPV6),
	.gc =			xfrm6_garbage_collect,
	.check =		xfrm_dst_check,
	.destroy =		xfrm_dst_destroy,
	.negative_advice =	xfrm_negative_advice,
	.link_failure =		xfrm_link_failure,
	.update_pmtu =		xfrm6_update_pmtu,
	.get_mss =		xfrm_get_mss,
	.gc_thresh =		1024,
	.entry_size =		sizeof(struct xfrm_dst),
};
#endif /* CONFIG_IPV6 || CONFIG_IPV6_MODULE */

void __init xfrm_init(void)
{
	xfrm4_dst_ops.kmem_cachep = kmem_cache_create("xfrm4_dst_cache",
						      sizeof(struct xfrm_dst),
						      0, SLAB_HWCACHE_ALIGN,
						      NULL, NULL);

	if (!xfrm4_dst_ops.kmem_cachep)
		panic("IP: failed to allocate xfrm4_dst_cache\n");

#if defined (CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	xfrm6_dst_ops.kmem_cachep = xfrm4_dst_ops.kmem_cachep;
#endif

	flow_cache_init();
	xfrm_state_init();
	xfrm_input_init();
}

