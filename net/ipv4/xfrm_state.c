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

spinlock_t xfrm_state_lock = SPIN_LOCK_UNLOCKED;

/* Hash table to find appropriate SA towards given target (endpoint
 * of tunnel or destination of transport mode) allowed by selector.
 *
 * Main use is finding SA after policy selected tunnel or transport mode.
 * Also, it can be used by ah/esp icmp error handler to find offending SA.
 */
struct list_head xfrm_state_bydst[XFRM_DST_HSIZE];
struct list_head xfrm_state_byspi[XFRM_DST_HSIZE];

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
	if (x->type)
		x->type->destructor(x);
	kfree(x);
}

struct xfrm_state *
xfrm_state_find(u32 daddr, struct flowi *fl, struct xfrm_tmpl *tmpl)
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
			} else if (x->km.state == XFRM_STATE_ERROR) {
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

		if (km_query(x) == 0) {
			list_add_tail(&x->bydst, xfrm_state_bydst+h);
			atomic_inc(&x->refcnt);
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
}

int xfrm_state_check_expire(struct xfrm_state *x)
{
	if (x->km.state != XFRM_STATE_VALID)
		return -EINVAL;

	if (x->props.hard_byte_limit &&
	    x->stats.bytes >= x->props.hard_byte_limit) {
		km_notify(x, SADB_EXT_LIFETIME_HARD);
		return -EINVAL;
	}

	if (x->km.warn_bytes &&
	    x->stats.bytes >= x->km.warn_bytes) {
		x->km.warn_bytes = 0;
		km_notify(x, SADB_EXT_LIFETIME_SOFT);
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
			x->stats.lastuse = xtime.tv_sec;
			spin_unlock_bh(&xfrm_state_lock);
			return x;
		}
	}
	spin_unlock_bh(&xfrm_state_lock);
	return NULL;
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

void km_notify(struct xfrm_state *x, int event)
{
}

int km_query(struct xfrm_state *x)
{
	return -EINVAL;
}

void __init xfrm_state_init(void)
{
	int i;

	for (i=0; i<XFRM_DST_HSIZE; i++) {
		INIT_LIST_HEAD(&xfrm_state_bydst[i]);
		INIT_LIST_HEAD(&xfrm_state_byspi[i]);
	}
}
