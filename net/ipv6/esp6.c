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
 * 	This file is derived from net/ipv4/esp.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <net/inet_ecn.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/esp.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <linux/random.h>
#include <net/icmp.h>
#include <net/ipv6.h>
#include <linux/icmpv6.h>

#define MAX_SG_ONSTACK 4

int esp6_output(struct sk_buff *skb)
{
	int err;
	int hdr_len = 0;
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x  = dst->xfrm;
	struct ipv6hdr *iph = NULL, *top_iph;
	struct ipv6_esp_hdr *esph;
	struct crypto_tfm *tfm;
	struct esp_data *esp;
	struct sk_buff *trailer;
	int blksize;
	int clen;
	int alen;
	int nfrags;
	u8 *prevhdr;
	u8 nexthdr = 0;

	/* First, if the skb is not checksummed, complete checksum. */
	if (skb->ip_summed == CHECKSUM_HW && skb_checksum_help(skb) == NULL) {
		err = -EINVAL;
		goto error_nolock;
	}

	spin_lock_bh(&x->lock);
	err = xfrm_check_output(x, skb, AF_INET6);
	if (err)
		goto error;
	err = -ENOMEM;

	/* Strip IP header in transport mode. Save it. */

	if (!x->props.mode) {
		hdr_len = ip6_find_1stfragopt(skb, &prevhdr);
		nexthdr = *prevhdr;
		*prevhdr = IPPROTO_ESP;
		iph = kmalloc(hdr_len, GFP_ATOMIC);
		if (!iph) {
			err = -ENOMEM;
			goto error;
		}
		memcpy(iph, skb->nh.raw, hdr_len);
		__skb_pull(skb, hdr_len);
	}

	/* Now skb is pure payload to encrypt */

	/* Round to block size */
	clen = skb->len;

	esp = x->data;
	alen = esp->auth.icv_trunc_len;
	tfm = esp->conf.tfm;
	blksize = (crypto_tfm_alg_blocksize(tfm) + 3) & ~3;
	clen = (clen + 2 + blksize-1)&~(blksize-1);
	if (esp->conf.padlen)
		clen = (clen + esp->conf.padlen-1)&~(esp->conf.padlen-1);

	if ((nfrags = skb_cow_data(skb, clen-skb->len+alen, &trailer)) < 0) {
		if (!x->props.mode && iph) kfree(iph);
		goto error;
	}

	/* Fill padding... */
	do {
		int i;
		for (i=0; i<clen-skb->len - 2; i++)
			*(u8*)(trailer->tail + i) = i+1;
	} while (0);
	*(u8*)(trailer->tail + clen-skb->len - 2) = (clen - skb->len)-2;
	pskb_put(skb, trailer, clen - skb->len);

	if (x->props.mode) {
		iph = skb->nh.ipv6h;
		top_iph = (struct ipv6hdr*)skb_push(skb, x->props.header_len);
		esph = (struct ipv6_esp_hdr*)(top_iph+1);
		*(u8*)(trailer->tail - 1) = IPPROTO_IPV6;
		top_iph->version = 6;
		top_iph->priority = iph->priority;
		top_iph->flow_lbl[0] = iph->flow_lbl[0];
		top_iph->flow_lbl[1] = iph->flow_lbl[1];
		top_iph->flow_lbl[2] = iph->flow_lbl[2];
		if (x->props.flags & XFRM_STATE_NOECN)
			IP6_ECN_clear(top_iph);
		top_iph->nexthdr = IPPROTO_ESP;
		top_iph->payload_len = htons(skb->len + alen - sizeof(struct ipv6hdr));
		top_iph->hop_limit = iph->hop_limit;
		ipv6_addr_copy(&top_iph->saddr,
			       (struct in6_addr *)&x->props.saddr);
		ipv6_addr_copy(&top_iph->daddr,
			       (struct in6_addr *)&x->id.daddr);
	} else { 
		esph = (struct ipv6_esp_hdr*)skb_push(skb, x->props.header_len);
		skb->h.raw = (unsigned char*)esph;
		top_iph = (struct ipv6hdr*)skb_push(skb, hdr_len);
		memcpy(top_iph, iph, hdr_len);
		kfree(iph);
		top_iph->payload_len = htons(skb->len + alen - sizeof(struct ipv6hdr));
		*(u8*)(trailer->tail - 1) = nexthdr;
	}

	esph->spi = x->id.spi;
	esph->seq_no = htonl(++x->replay.oseq);

	if (esp->conf.ivlen)
		crypto_cipher_set_iv(tfm, esp->conf.ivec, crypto_tfm_alg_ivsize(tfm));

	do {
		struct scatterlist sgbuf[nfrags>MAX_SG_ONSTACK ? 0 : nfrags];
		struct scatterlist *sg = sgbuf;

		if (unlikely(nfrags > MAX_SG_ONSTACK)) {
			sg = kmalloc(sizeof(struct scatterlist)*nfrags, GFP_ATOMIC);
			if (!sg)
				goto error;
		}
		skb_to_sgvec(skb, sg, esph->enc_data+esp->conf.ivlen-skb->data, clen);
		crypto_cipher_encrypt(tfm, sg, sg, clen);
		if (unlikely(sg != sgbuf))
			kfree(sg);
	} while (0);

	if (esp->conf.ivlen) {
		memcpy(esph->enc_data, esp->conf.ivec, crypto_tfm_alg_ivsize(tfm));
		crypto_cipher_get_iv(tfm, esp->conf.ivec, crypto_tfm_alg_ivsize(tfm));
	}

	if (esp->auth.icv_full_len) {
		esp->auth.icv(esp, skb, (u8*)esph-skb->data,
			sizeof(struct ipv6_esp_hdr) + esp->conf.ivlen+clen, trailer->tail);
		pskb_put(skb, trailer, alen);
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

int esp6_input(struct xfrm_state *x, struct xfrm_decap_state *decap, struct sk_buff *skb)
{
	struct ipv6hdr *iph;
	struct ipv6_esp_hdr *esph;
	struct esp_data *esp = x->data;
	struct sk_buff *trailer;
	int blksize = crypto_tfm_alg_blocksize(esp->conf.tfm);
	int alen = esp->auth.icv_trunc_len;
	int elen = skb->len - sizeof(struct ipv6_esp_hdr) - esp->conf.ivlen - alen;

	int hdr_len = skb->h.raw - skb->nh.raw;
	int nfrags;
	unsigned char *tmp_hdr = NULL;
	int ret = 0;

	if (!pskb_may_pull(skb, sizeof(struct ipv6_esp_hdr))) {
		ret = -EINVAL;
		goto out_nofree;
	}

	if (elen <= 0 || (elen & (blksize-1))) {
		ret = -EINVAL;
		goto out_nofree;
	}

	tmp_hdr = kmalloc(hdr_len, GFP_ATOMIC);
	if (!tmp_hdr) {
		ret = -ENOMEM;
		goto out_nofree;
	}
	memcpy(tmp_hdr, skb->nh.raw, hdr_len);

	/* If integrity check is required, do this. */
        if (esp->auth.icv_full_len) {
		u8 sum[esp->auth.icv_full_len];
		u8 sum1[alen];

		esp->auth.icv(esp, skb, 0, skb->len-alen, sum);

		if (skb_copy_bits(skb, skb->len-alen, sum1, alen))
			BUG();

		if (unlikely(memcmp(sum, sum1, alen))) {
			x->stats.integrity_failed++;
			ret = -EINVAL;
			goto out;
		}
	}

	if ((nfrags = skb_cow_data(skb, 0, &trailer)) < 0) {
		ret = -EINVAL;
		goto out;
	}

	skb->ip_summed = CHECKSUM_NONE;

	esph = (struct ipv6_esp_hdr*)skb->data;
	iph = skb->nh.ipv6h;

	/* Get ivec. This can be wrong, check against another impls. */
	if (esp->conf.ivlen)
		crypto_cipher_set_iv(esp->conf.tfm, esph->enc_data, crypto_tfm_alg_ivsize(esp->conf.tfm));

        {
		u8 nexthdr[2];
		struct scatterlist sgbuf[nfrags>MAX_SG_ONSTACK ? 0 : nfrags];
		struct scatterlist *sg = sgbuf;
		u8 padlen;
		u8 *prevhdr;

		if (unlikely(nfrags > MAX_SG_ONSTACK)) {
			sg = kmalloc(sizeof(struct scatterlist)*nfrags, GFP_ATOMIC);
			if (!sg) {
				ret = -ENOMEM;
				goto out;
			}
		}
		skb_to_sgvec(skb, sg, sizeof(struct ipv6_esp_hdr) + esp->conf.ivlen, elen);
		crypto_cipher_decrypt(esp->conf.tfm, sg, sg, elen);
		if (unlikely(sg != sgbuf))
			kfree(sg);

		if (skb_copy_bits(skb, skb->len-alen-2, nexthdr, 2))
			BUG();

		padlen = nexthdr[0];
		if (padlen+2 >= elen) {
			LIMIT_NETDEBUG(
				printk(KERN_WARNING "ipsec esp packet is garbage padlen=%d, elen=%d\n", padlen+2, elen));
			ret = -EINVAL;
			goto out;
		}
		/* ... check padding bits here. Silly. :-) */ 

		pskb_trim(skb, skb->len - alen - padlen - 2);
		skb->h.raw = skb_pull(skb, sizeof(struct ipv6_esp_hdr) + esp->conf.ivlen);
		skb->nh.raw += sizeof(struct ipv6_esp_hdr) + esp->conf.ivlen;
		memcpy(skb->nh.raw, tmp_hdr, hdr_len);
		skb->nh.ipv6h->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
		ip6_find_1stfragopt(skb, &prevhdr);
		ret = *prevhdr = nexthdr[1];
	}

out:
	kfree(tmp_hdr);
out_nofree:
	return ret;
}

static u32 esp6_get_max_size(struct xfrm_state *x, int mtu)
{
	struct esp_data *esp = x->data;
	u32 blksize = crypto_tfm_alg_blocksize(esp->conf.tfm);

	if (x->props.mode) {
		mtu = (mtu + 2 + blksize-1)&~(blksize-1);
	} else {
		/* The worst case. */
		mtu += 2 + blksize;
	}
	if (esp->conf.padlen)
		mtu = (mtu + esp->conf.padlen-1)&~(esp->conf.padlen-1);

	return mtu + x->props.header_len + esp->auth.icv_full_len;
}

void esp6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		int type, int code, int offset, __u32 info)
{
	struct ipv6hdr *iph = (struct ipv6hdr*)skb->data;
	struct ipv6_esp_hdr *esph = (struct ipv6_esp_hdr*)(skb->data+offset);
	struct xfrm_state *x;

	if (type != ICMPV6_DEST_UNREACH ||
	    type != ICMPV6_PKT_TOOBIG)
		return;

	x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, esph->spi, IPPROTO_ESP, AF_INET6);
	if (!x)
		return;
	printk(KERN_DEBUG "pmtu discovery on SA ESP/%08x/"
			"%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n", 
			ntohl(esph->spi), NIP6(iph->daddr));
	xfrm_state_put(x);
}

void esp6_destroy(struct xfrm_state *x)
{
	struct esp_data *esp = x->data;

	if (!esp)
		return;

	if (esp->conf.tfm) {
		crypto_free_tfm(esp->conf.tfm);
		esp->conf.tfm = NULL;
	}
	if (esp->conf.ivec) {
		kfree(esp->conf.ivec);
		esp->conf.ivec = NULL;
	}
	if (esp->auth.tfm) {
		crypto_free_tfm(esp->auth.tfm);
		esp->auth.tfm = NULL;
	}
	if (esp->auth.work_icv) {
		kfree(esp->auth.work_icv);
		esp->auth.work_icv = NULL;
	}
	kfree(esp);
}

int esp6_init_state(struct xfrm_state *x, void *args)
{
	struct esp_data *esp = NULL;

	if (x->aalg) {
		if (x->aalg->alg_key_len == 0 || x->aalg->alg_key_len > 512)
			goto error;
	}
	if (x->ealg == NULL)
		goto error;

	esp = kmalloc(sizeof(*esp), GFP_KERNEL);
	if (esp == NULL)
		return -ENOMEM;

	memset(esp, 0, sizeof(*esp));

	if (x->aalg) {
		struct xfrm_algo_desc *aalg_desc;

		esp->auth.key = x->aalg->alg_key;
		esp->auth.key_len = (x->aalg->alg_key_len+7)/8;
		esp->auth.tfm = crypto_alloc_tfm(x->aalg->alg_name, 0);
		if (esp->auth.tfm == NULL)
			goto error;
		esp->auth.icv = esp_hmac_digest;
 
		aalg_desc = xfrm_aalg_get_byname(x->aalg->alg_name);
		BUG_ON(!aalg_desc);
 
		if (aalg_desc->uinfo.auth.icv_fullbits/8 !=
			crypto_tfm_alg_digestsize(esp->auth.tfm)) {
				printk(KERN_INFO "ESP: %s digestsize %u != %hu\n",
					x->aalg->alg_name,
					crypto_tfm_alg_digestsize(esp->auth.tfm),
					aalg_desc->uinfo.auth.icv_fullbits/8);
				goto error;
		}
 
		esp->auth.icv_full_len = aalg_desc->uinfo.auth.icv_fullbits/8;
		esp->auth.icv_trunc_len = aalg_desc->uinfo.auth.icv_truncbits/8;
 
		esp->auth.work_icv = kmalloc(esp->auth.icv_full_len, GFP_KERNEL);
		if (!esp->auth.work_icv)
			goto error;
	}
	esp->conf.key = x->ealg->alg_key;
	esp->conf.key_len = (x->ealg->alg_key_len+7)/8;
	if (x->props.ealgo == SADB_EALG_NULL)
		esp->conf.tfm = crypto_alloc_tfm(x->ealg->alg_name, CRYPTO_TFM_MODE_ECB);
	else
		esp->conf.tfm = crypto_alloc_tfm(x->ealg->alg_name, CRYPTO_TFM_MODE_CBC);
	if (esp->conf.tfm == NULL)
		goto error;
	esp->conf.ivlen = crypto_tfm_alg_ivsize(esp->conf.tfm);
	esp->conf.padlen = 0;
	if (esp->conf.ivlen) {
		esp->conf.ivec = kmalloc(esp->conf.ivlen, GFP_KERNEL);
		if (unlikely(esp->conf.ivec == NULL))
			goto error;
		get_random_bytes(esp->conf.ivec, esp->conf.ivlen);
	}
	crypto_cipher_setkey(esp->conf.tfm, esp->conf.key, esp->conf.key_len);
	x->props.header_len = sizeof(struct ipv6_esp_hdr) + esp->conf.ivlen;
	if (x->props.mode)
		x->props.header_len += sizeof(struct ipv6hdr);
	x->data = esp;
	return 0;

error:
	if (esp) {
		if (esp->auth.tfm)
			crypto_free_tfm(esp->auth.tfm);
		if (esp->auth.work_icv)
			kfree(esp->auth.work_icv);
		if (esp->conf.tfm)
			crypto_free_tfm(esp->conf.tfm);
		kfree(esp);
	}
	return -EINVAL;
}

static struct xfrm_type esp6_type =
{
	.description	= "ESP6",
	.owner	     	= THIS_MODULE,
	.proto	     	= IPPROTO_ESP,
	.init_state	= esp6_init_state,
	.destructor	= esp6_destroy,
	.get_max_size	= esp6_get_max_size,
	.input		= esp6_input,
	.output		= esp6_output
};

static struct inet6_protocol esp6_protocol = {
	.handler 	=	xfrm6_rcv,
	.err_handler	=	esp6_err,
	.flags		=	INET6_PROTO_NOPOLICY,
};

int __init esp6_init(void)
{
	if (xfrm_register_type(&esp6_type, AF_INET6) < 0) {
		printk(KERN_INFO "ipv6 esp init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (inet6_add_protocol(&esp6_protocol, IPPROTO_ESP) < 0) {
		printk(KERN_INFO "ipv6 esp init: can't add protocol\n");
		xfrm_unregister_type(&esp6_type, AF_INET6);
		return -EAGAIN;
	}

	return 0;
}

static void __exit esp6_fini(void)
{
	if (inet6_del_protocol(&esp6_protocol, IPPROTO_ESP) < 0)
		printk(KERN_INFO "ipv6 esp close: can't remove protocol\n");
	if (xfrm_unregister_type(&esp6_type, AF_INET6) < 0)
		printk(KERN_INFO "ipv6 esp close: can't remove xfrm type\n");
}

module_init(esp6_init);
module_exit(esp6_fini);

MODULE_LICENSE("GPL");
