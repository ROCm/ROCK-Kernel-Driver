/*
 * This is a module which is used for rejecting packets.
 * Added support for customized reject packets (Jozsef Kadlecsik).
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/route.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_REJECT.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* If the original packet is part of a connection, but the connection
   is not confirmed, our manufactured reply will not be associated
   with it, so we need to do this manually. */
static void connection_attach(struct sk_buff *new_skb, struct nf_ct_info *nfct)
{
	void (*attach)(struct sk_buff *, struct nf_ct_info *);

	/* Avoid module unload race with ip_ct_attach being NULLed out */
	if (nfct && (attach = ip_ct_attach) != NULL) {
		mb(); /* Just to be sure: must be read before executing this */
		attach(new_skb, nfct);
	}
}

/* Send RST reply */
static unsigned int send_reset(struct sk_buff **pskb, int local)
{
	struct tcphdr tcph;
	struct rtable *rt;
	u_int16_t tmp_port;
	u_int32_t tmp_addr;
	int needs_ack, hh_len, datalen;
	struct nf_ct_info *oldnfct;

	/* No RSTs for fragments. */
	if ((*pskb)->nh.iph->frag_off & htons(IP_OFFSET))
		return NF_DROP;

	if (skb_copy_bits(*pskb, (*pskb)->nh.iph->ihl*4,
			  &tcph, sizeof(tcph)) < 0)
		return NF_DROP;

	/* No RST for RST. */
	if (tcph.rst)
		return NF_DROP;

	/* FIXME: Check checksum. */
	{
		struct flowi fl = { .nl_u = { .ip4_u =
					      { .daddr = (*pskb)->nh.iph->saddr,
						.saddr = (local ?
							  (*pskb)->nh.iph->daddr :
							  0),
						.tos = RT_TOS((*pskb)->nh.iph->tos) } } };

		/* Routing: if not headed for us, route won't like source */
		if (ip_route_output_key(&rt, &fl))
			return NF_DROP;

		hh_len = (rt->u.dst.dev->hard_header_len + 15)&~15;
	}

	/* We're going to flip the header around, drop options and data. */
	if (!skb_ip_make_writable(pskb, (*pskb)->nh.iph->ihl*4+sizeof(tcph))) {
		ip_rt_put(rt);
		return NF_DROP;
	}

	(*pskb)->h.th = (void *)(*pskb)->nh.iph + sizeof(tcph);
	datalen = (*pskb)->len - (*pskb)->nh.iph->ihl*4 - tcph.doff*4;

	/* Change over route. */
	dst_release((*pskb)->dst);
	(*pskb)->dst = &rt->u.dst;

	/* This packet will not be the same as the other: clear nf fields */
	(*pskb)->nfcache = 0;
#ifdef CONFIG_NETFILTER_DEBUG
	(*pskb)->nf_debug = 0;
#endif
	(*pskb)->nfmark = 0;

	/* Swap source and dest */
	tmp_addr = (*pskb)->nh.iph->saddr;
	(*pskb)->nh.iph->saddr = (*pskb)->nh.iph->daddr;
	(*pskb)->nh.iph->daddr = tmp_addr;
	tmp_port = (*pskb)->h.th->source;
	(*pskb)->h.th->source = (*pskb)->h.th->dest;
	(*pskb)->h.th->dest = tmp_port;

	/* Truncate to length (no data) */
	(*pskb)->h.th->doff = sizeof(struct tcphdr)/4;
	skb_trim(*pskb, (*pskb)->nh.iph->ihl*4 + sizeof(struct tcphdr));
	(*pskb)->nh.iph->tot_len = htons((*pskb)->len);

	if ((*pskb)->h.th->ack) {
		needs_ack = 0;
		(*pskb)->h.th->seq = tcph.ack_seq;
		(*pskb)->h.th->ack_seq = 0;
	} else {
		needs_ack = 1;
		(*pskb)->h.th->ack_seq = htonl(ntohl(tcph.seq)
						 + tcph.syn + tcph.fin
						 + datalen);
		(*pskb)->h.th->seq = 0;
	}

	/* Reset flags */
	memset((*pskb)->h.raw + 13, 0, 1);
	(*pskb)->h.th->rst = 1;
	(*pskb)->h.th->ack = needs_ack;

	(*pskb)->h.th->window = 0;
	(*pskb)->h.th->urg_ptr = 0;

	/* Adjust TCP checksum */
	(*pskb)->h.th->check = 0;
	(*pskb)->h.th->check
		= tcp_v4_check((*pskb)->h.th,
			       sizeof(struct tcphdr),
			       (*pskb)->nh.iph->saddr,
			       (*pskb)->nh.iph->daddr,
			       csum_partial((*pskb)->h.raw,
					    sizeof(struct tcphdr), 0));

	/* Adjust IP TTL, DF */
	(*pskb)->nh.iph->ttl = MAXTTL;
	/* Set DF, id = 0 */
	(*pskb)->nh.iph->frag_off = htons(IP_DF);
	(*pskb)->nh.iph->id = 0;

	/* Adjust IP checksum */
	(*pskb)->nh.iph->check = 0;
	(*pskb)->nh.iph->check = ip_fast_csum((*pskb)->nh.raw, 
					      (*pskb)->nh.iph->ihl);

	/* "Never happens" */
	if ((*pskb)->len > dst_pmtu((*pskb)->dst))
		return NF_DROP;

	/* Related to old connection. */
	oldnfct = (*pskb)->nfct;
	connection_attach(*pskb, oldnfct);
	nf_conntrack_put(oldnfct);

	NF_HOOK(PF_INET, NF_IP_LOCAL_OUT, *pskb, NULL, (*pskb)->dst->dev,
		ip_finish_output);
	return NF_STOLEN;
}

static void send_unreach(const struct sk_buff *skb_in, int code)
{
	struct sk_buff *nskb;
	u32 saddr;
	u8 tos;
	int hh_len, length;
	struct rtable *rt = (struct rtable*)skb_in->dst;
	unsigned char *data;

	if (!rt)
		return;

	/* FIXME: Use sysctl number. --RR */
	if (!xrlim_allow(&rt->u.dst, 1*HZ))
		return;

	/* No replies to physical multicast/broadcast */
	if (skb_in->pkt_type!=PACKET_HOST)
		return;

	/* Now check at the protocol level */
	if (rt->rt_flags&(RTCF_BROADCAST|RTCF_MULTICAST))
		return;

	/* Only reply to fragment 0. */
	if (skb_in->nh.iph->frag_off&htons(IP_OFFSET))
		return;

	/* Ensure we have at least 8 bytes of proto header. */
	if (skb_in->len < skb_in->nh.iph->ihl*4 + 8)
		return;

	/* If we send an ICMP error to an ICMP error a mess would result.. */
	if (skb_in->nh.iph->protocol == IPPROTO_ICMP) {
		struct icmphdr icmph;

		if (skb_copy_bits(skb_in, skb_in->nh.iph->ihl*4,
				  &icmph, sizeof(icmph)) < 0)
			return;

		/* Between echo-reply (0) and timestamp (13),
		   everything except echo-request (8) is an error.
		   Also, anything greater than NR_ICMP_TYPES is
		   unknown, and hence should be treated as an error... */
		if ((icmph.type < ICMP_TIMESTAMP
		     && icmph.type != ICMP_ECHOREPLY
		     && icmph.type != ICMP_ECHO)
		    || icmph.type > NR_ICMP_TYPES)
			return;
	}

	saddr = skb_in->nh.iph->daddr;
	if (!(rt->rt_flags & RTCF_LOCAL))
		saddr = 0;

	tos = (skb_in->nh.iph->tos & IPTOS_TOS_MASK)
		| IPTOS_PREC_INTERNETCONTROL;
	{
		struct flowi fl = { .nl_u = { .ip4_u =
					      { .daddr = skb_in->nh.iph->saddr,
						.saddr = saddr,
						.tos = RT_TOS(tos) } } };
		if (ip_route_output_key(&rt, &fl))
			return;
	}
	/* RFC says return as much as we can without exceeding 576 bytes. */
	length = skb_in->len + sizeof(struct iphdr) + sizeof(struct icmphdr);

	if (length > dst_pmtu(&rt->u.dst))
		length = dst_pmtu(&rt->u.dst);
	if (length > 576)
		length = 576;

	hh_len = (rt->u.dst.dev->hard_header_len + 15)&~15;

	nskb = alloc_skb(hh_len+15+length, GFP_ATOMIC);
	if (!nskb) {
		ip_rt_put(rt);
		return;
	}

	nskb->priority = 0;
	nskb->dst = &rt->u.dst;
	skb_reserve(nskb, hh_len);

	/* Set up IP header */
	nskb->nh.iph = (struct iphdr *)skb_put(nskb, sizeof(struct iphdr));
	nskb->nh.iph->version=4;
	nskb->nh.iph->ihl=5;
	nskb->nh.iph->tos=tos;
	nskb->nh.iph->tot_len = htons(length);

	/* PMTU discovery never applies to ICMP packets. */
	nskb->nh.iph->frag_off = 0;

	nskb->nh.iph->ttl = MAXTTL;
	ip_select_ident(nskb->nh.iph, &rt->u.dst, NULL);
	nskb->nh.iph->protocol=IPPROTO_ICMP;
	nskb->nh.iph->saddr=rt->rt_src;
	nskb->nh.iph->daddr=rt->rt_dst;
	nskb->nh.iph->check=0;
	nskb->nh.iph->check = ip_fast_csum(nskb->nh.raw,
					   nskb->nh.iph->ihl);

	/* Set up ICMP header. */
	nskb->h.icmph = (struct icmphdr *)skb_put(nskb,sizeof(struct icmphdr));
	nskb->h.icmph->type = ICMP_DEST_UNREACH;
	nskb->h.icmph->code = code;	
	nskb->h.icmph->un.gateway = 0;
	nskb->h.icmph->checksum = 0;
	
	/* Copy as much of original packet as will fit */
	data = skb_put(nskb,
		       length - sizeof(struct iphdr) - sizeof(struct icmphdr));
	skb_copy_bits(skb_in, 0, data,
		      length - sizeof(struct iphdr) - sizeof(struct icmphdr));
	nskb->h.icmph->checksum = ip_compute_csum(nskb->h.raw,
						  length-sizeof(struct iphdr));

	connection_attach(nskb, skb_in->nfct);

	NF_HOOK(PF_INET, NF_IP_LOCAL_OUT, nskb, NULL, nskb->dst->dev,
		ip_finish_output);
}	

static unsigned int reject(struct sk_buff **pskb,
			   const struct net_device *in,
			   const struct net_device *out,
			   unsigned int hooknum,
			   const void *targinfo,
			   void *userinfo)
{
	const struct ipt_reject_info *reject = targinfo;

	/* Our naive response construction doesn't deal with IP
           options, and probably shouldn't try. */
	if ((*pskb)->nh.iph->ihl<<2 != sizeof(struct iphdr))
		return NF_DROP;

	/* WARNING: This code causes reentry within iptables.
	   This means that the iptables jump stack is now crap.  We
	   must return an absolute verdict. --RR */
    	switch (reject->with) {
    	case IPT_ICMP_NET_UNREACHABLE:
    		send_unreach(*pskb, ICMP_NET_UNREACH);
    		break;
    	case IPT_ICMP_HOST_UNREACHABLE:
    		send_unreach(*pskb, ICMP_HOST_UNREACH);
    		break;
    	case IPT_ICMP_PROT_UNREACHABLE:
    		send_unreach(*pskb, ICMP_PROT_UNREACH);
    		break;
    	case IPT_ICMP_PORT_UNREACHABLE:
    		send_unreach(*pskb, ICMP_PORT_UNREACH);
    		break;
    	case IPT_ICMP_NET_PROHIBITED:
    		send_unreach(*pskb, ICMP_NET_ANO);
    		break;
	case IPT_ICMP_HOST_PROHIBITED:
    		send_unreach(*pskb, ICMP_HOST_ANO);
    		break;
	case IPT_TCP_RESET:
		return send_reset(pskb, hooknum == NF_IP_LOCAL_IN);
	case IPT_ICMP_ECHOREPLY:
		/* Doesn't happen. */
		break;
	}

	return NF_DROP;
}

static int check(const char *tablename,
		 const struct ipt_entry *e,
		 void *targinfo,
		 unsigned int targinfosize,
		 unsigned int hook_mask)
{
 	const struct ipt_reject_info *rejinfo = targinfo;

 	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_reject_info))) {
  		DEBUGP("REJECT: targinfosize %u != 0\n", targinfosize);
  		return 0;
  	}

	/* Only allow these for packet filtering. */
	if (strcmp(tablename, "filter") != 0) {
		DEBUGP("REJECT: bad table `%s'.\n", tablename);
		return 0;
	}
	if ((hook_mask & ~((1 << NF_IP_LOCAL_IN)
			   | (1 << NF_IP_FORWARD)
			   | (1 << NF_IP_LOCAL_OUT))) != 0) {
		DEBUGP("REJECT: bad hook mask %X\n", hook_mask);
		return 0;
	}

	if (rejinfo->with == IPT_ICMP_ECHOREPLY) {
		printk("REJECT: ECHOREPLY no longer supported.\n");
		return 0;
	} else if (rejinfo->with == IPT_TCP_RESET) {
		/* Must specify that it's a TCP packet */
		if (e->ip.proto != IPPROTO_TCP
		    || (e->ip.invflags & IPT_INV_PROTO)) {
			DEBUGP("REJECT: TCP_RESET illegal for non-tcp\n");
			return 0;
		}
	}

	return 1;
}

static struct ipt_target ipt_reject_reg = {
	.name		= "REJECT",
	.target		= reject,
	.checkentry	= check,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	if (ipt_register_target(&ipt_reject_reg))
		return -EINVAL;
	return 0;
}

static void __exit fini(void)
{
	ipt_unregister_target(&ipt_reject_reg);
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
