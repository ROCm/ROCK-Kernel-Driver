/*
 * IP Payload Compression Protocol (IPComp) for IPv6 - RFC3173
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Author	Mitsuru KANDA  <mk@linux-ipv6.org>
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
 */
/* 
 * [Memo]
 *
 * Outbound:
 *  The compression of IP datagram MUST be done before AH/ESP processing, 
 *  fragmentation, and the addition of Hop-by-Hop/Routing header. 
 *
 * Inbound:
 *  The decompression of IP datagram MUST be done after the reassembly, 
 *  AH/ESP processing.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/ipcomp.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <linux/random.h>
#include <net/icmp.h>
#include <net/ipv6.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>

static int ipcomp6_input(struct xfrm_state *x, struct xfrm_decap_state *decap, struct sk_buff *skb)
{
	int err = 0;
	u8 nexthdr = 0;
	int hdr_len = skb->h.raw - skb->nh.raw;
	unsigned char *tmp_hdr = NULL;
	struct ipv6hdr *iph;
	int plen, dlen;
	struct ipcomp_data *ipcd = x->data;
	u8 *start, *scratch = ipcd->scratch;

	if ((skb_is_nonlinear(skb) || skb_cloned(skb)) &&
		skb_linearize(skb, GFP_ATOMIC) != 0) {
		err = -ENOMEM;
		goto out;
	}

	skb->ip_summed = CHECKSUM_NONE;

	/* Remove ipcomp header and decompress original payload */
	iph = skb->nh.ipv6h;
	tmp_hdr = kmalloc(hdr_len, GFP_ATOMIC);
	if (!tmp_hdr)
		goto out;
	memcpy(tmp_hdr, iph, hdr_len);
	nexthdr = *(u8 *)skb->data;
	skb_pull(skb, sizeof(struct ipv6_comp_hdr)); 
	skb->nh.raw += sizeof(struct ipv6_comp_hdr);
	memcpy(skb->nh.raw, tmp_hdr, hdr_len);
	iph = skb->nh.ipv6h;
	iph->payload_len = htons(ntohs(iph->payload_len) - sizeof(struct ipv6_comp_hdr));
	skb->h.raw = skb->data;

	/* decompression */
	plen = skb->len;
	dlen = IPCOMP_SCRATCH_SIZE;
	start = skb->data;

	err = crypto_comp_decompress(ipcd->tfm, start, plen, scratch, &dlen);
	if (err) {
		err = -EINVAL;
		goto out;
	}

	if (dlen < (plen + sizeof(struct ipv6_comp_hdr))) {
		err = -EINVAL;
		goto out;
	}

	err = pskb_expand_head(skb, 0, dlen - plen, GFP_ATOMIC);
	if (err) {
		goto out;
	}

	skb_put(skb, dlen - plen);
	memcpy(skb->data, scratch, dlen);

	iph = skb->nh.ipv6h;
	iph->payload_len = htons(skb->len);
	
out:
	if (tmp_hdr)
		kfree(tmp_hdr);
	if (err)
		goto error_out;
	return nexthdr;
error_out:
	return err;
}

static int ipcomp6_output(struct sk_buff **pskb)
{
	int err;
	struct dst_entry *dst = (*pskb)->dst;
	struct xfrm_state *x = dst->xfrm;
	struct ipv6hdr *top_iph;
	int hdr_len;
	struct ipv6_comp_hdr *ipch;
	struct ipcomp_data *ipcd = x->data;
	int plen, dlen;
	u8 *start, *scratch = ipcd->scratch;

	hdr_len = (*pskb)->h.raw - (*pskb)->data;

	/* check whether datagram len is larger than threshold */
	if (((*pskb)->len - hdr_len) < ipcd->threshold) {
		goto out_ok;
	}

	if ((skb_is_nonlinear(*pskb) || skb_cloned(*pskb)) &&
		skb_linearize(*pskb, GFP_ATOMIC) != 0) {
		err = -ENOMEM;
		goto error;
	}

	/* compression */
	plen = (*pskb)->len - hdr_len;
	dlen = IPCOMP_SCRATCH_SIZE;
	start = (*pskb)->h.raw;

	err = crypto_comp_compress(ipcd->tfm, start, plen, scratch, &dlen);
	if (err) {
		goto error;
	}
	if ((dlen + sizeof(struct ipv6_comp_hdr)) >= plen) {
		goto out_ok;
	}
	memcpy(start + sizeof(struct ip_comp_hdr), scratch, dlen);
	pskb_trim(*pskb, hdr_len + dlen + sizeof(struct ip_comp_hdr));

	/* insert ipcomp header and replace datagram */
	top_iph = (struct ipv6hdr *)(*pskb)->data;

	top_iph->payload_len = htons((*pskb)->len - sizeof(struct ipv6hdr));

	ipch = (struct ipv6_comp_hdr *)start;
	ipch->nexthdr = *(*pskb)->nh.raw;
	ipch->flags = 0;
	ipch->cpi = htons((u16 )ntohl(x->id.spi));
	*(*pskb)->nh.raw = IPPROTO_COMP;

out_ok:
	err = 0;

error:
	return err;
}

static void ipcomp6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		                int type, int code, int offset, __u32 info)
{
	u32 spi;
	struct ipv6hdr *iph = (struct ipv6hdr*)skb->data;
	struct ipv6_comp_hdr *ipcomph = (struct ipv6_comp_hdr*)(skb->data+offset);
	struct xfrm_state *x;

	if (type != ICMPV6_DEST_UNREACH && type != ICMPV6_PKT_TOOBIG)
		return;

	spi = ntohl(ntohs(ipcomph->cpi));
	x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, spi, IPPROTO_COMP, AF_INET6);
	if (!x)
		return;

	printk(KERN_DEBUG "pmtu discovery on SA IPCOMP/%08x/"
			"%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
			spi, NIP6(iph->daddr));
	xfrm_state_put(x);
}

static struct xfrm_state *ipcomp6_tunnel_create(struct xfrm_state *x)
{
	struct xfrm_state *t = NULL;

	t = xfrm_state_alloc();
	if (!t)
		goto out;

	t->id.proto = IPPROTO_IPV6;
	t->id.spi = xfrm6_tunnel_alloc_spi((xfrm_address_t *)&x->props.saddr);
	memcpy(t->id.daddr.a6, x->id.daddr.a6, sizeof(struct in6_addr));
	memcpy(&t->sel, &x->sel, sizeof(t->sel));
	t->props.family = AF_INET6;
	t->props.mode = 1;
	memcpy(t->props.saddr.a6, x->props.saddr.a6, sizeof(struct in6_addr));

	t->type = xfrm_get_type(IPPROTO_IPV6, t->props.family);
	if (t->type == NULL)
		goto error;

	if (t->type->init_state(t, NULL))
		goto error;

	t->km.state = XFRM_STATE_VALID;
	atomic_set(&t->tunnel_users, 1);

out:
	return t;

error:
	xfrm_state_put(t);
	goto out;
}

static int ipcomp6_tunnel_attach(struct xfrm_state *x)
{
	int err = 0;
	struct xfrm_state *t = NULL;
	u32 spi;

	spi = xfrm6_tunnel_spi_lookup((xfrm_address_t *)&x->props.saddr);
	if (spi)
		t = xfrm_state_lookup((xfrm_address_t *)&x->id.daddr,
					      spi, IPPROTO_IPV6, AF_INET6);
	if (!t) {
		t = ipcomp6_tunnel_create(x);
		if (!t) {
			err = -EINVAL;
			goto out;
		}
		xfrm_state_insert(t);
		xfrm_state_hold(t);
	}
	x->tunnel = t;
	atomic_inc(&t->tunnel_users);

out:
	return err;
}

static void ipcomp6_free_data(struct ipcomp_data *ipcd)
{
	if (ipcd->tfm)
		crypto_free_tfm(ipcd->tfm);
	if (ipcd->scratch)
		kfree(ipcd->scratch);
}

static void ipcomp6_destroy(struct xfrm_state *x)
{
	struct ipcomp_data *ipcd = x->data;
	if (!ipcd)
		return;
	xfrm_state_delete_tunnel(x);
	ipcomp6_free_data(ipcd);
	kfree(ipcd);

	xfrm6_tunnel_free_spi((xfrm_address_t *)&x->props.saddr);
}

static int ipcomp6_init_state(struct xfrm_state *x, void *args)
{
	int err;
	struct ipcomp_data *ipcd;
	struct xfrm_algo_desc *calg_desc;

	err = -EINVAL;
	if (!x->calg)
		goto out;

	err = -ENOMEM;
	ipcd = kmalloc(sizeof(*ipcd), GFP_KERNEL);
	if (!ipcd)
		goto error;

	memset(ipcd, 0, sizeof(*ipcd));
	x->props.header_len = 0;
	if (x->props.mode)
		x->props.header_len += sizeof(struct ipv6hdr);
	
	ipcd->scratch = kmalloc(IPCOMP_SCRATCH_SIZE, GFP_KERNEL);
	if (!ipcd->scratch)
		goto error;

	ipcd->tfm = crypto_alloc_tfm(x->calg->alg_name, 0);
	if (!ipcd->tfm)
		goto error;

	if (x->props.mode) {
		err = ipcomp6_tunnel_attach(x);
		if (err)
			goto error;
	}

	calg_desc = xfrm_calg_get_byname(x->calg->alg_name);
	BUG_ON(!calg_desc);
	ipcd->threshold = calg_desc->uinfo.comp.threshold;
	x->data = ipcd;
	err = 0;
out:
	return err;
error:
	if (ipcd) {
		ipcomp6_free_data(ipcd);
		kfree(ipcd);
	}

	goto out;
}

static struct xfrm_type ipcomp6_type = 
{
	.description	= "IPCOMP6",
	.owner		= THIS_MODULE,
	.proto		= IPPROTO_COMP,
	.init_state	= ipcomp6_init_state,
	.destructor	= ipcomp6_destroy,
	.input		= ipcomp6_input,
	.output		= ipcomp6_output,
};

static struct inet6_protocol ipcomp6_protocol = 
{
	.handler	= xfrm6_rcv,
	.err_handler	= ipcomp6_err,
	.flags		= INET6_PROTO_NOPOLICY,
};

static int __init ipcomp6_init(void)
{
	if (xfrm_register_type(&ipcomp6_type, AF_INET6) < 0) {
		printk(KERN_INFO "ipcomp6 init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (inet6_add_protocol(&ipcomp6_protocol, IPPROTO_COMP) < 0) {
		printk(KERN_INFO "ipcomp6 init: can't add protocol\n");
		xfrm_unregister_type(&ipcomp6_type, AF_INET6);
		return -EAGAIN;
	}
	return 0;
}

static void __exit ipcomp6_fini(void)
{
	if (inet6_del_protocol(&ipcomp6_protocol, IPPROTO_COMP) < 0) 
		printk(KERN_INFO "ipv6 ipcomp close: can't remove protocol\n");
	if (xfrm_unregister_type(&ipcomp6_type, AF_INET6) < 0)
		printk(KERN_INFO "ipv6 ipcomp close: can't remove xfrm type\n");
}

module_init(ipcomp6_init);
module_exit(ipcomp6_fini);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IP Payload Compression Protocol (IPComp) for IPv6 - RFC3173");
MODULE_AUTHOR("Mitsuru KANDA <mk@linux-ipv6.org>");


