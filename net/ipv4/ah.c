#include <linux/config.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <net/icmp.h>
#include <asm/scatterlist.h>

struct ah_data
{
	u8			*key;
	int			key_len;
	u8			*work_digest;
	int			digest_len;

	void			(*digest)(struct ah_data*,
					  struct sk_buff *skb,
					  u8 *digest);

	struct crypto_tfm	*tfm;
};


/* Clear mutable options and find final destination to substitute
 * into IP header for digest calculation. Options are already checked
 * for validity, so paranoia is not required. */

int ip_clear_mutable_options(struct iphdr *iph, u32 *daddr)
{
	unsigned char * optptr = (unsigned char*)(iph+1);
	int  l = iph->ihl*4 - 20;
	int  optlen;

	while (l > 0) {
		switch (*optptr) {
		case IPOPT_END:
			return 0;
		case IPOPT_NOOP:
			l--;
			optptr++;
			continue;
		}
		optlen = optptr[1];
		if (optlen<2 || optlen>l)
			return -EINVAL;
		switch (*optptr) {
		case IPOPT_SEC:
		case 0x85:	/* Some "Extended Security" crap. */
		case 0x86:	/* Another "Commercial Security" crap. */
		case IPOPT_RA:
		case 0x80|21:	/* RFC1770 */
			break;
		case IPOPT_LSRR:
		case IPOPT_SSRR:
			if (optlen < 6)
				return -EINVAL;
			memcpy(daddr, optptr+optlen-4, 4);
			/* Fall through */
		default:
			memset(optptr+2, 0, optlen-2);
		}
		l -= optlen;
		optptr += optlen;
	}
	return 0;
}

void skb_ah_walk(const struct sk_buff *skb, struct crypto_tfm *tfm)
{
	int offset = 0;
	int len = skb->len;
	int start = skb->len - skb->data_len;
	int i, copy = start - offset;
	struct scatterlist sg;

	/* Checksum header. */
	if (copy > 0) {
		if (copy > len)
			copy = len;
		
		sg.page = virt_to_page(skb->data + offset);
		sg.offset = (unsigned long)(skb->data + offset) % PAGE_SIZE;
		sg.length = copy;
		
		crypto_hmac_update(tfm, &sg, 1);
		
		if ((len -= copy) == 0)
			return;
		offset += copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;

		BUG_TRAP(start <= offset + len);

		end = start + skb_shinfo(skb)->frags[i].size;
		if ((copy = end - offset) > 0) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			if (copy > len)
				copy = len;
			
			sg.page = frag->page;
			sg.offset = frag->page_offset + offset-start;
			sg.length = copy;
			
			crypto_hmac_update(tfm, &sg, 1);
			
			if (!(len -= copy))
				return;
			offset += copy;
		}
		start = end;
	}

	if (skb_shinfo(skb)->frag_list) {
		struct sk_buff *list = skb_shinfo(skb)->frag_list;

		for (; list; list = list->next) {
			int end;

			BUG_TRAP(start <= offset + len);

			end = start + list->len;
			if ((copy = end - offset) > 0) {
				if (copy > len)
					copy = len;
				skb_ah_walk(list, tfm);
				if ((len -= copy) == 0)
					return;
				offset += copy;
			}
			start = end;
		}
	}
	if (len)
		BUG();
}

static void
ah_hmac_digest(struct ah_data *ahp, struct sk_buff *skb, u8 *auth_data)
{
	struct crypto_tfm *tfm = ahp->tfm;

	memset(auth_data, 0, ahp->digest_len);
 	crypto_hmac_init(tfm, ahp->key, &ahp->key_len);
  	skb_ah_walk(skb, tfm);
	crypto_hmac_final(tfm, ahp->key, &ahp->key_len, ahp->work_digest);
	memcpy(auth_data, ahp->work_digest, ahp->digest_len);
}

int ah_output(struct sk_buff *skb)
{
	int err;
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x  = dst->xfrm;
	struct iphdr *iph, *top_iph;
	struct ip_auth_hdr *ah;
	struct ah_data *ahp;
	union {
		struct iphdr	iph;
		char 		buf[60];
	} tmp_iph;

	if (skb->ip_summed == CHECKSUM_HW && skb_checksum_help(skb) == NULL)
		return -EINVAL;

	spin_lock_bh(&x->lock);
	if ((err = xfrm_state_check_expire(x)) != 0)
		goto error;
	if ((err = xfrm_state_check_space(x, skb)) != 0)
		goto error;

	iph = skb->nh.iph;
	if (x->props.mode) {
		top_iph = (struct iphdr*)skb_push(skb, x->props.header_len);
		top_iph->ihl = 5;
		top_iph->version = 4;
		top_iph->tos = 0;
		top_iph->tot_len = htons(skb->len);
		top_iph->frag_off = 0;
		if (!(iph->frag_off&htons(IP_DF)))
			__ip_select_ident(top_iph, dst, 0);
		top_iph->ttl = 0;
		top_iph->protocol = IPPROTO_AH;
		top_iph->check = 0;
		top_iph->saddr = x->props.saddr.xfrm4_addr;
		top_iph->daddr = x->id.daddr.xfrm4_addr;
		ah = (struct ip_auth_hdr*)(top_iph+1);
		ah->nexthdr = IPPROTO_IPIP;
	} else {
		memcpy(&tmp_iph, skb->data, iph->ihl*4);
		top_iph = (struct iphdr*)skb_push(skb, x->props.header_len);
		memcpy(top_iph, &tmp_iph, iph->ihl*4);
		iph = &tmp_iph.iph;
		top_iph->tos = 0;
		top_iph->tot_len = htons(skb->len);
		top_iph->frag_off = 0;
		top_iph->ttl = 0;
		top_iph->protocol = IPPROTO_AH;
		top_iph->check = 0;
		if (top_iph->ihl != 5) {
			err = ip_clear_mutable_options(top_iph, &top_iph->daddr);
			if (err)
				goto error;
		}
		ah = (struct ip_auth_hdr*)((char*)top_iph+iph->ihl*4);
		ah->nexthdr = iph->protocol;
	}
	ahp = x->data;
	ah->hdrlen  = (((ahp->digest_len + 12 + 7)&~7)>>2)-2;
	ah->reserved = 0;
	ah->spi = x->id.spi;
	ah->seq_no = htonl(++x->replay.oseq);
	ahp->digest(ahp, skb, ah->auth_data);
	top_iph->tos = iph->tos;
	top_iph->ttl = iph->ttl;
	if (x->props.mode) {
		top_iph->frag_off = iph->frag_off&~htons(IP_MF|IP_OFFSET);
		memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));
	} else {
		top_iph->frag_off = iph->frag_off;
		top_iph->daddr = iph->daddr;
		if (iph->ihl != 5)
			memcpy(top_iph+1, iph+1, iph->ihl*5 - 20);
	}
	ip_send_check(top_iph);

	skb->nh.raw = skb->data;

	x->curlft.bytes += skb->len;
	x->curlft.packets++;
	spin_unlock_bh(&x->lock);
	if ((skb->dst = dst_pop(dst)) == NULL)
		goto error_nolock;
	return NET_XMIT_BYPASS;

error:
	spin_unlock_bh(&x->lock);
error_nolock:
	kfree_skb(skb);
	return err;
}

int ah_input(struct xfrm_state *x, struct sk_buff *skb)
{
	struct iphdr *iph;
	struct ip_auth_hdr *ah;
	struct ah_data *ahp;
	char work_buf[60];

	if (!pskb_may_pull(skb, sizeof(struct ip_auth_hdr)))
		goto out;

	ah = (struct ip_auth_hdr*)skb->data;

	ahp = x->data;

	if (((ah->hdrlen+2)<<2) != ((ahp->digest_len + 12 + 7)&~7))
		goto out;

	if (!pskb_may_pull(skb, (ah->hdrlen+2)<<2))
		goto out;

	/* We are going to _remove_ AH header to keep sockets happy,
	 * so... Later this can change. */
	if (skb_cloned(skb) &&
	    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
		goto out;

	ah = (struct ip_auth_hdr*)skb->data;
	iph = skb->nh.iph;

	memcpy(work_buf, iph, iph->ihl*4);

	iph->ttl = 0;
	iph->tos = 0;
	iph->frag_off = 0;
	iph->check = 0;
	if (iph->ihl != 5) {
		u32 dummy;
		if (ip_clear_mutable_options(iph, &dummy))
			goto out;
	}
        {
		u8 auth_data[ahp->digest_len];
		memcpy(auth_data, ah->auth_data, ahp->digest_len);
		skb_push(skb, skb->data - skb->nh.raw);
		ahp->digest(ahp, skb, ah->auth_data);
		if (memcmp(ah->auth_data, auth_data, ahp->digest_len)) {
			x->stats.integrity_failed++;
			goto out;
		}
	}
	((struct iphdr*)work_buf)->protocol = ah->nexthdr;
	skb->nh.raw = skb_pull(skb, (ah->hdrlen+2)<<2);
	memcpy(skb->nh.raw, work_buf, iph->ihl*4);
	skb->nh.iph->tot_len = htons(skb->len);
	skb_pull(skb, skb->nh.iph->ihl*4);
	skb->h.raw = skb->data;

	return 0;

out:
	return -EINVAL;
}

void ah4_err(struct sk_buff *skb, u32 info)
{
	struct iphdr *iph = (struct iphdr*)skb->data;
	struct ip_auth_hdr *ah = (struct ip_auth_hdr*)(skb->data+(iph->ihl<<2));
	struct xfrm_state *x;

	if (skb->h.icmph->type != ICMP_DEST_UNREACH ||
	    skb->h.icmph->code != ICMP_FRAG_NEEDED)
		return;

	x = xfrm_state_lookup(iph->daddr, ah->spi, IPPROTO_AH);
	if (!x)
		return;
	printk(KERN_DEBUG "pmtu discvovery on SA AH/%08x/%08x\n",
	       ntohl(ah->spi), ntohl(iph->daddr));
	xfrm_state_put(x);
}

int ah_init_state(struct xfrm_state *x, void *args)
{
	struct ah_data *ahp = NULL;

	if (x->aalg == NULL || x->aalg->alg_key_len == 0 ||
	    x->aalg->alg_key_len > 512)
		goto error;

	ahp = kmalloc(sizeof(*ahp), GFP_KERNEL);
	if (ahp == NULL)
		return -ENOMEM;

	memset(ahp, 0, sizeof(*ahp));

	ahp->key = x->aalg->alg_key;
	ahp->key_len = (x->aalg->alg_key_len+7)/8;
	ahp->tfm = crypto_alloc_tfm(x->aalg->alg_name, 0);
	if (!ahp->tfm)
		goto error;
	ahp->digest = ah_hmac_digest;
	ahp->digest_len = 12;
	ahp->work_digest = kmalloc(crypto_tfm_alg_digestsize(ahp->tfm),
				   GFP_KERNEL);
	if (!ahp->work_digest)
		goto error;
	x->props.header_len = (12 + ahp->digest_len + 7)&~7;
	if (x->props.mode)
		x->props.header_len += 20;
	x->data = ahp;

	return 0;

error:
	if (ahp) {
		if (ahp->work_digest)
			kfree(ahp->work_digest);
		if (ahp->tfm)
			crypto_free_tfm(ahp->tfm);
		kfree(ahp);
	}
	return -EINVAL;
}

void ah_destroy(struct xfrm_state *x)
{
	struct ah_data *ahp = x->data;

	if (ahp->work_digest) {
		kfree(ahp->work_digest);
		ahp->work_digest = NULL;
	}
	if (ahp->tfm) {
		crypto_free_tfm(ahp->tfm);
		ahp->tfm = NULL;
	}
}


static struct xfrm_type ah_type =
{
	.description	= "AH4",
	.proto	     	= IPPROTO_AH,
	.init_state	= ah_init_state,
	.destructor	= ah_destroy,
	.input		= ah_input,
	.output		= ah_output
};

static struct inet_protocol ah4_protocol = {
	.handler	=	xfrm4_rcv,
	.err_handler	=	ah4_err,
	.no_policy	=	1,
};

int __init ah4_init(void)
{
	SET_MODULE_OWNER(&ah_type);
	if (xfrm_register_type(&ah_type) < 0) {
		printk(KERN_INFO "ip ah init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (inet_add_protocol(&ah4_protocol, IPPROTO_AH) < 0) {
		printk(KERN_INFO "ip ah init: can't add protocol\n");
		xfrm_unregister_type(&ah_type);
		return -EAGAIN;
	}
	return 0;
}

static void __exit ah4_fini(void)
{
	if (inet_del_protocol(&ah4_protocol, IPPROTO_AH) < 0)
		printk(KERN_INFO "ip ah close: can't remove protocol\n");
	if (xfrm_unregister_type(&ah_type) < 0)
		printk(KERN_INFO "ip ah close: can't remove xfrm type\n");
}

module_init(ah4_init);
module_exit(ah4_fini);
MODULE_LICENSE("GPL");
