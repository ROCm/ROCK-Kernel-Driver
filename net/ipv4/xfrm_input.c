#include <net/ip.h>
#include <net/xfrm.h>

static kmem_cache_t *secpath_cachep;

void __secpath_destroy(struct sec_path *sp)
{
	int i;
	for (i = 0; i < sp->len; i++)
		xfrm_state_put(sp->xvec[i]);
	kmem_cache_free(secpath_cachep, sp);
}

/* Fetch spi and seq frpm ipsec header */

static int xfrm_parse_spi(struct sk_buff *skb, u32 *spi, u32 *seq)
{
	int offset, offset_seq;

	switch (skb->nh.iph->protocol) {
	case IPPROTO_AH:
		offset = offsetof(struct ip_auth_hdr, spi);
		offset_seq = offsetof(struct ip_auth_hdr, seq_no);
		break;
	case IPPROTO_ESP:
		offset = offsetof(struct ip_esp_hdr, spi);
		offset_seq = offsetof(struct ip_esp_hdr, seq_no);
		break;
	case IPPROTO_COMP:
		if (!pskb_may_pull(skb, 4))
			return -EINVAL;
		*spi = *(u16*)(skb->h.raw + 2);
		*seq = 0;
		return 0;
	default:
		return 1;
	}

	if (!pskb_may_pull(skb, 16))
		return -EINVAL;

	*spi = *(u32*)(skb->h.raw + offset);
	*seq = *(u32*)(skb->h.raw + offset_seq);
	return 0;
}



int xfrm4_rcv(struct sk_buff *skb)
{
	int err;
	u32 spi, seq;
	struct xfrm_state *xfrm_vec[XFRM_MAX_DEPTH];
	struct xfrm_state *x;
	int xfrm_nr = 0;
	int decaps = 0;

	if ((err = xfrm_parse_spi(skb, &spi, &seq)) != 0)
		goto drop;

	do {
		struct iphdr *iph = skb->nh.iph;

		if (xfrm_nr == XFRM_MAX_DEPTH)
			goto drop;

		x = xfrm_state_lookup(iph->daddr, spi, iph->protocol);
		if (x == NULL)
			goto drop;

		spin_lock(&x->lock);
		if (unlikely(x->km.state != XFRM_STATE_VALID))
			goto drop_unlock;

		if (x->props.replay_window && xfrm_replay_check(x, seq))
			goto drop_unlock;

		if (x->type->input(x, skb))
			goto drop_unlock;

		if (x->props.replay_window)
			xfrm_replay_advance(x, seq);

		spin_unlock(&x->lock);

		xfrm_vec[xfrm_nr++] = x;

		iph = skb->nh.iph;

		if (x->props.mode) {
			if (iph->protocol != IPPROTO_IP)
				goto drop;
			skb->nh.raw = skb->data;
			iph = skb->nh.iph;
			memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));
			decaps = 1;
			break;
		}

		if ((err = xfrm_parse_spi(skb, &spi, &seq)) < 0)
			goto drop;
	} while (!err);

	/* Allocate new secpath or COW existing one. */

	if (!skb->sp || atomic_read(&skb->sp->refcnt) != 1) {
		struct sec_path *sp;
		sp = kmem_cache_alloc(secpath_cachep, SLAB_ATOMIC);
		if (!sp)
			goto drop;
		if (skb->sp) {
			memcpy(sp, skb->sp, sizeof(struct sec_path));
			secpath_put(skb->sp);
		} else
			sp->len = 0;
		atomic_set(&sp->refcnt, 1);
		skb->sp = sp;
	}
	if (xfrm_nr + skb->sp->len > XFRM_MAX_DEPTH)
		goto drop;

	memcpy(skb->sp->xvec+skb->sp->len, xfrm_vec, xfrm_nr*sizeof(void*));
	skb->sp->len += xfrm_nr;

	if (decaps) {
		if (!(skb->dev->flags&IFF_LOOPBACK)) {
			dst_release(skb->dst);
			skb->dst = NULL;
		}
		netif_rx(skb);
		return 0;
	} else {
		return -skb->nh.iph->protocol;
	}

drop_unlock:
	spin_unlock(&x->lock);
	xfrm_state_put(x);
drop:
	while (--xfrm_nr >= 0)
		xfrm_state_put(xfrm_vec[xfrm_nr]);
	kfree_skb(skb);
	return 0;
}


void __init xfrm_input_init(void)
{
	secpath_cachep = kmem_cache_create("secpath_cache",
					   sizeof(struct sec_path),
					   0, SLAB_HWCACHE_ALIGN,
					   NULL, NULL);

	if (!secpath_cachep)
		panic("IP: failed to allocate secpath_cache\n");
}
