#include <net/xfrm.h>
#include <linux/pfkeyv2.h>

/* Each xfrm_state is linked to three tables:

   1. Hash table by (spi,daddr,ah/esp) to find SA by SPI. (input,ctl)
   2. Hash table by daddr to find what SAs exist for given
      destination/tunnel endpoint. (output)
   3. (optional, NI) Radix tree by _selector_ for the case,
      when we have to find a tunnel mode SA appropriate for given flow,
      but do not know tunnel endpoint. At the moment we do
      not support this and assume that tunnel endpoint is given
      by policy. (output)
 */

static spinlock_t xfrm_state_lock = SPIN_LOCK_UNLOCKED;

#define XFRM_DST_HSIZE		1024

/* Hash table to find appropriate SA towards given target (endpoint
 * of tunnel or destination of transport mode) allowed by selector.
 *
 * Main use is finding SA after policy selected tunnel or transport mode.
 * Also, it can be used by ah/esp icmp error handler to find offending SA.
 */
static struct list_head xfrm_state_bydst[XFRM_DST_HSIZE];
static struct list_head xfrm_state_byspi[XFRM_DST_HSIZE];

wait_queue_head_t *km_waitq;

struct xfrm_state *xfrm_state_alloc(void)
{
	struct xfrm_state *x;

	x = kmalloc(sizeof(struct xfrm_state), GFP_ATOMIC);

	if (x) {
		memset(x, 0, sizeof(struct xfrm_state));
		atomic_set(&x->refcnt, 1);
		INIT_LIST_HEAD(&x->bydst);
		INIT_LIST_HEAD(&x->byspi);
		x->lock = SPIN_LOCK_UNLOCKED;
	}
	return x;
}

void __xfrm_state_destroy(struct xfrm_state *x)
{
	BUG_TRAP(x->km.state == XFRM_STATE_DEAD);
	if (x->aalg)
		kfree(x->aalg);
	if (x->ealg)
		kfree(x->ealg);
	if (x->calg)
		kfree(x->calg);
	if (x->type)
		xfrm_put_type(x->type);
	kfree(x);
}

void xfrm_state_delete(struct xfrm_state *x)
{
	int kill = 0;

	spin_lock_bh(&x->lock);
	if (x->km.state != XFRM_STATE_DEAD) {
		x->km.state = XFRM_STATE_DEAD;
		kill = 1;
		spin_lock(&xfrm_state_lock);
		list_del(&x->bydst);
		atomic_dec(&x->refcnt);
		if (x->id.spi) {
			list_del(&x->byspi);
			atomic_dec(&x->refcnt);
		}
		spin_unlock(&xfrm_state_lock);
	}
	spin_unlock_bh(&x->lock);

	if (kill && x->type)
		x->type->destructor(x);
	wake_up(km_waitq);
}

void xfrm_state_flush(u8 proto)
{
	int i;
	struct xfrm_state *x;

	spin_lock_bh(&xfrm_state_lock);
	for (i = 0; i < XFRM_DST_HSIZE; i++) {
restart:
		list_for_each_entry(x, xfrm_state_bydst+i, bydst) {
			if (!proto || x->id.proto == proto) {
				atomic_inc(&x->refcnt);
				spin_unlock_bh(&xfrm_state_lock);

				xfrm_state_delete(x);
				xfrm_state_put(x);

				spin_lock_bh(&xfrm_state_lock);
				goto restart;
			}
		}
	}
	spin_unlock_bh(&xfrm_state_lock);
	wake_up(km_waitq);
}

struct xfrm_state *
xfrm_state_find(u32 daddr, struct flowi *fl, struct xfrm_tmpl *tmpl, struct xfrm_policy *pol)
{
	unsigned h = ntohl(daddr);
	struct xfrm_state *x;
	int acquire_in_progress = 0;
	int error = 0;

	h = (h ^ (h>>16)) % XFRM_DST_HSIZE;

	spin_lock_bh(&xfrm_state_lock);
	list_for_each_entry(x, xfrm_state_bydst+h, bydst) {
		if (daddr == x->id.daddr.xfrm4_addr &&
		    tmpl->mode == x->props.mode &&
		    tmpl->id.proto == x->id.proto) {
			/* Resolution logic:
			   1. There is a valid state with matching selector.
			      Done.
			   2. Valid state with inappropriate selector. Skip.

			   Entering area of "sysdeps".

			   3. If state is not valid, selector is temporary,
			      it selects only session which triggered
			      previous resolution. Key manager will do
			      something to install a state with proper
			      selector.
			 */
			if (x->km.state == XFRM_STATE_VALID) {
				if (!xfrm4_selector_match(&x->sel, fl))
					continue;
				atomic_inc(&x->refcnt);
				spin_unlock_bh(&xfrm_state_lock);
				return x;
			} else if (x->km.state == XFRM_STATE_ACQ) {
				acquire_in_progress = 1;
			} else if (x->km.state == XFRM_STATE_ERROR ||
				   x->km.state == XFRM_STATE_EXPIRED) {
				if (xfrm4_selector_match(&x->sel, fl))
					error = 1;
			}
		}
	}

	x = NULL;
	if (!error && !acquire_in_progress &&
	    ((x = xfrm_state_alloc()) != NULL)) {
		/* Initialize temporary selector matching only
		 * to current session. */

		x->sel.daddr.xfrm4_addr = fl->fl4_dst;
		x->sel.daddr.xfrm4_mask = ~0;
		x->sel.saddr.xfrm4_addr = fl->fl4_src;
		x->sel.saddr.xfrm4_mask = ~0;
		x->sel.dport = fl->uli_u.ports.dport;
		x->sel.dport_mask = ~0;
		x->sel.sport = fl->uli_u.ports.sport;
		x->sel.sport_mask = ~0;
		x->sel.prefixlen_d = 32;
		x->sel.prefixlen_s = 32;
		x->sel.proto = fl->proto;
		x->sel.ifindex = fl->oif;
		x->id = tmpl->id;
		if (x->id.daddr.xfrm4_addr == 0)
			x->id.daddr = x->sel.daddr;
		x->props.saddr = tmpl->saddr;
		if (x->props.saddr.xfrm4_addr == 0)
			x->props.saddr = x->sel.saddr;
		x->props.mode = tmpl->mode;

		if (km_query(x, tmpl, pol) == 0) {
			x->km.state = XFRM_STATE_ACQ;
			list_add_tail(&x->bydst, xfrm_state_bydst+h);
			atomic_inc(&x->refcnt);
			if (x->id.spi) {
				h = ntohl(x->id.daddr.xfrm4_addr^x->id.spi^x->id.proto);
				h = (h ^ (h>>10) ^ (h>>20)) % XFRM_DST_HSIZE;
				list_add(&x->byspi, xfrm_state_byspi+h);
				atomic_inc(&x->refcnt);
			}
		} else {
			x->km.state = XFRM_STATE_DEAD;
			xfrm_state_put(x);
			x = NULL;
		}
	}
	spin_unlock_bh(&xfrm_state_lock);
	return x;
}

void xfrm_state_insert(struct xfrm_state *x)
{
	unsigned h = ntohl(x->id.daddr.xfrm4_addr);

	h = (h ^ (h>>16)) % XFRM_DST_HSIZE;

	spin_lock_bh(&xfrm_state_lock);
	list_add(&x->bydst, xfrm_state_bydst+h);
	atomic_inc(&x->refcnt);

	h = ntohl(x->id.daddr.xfrm4_addr^x->id.spi^x->id.proto);
	h = (h ^ (h>>10) ^ (h>>20)) % XFRM_DST_HSIZE;
	list_add(&x->byspi, xfrm_state_byspi+h);
	atomic_inc(&x->refcnt);

	spin_unlock_bh(&xfrm_state_lock);
	wake_up(km_waitq);
}

int xfrm_state_check_expire(struct xfrm_state *x)
{
	if (x->km.state != XFRM_STATE_VALID)
		return -EINVAL;

	if (x->lft.hard_byte_limit &&
	    x->curlft.bytes >= x->lft.hard_byte_limit) {
		km_expired(x);
		return -EINVAL;
	}

	if (x->km.warn_bytes &&
	    x->curlft.bytes >= x->km.warn_bytes) {
		x->km.warn_bytes = 0;
		km_warn_expired(x);
	}
	return 0;
}

int xfrm_state_check_space(struct xfrm_state *x, struct sk_buff *skb)
{
	int nhead = x->props.header_len + skb->dst->dev->hard_header_len
		- skb_headroom(skb);

	if (nhead > 0)
		return pskb_expand_head(skb, nhead, 0, GFP_ATOMIC);

	/* Check tail too... */
	return 0;
}

struct xfrm_state *
xfrm_state_lookup(u32 daddr, u32 spi, u8 proto)
{
	unsigned h = ntohl(daddr^spi^proto);
	struct xfrm_state *x;

	h = (h ^ (h>>10) ^ (h>>20)) % XFRM_DST_HSIZE;

	spin_lock_bh(&xfrm_state_lock);
	list_for_each_entry(x, xfrm_state_byspi+h, byspi) {
		if (spi == x->id.spi &&
		    daddr == x->id.daddr.xfrm4_addr &&
		    proto == x->id.proto) {
			atomic_inc(&x->refcnt);
			spin_unlock_bh(&xfrm_state_lock);
			return x;
		}
	}
	spin_unlock_bh(&xfrm_state_lock);
	return NULL;
}

struct xfrm_state *
xfrm_find_acq(u8 mode, u16 reqid, u8 proto, u32 daddr, u32 saddr)
{
	struct xfrm_state *x, *x0;
	unsigned h = ntohl(daddr);

	h = (h ^ (h>>16)) % XFRM_DST_HSIZE;
	x0 = NULL;

	spin_lock_bh(&xfrm_state_lock);
	list_for_each_entry(x, xfrm_state_bydst+h, bydst) {
		if (daddr == x->id.daddr.xfrm4_addr &&
		    mode == x->props.mode &&
		    proto == x->id.proto &&
		    saddr == x->props.saddr.xfrm4_addr &&
		    (!reqid || reqid == x->props.reqid)) {
			    if (!x0)
				    x0 = x;
			    if (x->km.state != XFRM_STATE_ACQ)
				    continue;
			    x0 = x;
			    break;
		    }
	}
	if (x0) {
		atomic_inc(&x0->refcnt);
	} else if ((x0 = xfrm_state_alloc()) != NULL) {
		x0->sel.daddr.xfrm4_addr = daddr;
		x0->sel.daddr.xfrm4_mask = ~0;
		x0->sel.saddr.xfrm4_addr = saddr;
		x0->sel.saddr.xfrm4_mask = ~0;
		x0->sel.prefixlen_d = 32;
		x0->sel.prefixlen_s = 32;
		x0->props.saddr.xfrm4_addr = saddr;
		x0->km.state = XFRM_STATE_ACQ;
		x0->id.daddr.xfrm4_addr = daddr;
		x0->id.proto = proto;
		x0->props.mode = mode;
		x0->props.reqid = reqid;
		atomic_inc(&x0->refcnt);
		list_add_tail(&x0->bydst, xfrm_state_bydst+h);
		wake_up(km_waitq);
	}
	spin_unlock_bh(&xfrm_state_lock);
	return x0;
}

/* Silly enough, but I'm lazy to build resolution list */

struct xfrm_state * xfrm_find_acq_byseq(u32 seq)
{
	int i;
	struct xfrm_state *x;

	spin_lock_bh(&xfrm_state_lock);
	for (i = 0; i < XFRM_DST_HSIZE; i++) {
		list_for_each_entry(x, xfrm_state_bydst+i, bydst) {
			if (x->km.seq == seq) {
				atomic_inc(&x->refcnt);
				spin_unlock_bh(&xfrm_state_lock);
				return x;
			}
		}
	}
	spin_unlock_bh(&xfrm_state_lock);
	return NULL;
}


void
xfrm_alloc_spi(struct xfrm_state *x, u32 minspi, u32 maxspi)
{
	u32 h;
	struct xfrm_state *x0;

	if (x->id.spi)
		return;

	if (minspi == maxspi) {
		x0 = xfrm_state_lookup(x->id.daddr.xfrm4_addr, minspi, x->id.proto);
		if (x0) {
			xfrm_state_put(x0);
			return;
		}
		x->id.spi = minspi;
	} else {
		u32 spi = 0;
		minspi = ntohl(minspi);
		maxspi = ntohl(maxspi);
		for (h=0; h<maxspi-minspi+1; h++) {
			spi = minspi + net_random()%(maxspi-minspi+1);
			x0 = xfrm_state_lookup(x->id.daddr.xfrm4_addr, htonl(spi), x->id.proto);
			if (x0 == NULL)
				break;
			xfrm_state_put(x0);
		}
		x->id.spi = htonl(spi);
	}
	if (x->id.spi) {
		spin_lock_bh(&xfrm_state_lock);
		h = ntohl(x->id.daddr.xfrm4_addr^x->id.spi^x->id.proto);
		h = (h ^ (h>>10) ^ (h>>20)) % XFRM_DST_HSIZE;
		list_add(&x->byspi, xfrm_state_byspi+h);
		atomic_inc(&x->refcnt);
		spin_unlock_bh(&xfrm_state_lock);
		wake_up(km_waitq);
	}
}

int xfrm_state_walk(u8 proto, int (*func)(struct xfrm_state *, int, void*),
		    void *data)
{
	int i;
	struct xfrm_state *x;
	int count = 0;
	int err = 0;

	spin_lock_bh(&xfrm_state_lock);
	for (i = 0; i < XFRM_DST_HSIZE; i++) {
		list_for_each_entry(x, xfrm_state_bydst+i, bydst) {
			if (proto == 255 || x->id.proto == proto)
				count++;
		}
	}
	if (count == 0) {
		err = -ENOENT;
		goto out;
	}

	for (i = 0; i < XFRM_DST_HSIZE; i++) {
		list_for_each_entry(x, xfrm_state_bydst+i, bydst) {
			if (proto != 255 && x->id.proto != proto)
				continue;
			err = func(x, --count, data);
			if (err)
				goto out;
		}
	}
out:
	spin_unlock_bh(&xfrm_state_lock);
	return err;
}


int xfrm_replay_check(struct xfrm_state *x, u32 seq)
{
	u32 diff;

	seq = ntohl(seq);

	if (unlikely(seq == 0))
		return -EINVAL;

	if (likely(seq > x->replay.seq))
		return 0;

	diff = x->replay.seq - seq;
	if (diff >= x->props.replay_window) {
		x->stats.replay_window++;
		return -EINVAL;
	}

	if (x->replay.bitmap & (1U << diff)) {
		x->stats.replay++;
		return -EINVAL;
	}
	return 0;
}

void xfrm_replay_advance(struct xfrm_state *x, u32 seq)
{
	u32 diff;

	seq = ntohl(seq);

	if (seq > x->replay.seq) {
		diff = seq - x->replay.seq;
		if (diff < x->props.replay_window)
			x->replay.bitmap = ((x->replay.bitmap) << diff) | 1;
		else
			x->replay.bitmap = 1;
		x->replay.seq = seq;
	} else {
		diff = x->replay.seq - seq;
		x->replay.bitmap |= (1U << diff);
	}
}

int xfrm_check_selectors(struct xfrm_state **x, int n, struct flowi *fl)
{
	int i;

	for (i=0; i<n; i++) {
		if (!xfrm4_selector_match(&x[i]->sel, fl))
			return -EINVAL;
	}
	return 0;
}

static struct list_head xfrm_km_list = LIST_HEAD_INIT(xfrm_km_list);
static rwlock_t		xfrm_km_lock = RW_LOCK_UNLOCKED;

void km_warn_expired(struct xfrm_state *x)
{
	struct xfrm_mgr *km;

	read_lock(&xfrm_km_lock);
	list_for_each_entry(km, &xfrm_km_list, list)
		km->notify(x, 0);
	read_unlock(&xfrm_km_lock);
}

void km_expired(struct xfrm_state *x)
{
	struct xfrm_mgr *km;

	x->km.state = XFRM_STATE_EXPIRED;

	read_lock(&xfrm_km_lock);
	list_for_each_entry(km, &xfrm_km_list, list)
		km->notify(x, 1);
	read_unlock(&xfrm_km_lock);
}

int km_query(struct xfrm_state *x, struct xfrm_tmpl *t, struct xfrm_policy *pol)
{
	int err = -EINVAL;
	struct xfrm_mgr *km;

	read_lock(&xfrm_km_lock);
	list_for_each_entry(km, &xfrm_km_list, list) {
		err = km->acquire(x, t, pol, XFRM_POLICY_OUT);
		if (!err)
			break;
	}
	read_unlock(&xfrm_km_lock);
	return err;
}

int xfrm_user_policy(struct sock *sk, int optname, u8 *optval, int optlen)
{
	int err;
	u8 *data;
	struct xfrm_mgr *km;
	struct xfrm_policy *pol = NULL;

	if (optlen <= 0 || optlen > PAGE_SIZE)
		return -EMSGSIZE;

	data = kmalloc(optlen, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	err = -EFAULT;
	if (copy_from_user(data, optval, optlen))
		goto out;

	err = -EINVAL;
	read_lock(&xfrm_km_lock);
	list_for_each_entry(km, &xfrm_km_list, list) {
		pol = km->compile_policy(optname, data, optlen, &err);
		if (err >= 0)
			break;
	}
	read_unlock(&xfrm_km_lock);

	if (err >= 0) {
		xfrm_sk_policy_insert(sk, err, pol);
		err = 0;
	}

out:
	kfree(data);
	return err;
}

int xfrm_register_km(struct xfrm_mgr *km)
{
	write_lock_bh(&xfrm_km_lock);
	list_add_tail(&km->list, &xfrm_km_list);
	write_unlock_bh(&xfrm_km_lock);
	return 0;
}

int xfrm_unregister_km(struct xfrm_mgr *km)
{
	write_lock_bh(&xfrm_km_lock);
	list_del(&km->list);
	write_unlock_bh(&xfrm_km_lock);
	return 0;
}

void __init xfrm_state_init(void)
{
	int i;

	for (i=0; i<XFRM_DST_HSIZE; i++) {
		INIT_LIST_HEAD(&xfrm_state_bydst[i]);
		INIT_LIST_HEAD(&xfrm_state_byspi[i]);
	}
}
