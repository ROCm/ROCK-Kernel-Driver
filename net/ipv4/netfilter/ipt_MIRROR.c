/*
  This is a module which is used for resending packets with inverted src and dst.

  Based on code from: ip_nat_dumb.c,v 1.9 1999/08/20
  and various sources.

  Copyright (C) 2000 Emmanuel Roger <winfield@freegates.be>

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2 of the License, or (at your
  option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netdevice.h>
#include <linux/route.h>
struct in_device;
#include <net/route.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static int route_mirror(struct sk_buff *skb)
{
        struct iphdr *iph = skb->nh.iph;
	struct rtable *rt;

	/* Backwards */
	if (ip_route_output(&rt, iph->saddr, iph->daddr,
			    RT_TOS(iph->tos) | RTO_CONN,
			    0)) {
		return 0;
	}

	/* check if the interface we are leaving by is the same as the
           one we arrived on */
	if (skb->dev == rt->u.dst.dev) {
		/* Drop old route. */
		dst_release(skb->dst);
		skb->dst = &rt->u.dst;
		return 1;
	}
	return 0;
}

static void
ip_rewrite(struct sk_buff *skb)
{
	struct iphdr *iph = skb->nh.iph;
	u32 odaddr = iph->saddr;
	u32 osaddr = iph->daddr;

	skb->nfcache |= NFC_ALTERED;

	/* Rewrite IP header */
	iph->daddr = odaddr;
	iph->saddr = osaddr;
}

/* Stolen from ip_finish_output2 */
static void ip_direct_send(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct hh_cache *hh = dst->hh;

	if (hh) {
		read_lock_bh(&hh->hh_lock);
  		memcpy(skb->data - 16, hh->hh_data, 16);
		read_unlock_bh(&hh->hh_lock);
	        skb_push(skb, hh->hh_len);
		hh->hh_output(skb);
	} else if (dst->neighbour)
		dst->neighbour->output(skb);
	else {
		printk(KERN_DEBUG "khm in MIRROR\n");
		kfree_skb(skb);
	}
}

static unsigned int ipt_mirror_target(struct sk_buff **pskb,
				      unsigned int hooknum,
				      const struct net_device *in,
				      const struct net_device *out,
				      const void *targinfo,
				      void *userinfo)
{
	if ((*pskb)->dst != NULL) {
		if (route_mirror(*pskb)) {
			ip_rewrite(*pskb);
			/* Don't let conntrack code see this packet:
                           it will think we are starting a new
                           connection! --RR */
			ip_direct_send(*pskb);
			return NF_STOLEN;
		}
	}
	return NF_DROP;
}

static int ipt_mirror_checkentry(const char *tablename,
				 const struct ipt_entry *e,
				 void *targinfo,
				 unsigned int targinfosize,
				 unsigned int hook_mask)
{
	/* Only on INPUT, FORWARD or PRE_ROUTING, otherwise loop danger. */
	if (hook_mask & ~((1 << NF_IP_PRE_ROUTING)
			  | (1 << NF_IP_FORWARD)
			  | (1 << NF_IP_LOCAL_IN))) {
		DEBUGP("MIRROR: bad hook\n");
		return 0;
	}

	if (targinfosize != IPT_ALIGN(0)) {
		DEBUGP("MIRROR: targinfosize %u != 0\n", targinfosize);
		return 0;
	}

	return 1;
}

static struct ipt_target ipt_mirror_reg
= { { NULL, NULL }, "MIRROR", ipt_mirror_target, ipt_mirror_checkentry, NULL,
    THIS_MODULE };

static int __init init(void)
{
	return ipt_register_target(&ipt_mirror_reg);
}

static void __exit fini(void)
{
	ipt_unregister_target(&ipt_mirror_reg);
}

module_init(init);
module_exit(fini);
