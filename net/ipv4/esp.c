#include <linux/config.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <linux/random.h>
#include <net/icmp.h>

#define MAX_SG_ONSTACK 4

/* BUGS:
 * - we assume replay seqno is always present.
 */

struct esp_data
{
	/* Confidentiality */
	struct {
		u8			*key;		/* Key */
		int			key_len;	/* Key length */
		u8			*ivec;		/* ivec buffer */
		/* ivlen is offset from enc_data, where encrypted data start.
		 * It is logically different of crypto_tfm_alg_ivsize(tfm).
		 * We assume that it is either zero (no ivec), or
		 * >= crypto_tfm_alg_ivsize(tfm). */
		int			ivlen;
		int			padlen;		/* 0..255 */
		struct crypto_tfm	*tfm;		/* crypto handle */
	} conf;

	/* Integrity. It is active when authlen != 0 */
	struct {
		u8			*key;		/* Key */
		int			key_len;	/* Length of the key */
		/* authlen is length of trailer containing auth token.
		 * If it is not zero it is assumed to be
		 * >= crypto_tfm_alg_digestsize(atfm) */
		int			authlen;
		void			(*digest)(struct esp_data*,
						  struct sk_buff *skb,
						  int offset,
						  int len,
						  u8 *digest);
		struct crypto_tfm	*tfm;
	} auth;
};

/* Move to common area: it is shared with AH. */

void skb_digest_walk(const struct sk_buff *skb, struct crypto_tfm *tfm,
		     int offset, int len)
{
	int start = skb->len - skb->data_len;
	int i, copy = start - offset;

	/* Checksum header. */
	if (copy > 0) {
		if (copy > len)
			copy = len;
		tfm->__crt_alg->cra_digest.dia_update(tfm->crt_ctx, skb->data+offset, copy);
		if ((len -= copy) == 0)
			return;
		offset += copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;

		BUG_TRAP(start <= offset + len);

		end = start + skb_shinfo(skb)->frags[i].size;
		if ((copy = end - offset) > 0) {
			u8 *vaddr;
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			if (copy > len)
				copy = len;
			vaddr = kmap_skb_frag(frag);
			tfm->__crt_alg->cra_digest.dia_update(tfm->crt_ctx, vaddr+frag->page_offset+offset-start, copy);
			kunmap_skb_frag(vaddr);
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
				skb_digest_walk(list, tfm, offset-start, copy);
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


/* Looking generic it is not used in another places. */

int
skb_to_sgvec(struct sk_buff *skb, struct scatterlist *sg, int offset, int len)
{
	int start = skb->len - skb->data_len;
	int i, copy = start - offset;
	int elt = 0;

	if (copy > 0) {
		if (copy > len)
			copy = len;
		sg[elt].page = virt_to_page(skb->data + offset);
		sg[elt].offset = (unsigned long)(skb->data + offset) % PAGE_SIZE;
		sg[elt].length = copy;
		elt++;
		if ((len -= copy) == 0)
			return elt;
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
			sg[elt].page = frag->page;
			sg[elt].offset = frag->page_offset+offset-start;
			sg[elt].length = copy;
			elt++;
			if (!(len -= copy))
				return elt;
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
				elt += skb_to_sgvec(list, sg+elt, offset - start, copy);
				if ((len -= copy) == 0)
					return elt;
				offset += copy;
			}
			start = end;
		}
	}
	if (len)
		BUG();
	return elt;
}

/* Common with AH after some work on arguments. */

static void
esp_hmac_digest(struct esp_data *esp, struct sk_buff *skb, int offset,
		int len, u8 *auth_data)
{
	struct crypto_tfm *tfm = esp->auth.tfm;
	char digest[crypto_tfm_alg_digestsize(tfm)];

	memset(auth_data, 0, esp->auth.authlen);
 	crypto_hmac_init(tfm, esp->auth.key, &esp->auth.key_len);
	skb_digest_walk(skb, tfm, offset, len);
	crypto_hmac_final(tfm, esp->auth.key, &esp->auth.key_len, digest);
	memcpy(auth_data, digest, crypto_tfm_alg_digestsize(tfm));
}

/* Check that skb data bits are writable. If they are not, copy data
 * to newly created private area. If "tailbits" is given, make sure that
 * tailbits bytes beoynd current end of skb are writable.
 *
 * Returns amount of elements of scatterlist to load for subsequent
 * transformations and pointer to writable trailer skb.
 */

int skb_cow_data(struct sk_buff *skb, int tailbits, struct sk_buff **trailer)
{
	int copyflag;
	int elt;
	struct sk_buff *skb1, **skb_p;

	/* If skb is cloned or its head is paged, reallocate
	 * head pulling out all the pages (pages are considered not writable
	 * at the moment even if they are anonymous).
	 */
	if ((skb_cloned(skb) || skb_shinfo(skb)->nr_frags) &&
	    __pskb_pull_tail(skb, skb_pagelen(skb)-skb_headlen(skb)) == NULL)
		return -ENOMEM;

	/* Easy case. Most of packets will go this way. */
	if (!skb_shinfo(skb)->frag_list) {
		/* A little of trouble, not enough of space for trailer.
		 * This should not happen, when stack is tuned to generate
		 * good frames. OK, on miss we reallocate and reserve even more
		 * space, 128 bytes is fair. */

		if (skb_tailroom(skb) < tailbits &&
		    pskb_expand_head(skb, 0, tailbits-skb_tailroom(skb)+128, GFP_ATOMIC))
			return -ENOMEM;

		/* Voila! */
		*trailer = skb;
		return 1;
	}

	/* Misery. We are in troubles, going to mincer fragments... */

	elt = 1;
	skb_p = &skb_shinfo(skb)->frag_list;
	copyflag = 0;

	while ((skb1 = *skb_p) != NULL) {
		int ntail = 0;

		/* The fragment is partially pulled by someone,
		 * this can happen on input. Copy it and everything
		 * after it. */

		if (skb_shared(skb1))
			copyflag = 1;

		/* If the skb is the last, worry about trailer. */

		if (skb1->next == NULL && tailbits) {
			if (skb_shinfo(skb1)->nr_frags ||
			    skb_shinfo(skb1)->frag_list ||
			    skb_tailroom(skb1) < tailbits)
				ntail = tailbits + 128;
		}

		if (copyflag ||
		    skb_cloned(skb1) ||
		    ntail ||
		    skb_shinfo(skb1)->nr_frags ||
		    skb_shinfo(skb1)->frag_list) {
			struct sk_buff *skb2;

			/* Fuck, we are miserable poor guys... */
			if (ntail == 0)
				skb2 = skb_copy(skb1, GFP_ATOMIC);
			else
				skb2 = skb_copy_expand(skb1,
						       skb_headroom(skb1),
						       ntail,
						       GFP_ATOMIC);
			if (unlikely(skb2 == NULL))
				return -ENOMEM;

			if (skb1->sk)
				skb_set_owner_w(skb, skb1->sk);

			/* Looking around. Are we still alive?
			 * OK, link new skb, drop old one */

			skb2->next = skb1->next;
			*skb_p = skb2;
			kfree_skb(skb1);
			skb1 = skb2;
		}
		elt++;
		*trailer = skb1;
		skb_p = &skb1->next;
	}

	return elt;
}

void *pskb_put(struct sk_buff *skb, struct sk_buff *tail, int len)
{
	if (tail != skb) {
		skb->data_len += len;
		skb->len += len;
	}
	return skb_put(tail, len);
}

int esp_output(struct sk_buff *skb)
{
	int err;
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x  = dst->xfrm;
	struct iphdr *iph, *top_iph;
	struct ip_esp_hdr *esph;
	struct crypto_tfm *tfm;
	struct esp_data *esp;
	struct sk_buff *trailer;
	int blksize;
	int clen;
	int nfrags;
	union {
		struct iphdr	iph;
		char 		buf[60];
	} tmp_iph;

	/* First, if the skb is not checksummed, complete checksum. */
	if (skb->ip_summed == CHECKSUM_HW && skb_checksum_help(skb) == NULL)
		return -EINVAL;

	spin_lock_bh(&x->lock);
	if ((err = xfrm_state_check_expire(x)) != 0)
		goto error;
	if ((err = xfrm_state_check_space(x, skb)) != 0)
		goto error;

	err = -ENOMEM;

	/* Strip IP header in transport mode. Save it. */
	if (!x->props.mode) {
		iph = skb->nh.iph;
		memcpy(&tmp_iph, iph, iph->ihl*4);
		__skb_pull(skb, iph->ihl*4);
	}
	/* Now skb is pure payload to encrypt */

	/* Round to block size */
	clen = skb->len;

	esp = x->data;
	tfm = esp->conf.tfm;
	blksize = crypto_tfm_alg_blocksize(tfm);
	clen = (clen + 2 + blksize-1)&~(blksize-1);
	if (esp->conf.padlen)
		clen = (clen + esp->conf.padlen-1)&~(esp->conf.padlen-1);

	if ((nfrags = skb_cow_data(skb, clen-skb->len+esp->auth.authlen, &trailer)) < 0)
		goto error;

	/* Fill padding... */
	do {
		int i;
		for (i=0; i<clen-skb->len - 2; i++)
			*(u8*)(trailer->tail + i) = i+1;
	} while (0);
	*(u8*)(trailer->tail + clen-skb->len - 2) = (clen - skb->len)-2;
	pskb_put(skb, trailer, clen - skb->len);

	iph = skb->nh.iph;
	if (x->props.mode) {
		top_iph = (struct iphdr*)skb_push(skb, x->props.header_len);
		esph = (struct ip_esp_hdr*)(top_iph+1);
		*(u8*)(trailer->tail - 1) = IPPROTO_IP;
		top_iph->ihl = 5;
		top_iph->version = 4;
		top_iph->tos = iph->tos;	/* DS disclosed */
		top_iph->tot_len = htons(skb->len + esp->auth.authlen);
		top_iph->frag_off = iph->frag_off&htons(IP_DF);
		if (!(top_iph->frag_off))
			ip_select_ident(top_iph, dst, 0);
		top_iph->ttl = iph->ttl;	/* TTL disclosed */
		top_iph->protocol = IPPROTO_ESP;
		top_iph->check = 0;
		top_iph->saddr = x->props.saddr.xfrm4_addr;
		top_iph->daddr = x->id.daddr.xfrm4_addr;
		memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));
	} else {
		esph = (struct ip_esp_hdr*)skb_push(skb, x->props.header_len);
		top_iph = (struct iphdr*)skb_push(skb, iph->ihl*4);
		memcpy(top_iph, &tmp_iph, iph->ihl*4);
		iph = &tmp_iph.iph;
		top_iph->tot_len = htons(skb->len + esp->auth.authlen);
		top_iph->protocol = IPPROTO_ESP;
		top_iph->check = 0;
		top_iph->frag_off = iph->frag_off;
		*(u8*)(trailer->tail - 1) = iph->protocol;
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
		crypto_cipher_encrypt(tfm, sg, nfrags);
		if (unlikely(sg != sgbuf))
			kfree(sg);
	} while (0);

	if (esp->conf.ivlen) {
		memcpy(esph->enc_data, esp->conf.ivec, crypto_tfm_alg_ivsize(tfm));
		crypto_cipher_get_iv(tfm, esp->conf.ivec, crypto_tfm_alg_ivsize(tfm));
	}

	if (esp->auth.authlen) {
		esp->auth.digest(esp, skb, (u8*)esph-skb->data,
				 8+esp->conf.ivlen+clen, trailer->tail);
		pskb_put(skb, trailer, esp->auth.authlen);
	}

	ip_send_check(top_iph);

	skb->nh.raw = skb->data;

	x->curlft.bytes += skb->len;
	spin_unlock_bh(&x->lock);
	if ((skb->dst = dst_pop(dst)) == NULL)
		goto error;
	return NET_XMIT_BYPASS;

error:
	spin_unlock_bh(&x->lock);
	kfree_skb(skb);
	return err;
}

int esp_input(struct xfrm_state *x, struct sk_buff *skb)
{
	struct iphdr *iph;
	struct ip_esp_hdr *esph;
	struct esp_data *esp = x->data;
	struct sk_buff *trailer;
	int blksize = crypto_tfm_alg_blocksize(esp->conf.tfm);
	int elen = skb->len - 8 - esp->conf.ivlen - esp->auth.authlen;
	int nfrags;

	if (!pskb_may_pull(skb, sizeof(struct ip_esp_hdr)))
		goto out;

	if (elen <= 0 || (elen & (blksize-1)))
		goto out;

	/* If integrity check is required, do this. */
	if (esp->auth.authlen) {
		int icvsize = crypto_tfm_alg_digestsize(esp->auth.tfm);
		u8 sum[icvsize];
		u8 sum1[icvsize];

		esp->auth.digest(esp, skb, 0, skb->len-esp->auth.authlen, sum);

		if (skb_copy_bits(skb, skb->len-esp->auth.authlen, sum1, icvsize))
			BUG();

		if (unlikely(memcmp(sum, sum1, icvsize))) {
			x->stats.integrity_failed++;
			goto out;
		}
	}

	if ((nfrags = skb_cow_data(skb, 0, &trailer)) < 0)
		goto out;

	esph = (struct ip_esp_hdr*)skb->data;
	iph = skb->nh.iph;

	/* Get ivec. This can be wrong, check against another impls. */
	if (esp->conf.ivlen)
		crypto_cipher_set_iv(esp->conf.tfm, esph->enc_data, crypto_tfm_alg_ivsize(esp->conf.tfm));

        {
		u8 nexthdr[2];
		struct scatterlist sgbuf[nfrags>MAX_SG_ONSTACK ? 0 : nfrags];
		struct scatterlist *sg = sgbuf;
		u8 workbuf[60];
		int padlen;

		if (unlikely(nfrags > MAX_SG_ONSTACK)) {
			sg = kmalloc(sizeof(struct scatterlist)*nfrags, GFP_ATOMIC);
			if (!sg)
				goto out;
		}
		skb_to_sgvec(skb, sg, 8+esp->conf.ivlen, elen);
		crypto_cipher_decrypt(esp->conf.tfm, sg, nfrags);
		if (unlikely(sg != sgbuf))
			kfree(sg);

		if (skb_copy_bits(skb, skb->len-esp->auth.authlen-2,
				  nexthdr, 2))
			BUG();

		padlen = nexthdr[0];
		if (padlen+2 >= elen)
			goto out;

		/* ... check padding bits here. Silly. :-) */ 

		iph->protocol = nexthdr[1];
		pskb_trim(skb, skb->len - esp->auth.authlen - padlen - 2);
		memcpy(workbuf, skb->nh.raw, iph->ihl*4);
		skb->h.raw = skb_pull(skb, 8 + esp->conf.ivlen);
		skb->nh.raw += 8 + esp->conf.ivlen;
		memcpy(skb->nh.raw, workbuf, iph->ihl*4);
		skb->nh.iph->tot_len = htons(skb->len);
	}

	return 0;

out:
	return -EINVAL;
}

static u32 esp4_get_max_size(struct xfrm_state *x, int mtu)
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

	return mtu + x->props.header_len + esp->auth.authlen;
}

void esp4_err(struct sk_buff *skb, u32 info)
{
	struct iphdr *iph = (struct iphdr*)skb->data;
	struct ip_esp_hdr *esph = (struct ip_esp_hdr*)(skb->data+(iph->ihl<<2));
	struct xfrm_state *x;

	if (skb->h.icmph->type != ICMP_DEST_UNREACH ||
	    skb->h.icmph->code != ICMP_FRAG_NEEDED)
		return;

	x = xfrm_state_lookup(iph->daddr, esph->spi, IPPROTO_ESP);
	if (!x)
		return;
	printk(KERN_DEBUG "pmtu discvovery on SA ESP/%08x/%08x\n",
	       ntohl(esph->spi), ntohl(iph->daddr));
	xfrm_state_put(x);
}

void esp_destroy(struct xfrm_state *x)
{
	struct esp_data *esp = x->data;

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
}

int esp_init_state(struct xfrm_state *x, void *args)
{
	struct esp_data *esp = NULL;

	if (x->aalg) {
		if (x->aalg->alg_key_len == 0 || x->aalg->alg_key_len > 512)
			goto error;
	}
	if (x->ealg == NULL || x->ealg->alg_key_len == 0)
		goto error;

	esp = kmalloc(sizeof(*esp), GFP_KERNEL);
	if (esp == NULL)
		return -ENOMEM;

	if (x->aalg) {
		esp->auth.key = x->aalg->alg_key;
		esp->auth.key_len = (x->aalg->alg_key_len+7)/8;
		esp->auth.tfm = crypto_alloc_tfm(x->aalg->alg_name, 0);
		if (esp->auth.tfm == NULL)
			goto error;
		esp->auth.digest = esp_hmac_digest;
		esp->auth.authlen = crypto_tfm_alg_digestsize(esp->auth.tfm);
	}
	esp->conf.key = x->ealg->alg_key;
	esp->conf.key_len = (x->ealg->alg_key_len+7)/8;
	esp->conf.tfm = crypto_alloc_tfm(x->ealg->alg_name, CRYPTO_TFM_MODE_CBC);
	if (esp->conf.tfm == NULL)
		goto error;
	esp->conf.ivlen = crypto_tfm_alg_ivsize(esp->conf.tfm);
	esp->conf.padlen = 0;
	if (esp->conf.ivlen) {
		esp->conf.ivec = kmalloc(esp->conf.ivlen, GFP_KERNEL);
		get_random_bytes(esp->conf.ivec, esp->conf.ivlen);
	}
	crypto_cipher_setkey(esp->conf.tfm, esp->conf.key, esp->conf.key_len);
	x->props.header_len = 8 + esp->conf.ivlen;
	if (x->props.mode)
		x->props.header_len += 20;
	x->props.trailer_len = esp->auth.authlen + crypto_tfm_alg_blocksize(esp->conf.tfm);
	x->data = esp;
	return 0;

error:
	if (esp) {
		if (esp->auth.tfm)
			crypto_free_tfm(esp->auth.tfm);
		if (esp->conf.tfm)
			crypto_free_tfm(esp->conf.tfm);
		kfree(esp);
	}
	return -EINVAL;
}

static struct xfrm_type esp_type =
{
	.description	= "ESP4",
	.proto	     	= IPPROTO_ESP,
	.init_state	= esp_init_state,
	.destructor	= esp_destroy,
	.get_max_size	= esp4_get_max_size,
	.input		= esp_input,
	.output		= esp_output
};

static struct inet_protocol esp4_protocol = {
	.handler	=	xfrm4_rcv,
	.err_handler	=	esp4_err,
	.no_policy	=	1,
};

int __init esp4_init(void)
{
	SET_MODULE_OWNER(&esp_type);
	if (xfrm_register_type(&esp_type) < 0) {
		printk(KERN_INFO "ip esp init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (inet_add_protocol(&esp4_protocol, IPPROTO_ESP) < 0) {
		printk(KERN_INFO "ip esp init: can't add protocol\n");
		xfrm_unregister_type(&esp_type);
		return -EAGAIN;
	}
	return 0;
}

static void __exit esp4_fini(void)
{
	if (inet_del_protocol(&esp4_protocol, IPPROTO_ESP) < 0)
		printk(KERN_INFO "ip esp close: can't remove protocol\n");
	if (xfrm_unregister_type(&esp_type) < 0)
		printk(KERN_INFO "ip esp close: can't remove xfrm type\n");
}

module_init(esp4_init);
module_exit(esp4_fini);
MODULE_LICENSE("GPL");
