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
 * 	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 * 	
 * 	This file is derived from net/ipv4/ah.c.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <net/inet_ecn.h>
#include <net/ip.h>
#include <net/ah.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <net/icmp.h>
#include <net/ipv6.h>
#include <net/xfrm.h>
#include <asm/scatterlist.h>

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

static int ipv6_clear_mutable_options(struct sk_buff *skb, u16 *nh_offset, int dir)
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
				LIMIT_NETDEBUG(
				printk(KERN_WARNING "overrun hopopts\n")); 
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
				LIMIT_NETDEBUG(
					printk(KERN_WARNING "overrun destopt\n")); 
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
					LIMIT_NETDEBUG(
						printk(KERN_WARNING "overrun destopt\n"));
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
	err = xfrm_check_output(x, skb, AF_INET6);
	if (err)
		goto error;

	if (x->props.mode) {
		iph = skb->nh.ipv6h;
		skb->nh.ipv6h = (struct ipv6hdr*)skb_push(skb, x->props.header_len);
		skb->nh.ipv6h->version = 6;
		skb->nh.ipv6h->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
		skb->nh.ipv6h->nexthdr = IPPROTO_AH;
		ipv6_addr_copy(&skb->nh.ipv6h->saddr,
			       (struct in6_addr *) &x->props.saddr);
		ipv6_addr_copy(&skb->nh.ipv6h->daddr,
			       (struct in6_addr *) &x->id.daddr);
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
		nexthdr = ipv6_clear_mutable_options(skb, &nh_offset, XFRM_POLICY_OUT);
		if (nexthdr == 0)
			goto error_free_iph;

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
	ah->hdrlen  = (XFRM_ALIGN8(sizeof(struct ipv6_auth_hdr) + 
				   ahp->icv_trunc_len) >> 2) - 2;

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
		if (x->props.flags & XFRM_STATE_NOECN)
			IP6_ECN_clear(skb->nh.ipv6h);
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
error_free_iph:
	kfree(iph);
error:
	spin_unlock_bh(&x->lock);
error_nolock:
	kfree_skb(skb);
	return err;
}

int ah6_input(struct xfrm_state *x, struct xfrm_decap_state *decap, struct sk_buff *skb)
{
	/*
	 * Before process AH
	 * [IPv6][Ext1][Ext2][AH][Dest][Payload]
	 * |<-------------->| hdr_len
	 * |<------------------------>| cleared_hlen
	 *
	 * To erase AH:
	 * Keeping copy of cleared headers. After AH processing,
	 * Moving the pointer of skb->nh.raw by using skb_pull as long as AH
	 * header length. Then copy back the copy as long as hdr_len
	 * If destination header following AH exists, copy it into after [Ext2].
	 * 
	 * |<>|[IPv6][Ext1][Ext2][Dest][Payload]
	 * There is offset of AH before IPv6 header after the process.
	 */

	struct ipv6_auth_hdr *ah;
	struct ah_data *ahp;
	unsigned char *tmp_hdr = NULL;
	u16 hdr_len;
	u16 ah_hlen;
	u16 cleared_hlen;
	u16 nh_offset = 0;
	u8 nexthdr = 0;
	u8 *prevhdr;

	if (!pskb_may_pull(skb, sizeof(struct ip_auth_hdr)))
		goto out;

	/* We are going to _remove_ AH header to keep sockets happy,
	 * so... Later this can change. */
	if (skb_cloned(skb) &&
	    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
		goto out;

	hdr_len = skb->data - skb->nh.raw;
	cleared_hlen = hdr_len;
	ah = (struct ipv6_auth_hdr*)skb->data;
	ahp = x->data;
	nexthdr = ah->nexthdr;
	ah_hlen = (ah->hdrlen + 2) << 2;
	cleared_hlen += ah_hlen;

	if (nexthdr == NEXTHDR_DEST) {
		struct ipv6_opt_hdr *dsthdr = (struct ipv6_opt_hdr*)(skb->data + ah_hlen);
		cleared_hlen += ipv6_optlen(dsthdr);
	}

        if (ah_hlen != XFRM_ALIGN8(sizeof(struct ipv6_auth_hdr) + ahp->icv_full_len) &&
            ah_hlen != XFRM_ALIGN8(sizeof(struct ipv6_auth_hdr) + ahp->icv_trunc_len))
                goto out;

	if (!pskb_may_pull(skb, ah_hlen))
		goto out;

	tmp_hdr = kmalloc(cleared_hlen, GFP_ATOMIC);
	if (!tmp_hdr)
		goto out;
	memcpy(tmp_hdr, skb->nh.raw, cleared_hlen);
	ipv6_clear_mutable_options(skb, &nh_offset, XFRM_POLICY_IN);
	skb->nh.ipv6h->priority    = 0;
	skb->nh.ipv6h->flow_lbl[0] = 0;
	skb->nh.ipv6h->flow_lbl[1] = 0;
	skb->nh.ipv6h->flow_lbl[2] = 0;
	skb->nh.ipv6h->hop_limit   = 0;

        {
		u8 auth_data[MAX_AH_AUTH_LEN];

		memcpy(auth_data, ah->auth_data, ahp->icv_trunc_len);
		memset(ah->auth_data, 0, ahp->icv_trunc_len);
		skb_push(skb, skb->data - skb->nh.raw);
		ahp->icv(ahp, skb, ah->auth_data);
		if (memcmp(ah->auth_data, auth_data, ahp->icv_trunc_len)) {
			LIMIT_NETDEBUG(
				printk(KERN_WARNING "ipsec ah authentication error\n"));
			x->stats.integrity_failed++;
			goto free_out;
		}
	}

	skb->nh.raw = skb_pull(skb, ah_hlen);
	memcpy(skb->nh.raw, tmp_hdr, hdr_len);
	if (nexthdr == NEXTHDR_DEST) {
		memcpy(skb->nh.raw + hdr_len,
		       tmp_hdr + hdr_len + ah_hlen,
		       cleared_hlen - hdr_len - ah_hlen);
	}
	prevhdr = (u8*)(skb->nh.raw + nh_offset);
	*prevhdr = nexthdr;
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

	NETDEBUG(printk(KERN_DEBUG "pmtu discovery on SA AH/%08x/"
			"%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
	       ntohl(ah->spi), NIP6(iph->daddr)));

	xfrm_state_put(x);
}

static int ah6_init_state(struct xfrm_state *x, void *args)
{
	struct ah_data *ahp = NULL;
	struct xfrm_algo_desc *aalg_desc;

	if (!x->aalg)
		goto error;

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
	
	BUG_ON(ahp->icv_trunc_len > MAX_AH_AUTH_LEN);
	
	ahp->work_icv = kmalloc(ahp->icv_full_len, GFP_KERNEL);
	if (!ahp->work_icv)
		goto error;
	
	x->props.header_len = XFRM_ALIGN8(sizeof(struct ipv6_auth_hdr) + ahp->icv_trunc_len);
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

	if (!ahp)
		return;

	if (ahp->work_icv) {
		kfree(ahp->work_icv);
		ahp->work_icv = NULL;
	}
	if (ahp->tfm) {
		crypto_free_tfm(ahp->tfm);
		ahp->tfm = NULL;
	}
	kfree(ahp);
}

static struct xfrm_type ah6_type =
{
	.description	= "AH6",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_AH,
	.init_state	= ah6_init_state,
	.destructor	= ah6_destroy,
	.input		= ah6_input,
	.output		= ah6_output
};

static struct inet6_protocol ah6_protocol = {
	.handler	=	xfrm6_rcv,
	.err_handler	=	ah6_err,
	.flags		=	INET6_PROTO_NOPOLICY,
};

int __init ah6_init(void)
{
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
