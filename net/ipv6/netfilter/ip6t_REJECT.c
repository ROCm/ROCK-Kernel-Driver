/*
 * IP6 tables REJECT target module
 * Linux INET6 implementation
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on net/ipv4/netfilter/ipt_REJECT.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* This module works well with IPv6 Connection Tracking. - kozakai */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmpv6.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <net/icmp.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <net/flow.h>
#include <net/ip6_checksum.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_REJECT.h>

MODULE_AUTHOR("Yasuyuki KOZAKAI <yasuyuki.kozakai@toshiba.co.jp>");
MODULE_DESCRIPTION("IP6 tables REJECT target module");
MODULE_LICENSE("GPL");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static void connection_attach(struct sk_buff *new_skb, struct nf_ct_info *nfct)
{
	void (*attach)(struct sk_buff *, struct nf_ct_info *);
	if (nfct && (attach = ip6_ct_attach) != NULL) {
		mb();
		attach(new_skb, nfct);
	}
}

static int maybe_reroute(struct sk_buff *skb)
{
	if (skb->nfcache & NFC_ALTERED){
		if (ip6_route_me_harder(skb) != 0){
			kfree_skb(skb);
			return -EINVAL;
		}
	}

	return dst_output(skb);
}

/* Send RST reply */
static void send_reset(struct sk_buff *oldskb)
{
	struct sk_buff *nskb;
	struct tcphdr otcph, *tcph;
	unsigned int otcplen, tcphoff, hh_len;
	int needs_ack;
	struct ipv6hdr *oip6h = oldskb->nh.ipv6h, *ip6h;
	struct dst_entry *dst = NULL;
	u8 proto;
	struct flowi fl;
	proto = oip6h->nexthdr;
	int err;

	if ((!(ipv6_addr_type(&oip6h->saddr) & IPV6_ADDR_UNICAST)) ||
	    (!(ipv6_addr_type(&oip6h->daddr) & IPV6_ADDR_UNICAST))) {
		DEBUGP("addr is not unicast.\n");
		return;
	}

	tcphoff = ipv6_skip_exthdr(oldskb, ((u8*)(oip6h+1) - oldskb->data),
				   &proto, oldskb->len - ((u8*)(oip6h+1)
							  - oldskb->data));

	if ((tcphoff < 0) || (tcphoff > oldskb->len)) {
		if (net_ratelimit())
			printk("ip6t_REJECT: Can't get TCP header.\n");
		return;
	}

	otcplen = oldskb->len - tcphoff;

	/* IP header checks: fragment, too short. */
	if ((proto != IPPROTO_TCP) || (otcplen < sizeof(struct tcphdr))) {
		DEBUGP("proto(%d) != IPPROTO_TCP, or too short. otcplen = %d\n",
			proto, otcplen);
		return;
	}

	if (skb_copy_bits(oldskb, tcphoff, &otcph, sizeof(struct tcphdr))) {
		if (net_ratelimit())
			printk("ip6t_REJECT: Can't copy tcp header\n");
		return;
	}

	/* No RST for RST. */
	if (otcph.rst) {
		DEBUGP("RST is set\n");
		return;
	}

	/* Check checksum. */
	if (csum_ipv6_magic(&oip6h->saddr, &oip6h->daddr, otcplen, IPPROTO_TCP,
			    skb_checksum(oldskb, tcphoff, otcplen, 0))) {
		DEBUGP("TCP checksum is invalid\n");
		return;
	}

	memset(&fl, 0, sizeof(fl));
	fl.proto = IPPROTO_TCP;
	ipv6_addr_copy(&fl.fl6_src, &oip6h->daddr);
	ipv6_addr_copy(&fl.fl6_dst, &oip6h->saddr);
	fl.fl_ip_sport = otcph.dest;
	fl.fl_ip_dport = otcph.source;
	err = ip6_dst_lookup(NULL, &dst, &fl);
	if (err) {
		if (net_ratelimit())
			printk("ip6t_REJECT: can't find dst. err = %d\n", err);
		return;
	}

	hh_len = (dst->dev->hard_header_len + 15)&~15;
	nskb = alloc_skb(hh_len + 15 + dst->header_len + sizeof(struct ipv6hdr)
			 + sizeof(struct tcphdr) + dst->trailer_len,
			 GFP_ATOMIC);

	if (!nskb) {
		if (net_ratelimit())
			printk("ip6t_REJECT: Can't alloc skb\n");
		dst_release(dst);
		return;
	}

	nskb->dst = dst;
	dst_hold(dst);

	skb_reserve(nskb, hh_len + dst->header_len);

	ip6h = nskb->nh.ipv6h = (struct ipv6hdr *)
					skb_put(nskb, sizeof(struct ipv6hdr));
	ip6h->version = 6;
	ip6h->hop_limit = dst_metric(dst, RTAX_HOPLIMIT);
	ip6h->nexthdr = IPPROTO_TCP;
	ip6h->payload_len = htons(sizeof(struct tcphdr));
	ipv6_addr_copy(&ip6h->saddr, &oip6h->daddr);
	ipv6_addr_copy(&ip6h->daddr, &oip6h->saddr);

	tcph = (struct tcphdr *)skb_put(nskb, sizeof(struct tcphdr));
	/* Truncate to length (no data) */
	tcph->doff = sizeof(struct tcphdr)/4;
	tcph->source = otcph.dest;
	tcph->dest = otcph.source;

	if (otcph.ack) {
		needs_ack = 0;
		tcph->seq = otcph.ack_seq;
		tcph->ack_seq = 0;
	} else {
		needs_ack = 1;
		tcph->ack_seq = htonl(ntohl(otcph.seq) + otcph.syn + otcph.fin
				      + otcplen - (otcph.doff<<2));
		tcph->seq = 0;
	}

	/* Reset flags */
	((u_int8_t *)tcph)[13] = 0;
	tcph->rst = 1;
	tcph->ack = needs_ack;
	tcph->window = 0;
	tcph->urg_ptr = 0;
	tcph->check = 0;

	/* Adjust TCP checksum */
	tcph->check = csum_ipv6_magic(&nskb->nh.ipv6h->saddr,
				      &nskb->nh.ipv6h->daddr,
				      sizeof(struct tcphdr), IPPROTO_TCP,
				      csum_partial((char *)tcph,
						   sizeof(struct tcphdr), 0));

	connection_attach(nskb, oldskb->nfct);

	NF_HOOK(PF_INET6, NF_IP6_LOCAL_OUT, nskb, NULL, nskb->dst->dev,
		maybe_reroute);

	dst_release(dst);
}

static void send_unreach(struct sk_buff *skb_in, unsigned char code)
{
	struct ipv6hdr *ip6h, *hdr = skb_in->nh.ipv6h;
	struct icmp6hdr *icmp6h;
	struct dst_entry *dst = NULL;
	struct rt6_info *rt;
	int tmo;
	__u32 csum;
	unsigned int len, datalen, hh_len;
	int saddr_type, daddr_type;
	unsigned int ptr, ip6off;
	u8 proto;
	struct flowi fl;
	struct sk_buff *nskb;
	char *data;

	saddr_type = ipv6_addr_type(&hdr->saddr);
	daddr_type = ipv6_addr_type(&hdr->daddr);

	if ((!(saddr_type & IPV6_ADDR_UNICAST)) ||
	    (!(daddr_type & IPV6_ADDR_UNICAST))) {
		DEBUGP("addr is not unicast.\n");
		return;
	}

	ip6off = skb_in->nh.raw - skb_in->data;
	proto = hdr->nexthdr;
	ptr = ipv6_skip_exthdr(skb_in, ip6off + sizeof(struct ipv6hdr), &proto,
			       skb_in->len - ip6off);

	if ((ptr < 0) || (ptr > skb_in->len)) {
		ptr = ip6off + sizeof(struct ipv6hdr);
		proto = hdr->nexthdr;
	} else if (proto == IPPROTO_ICMPV6) {
                u8 type;

                if (skb_copy_bits(skb_in, ptr + offsetof(struct icmp6hdr,
						      icmp6_type), &type, 1)) {
			if (net_ratelimit())
				printk("ip6t_REJECT: Can't get ICMPv6 type\n");
			return;
		}

		if (!(type & ICMPV6_INFOMSG_MASK)) {
			if (net_ratelimit())
				printk(KERN_DEBUG "ip6t_REJECT: no reply to icmp error\n");
			return;
		}
        } else if (proto == IPPROTO_UDP) {
		int plen = skb_in->len - (ptr - ip6off);
		uint16_t check;

		if (plen < sizeof(struct udphdr)) {
			DEBUGP("too short\n");
			return;
		}

		if (skb_copy_bits(skb_in, ptr + offsetof(struct udphdr, check),
				  &check, 2)) {
			if (net_ratelimit())
				printk("ip6t_REJECT: can't get copy from skb");
			return;
		}

		if (check &&
		    csum_ipv6_magic(&hdr->saddr, &hdr->daddr, plen,
				    IPPROTO_UDP,
				    skb_checksum(skb_in, ptr, plen, 0))) {
			DEBUGP("ip6t_REJECT: UDP checksum is invalid.\n");
			return;
		}
	}

	memset(&fl, 0, sizeof(fl));
	fl.proto = IPPROTO_ICMPV6;
	ipv6_addr_copy(&fl.fl6_src, &hdr->daddr);
	ipv6_addr_copy(&fl.fl6_dst, &hdr->saddr);
	fl.fl_icmp_type = ICMPV6_DEST_UNREACH;
	fl.fl_icmp_code = code;

	if (ip6_dst_lookup(NULL, &dst, &fl)) {
		return;
	}

	rt = (struct rt6_info *)dst;
	tmo = 1*HZ;

	if (rt->rt6i_dst.plen < 128)
		tmo >>= ((128 - rt->rt6i_dst.plen)>>5);

	if (!xrlim_allow(dst, tmo)) {
		if (net_ratelimit())
			printk("ip6t_REJECT: rate limitted\n");
		goto dst_release_out;
	}

	len = skb_in->len + sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr);

	if (len > dst_pmtu(dst))
		len = dst_pmtu(dst);
	if (len > IPV6_MIN_MTU)
		len = IPV6_MIN_MTU;

	datalen = len - sizeof(struct ipv6hdr) - sizeof(struct icmp6hdr);
	hh_len = (rt->u.dst.dev->hard_header_len + 15)&~15;

	nskb = alloc_skb(hh_len + 15 + dst->header_len + dst->trailer_len + len,
			 GFP_ATOMIC);

	if (!nskb) {
		if (net_ratelimit())
			printk("ip6t_REJECT: can't alloc skb\n");
		goto dst_release_out;
	}

	nskb->priority = 0;
	nskb->dst = dst;
	dst_hold(dst);

	skb_reserve(nskb, hh_len + dst->header_len);

	ip6h = nskb->nh.ipv6h = (struct ipv6hdr *)
					skb_put(nskb, sizeof(struct ipv6hdr));
	ip6h->version = 6;
	ip6h->hop_limit = dst_metric(dst, RTAX_HOPLIMIT);
	ip6h->nexthdr = IPPROTO_ICMPV6;
	ip6h->payload_len = htons(datalen + sizeof(struct icmp6hdr));
	ipv6_addr_copy(&ip6h->saddr, &hdr->daddr);
	ipv6_addr_copy(&ip6h->daddr, &hdr->saddr);

	icmp6h = (struct icmp6hdr *) skb_put(nskb, sizeof(struct icmp6hdr));
	icmp6h->icmp6_type = ICMPV6_DEST_UNREACH;
	icmp6h->icmp6_code = code;
	icmp6h->icmp6_cksum = 0;

	data = skb_put(nskb, datalen);

	csum = csum_partial((unsigned char *)icmp6h, sizeof(struct icmp6hdr), 0);
	csum = skb_copy_and_csum_bits(skb_in, ip6off, data, datalen, csum);
	icmp6h->icmp6_cksum = csum_ipv6_magic(&hdr->saddr, &hdr->daddr,
					     datalen + sizeof(struct icmp6hdr),
					     IPPROTO_ICMPV6, csum);

	connection_attach(nskb, skb_in->nfct);
	NF_HOOK(PF_INET6, NF_IP6_LOCAL_OUT, nskb, NULL, nskb->dst->dev,
		maybe_reroute);

dst_release_out:
	dst_release(dst);
}

static unsigned int reject6_target(struct sk_buff **pskb,
			   unsigned int hooknum,
			   const struct net_device *in,
			   const struct net_device *out,
			   const void *targinfo,
			   void *userinfo)
{
	const struct ip6t_reject_info *reject = targinfo;

	DEBUGP(KERN_DEBUG "%s: medium point\n", __FUNCTION__);
	/* WARNING: This code causes reentry within ip6tables.
	   This means that the ip6tables jump stack is now crap.  We
	   must return an absolute verdict. --RR */
    	switch (reject->with) {
    	case IP6T_ICMP6_NO_ROUTE:
    		send_unreach(*pskb, ICMPV6_NOROUTE);
    		break;
    	case IP6T_ICMP6_ADM_PROHIBITED:
    		send_unreach(*pskb, ICMPV6_ADM_PROHIBITED);
    		break;
    	case IP6T_ICMP6_NOT_NEIGHBOUR:
    		send_unreach(*pskb, ICMPV6_NOT_NEIGHBOUR);
    		break;
    	case IP6T_ICMP6_ADDR_UNREACH:
    		send_unreach(*pskb, ICMPV6_ADDR_UNREACH);
    		break;
    	case IP6T_ICMP6_PORT_UNREACH:
    		send_unreach(*pskb, ICMPV6_PORT_UNREACH);
    		break;
    	case IP6T_ICMP6_ECHOREPLY:
		/* Do nothing */
		break;
	case IP6T_TCP_RESET:
		send_reset(*pskb);
		break;
	default:
		if (net_ratelimit())
			printk(KERN_WARNING "REJECTv6: case %u not handled yet\n", reject->with);
		break;
	}

	return NF_DROP;
}

static int check(const char *tablename,
		 const struct ip6t_entry *e,
		 void *targinfo,
		 unsigned int targinfosize,
		 unsigned int hook_mask)
{
 	const struct ip6t_reject_info *rejinfo = targinfo;

 	if (targinfosize != IP6T_ALIGN(sizeof(struct ip6t_reject_info))) {
  		DEBUGP("REJECTv6: targinfosize %u != 0\n", targinfosize);
  		return 0;
  	}

	/* Only allow these for packet filtering. */
	if (strcmp(tablename, "filter") != 0) {
		DEBUGP("REJECTv6: bad table `%s'.\n", tablename);
		return 0;
	}

	if ((hook_mask & ~((1 << NF_IP6_LOCAL_IN)
			   | (1 << NF_IP6_FORWARD)
			   | (1 << NF_IP6_LOCAL_OUT))) != 0) {
		DEBUGP("REJECTv6: bad hook mask %X\n", hook_mask);
		return 0;
	}

	if (rejinfo->with == IP6T_ICMP6_ECHOREPLY) {
		printk("REJECT: ECHOREPLY is not supported.\n");
		return 0;
	} else if (rejinfo->with == IP6T_TCP_RESET) {
		/* Must specify that it's a TCP packet */
		if (e->ipv6.proto != IPPROTO_TCP
		    || (e->ipv6.invflags & IP6T_INV_PROTO)) {
			DEBUGP("REJECTv6: TCP_RESET illegal for non-tcp\n");
			return 0;
		}
	}

	return 1;
}

static struct ip6t_target ip6t_reject_reg = {
	.name		= "REJECT",
	.target		= reject6_target,
	.checkentry	= check,
	.me		= THIS_MODULE
};

static int __init init(void)
{
	if (ip6t_register_target(&ip6t_reject_reg))
		return -EINVAL;
	return 0;
}

static void __exit fini(void)
{
	ip6t_unregister_target(&ip6t_reject_reg);
}

module_init(init);
module_exit(fini);
