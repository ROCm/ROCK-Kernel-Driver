/* 
 * xfrm_policy.c
 *
 * Changes:
 *	Mitsuru KANDA @USAGI
 * 	Kazunori MIYAZAWA @USAGI
 * 	Kunihiro Ishiguro
 * 		IPv6 support
 * 	Kazunori MIYAZAWA @USAGI
 * 	YOSHIFUJI Hideaki
 * 		Split up af-specific portion
 *	Derek Atkins <derek@ihtfp.com>		Add the post_input processor
 * 	
 */

#include <linux/config.h>
#include <net/xfrm.h>
#include <net/ip.h>

DECLARE_MUTEX(xfrm_cfg_sem);

static rwlock_t xfrm_policy_lock = RW_LOCK_UNLOCKED;

struct xfrm_policy *xfrm_policy_list[XFRM_POLICY_MAX*2];

static rwlock_t xfrm_policy_afinfo_lock = RW_LOCK_UNLOCKED;
static struct xfrm_policy_afinfo *xfrm_policy_afinfo[NPROTO];

kmem_cache_t *xfrm_dst_cache;

int xfrm_register_type(struct xfrm_type *type, unsigned short family)
{
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
	struct xfrm_type_map *typemap;
	int err = 0;

	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;
	typemap = afinfo->type_map;

	write_lock(&typemap->lock);
	if (likely(typemap->map[type->proto] == NULL))
		typemap->map[type->proto] = type;
	else
		err = -EEXIST;
	write_unlock(&typemap->lock);
	xfrm_policy_put_afinfo(afinfo);
	return err;
}

int xfrm_unregister_type(struct xfrm_type *type, unsigned short family)
{
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
	struct xfrm_type_map *typemap;
	int err = 0;

	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;
	typemap = afinfo->type_map;

	write_lock(&typemap->lock);
	if (unlikely(typemap->map[type->proto] != type))
		err = -ENOENT;
	else
		typemap->map[type->proto] = NULL;
	write_unlock(&typemap->lock);
	xfrm_policy_put_afinfo(afinfo);
	return err;
}

struct xfrm_type *xfrm_get_type(u8 proto, unsigned short family)
{
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
	struct xfrm_type_map *typemap;
	struct xfrm_type *type;

	if (unlikely(afinfo == NULL))
		return NULL;
	typemap = afinfo->type_map;

	read_lock(&typemap->lock);
	type = typemap->map[proto];
	if (unlikely(type && !try_module_get(type->owner)))
		type = NULL;
	read_unlock(&typemap->lock);
	xfrm_policy_put_afinfo(afinfo);
	return type;
}

int xfrm_dst_lookup(struct xfrm_dst **dst, struct flowi *fl, 
		    unsigned short family)
{
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
	int err = 0;

	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;

	if (likely(afinfo->dst_lookup != NULL))
		err = afinfo->dst_lookup(dst, fl);
	else
		err = -EINVAL;
	xfrm_policy_put_afinfo(afinfo);
	return err;
}

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
		xfrm_pol_hold(xp);

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
	xfrm_pol_hold(policy);
	policy->next = pol ? pol->next : NULL;
	*p = policy;
	atomic_inc(&flow_cache_genid);
	policy->index = pol ? pol->index : xfrm_gen_index(dir);
	policy->curlft.add_time = (unsigned long)xtime.tv_sec;
	policy->curlft.use_time = 0;
	if (policy->lft.hard_add_expires_seconds &&
	    !mod_timer(&policy->timer, jiffies + HZ))
		xfrm_pol_hold(policy);
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
		atomic_inc(&flow_cache_genid);
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
			atomic_inc(&flow_cache_genid);
		else
			xfrm_pol_hold(pol);
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
	atomic_inc(&flow_cache_genid);
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

void xfrm_policy_lookup(struct flowi *fl, u16 family, u8 dir,
			void **objp, atomic_t **obj_refp)
{
	struct xfrm_policy *pol;

	read_lock_bh(&xfrm_policy_lock);
	for (pol = xfrm_policy_list[dir]; pol; pol = pol->next) {
		struct xfrm_selector *sel = &pol->selector;
		int match;

		if (pol->family != family)
			continue;

		match = xfrm_selector_match(sel, fl, family);
		if (match) {
			xfrm_pol_hold(pol);
			break;
		}
	}
	read_unlock_bh(&xfrm_policy_lock);
	if ((*objp = (void *) pol) != NULL)
		*obj_refp = &pol->refcnt;
}

struct xfrm_policy *xfrm_sk_policy_lookup(struct sock *sk, int dir, struct flowi *fl)
{
	struct xfrm_policy *pol;

	read_lock_bh(&xfrm_policy_lock);
	if ((pol = sk->policy[dir]) != NULL) {
		int match;

		match = xfrm_selector_match(&pol->selector, fl, sk->family);
		if (match)
			xfrm_pol_hold(pol);
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
	xfrm_pol_hold(pol);
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
xfrm_tmpl_resolve(struct xfrm_policy *policy, struct flowi *fl,
		  struct xfrm_state **xfrm,
		  unsigned short family)
{
	int nx;
	int i, error;
	xfrm_address_t *daddr = xfrm_flowi_daddr(fl, family);
	xfrm_address_t *saddr = xfrm_flowi_saddr(fl, family);

	for (nx=0, i = 0; i < policy->xfrm_nr; i++) {
		struct xfrm_state *x;
		xfrm_address_t *remote = daddr;
		xfrm_address_t *local  = saddr;
		struct xfrm_tmpl *tmpl = &policy->xfrm_vec[i];

		if (tmpl->mode) {
			remote = &tmpl->id.daddr;
			local = &tmpl->saddr;
		}

		x = xfrm_state_find(remote, local, fl, tmpl, policy, &error, family);

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

/* Check that the bundle accepts the flow and its components are
 * still valid.
 */

static struct dst_entry *
xfrm_find_bundle(struct flowi *fl, struct rtable *rt, struct xfrm_policy *policy, unsigned short family)
{
	struct dst_entry *x;
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
	if (unlikely(afinfo == NULL))
		return ERR_PTR(-EINVAL);
	x = afinfo->find_bundle(fl, rt, policy);
	xfrm_policy_put_afinfo(afinfo);
	return x;
}

/* Allocate chain of dst_entry's, attach known xfrm's, calculate
 * all the metrics... Shortly, bundle a bundle.
 */

static int
xfrm_bundle_create(struct xfrm_policy *policy, struct xfrm_state **xfrm, int nx,
		   struct flowi *fl, struct dst_entry **dst_p,
		   unsigned short family)
{
	int err;
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);
	if (unlikely(afinfo == NULL))
		return -EINVAL;
	err = afinfo->bundle_create(policy, xfrm, nx, fl, dst_p);
	xfrm_policy_put_afinfo(afinfo);
	return err;
}

static inline int policy_to_flow_dir(int dir)
{
	if (XFRM_POLICY_IN == FLOW_DIR_IN &&
	    XFRM_POLICY_OUT == FLOW_DIR_OUT &&
	    XFRM_POLICY_FWD == FLOW_DIR_FWD)
		return dir;
	switch (dir) {
	default:
	case XFRM_POLICY_IN:
		return FLOW_DIR_IN;
	case XFRM_POLICY_OUT:
		return FLOW_DIR_OUT;
	case XFRM_POLICY_FWD:
		return FLOW_DIR_FWD;
	};
}

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
		/* nothing */;
	}

restart:
	genid = atomic_read(&flow_cache_genid);
	policy = NULL;
	if (sk && sk->policy[1])
		policy = xfrm_sk_policy_lookup(sk, XFRM_POLICY_OUT, fl);

	if (!policy) {
		/* To accelerate a bit...  */
		if ((rt->u.dst.flags & DST_NOXFRM) || !xfrm_policy_list[XFRM_POLICY_OUT])
			return 0;

		policy = flow_cache_lookup(fl, family,
					   policy_to_flow_dir(XFRM_POLICY_OUT),
					   xfrm_policy_lookup);
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
		dst = xfrm_find_bundle(fl, rt, policy, family);
		if (IS_ERR(dst)) {
			xfrm_pol_put(policy);
			return PTR_ERR(dst);
		}

		if (dst)
			break;

		nx = xfrm_tmpl_resolve(policy, fl, xfrm, family);

		if (unlikely(nx<0)) {
			err = nx;
			if (err == -EAGAIN) {
				struct task_struct *tsk = current;
				DECLARE_WAITQUEUE(wait, tsk);
				if (!flags)
					goto error;

				__set_task_state(tsk, TASK_INTERRUPTIBLE);
				add_wait_queue(&km_waitq, &wait);
				err = xfrm_tmpl_resolve(policy, fl, xfrm, family);
				if (err == -EAGAIN)
					schedule();
				__set_task_state(tsk, TASK_RUNNING);
				remove_wait_queue(&km_waitq, &wait);

				if (err == -EAGAIN && signal_pending(current)) {
					err = -ERESTART;
					goto error;
				}
				if (err == -EAGAIN ||
				    genid != atomic_read(&flow_cache_genid))
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
		err = xfrm_bundle_create(policy, xfrm, nx, fl, &dst, family);

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
xfrm_state_ok(struct xfrm_tmpl *tmpl, struct xfrm_state *x, 
	      unsigned short family)
{
	return	x->id.proto == tmpl->id.proto &&
		(x->id.spi == tmpl->id.spi || !tmpl->id.spi) &&
		x->props.mode == tmpl->mode &&
		(tmpl->aalgos & (1<<x->props.aalgo)) &&
		!(x->props.mode && xfrm_state_addr_cmp(tmpl, x, family));
}

static inline int
xfrm_policy_ok(struct xfrm_tmpl *tmpl, struct sec_path *sp, int idx,
	       unsigned short family)
{
	for (; idx < sp->len; idx++) {
		if (xfrm_state_ok(tmpl, sp->x[idx].xvec, family))
			return ++idx;
	}
	return -1;
}

static int
_decode_session(struct sk_buff *skb, struct flowi *fl, unsigned short family)
{
	struct xfrm_policy_afinfo *afinfo = xfrm_policy_get_afinfo(family);

	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;

	afinfo->decode_session(skb, fl);
	xfrm_policy_put_afinfo(afinfo);
	return 0;
}

int __xfrm_policy_check(struct sock *sk, int dir, struct sk_buff *skb, 
			unsigned short family)
{
	struct xfrm_policy *pol;
	struct flowi fl;

	if (_decode_session(skb, &fl, family) < 0)
		return 0;

	/* First, check used SA against their selectors. */
	if (skb->sp) {
		int i;

		for (i=skb->sp->len-1; i>=0; i--) {
		  struct sec_decap_state *xvec = &(skb->sp->x[i]);
			if (!xfrm_selector_match(&xvec->xvec->sel, &fl, family))
				return 0;

			/* If there is a post_input processor, try running it */
			if (xvec->xvec->type->post_input &&
			    (xvec->xvec->type->post_input)(xvec->xvec,
							   &(xvec->decap),
							   skb) != 0)
				return 0;
		}
	}

	pol = NULL;
	if (sk && sk->policy[dir])
		pol = xfrm_sk_policy_lookup(sk, dir, &fl);

	if (!pol)
		pol = flow_cache_lookup(&fl, family,
					policy_to_flow_dir(dir),
					xfrm_policy_lookup);

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
				k = xfrm_policy_ok(pol->xfrm_vec+i, sp, k, family);
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

	if (_decode_session(skb, &fl, family) < 0)
		return 0;

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

int xfrm_policy_register_afinfo(struct xfrm_policy_afinfo *afinfo)
{
	int err = 0;
	if (unlikely(afinfo == NULL))
		return -EINVAL;
	if (unlikely(afinfo->family >= NPROTO))
		return -EAFNOSUPPORT;
	write_lock(&xfrm_policy_afinfo_lock);
	if (unlikely(xfrm_policy_afinfo[afinfo->family] != NULL))
		err = -ENOBUFS;
	else {
		struct dst_ops *dst_ops = afinfo->dst_ops;
		if (likely(dst_ops->kmem_cachep == NULL))
			dst_ops->kmem_cachep = xfrm_dst_cache;
		if (likely(dst_ops->check == NULL))
			dst_ops->check = xfrm_dst_check;
		if (likely(dst_ops->destroy == NULL))
			dst_ops->destroy = xfrm_dst_destroy;
		if (likely(dst_ops->negative_advice == NULL))
			dst_ops->negative_advice = xfrm_negative_advice;
		if (likely(dst_ops->link_failure == NULL))
			dst_ops->link_failure = xfrm_link_failure;
		if (likely(dst_ops->get_mss == NULL))
			dst_ops->get_mss = xfrm_get_mss;
		if (likely(afinfo->garbage_collect == NULL))
			afinfo->garbage_collect = __xfrm_garbage_collect;
		xfrm_policy_afinfo[afinfo->family] = afinfo;
	}
	write_unlock(&xfrm_policy_afinfo_lock);
	return err;
}

int xfrm_policy_unregister_afinfo(struct xfrm_policy_afinfo *afinfo)
{
	int err = 0;
	if (unlikely(afinfo == NULL))
		return -EINVAL;
	if (unlikely(afinfo->family >= NPROTO))
		return -EAFNOSUPPORT;
	write_lock(&xfrm_policy_afinfo_lock);
	if (likely(xfrm_policy_afinfo[afinfo->family] != NULL)) {
		if (unlikely(xfrm_policy_afinfo[afinfo->family] != afinfo))
			err = -EINVAL;
		else {
			struct dst_ops *dst_ops = afinfo->dst_ops;
			xfrm_policy_afinfo[afinfo->family] = NULL;
			dst_ops->kmem_cachep = NULL;
			dst_ops->check = NULL;
			dst_ops->destroy = NULL;
			dst_ops->negative_advice = NULL;
			dst_ops->link_failure = NULL;
			dst_ops->get_mss = NULL;
			afinfo->garbage_collect = NULL;
		}
	}
	write_unlock(&xfrm_policy_afinfo_lock);
	return err;
}

struct xfrm_policy_afinfo *xfrm_policy_get_afinfo(unsigned short family)
{
	struct xfrm_policy_afinfo *afinfo;
	if (unlikely(family >= NPROTO))
		return NULL;
	read_lock(&xfrm_policy_afinfo_lock);
	afinfo = xfrm_policy_afinfo[family];
	if (likely(afinfo != NULL))
		read_lock(&afinfo->lock);
	read_unlock(&xfrm_policy_afinfo_lock);
	return afinfo;
}

void xfrm_policy_put_afinfo(struct xfrm_policy_afinfo *afinfo)
{
	if (unlikely(afinfo == NULL))
		return;
	read_unlock(&afinfo->lock);
}

void __init xfrm_policy_init(void)
{
	xfrm_dst_cache = kmem_cache_create("xfrm_dst_cache",
					   sizeof(struct xfrm_dst),
					   0, SLAB_HWCACHE_ALIGN,
					   NULL, NULL);
	if (!xfrm_dst_cache)
		panic("XFRM: failed to allocate xfrm_dst_cache\n");
}

void __init xfrm_init(void)
{
	xfrm_state_init();
	xfrm_policy_init();
}

