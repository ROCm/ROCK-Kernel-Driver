/* xfrm4_tunnel.c: Generic IP tunnel transformer.
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/skbuff.h>
#include <net/xfrm.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/inet_ecn.h>

int xfrm4_tunnel_check_size(struct sk_buff *skb)
{
	int mtu, ret = 0;
	struct dst_entry *dst;
	struct iphdr *iph = skb->nh.iph;

	if (IPCB(skb)->flags & IPSKB_XFRM_TUNNEL_SIZE)
		goto out;

	IPCB(skb)->flags |= IPSKB_XFRM_TUNNEL_SIZE;
	
	if (!(iph->frag_off & htons(IP_DF)))
		goto out;

	dst = skb->dst;
	mtu = dst_pmtu(dst) - dst->header_len - dst->trailer_len;
	if (skb->len > mtu) {
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
		ret = -EMSGSIZE;
	}
out:
	return ret;
}

static int ipip_output(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x = dst->xfrm;
	struct iphdr *iph, *top_iph;
	int tos, err;

	if ((err = xfrm4_tunnel_check_size(skb)) != 0)
		goto error_nolock;
		
	iph = skb->nh.iph;

	spin_lock_bh(&x->lock);

	tos = iph->tos;

	top_iph = (struct iphdr *) skb_push(skb, x->props.header_len);
	top_iph->ihl = 5;
	top_iph->version = 4;
	top_iph->tos = INET_ECN_encapsulate(tos, iph->tos);
	top_iph->tot_len = htons(skb->len);
	top_iph->frag_off = iph->frag_off & ~htons(IP_MF|IP_OFFSET);
	if (!(iph->frag_off & htons(IP_DF)))
		__ip_select_ident(top_iph, dst, 0);
	top_iph->ttl = iph->ttl;
	top_iph->protocol = IPPROTO_IPIP;
	top_iph->check = 0;
	top_iph->saddr = x->props.saddr.a4;
	top_iph->daddr = x->id.daddr.a4;
	memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));
	ip_send_check(top_iph);

	skb->nh.raw = skb->data;
	x->curlft.bytes += skb->len;
	x->curlft.packets++;

	spin_unlock_bh(&x->lock);

	if ((skb->dst = dst_pop(dst)) == NULL) {
		kfree_skb(skb);
		err = -EHOSTUNREACH;
		goto error_nolock;
	}
	return NET_XMIT_BYPASS;

error_nolock:
	kfree_skb(skb);
	return err;
}

static int ipip_xfrm_rcv(struct xfrm_state *x, struct xfrm_decap_state *decap, struct sk_buff *skb)
{
	return 0;
}

static struct xfrm_tunnel *ipip_handler;
static DECLARE_MUTEX(xfrm4_tunnel_sem);

int xfrm4_tunnel_register(struct xfrm_tunnel *handler)
{
	int ret;

	down(&xfrm4_tunnel_sem);
	ret = 0;
	if (ipip_handler != NULL)
		ret = -EINVAL;
	if (!ret)
		ipip_handler = handler;
	up(&xfrm4_tunnel_sem);

	return ret;
}

int xfrm4_tunnel_deregister(struct xfrm_tunnel *handler)
{
	int ret;

	down(&xfrm4_tunnel_sem);
	ret = 0;
	if (ipip_handler != handler)
		ret = -EINVAL;
	if (!ret)
		ipip_handler = NULL;
	up(&xfrm4_tunnel_sem);

	synchronize_net();

	return ret;
}

static int ipip_rcv(struct sk_buff *skb)
{
	struct xfrm_tunnel *handler = ipip_handler;

	/* Tunnel devices take precedence.  */
	if (handler && handler->handler(skb) == 0)
		return 0;

	return xfrm4_rcv_encap(skb, 0);
}

static void ipip_err(struct sk_buff *skb, u32 info)
{
	struct xfrm_tunnel *handler = ipip_handler;
	u32 arg = info;

	if (handler)
		handler->err_handler(skb, &arg);
}

static int ipip_init_state(struct xfrm_state *x, void *args)
{
	if (!x->props.mode)
		return -EINVAL;
	x->props.header_len = sizeof(struct iphdr);

	return 0;
}

static void ipip_destroy(struct xfrm_state *x)
{
}

static struct xfrm_type ipip_type = {
	.description	= "IPIP",
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_IPIP,
	.init_state	= ipip_init_state,
	.destructor	= ipip_destroy,
	.input		= ipip_xfrm_rcv,
	.output		= ipip_output
};

static struct inet_protocol ipip_protocol = {
	.handler	=	ipip_rcv,
	.err_handler	=	ipip_err,
	.no_policy	=	1,
};

static int __init ipip_init(void)
{
	if (xfrm_register_type(&ipip_type, AF_INET) < 0) {
		printk(KERN_INFO "ipip init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (inet_add_protocol(&ipip_protocol, IPPROTO_IPIP) < 0) {
		printk(KERN_INFO "ipip init: can't add protocol\n");
		xfrm_unregister_type(&ipip_type, AF_INET);
		return -EAGAIN;
	}
	return 0;
}

static void __exit ipip_fini(void)
{
	if (inet_del_protocol(&ipip_protocol, IPPROTO_IPIP) < 0)
		printk(KERN_INFO "ipip close: can't remove protocol\n");
	if (xfrm_unregister_type(&ipip_type, AF_INET) < 0)
		printk(KERN_INFO "ipip close: can't remove xfrm type\n");
}

module_init(ipip_init);
module_exit(ipip_fini);
MODULE_LICENSE("GPL");
