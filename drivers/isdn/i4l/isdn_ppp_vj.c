/* Linux ISDN subsystem, PPP VJ header compression
 *
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include "isdn_ppp_vj.h"
#include "isdn_common.h"
#include "isdn_net_lib.h"
#include "isdn_ppp.h"

struct slcompress *
ippp_vj_alloc(void)
{
	return slhc_init(16, 16);
}

void
ippp_vj_free(struct slcompress *slcomp)
{
	slhc_free(slcomp);
}

int
ippp_vj_set_maxcid(isdn_net_dev *idev, int val)
{
	struct inl_ppp *inl_ppp = idev->mlp->inl_priv;
	struct slcompress *sltmp;

	sltmp = slhc_init(16, val + 1);
	if (!sltmp)
		return -ENOMEM;

	if (inl_ppp->slcomp)
		slhc_free(inl_ppp->slcomp);

	inl_ppp->slcomp = sltmp;
	return 0;
}

void
ippp_vj_decompress(isdn_net_dev *idev, struct sk_buff *skb_old, u16 proto)
{
	struct inl_ppp *inl_ppp = idev->mlp->inl_priv;
	struct slcompress *slcomp = inl_ppp->slcomp;
	struct sk_buff *skb;
	int len;

	switch (proto) {
	case PPP_VJC_UNCOMP:
		if (slhc_remember(slcomp, skb_old->data, skb_old->len) <= 0)
			goto drop;
		
		skb = skb_old;
		break;
	case PPP_VJC_COMP:
		skb = dev_alloc_skb(skb_old->len + 128);
		if (!skb)
			goto drop;

		memcpy(skb->data, skb_old->data, skb_old->len);
		len = slhc_uncompress(slcomp, skb->data, skb_old->len);
		if (len < 0)
			goto drop_both;

		skb_put(skb, len);
		kfree_skb(skb_old);
		break;
	default:
		isdn_BUG();
		goto drop;
	}
	isdn_netif_rx(idev, skb, htons(ETH_P_IP));
	return;

 drop_both:
	kfree_skb(skb);
 drop:
	kfree_skb(skb_old);
	idev->mlp->stats.rx_dropped++;
}

struct sk_buff *
ippp_vj_compress(isdn_net_dev *idev, struct sk_buff *skb_old, u16 *proto)
{
	struct inl_ppp *inl_ppp = idev->mlp->inl_priv;
	struct ind_ppp *ind_ppp = idev->ind_priv;
	struct slcompress *slcomp = inl_ppp->slcomp;
	struct sk_buff *skb;
	unsigned char *buf;
	int len;

	if (!(ind_ppp->pppcfg & SC_COMP_TCP) || *proto != PPP_IP)
		return skb_old;

	skb = isdn_ppp_dev_alloc_skb(idev, skb_old->len, GFP_ATOMIC);
	if (!skb)
		return skb_old;

	skb_put(skb, skb_old->len);
	buf = skb_old->data;
	// FIXME flag should be per bundle
	len = slhc_compress(slcomp, skb_old->data, skb_old->len, skb->data,
			    &buf, !(ind_ppp->pppcfg & SC_NO_TCP_CCID)); 

	if (buf == skb_old->data) {
		kfree_skb(skb);
		skb = skb_old;
	} else {
		kfree_skb(skb_old);
	}
	skb_trim(skb, len);
			
	/* cslip style -> PPP */
	if ((skb->data[0] & SL_TYPE_COMPRESSED_TCP) == SL_TYPE_COMPRESSED_TCP) {
		skb->data[0] &= ~SL_TYPE_COMPRESSED_TCP;
		*proto = PPP_VJC_COMP;
	} else if ((skb->data[0] & SL_TYPE_UNCOMPRESSED_TCP) == SL_TYPE_UNCOMPRESSED_TCP) {
		skb->data[0] &= ~SL_TYPE_UNCOMPRESSED_TCP;
		skb->data[0] |= SL_TYPE_IP;
		*proto = PPP_VJC_UNCOMP;
	}
	return skb;
}

