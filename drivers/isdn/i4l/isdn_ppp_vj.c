
#include "isdn_ppp_vj.h"
#include "isdn_common.h"
#include "isdn_net.h"
#include "isdn_ppp.h"

/* ====================================================================== */
/* VJ header compression                                                  */
/* ====================================================================== */

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
	struct slcompress *sltmp;

	sltmp = slhc_init(16, val + 1);
	if (!sltmp)
		return -ENOMEM;

	if (idev->mlp->slcomp)
		slhc_free(idev->mlp->slcomp);

	idev->mlp->slcomp = sltmp;
	return 0;
}

struct sk_buff *
ippp_vj_decompress(struct slcompress *slcomp, struct sk_buff *skb_old, 
		   u16 proto)
{
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
	skb->protocol = htons(ETH_P_IP);
	return skb;

 drop_both:
	kfree_skb(skb);
 drop:
	kfree_skb(skb_old);
	return NULL;
}

struct sk_buff *
ippp_vj_compress(isdn_net_dev *idev, struct sk_buff *skb_old, u16 *proto)
{
	struct sk_buff *skb;
	unsigned char *buf;
	int len;

	if (!(idev->pppcfg & SC_COMP_TCP) || *proto != PPP_IP)
		return skb_old;

	skb = isdn_ppp_dev_alloc_skb(idev, skb_old->len, GFP_ATOMIC);
	if (!skb)
		return skb_old;

	skb_put(skb, skb_old->len);
	buf = skb_old->data;
	len = slhc_compress(idev->mlp->slcomp, skb_old->data, skb_old->len,
			    skb->data, &buf, !(idev->pppcfg & SC_NO_TCP_CCID));

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

