/*
 * xfrm6_input.c: based on net/ipv4/xfrm4_input.c
 *
 * Authors:
 *	Mitsuru KANDA @USAGI
 * 	Kazunori MIYAZAWA @USAGI
 * 	Kunihiro Ishiguro
 *	YOSHIFUJI Hideaki @USAGI
 *		IPv6 support
 */

#include <net/ip.h>
#include <net/ipv6.h>
#include <net/xfrm.h>

static kmem_cache_t *secpath_cachep;

int xfrm6_rcv(struct sk_buff **pskb, unsigned int *nhoffp)
{
	struct sk_buff *skb = *pskb;
	int err;
	u32 spi, seq;
	struct sec_decap_state xfrm_vec[XFRM_MAX_DEPTH];
	struct xfrm_state *x;
	int xfrm_nr = 0;
	int decaps = 0;
	int nexthdr = 0;
	u8 *prevhdr = NULL;

	ip6_find_1stfragopt(skb, &prevhdr);
	nexthdr = *prevhdr;
	*nhoffp = prevhdr - skb->nh.raw;

	if ((err = xfrm_parse_spi(skb, nexthdr, &spi, &seq)) != 0)
		goto drop;
	
	do {
		struct ipv6hdr *iph = skb->nh.ipv6h;

		if (xfrm_nr == XFRM_MAX_DEPTH)
			goto drop;

		x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, spi, nexthdr, AF_INET6);
		if (x == NULL)
			goto drop;
		spin_lock(&x->lock);
		if (unlikely(x->km.state != XFRM_STATE_VALID))
			goto drop_unlock;

		if (x->props.replay_window && xfrm_replay_check(x, seq))
			goto drop_unlock;

		if (xfrm_state_check_expire(x))
			goto drop_unlock;

		nexthdr = x->type->input(x, &(xfrm_vec[xfrm_nr].decap), skb);
		if (nexthdr <= 0)
			goto drop_unlock;

		if (x->props.replay_window)
			xfrm_replay_advance(x, seq);

		x->curlft.bytes += skb->len;
		x->curlft.packets++;

		spin_unlock(&x->lock);

		xfrm_vec[xfrm_nr++].xvec = x;

		if (x->props.mode) { /* XXX */
			if (nexthdr != IPPROTO_IPV6)
				goto drop;
			skb->nh.raw = skb->data;
			iph = skb->nh.ipv6h;
			decaps = 1;
			break;
		}

		if ((err = xfrm_parse_spi(skb, nexthdr, &spi, &seq)) < 0)
			goto drop;
	} while (!err);

	/* Allocate new secpath or COW existing one. */
	if (!skb->sp || atomic_read(&skb->sp->refcnt) != 1) {
		kmem_cache_t *pool = skb->sp ? skb->sp->pool : secpath_cachep;
		struct sec_path *sp;
		sp = kmem_cache_alloc(pool, SLAB_ATOMIC);
		if (!sp)
			goto drop;
		if (skb->sp) {
			memcpy(sp, skb->sp, sizeof(struct sec_path));
			secpath_put(skb->sp);
		} else {
			sp->pool = pool;
			sp->len = 0;
		}
		atomic_set(&sp->refcnt, 1);
		skb->sp = sp;
	}

	if (xfrm_nr + skb->sp->len > XFRM_MAX_DEPTH)
		goto drop;

	memcpy(skb->sp->x+skb->sp->len, xfrm_vec, xfrm_nr*sizeof(struct sec_decap_state));
	skb->sp->len += xfrm_nr;
	skb->ip_summed = CHECKSUM_NONE;

	if (decaps) {
		if (!(skb->dev->flags&IFF_LOOPBACK)) {
			dst_release(skb->dst);
			skb->dst = NULL;
		}
		netif_rx(skb);
		return -1;
	} else {
		return 1;
	}

drop_unlock:
	spin_unlock(&x->lock);
	xfrm_state_put(x);
drop:
	while (--xfrm_nr >= 0)
		xfrm_state_put(xfrm_vec[xfrm_nr].xvec);
	kfree_skb(skb);
	return -1;
}

void __init xfrm6_input_init(void)
{
	secpath_cachep = kmem_cache_create("secpath6_cache",
					   sizeof(struct sec_path),
					   0, SLAB_HWCACHE_ALIGN,
					   NULL, NULL);

	if (!secpath_cachep)
		panic("IPv6: failed to allocate secpath6_cache\n");
}

