/* Linux ISDN subsystem, PPP CCP support
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include "isdn_ppp_mp.h"
#include "isdn_ppp_ccp.h"
#include "isdn_common.h"
#include "isdn_net_lib.h"
#include "isdn_ppp.h"

/* ====================================================================== */

#define MP_END_FRAG             0x40
#define MP_BEGIN_FRAG           0x80

#define MP_MAX_QUEUE_LEN	16

/* ====================================================================== */

int
ippp_mp_bind(isdn_net_dev *idev)
{
	struct ind_ppp *ind_ppp = idev->ind_priv;
	struct inl_ppp *inl_ppp = idev->mlp->inl_priv;

	/* seq no last seen, maybe set to bundle min, when joining? */
	ind_ppp->mp_rxseq = 0;

	if (!list_empty(&idev->mlp->online))
		return 0;

	/* first channel for this link, do some setup */

	inl_ppp->mp_cfg   = 0;        /* MPPP configuration */
	inl_ppp->mp_txseq = 0;        /* MPPP tx sequence number */
	inl_ppp->mp_rxseq = (u32) -1;
	skb_queue_head_init(&inl_ppp->mp_frags);

	return 0;
}

int
ippp_mp_bundle(isdn_net_dev *idev, int unit)
{
	isdn_net_local *lp = idev->mlp;
	char ifn[IFNAMSIZ + 1];
	isdn_net_dev *n_idev;
	struct ind_ppp *ind_ppp;

	printk(KERN_DEBUG "%s: %s: slave unit: %d\n",
	       __FUNCTION__, idev->name, unit);

	sprintf(ifn, "ippp%d", unit);
	list_for_each_entry(n_idev, &lp->slaves, slaves) {
		if (strcmp(n_idev->name, ifn) == 0)
			goto found;
	}
	
	printk(KERN_INFO "%s: cannot find %s\n", __FUNCTION__, ifn);
	return -ENODEV;

 found:
	ind_ppp = n_idev->ind_priv;
	if (!ind_ppp->ipppd) {
		printk(KERN_INFO "%s: no ipppd?\n", __FUNCTION__);
		return -ENXIO;
	}
	ind_ppp->pppcfg |= SC_ENABLE_IP;
	isdn_net_online(n_idev);

	return 0;
}
  
void
ippp_mp_disconnected(isdn_net_dev *idev)
{
	struct inl_ppp *inl_ppp = idev->mlp->inl_priv;

	if (!list_empty(&idev->mlp->online))
		return;

	/* we're the last link going down */
	skb_queue_purge(&inl_ppp->mp_frags);
}

void
ippp_mp_xmit(isdn_net_dev *idev, struct sk_buff *skb)
{
	struct ind_ppp *ind_ppp = idev->ind_priv;
	struct inl_ppp *inl_ppp = idev->mlp->inl_priv;
	unsigned char *p;
	u32 txseq;
	u16 proto;

	if (!(inl_ppp->mp_cfg & SC_MP_PROT)) {
		return ippp_xmit(idev, skb);
	}

	/* we could do something smarter than just sending
	 * the complete packet as fragment... */

	txseq = inl_ppp->mp_txseq++;
	
	if (inl_ppp->mp_cfg & SC_OUT_SHORT_SEQ) {
		/* sequence number: 12bit */
		p = skb_push(skb, 2);
		p[0] = MP_BEGIN_FRAG | MP_END_FRAG | ((txseq >> 8) & 0xf);
		p[1] = txseq & 0xff;
	} else {
		/* sequence number: 24bit */
		p = skb_push(skb, 4);
		p[0] = MP_BEGIN_FRAG | MP_END_FRAG;
		p[1] = (txseq >> 16) & 0xff;
		p[2] = (txseq >>  8) & 0xff;
		p[3] = (txseq >>  0) & 0xff;
	}
	proto = PPP_MP;
	skb = ippp_ccp_compress(ind_ppp->ccp, skb, &proto);
	ippp_push_proto(ind_ppp, skb, proto);
	ippp_xmit(idev, skb);
}

static void mp_receive(isdn_net_dev *idev, struct sk_buff *skb);

void
ippp_mp_receive(isdn_net_dev *idev, struct sk_buff *skb, u16 proto)
{
	struct ind_ppp *ind_ppp = idev->ind_priv;
	struct inl_ppp *inl_ppp = idev->mlp->inl_priv;

 	if (inl_ppp->mp_cfg & SC_REJ_MP_PROT)
		goto out;

	skb = ippp_ccp_decompress(ind_ppp->ccp, skb, &proto);
	if (!skb)
		goto drop;

	if (proto == PPP_MP)
		return mp_receive(idev, skb);

 out:
	return ippp_receive(idev, skb, proto);

 drop:
	idev->mlp->stats.rx_errors++;
	kfree_skb(skb);
}

#define MP_LONGSEQ_MASK		0x00ffffff
#define MP_SHORTSEQ_MASK	0x00000fff
#define MP_LONGSEQ_MAX		MP_LONGSEQ_MASK
#define MP_SHORTSEQ_MAX		MP_SHORTSEQ_MASK
#define MP_LONGSEQ_MAXBIT	((MP_LONGSEQ_MASK+1)>>1)
#define MP_SHORTSEQ_MAXBIT	((MP_SHORTSEQ_MASK+1)>>1)

/* sequence-wrap safe comparisions (for long sequence)*/ 
#define MP_LT(a,b)	((a-b)&MP_LONGSEQ_MAXBIT)
#define MP_LE(a,b) 	!((b-a)&MP_LONGSEQ_MAXBIT)
#define MP_GT(a,b) 	((b-a)&MP_LONGSEQ_MAXBIT)
#define MP_GE(a,b)	!((a-b)&MP_LONGSEQ_MAXBIT)

#define MP_SEQUENCE(skb) (skb)->priority
#define MP_FLAGS(skb)    (skb)->cb[0]

static u32
get_seq(struct sk_buff *skb, u32 last_seq, int short_seq)
{
	u32 seq;
	u16 shseq;
	u8  flags;
	int delta;
   
	get_u8(skb->data, &flags);
   	if (short_seq) {
		/* convert 12-bit short seq number to 24-bit long one */
		get_u16(skb->data, &shseq);
		delta = (shseq & MP_SHORTSEQ_MASK) - 
			(last_seq & MP_SHORTSEQ_MASK);
		/* check for seqence wrap */
		if (delta < 0)
			delta += MP_SHORTSEQ_MAX + 1;

		seq = last_seq + delta;
		skb_pull(skb, 2);
	} else {
		get_u32(skb->data, &seq);
		skb_pull(skb, 4);
	}
	seq &= MP_LONGSEQ_MASK;
	MP_SEQUENCE(skb) = seq;
	MP_FLAGS(skb) = flags;
	return seq;
}

static int
mp_insert_frag(struct sk_buff_head *frags, struct sk_buff *skb)
{
	struct sk_buff *p;

	/* If our queue of not yet reassembled fragments grows too
	   large, throw away the oldest fragment */
	if (skb_queue_len(frags) > MP_MAX_QUEUE_LEN)
		kfree_skb(skb_dequeue(frags));
	
	for (p = frags->next; p != (struct sk_buff *) frags; p = p->next) {
		if (MP_LE(MP_SEQUENCE(skb), MP_SEQUENCE(p)))
			break;
	}
	/* duplicate ? */
	if (MP_SEQUENCE(skb) == MP_SEQUENCE(p))
		return -EBUSY;

	__skb_insert(skb, p->prev, p, frags);
	return 0;
}

struct sk_buff *
mp_complete_seq(isdn_net_local *lp, struct sk_buff *b, struct sk_buff *e)
{
	struct sk_buff *p, *n, *skb;
	int len = 0;

	if (b->next == e) {
		/* sequence with only one frag */
		skb_unlink(b);
		return b;
	}
	for (p = b, n = p->next; p != e; p = n, n = p->next ) {
		len += p->len;
	}
	// FIXME check against mrru?
	skb = dev_alloc_skb(len);
	if (!skb)
		lp->stats.rx_errors++;

	for (p = b, n = p->next; p != e; p = n, n = p->next ) {
		if (skb)
			memcpy(skb_put(skb, p->len), p->data, p->len);
		
		skb_unlink(p);
		kfree_skb(p);
	}
	return skb;
}

struct sk_buff *
mp_reassemble(isdn_net_local *lp)
{
	struct inl_ppp *inl_ppp = lp->inl_priv;
	struct sk_buff_head *frags = &inl_ppp->mp_frags;
	struct sk_buff *p, *n, *pp, *start;
	u32 min_seq = inl_ppp->mp_rxseq;
	u32 next_seq = 0;

 again:
	start = NULL;
	for (p = frags->next, n = p->next; p != (struct sk_buff *) frags; p = n, n = p->next ) {
		if (!start) {
			if (MP_FLAGS(p) & MP_BEGIN_FRAG) {
				start = p;
				next_seq = MP_SEQUENCE(p);
			} else {
				/* start frag is missing */
				goto frag_missing;
			}
		}
		/* we've seen the first fragment of this series */
		if (MP_SEQUENCE(p) != next_seq) {
			/* previous frag is missing */
			goto frag_missing;
		}
		if (MP_FLAGS(p) & MP_END_FRAG) {
			/* we got a full sequence */
			return mp_complete_seq(lp, start, p->next);
		}
		next_seq = MP_SEQUENCE(p) + 1;
	}
	return NULL;
	
 frag_missing:
	if (MP_SEQUENCE(p) - 1 > min_seq)
		/* may come later */
		return NULL;

	/* for all fragments up to p */
	p = p->next;
	for (pp = frags->next, n = pp->next; pp != p; pp = n, n = pp->next ) {
		skb_unlink(pp);
		kfree_skb(pp);
		lp->stats.rx_errors++;
	}
	goto again;

}

static void
mp_receive(isdn_net_dev *idev, struct sk_buff *skb)
{
	isdn_net_local *lp = idev->mlp;
	struct inl_ppp *inl_ppp = lp->inl_priv;
	struct ind_ppp *ind_ppp = idev->ind_priv;
	isdn_net_dev *qdev;
	struct sk_buff_head *frags = &inl_ppp->mp_frags;
	u32 seq;
	u16 proto;

	if (skb->len < (inl_ppp->mp_cfg & SC_IN_SHORT_SEQ ? 2 : 4))
		goto drop;

	seq = get_seq(skb, ind_ppp->mp_rxseq, inl_ppp->mp_cfg & SC_IN_SHORT_SEQ);
	ind_ppp->mp_rxseq = seq;

	if (inl_ppp->mp_rxseq == (u32) -1) { 
		/* first packet */
		inl_ppp->mp_rxseq = seq;
	}
	if (MP_LT(seq, inl_ppp->mp_rxseq)) {
		goto drop;
	}
	/* Find the minimum sequence number received over all channels.
	 * No fragments with numbers lower than this will arrive later. */
	inl_ppp->mp_rxseq = seq;
	list_for_each_entry(qdev, &lp->online, online) {
		struct ind_ppp *ind_ppp = qdev->ind_priv;
		if (MP_LT(ind_ppp->mp_rxseq, inl_ppp->mp_rxseq))
			inl_ppp->mp_rxseq = ind_ppp->mp_rxseq;
	}

	/* Insert the skb into the list of received fragments, ordered by
	 * sequence number */
	if (mp_insert_frag(frags, skb))
		goto drop;

	while ((skb = mp_reassemble(lp))) {
		if (isdn_ppp_strip_proto(skb, &proto)) {
			kfree_skb(skb);
			continue;
		}
		ippp_receive(idev, skb, proto);
	}
	return;
	
 drop:
	lp->stats.rx_errors++;
	kfree_skb(skb);
}
