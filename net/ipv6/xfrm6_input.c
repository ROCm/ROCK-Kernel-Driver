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

static int zero_out_mutable_opts(struct ipv6_opt_hdr *opthdr)
{
	u8 *opt = (u8 *)opthdr;
	int len = ipv6_optlen(opthdr);
	int off = 0;
	int optlen = 0;

	off += 2;
	len -= 2;

	while (len > 0) {

		switch (opt[off]) {

		case IPV6_TLV_PAD0:
			optlen = 1;
			break;
		default:
			if (len < 2) 
				goto bad;
			optlen = opt[off+1]+2;
			if (len < optlen)
				goto bad;
			if (opt[off] & 0x20)
				memset(&opt[off+2], 0, opt[off+1]);
			break;
		}

		off += optlen;
		len -= optlen;
	}
	if (len == 0)
		return 1;

bad:
	return 0;
}

int xfrm6_clear_mutable_options(struct sk_buff *skb, u16 *nh_offset, int dir)
{
	u16 offset = sizeof(struct ipv6hdr);
	struct ipv6_opt_hdr *exthdr = (struct ipv6_opt_hdr*)(skb->nh.raw + offset);
	unsigned int packet_len = skb->tail - skb->nh.raw;
	u8 nexthdr = skb->nh.ipv6h->nexthdr;
	u8 nextnexthdr = 0;

	*nh_offset = ((unsigned char *)&skb->nh.ipv6h->nexthdr) - skb->nh.raw;

	while (offset + 1 <= packet_len) {

		switch (nexthdr) {

		case NEXTHDR_HOP:
			*nh_offset = offset;
			offset += ipv6_optlen(exthdr);
			if (!zero_out_mutable_opts(exthdr)) {
				if (net_ratelimit())
					printk(KERN_WARNING "overrun hopopts\n"); 
				return 0;
			}
			nexthdr = exthdr->nexthdr;
			exthdr = (struct ipv6_opt_hdr*)(skb->nh.raw + offset);
			break;

		case NEXTHDR_ROUTING:
			*nh_offset = offset;
			offset += ipv6_optlen(exthdr);
			((struct ipv6_rt_hdr*)exthdr)->segments_left = 0; 
			nexthdr = exthdr->nexthdr;
			exthdr = (struct ipv6_opt_hdr*)(skb->nh.raw + offset);
			break;

		case NEXTHDR_DEST:
			*nh_offset = offset;
			offset += ipv6_optlen(exthdr);
			if (!zero_out_mutable_opts(exthdr))  {
				if (net_ratelimit())
					printk(KERN_WARNING "overrun destopt\n"); 
				return 0;
			}
			nexthdr = exthdr->nexthdr;
			exthdr = (struct ipv6_opt_hdr*)(skb->nh.raw + offset);
			break;

		case NEXTHDR_AUTH:
			if (dir == XFRM_POLICY_OUT) {
				memset(((struct ipv6_auth_hdr*)exthdr)->auth_data, 0, 
				       (((struct ipv6_auth_hdr*)exthdr)->hdrlen - 1) << 2);
			}
			if (exthdr->nexthdr == NEXTHDR_DEST) {
				offset += (((struct ipv6_auth_hdr*)exthdr)->hdrlen + 2) << 2;
				exthdr = (struct ipv6_opt_hdr*)(skb->nh.raw + offset);
				nextnexthdr = exthdr->nexthdr;
				if (!zero_out_mutable_opts(exthdr)) {
					if (net_ratelimit())
						printk(KERN_WARNING "overrun destopt\n");
					return 0;
				}
			}
			return nexthdr;
		default :
			return nexthdr;
		}
	}

	return nexthdr;
}

int xfrm6_rcv(struct sk_buff **pskb, unsigned int *nhoffp)
{
	struct sk_buff *skb = *pskb;
	int err;
	u32 spi, seq;
	struct sec_decap_state xfrm_vec[XFRM_MAX_DEPTH];
	struct xfrm_state *x;
	int xfrm_nr = 0;
	int decaps = 0;
	struct ipv6hdr *hdr = skb->nh.ipv6h;
	unsigned char *tmp_hdr = NULL;
	int hdr_len = 0;
	u16 nh_offset = 0;
	int nexthdr = 0;

	nh_offset = ((unsigned char*)&skb->nh.ipv6h->nexthdr) - skb->nh.raw;
	hdr_len = sizeof(struct ipv6hdr);

	tmp_hdr = kmalloc(hdr_len, GFP_ATOMIC);
	if (!tmp_hdr)
		goto drop;
	memcpy(tmp_hdr, skb->nh.raw, hdr_len);

	nexthdr = xfrm6_clear_mutable_options(skb, &nh_offset, XFRM_POLICY_IN);
	hdr->priority    = 0;
	hdr->flow_lbl[0] = 0;
	hdr->flow_lbl[1] = 0;
	hdr->flow_lbl[2] = 0;
	hdr->hop_limit   = 0;

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

		iph = skb->nh.ipv6h;

		if (x->props.mode) { /* XXX */
			if (iph->nexthdr != IPPROTO_IPV6)
				goto drop;
			skb->nh.raw = skb->data;
			iph = skb->nh.ipv6h;
			decaps = 1;
			break;
		}

		if ((err = xfrm_parse_spi(skb, nexthdr, &spi, &seq)) < 0)
			goto drop;
	} while (!err);

	if (!decaps) {
		memcpy(skb->nh.raw, tmp_hdr, hdr_len);
		skb->nh.raw[nh_offset] = nexthdr;
		skb->nh.ipv6h->payload_len = htons(hdr_len + skb->len - sizeof(struct ipv6hdr));
	}

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
		*nhoffp = nh_offset;
		return 1;
	}

drop_unlock:
	spin_unlock(&x->lock);
	xfrm_state_put(x);
drop:
	if (tmp_hdr) kfree(tmp_hdr);
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

