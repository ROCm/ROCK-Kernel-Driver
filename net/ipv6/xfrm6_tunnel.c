/*
 * Copyright (C)2003 USAGI/WIDE Project
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
 * Author	Mitsuru KANDA  <mk@linux-ipv6.org>
 *
 * Based on xfrm4_tunnel
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/xfrm.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/icmp.h>
#include <net/ipv6.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>

#define Iprintk(x...) 

#define XFRM6_TUNNEL_HSIZE 1024
/* note: we assume index of xfrm_tunnel_table[] == spi */
static xfrm_address_t *xfrm6_tunnel_table[XFRM6_TUNNEL_HSIZE];

static spinlock_t xfrm6_tunnel_lock = SPIN_LOCK_UNLOCKED;

static unsigned xfrm6_addr_hash(xfrm_address_t *addr)
{
	unsigned h;
	Iprintk(KERN_DEBUG "%s:called\n", __FUNCTION__);
	h = ntohl(addr->a6[0]^addr->a6[1]^addr->a6[2]^addr->a6[3]);
	h = (h ^ (h>>16)) % XFRM6_TUNNEL_HSIZE;
	Iprintk(KERN_DEBUG "%s:hash:%u\n", __FUNCTION__, h);
	return h;
}

static void xfrm6_tunnel_htable_init(void)
{
	int i;
	Iprintk(KERN_DEBUG "%s:called\n", __FUNCTION__);
	for (i=0; i<XFRM6_TUNNEL_HSIZE; i++)
		xfrm6_tunnel_table[i] = NULL;
}

u32 xfrm6_tunnel_spi_lookup(xfrm_address_t *saddr)
{
	u32 spi = 0;
	u32 index = xfrm6_addr_hash(saddr);
	xfrm_address_t *index_addr;
	int i;

	Iprintk(KERN_DEBUG "%s:called\n", __FUNCTION__);
	spin_lock(&xfrm6_tunnel_lock);
	for (i = index; i < XFRM6_TUNNEL_HSIZE; i++) {
		index_addr = xfrm6_tunnel_table[i];
		if (index_addr == NULL)
			continue;
		if (!memcmp(index_addr, saddr, sizeof(xfrm_address_t))) {
			Iprintk(KERN_DEBUG "%s:match\n", __FUNCTION__);
			spi = htonl(i);
			goto out;
		}
	}
out:
	spin_unlock(&xfrm6_tunnel_lock);
	Iprintk(KERN_DEBUG "%s:spi:%u\n", __FUNCTION__,ntohl(spi));
	return spi;
}

u32 xfrm6_tunnel_alloc_spi(xfrm_address_t *saddr)
{
	u32 spi = 0;
	u32 index = xfrm6_addr_hash(saddr);
	xfrm_address_t *index_addr;
	int i;

	spin_lock(&xfrm6_tunnel_lock);
	for (i = index; i < XFRM6_TUNNEL_HSIZE; i++) {
		if (xfrm6_tunnel_table[i] == NULL) {
			Iprintk(KERN_DEBUG "%s:new alloc:"
				"%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
				__FUNCTION__, NIP6(*(struct in6_addr *)saddr));
			index_addr = kmalloc(sizeof(xfrm_address_t), GFP_ATOMIC);
			memcpy(index_addr, saddr, sizeof(xfrm_address_t));
			xfrm6_tunnel_table[i] = index_addr;
			spi = htonl(i);
			goto out;
		}
	}

out:
	spin_unlock(&xfrm6_tunnel_lock);
	Iprintk(KERN_DEBUG "%s:spi:%u\n", __FUNCTION__,ntohl(spi));
	return spi;
}

static void xfrm6_tunnel_free_spi(xfrm_address_t *saddr){
	u32 index = ntohl(xfrm6_tunnel_spi_lookup(saddr));

	Iprintk(KERN_DEBUG "%s:spi:%u\n", __FUNCTION__,index);
	if (index) {
		spin_lock(&xfrm6_tunnel_lock);
		kfree(xfrm6_tunnel_table[index]);
		xfrm6_tunnel_table[index] = NULL;
		spin_unlock(&xfrm6_tunnel_lock);
		Iprintk(KERN_DEBUG "%s:spi freed\n", __FUNCTION__);
	}
}


int xfrm6_tunnel_check_size(struct sk_buff *skb)
{
	int mtu, ret = 0;
	struct dst_entry *dst = skb->dst;

	mtu = dst_pmtu(dst) - sizeof(struct ipv6hdr);
	if (mtu < IPV6_MIN_MTU)
		mtu = IPV6_MIN_MTU;

	if (skb->len > mtu) {
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu, skb->dev);
		ret = -EMSGSIZE;
	}

	return ret;
}

static int ip6ip6_output(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x = dst->xfrm;
	struct ipv6hdr *iph, *top_iph;
	int err;

	if ((err = xfrm6_tunnel_check_size(skb)) != 0)
		goto error_nolock;

	iph = skb->nh.ipv6h;

	top_iph = (struct ipv6hdr *)skb_push(skb, x->props.header_len);
	top_iph->version = 6;
	top_iph->priority = iph->priority;
	top_iph->flow_lbl[0] = iph->flow_lbl[0];
	top_iph->flow_lbl[1] = iph->flow_lbl[1];
	top_iph->flow_lbl[2] = iph->flow_lbl[2];
	top_iph->nexthdr = IPPROTO_IPV6; 
	top_iph->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
	top_iph->hop_limit = iph->hop_limit;
	memcpy(&top_iph->saddr, (struct in6_addr *)&x->props.saddr, sizeof(struct in6_addr));
	memcpy(&top_iph->daddr, (struct in6_addr *)&x->id.daddr, sizeof(struct in6_addr));
	skb->nh.raw = skb->data;
	skb->h.raw = skb->nh.raw + sizeof(struct ipv6hdr);

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

static int ip6ip6_xfrm_rcv(struct xfrm_state *x, struct xfrm_decap_state *decap, struct sk_buff *skb)
{
	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr))) 
		return -EINVAL;

	skb->mac.raw = skb->nh.raw;
	skb->nh.raw = skb->data;
	dst_release(skb->dst);
	skb->dst = NULL;
	skb->protocol = htons(ETH_P_IPV6);
	skb->pkt_type = PACKET_HOST;
	netif_rx(skb);

	return 0;
}

static struct xfrm6_tunnel *ip6ip6_handler;
static DECLARE_MUTEX(xfrm6_tunnel_sem);

int xfrm6_tunnel_register(struct xfrm6_tunnel *handler)
{
	int ret;

	down(&xfrm6_tunnel_sem);
	ret = 0;
	if (ip6ip6_handler != NULL)
		ret = -EINVAL;
	if (!ret)
		ip6ip6_handler = handler;
	up(&xfrm6_tunnel_sem);

	return ret;
}

int xfrm6_tunnel_deregister(struct xfrm6_tunnel *handler)
{
	int ret;

	down(&xfrm6_tunnel_sem);
	ret = 0;
	if (ip6ip6_handler != handler)
	ret = -EINVAL;
	if (!ret)
		ip6ip6_handler = NULL;
	up(&xfrm6_tunnel_sem);

	synchronize_net();

	return ret;
}

static int ip6ip6_rcv(struct sk_buff **pskb, unsigned int *nhoffp)
{
	struct sk_buff *skb = *pskb;
	struct xfrm6_tunnel *handler = ip6ip6_handler;
	struct xfrm_state *x = NULL;
	struct ipv6hdr *iph = skb->nh.ipv6h;
	int err = 0;
	u32 spi;

	/* device-like_ip6ip6_handler() */
	if (handler) {
		err = handler->handler(pskb, nhoffp);
		if (!err)
			goto out;
	}

	spi = xfrm6_tunnel_spi_lookup((xfrm_address_t *)&iph->saddr);
	Iprintk(KERN_DEBUG "%s:spi:%u\n", __FUNCTION__,spi);
	x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr, 
			spi,
			IPPROTO_IPV6, AF_INET6);

	if (!x)
		goto drop;

	spin_lock(&x->lock);

	if (unlikely(x->km.state != XFRM_STATE_VALID))
		goto drop_unlock;

	err = ip6ip6_xfrm_rcv(x, NULL, skb);
	if (err)
		goto drop_unlock;

	x->curlft.bytes += skb->len;
	x->curlft.packets++; 
	spin_unlock(&x->lock); 
	xfrm_state_put(x); 


out:
	return 0;

drop_unlock:
	spin_unlock(&x->lock);
	xfrm_state_put(x);
drop:
	kfree_skb(skb);

	return -1;
}

static void ip6ip6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			int type, int code, int offset, __u32 info)
{
	struct xfrm6_tunnel *handler = ip6ip6_handler;

	/* call here first for device-like ip6ip6 err handling */
	if (handler) {
		handler->err_handler(skb, opt, type, code, offset, info);
		return;
	}

	/* xfrm ip6ip6 native err handling */
	switch (type) {
	case ICMPV6_DEST_UNREACH: 
		switch (code) {
		case ICMPV6_NOROUTE: 
		case ICMPV6_ADM_PROHIBITED:
		case ICMPV6_NOT_NEIGHBOUR:
		case ICMPV6_ADDR_UNREACH:
		case ICMPV6_PORT_UNREACH:
		default:
			NETDEBUG(printk(KERN_ERR "xfrm ip6ip6: Destination Unreach.\n"););
			break;
		}
		break;
	case ICMPV6_PKT_TOOBIG:
			NETDEBUG(printk(KERN_ERR "xfrm ip6ip6: Packet Too Big.\n"));
		break;
	case ICMPV6_TIME_EXCEED:
		switch (code) {
		case ICMPV6_EXC_HOPLIMIT:
			NETDEBUG(printk(KERN_ERR "xfrm ip6ip6: Too small Hoplimit.\n"));
			break;
		case ICMPV6_EXC_FRAGTIME:
		default: 
			break;
		}
		break;
	case ICMPV6_PARAMPROB:
		switch (code) {
		case ICMPV6_HDR_FIELD: break;
		case ICMPV6_UNK_NEXTHDR: break;
		case ICMPV6_UNK_OPTION: break;
		}
		break;
	default:
		break;
	}
	return;
}

static int ip6ip6_init_state(struct xfrm_state *x, void *args)
{
	if (!x->props.mode)
		return -EINVAL;

	x->props.header_len = sizeof(struct ipv6hdr);

	return 0;
}

static void ip6ip6_destroy(struct xfrm_state *x)
{
	xfrm6_tunnel_free_spi((xfrm_address_t *)&x->props.saddr);
}

static struct xfrm_type ip6ip6_type = {
	.description	= "IP6IP6",
	.owner          = THIS_MODULE,
	.proto		= IPPROTO_IPV6,
	.init_state	= ip6ip6_init_state,
	.destructor	= ip6ip6_destroy,
	.input		= ip6ip6_xfrm_rcv,
	.output		= ip6ip6_output,
};

static struct inet6_protocol ip6ip6_protocol = {
	.handler	= ip6ip6_rcv,
	.err_handler	= ip6ip6_err, 
	.flags          = INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

#if 0
static int __init ip6ip6_init(void)
#else
int __init ip6ip6_init(void)
#endif
{
	Iprintk(KERN_DEBUG "ip6ip6 init\n");
	if (xfrm_register_type(&ip6ip6_type, AF_INET6) < 0) {
		printk(KERN_INFO "ip6ip6 init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (inet6_add_protocol(&ip6ip6_protocol, IPPROTO_IPV6) < 0) {
		printk(KERN_INFO "ip6ip6 init: can't add protocol\n");
		xfrm_unregister_type(&ip6ip6_type, AF_INET6);
		return -EAGAIN;
	}
	xfrm6_tunnel_htable_init();
	return 0;
}

#if 0
static void __exit ip6ip6_fini(void)
#else
void __exit ip6ip6_fini(void)
#endif
{
	Iprintk(KERN_DEBUG "ip6ip6 fini\n");
	if (inet6_del_protocol(&ip6ip6_protocol, IPPROTO_IPV6) < 0)
		printk(KERN_INFO "ip6ip6 close: can't remove protocol\n");
	if (xfrm_unregister_type(&ip6ip6_type, AF_INET6) < 0)
		printk(KERN_INFO "ip6ip6 close: can't remove xfrm type\n");
}

#if 0
module_init(ip6ip6_init);
module_exit(ip6ip6_fini);
MODULE_LICENSE("GPL");
#endif
