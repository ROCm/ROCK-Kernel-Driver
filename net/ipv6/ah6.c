/*
 * Copyright (C)2002 USAGI/WIDE Project
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors
 *
 *	Mitsuru KANDA @USAGI       : IPv6 Support 
 * 	Kazunori MIYAZAWA @USAGI   :
 * 	Kunihiro Ishiguro          :
 * 	
 * 	This file is derived from net/ipv4/ah.c.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/ah.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <net/icmp.h>
#include <net/ipv6.h>
#include <net/xfrm.h>
#include <asm/scatterlist.h>

#define AH_HLEN_NOICV	12

/* XXX no ipv6 ah specific */
#define NIP6(addr) \
	ntohs((addr).s6_addr16[0]),\
	ntohs((addr).s6_addr16[1]),\
	ntohs((addr).s6_addr16[2]),\
	ntohs((addr).s6_addr16[3]),\
	ntohs((addr).s6_addr16[4]),\
	ntohs((addr).s6_addr16[5]),\
	ntohs((addr).s6_addr16[6]),\
	ntohs((addr).s6_addr16[7])

int ah6_output(struct sk_buff *skb)
{
	int err;
	int hdr_len = sizeof(struct ipv6hdr);
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x  = dst->xfrm;
	struct ipv6hdr *iph = NULL;
	struct ip_auth_hdr *ah;
	struct ah_data *ahp;
	u16 nh_offset = 0;
	u8 nexthdr;

	if (skb->ip_summed == CHECKSUM_HW && skb_checksum_help(skb) == NULL) {
		err = -EINVAL;
		goto error_nolock;
	}

	spin_lock_bh(&x->lock);
	if ((err = xfrm_state_check_expire(x)) != 0)
		goto error;
	if ((err = xfrm_state_check_space(x, skb)) != 0)
		goto error;

	if (x->props.mode) {
		iph = skb->nh.ipv6h;
		skb->nh.ipv6h = (struct ipv6hdr*)skb_push(skb, x->props.header_len);
		skb->nh.ipv6h->version = 6;
		skb->nh.ipv6h->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
		skb->nh.ipv6h->nexthdr = IPPROTO_AH;
		memcpy(&skb->nh.ipv6h->saddr, &x->props.saddr, sizeof(struct in6_addr));
		memcpy(&skb->nh.ipv6h->daddr, &x->id.daddr, sizeof(struct in6_addr));
		ah = (struct ip_auth_hdr*)(skb->nh.ipv6h+1);
		ah->nexthdr = IPPROTO_IPV6;
	} else {
		hdr_len = skb->h.raw - skb->nh.raw;
		iph = kmalloc(hdr_len, GFP_ATOMIC);
		if (!iph) {
			err = -ENOMEM;
			goto error;
		}
		memcpy(iph, skb->data, hdr_len);
		skb->nh.ipv6h = (struct ipv6hdr*)skb_push(skb, x->props.header_len);
		memcpy(skb->nh.ipv6h, iph, hdr_len);
		nexthdr = xfrm6_clear_mutable_options(skb, &nh_offset, XFRM_POLICY_OUT);
		if (nexthdr == 0)
			goto error;

		skb->nh.raw[nh_offset] = IPPROTO_AH;
		skb->nh.ipv6h->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
		ah = (struct ip_auth_hdr*)(skb->nh.raw+hdr_len);
		skb->h.raw = (unsigned char*) ah;
		ah->nexthdr = nexthdr;
	}

	skb->nh.ipv6h->priority    = 0;
	skb->nh.ipv6h->flow_lbl[0] = 0;
	skb->nh.ipv6h->flow_lbl[1] = 0;
	skb->nh.ipv6h->flow_lbl[2] = 0;
	skb->nh.ipv6h->hop_limit    = 0;

	ahp = x->data;
	ah->hdrlen  = (XFRM_ALIGN8(ahp->icv_trunc_len +
		AH_HLEN_NOICV) >> 2) - 2;

	ah->reserved = 0;
	ah->spi = x->id.spi;
	ah->seq_no = htonl(++x->replay.oseq);
	ahp->icv(ahp, skb, ah->auth_data);

	if (x->props.mode) {
		skb->nh.ipv6h->hop_limit   = iph->hop_limit;
		skb->nh.ipv6h->priority    = iph->priority; 	
		skb->nh.ipv6h->flow_lbl[0] = iph->flow_lbl[0];
		skb->nh.ipv6h->flow_lbl[1] = iph->flow_lbl[1];
		skb->nh.ipv6h->flow_lbl[2] = iph->flow_lbl[2];
	} else {
		memcpy(skb->nh.ipv6h, iph, hdr_len);
		skb->nh.raw[nh_offset] = IPPROTO_AH;
		skb->nh.ipv6h->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
		kfree (iph);
	}

	skb->nh.raw = skb->data;

	x->curlft.bytes += skb->len;
	x->curlft.packets++;
	spin_unlock_bh(&x->lock);
	if ((skb->dst = dst_pop(dst)) == NULL) {
		err = -EHOSTUNREACH;
		goto error_nolock;
	}
	return NET_XMIT_BYPASS;
error:
	spin_unlock_bh(&x->lock);
error_nolock:
	kfree_skb(skb);
	return err;
}

int ah6_input(struct xfrm_state *x, struct sk_buff *skb)
{
	int ah_hlen;
	struct ipv6hdr *iph;
	struct ipv6_auth_hdr *ah;
	struct ah_data *ahp;
	unsigned char *tmp_hdr = NULL;
	int hdr_len = skb->h.raw - skb->nh.raw;
	u8 nexthdr = 0;

	if (!pskb_may_pull(skb, sizeof(struct ip_auth_hdr)))
		goto out;

	ah = (struct ipv6_auth_hdr*)skb->data;
	ahp = x->data;
        ah_hlen = (ah->hdrlen + 2) << 2;

        if (ah_hlen != XFRM_ALIGN8(ahp->icv_full_len + AH_HLEN_NOICV) &&
            ah_hlen != XFRM_ALIGN8(ahp->icv_trunc_len + AH_HLEN_NOICV))
                goto out;

	if (!pskb_may_pull(skb, ah_hlen))
		goto out;

	/* We are going to _remove_ AH header to keep sockets happy,
	 * so... Later this can change. */
	if (skb_cloned(skb) &&
	    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
		goto out;

	tmp_hdr = kmalloc(hdr_len, GFP_ATOMIC);
	if (!tmp_hdr)
		goto out;
	memcpy(tmp_hdr, skb->nh.raw, hdr_len);
	ah = (struct ipv6_auth_hdr*)skb->data;
	iph = skb->nh.ipv6h;

        {
		u8 auth_data[ahp->icv_trunc_len];

		memcpy(auth_data, ah->auth_data, ahp->icv_trunc_len);
		skb_push(skb, skb->data - skb->nh.raw);
		ahp->icv(ahp, skb, ah->auth_data);
		if (memcmp(ah->auth_data, auth_data, ahp->icv_trunc_len)) {
			if (net_ratelimit())
				printk(KERN_WARNING "ipsec ah authentication error\n");
			x->stats.integrity_failed++;
			goto free_out;
		}
	}

	nexthdr = ((struct ipv6hdr*)tmp_hdr)->nexthdr = ah->nexthdr;
	skb->nh.raw = skb_pull(skb, (ah->hdrlen+2)<<2);
	memcpy(skb->nh.raw, tmp_hdr, hdr_len);
	skb->nh.ipv6h->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
	skb_pull(skb, hdr_len);
	skb->h.raw = skb->data;


	kfree(tmp_hdr);

	return nexthdr;

free_out:
	kfree(tmp_hdr);
out:
	return -EINVAL;
}

void ah6_err(struct sk_buff *skb, struct inet6_skb_parm *opt, 
	 int type, int code, int offset, __u32 info)
{
	struct ipv6hdr *iph = (struct ipv6hdr*)skb->data;
	struct ip_auth_hdr *ah = (struct ip_auth_hdr*)(skb->data+offset);
	struct xfrm_state *x;

	if (type != ICMPV6_DEST_UNREACH ||
	    type != ICMPV6_PKT_TOOBIG)
		return;

	x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, ah->spi, IPPROTO_AH, AF_INET6);
	if (!x)
		return;

	printk(KERN_DEBUG "pmtu discvovery on SA AH/%08x/"
			"%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
	       ntohl(ah->spi), NIP6(iph->daddr));

	xfrm_state_put(x);
}

static int ah6_init_state(struct xfrm_state *x, void *args)
{
	struct ah_data *ahp = NULL;
	struct xfrm_algo_desc *aalg_desc;

	/* null auth can use a zero length key */
	if (x->aalg->alg_key_len > 512)
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
	ahp->icv = ah_hmac_digest;
	
	/*
	 * Lookup the algorithm description maintained by xfrm_algo,
	 * verify crypto transform properties, and store information
	 * we need for AH processing.  This lookup cannot fail here
	 * after a successful crypto_alloc_tfm().
	 */
	aalg_desc = xfrm_aalg_get_byname(x->aalg->alg_name);
	BUG_ON(!aalg_desc);

	if (aalg_desc->uinfo.auth.icv_fullbits/8 !=
	    crypto_tfm_alg_digestsize(ahp->tfm)) {
		printk(KERN_INFO "AH: %s digestsize %u != %hu\n",
		       x->aalg->alg_name, crypto_tfm_alg_digestsize(ahp->tfm),
		       aalg_desc->uinfo.auth.icv_fullbits/8);
		goto error;
	}
	
	ahp->icv_full_len = aalg_desc->uinfo.auth.icv_fullbits/8;
	ahp->icv_trunc_len = aalg_desc->uinfo.auth.icv_truncbits/8;
	
	ahp->work_icv = kmalloc(ahp->icv_full_len, GFP_KERNEL);
	if (!ahp->work_icv)
		goto error;
	
	x->props.header_len = XFRM_ALIGN8(ahp->icv_trunc_len + AH_HLEN_NOICV);
	if (x->props.mode)
		x->props.header_len += sizeof(struct ipv6hdr);
	x->data = ahp;

	return 0;

error:
	if (ahp) {
		if (ahp->work_icv)
			kfree(ahp->work_icv);
		if (ahp->tfm)
			crypto_free_tfm(ahp->tfm);
		kfree(ahp);
	}
	return -EINVAL;
}

static void ah6_destroy(struct xfrm_state *x)
{
	struct ah_data *ahp = x->data;

	if (ahp->work_icv) {
		kfree(ahp->work_icv);
		ahp->work_icv = NULL;
	}
	if (ahp->tfm) {
		crypto_free_tfm(ahp->tfm);
		ahp->tfm = NULL;
	}
}

static struct xfrm_type ah6_type =
{
	.description	= "AH6",
	.proto	     	= IPPROTO_AH,
	.init_state	= ah6_init_state,
	.destructor	= ah6_destroy,
	.input		= ah6_input,
	.output		= ah6_output
};

static struct inet6_protocol ah6_protocol = {
	.handler	=	xfrm6_rcv,
	.err_handler	=	ah6_err,
	.no_policy	=	1,
};

int __init ah6_init(void)
{
	SET_MODULE_OWNER(&ah6_type);

	if (xfrm_register_type(&ah6_type, AF_INET6) < 0) {
		printk(KERN_INFO "ipv6 ah init: can't add xfrm type\n");
		return -EAGAIN;
	}

	if (inet6_add_protocol(&ah6_protocol, IPPROTO_AH) < 0) {
		printk(KERN_INFO "ipv6 ah init: can't add protocol\n");
		xfrm_unregister_type(&ah6_type, AF_INET6);
		return -EAGAIN;
	}

	return 0;
}

static void __exit ah6_fini(void)
{
	if (inet6_del_protocol(&ah6_protocol, IPPROTO_AH) < 0)
		printk(KERN_INFO "ipv6 ah close: can't remove protocol\n");

	if (xfrm_unregister_type(&ah6_type, AF_INET6) < 0)
		printk(KERN_INFO "ipv6 ah close: can't remove xfrm type\n");

}

module_init(ah6_init);
module_exit(ah6_fini);

MODULE_LICENSE("GPL");
