/*
 * This is a module which is used for rejecting packets.
 * Added support for customized reject packets (Jozsef Kadlecsik).
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/tcp.h>
struct in_device;
#include <net/route.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_REJECT.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* Send RST reply */
static void send_reset(struct sk_buff *oldskb, int local)
{
	struct sk_buff *nskb;
	struct tcphdr *otcph, *tcph;
	struct rtable *rt;
	unsigned int otcplen;
	u_int16_t tmp;
	int needs_ack;

	/* IP header checks: fragment, too short. */
	if (oldskb->nh.iph->frag_off & htons(IP_OFFSET)
	    || oldskb->len < (oldskb->nh.iph->ihl<<2) + sizeof(struct tcphdr))
		return;

	otcph = (struct tcphdr *)((u_int32_t*)oldskb->nh.iph + oldskb->nh.iph->ihl);
	otcplen = oldskb->len - oldskb->nh.iph->ihl*4;

	/* No RST for RST. */
	if (otcph->rst)
		return;

	/* Check checksum. */
	if (tcp_v4_check(otcph, otcplen, oldskb->nh.iph->saddr,
			 oldskb->nh.iph->daddr,
			 csum_partial((char *)otcph, otcplen, 0)) != 0)
		return;

	/* Copy skb (even if skb is about to be dropped, we can't just
           clone it because there may be other things, such as tcpdump,
           interested in it) */
	nskb = skb_copy(oldskb, GFP_ATOMIC);
	if (!nskb)
		return;

	/* This packet will not be the same as the other: clear nf fields */
	nf_conntrack_put(nskb->nfct);
	nskb->nfct = NULL;
	nskb->nfcache = 0;
#ifdef CONFIG_NETFILTER_DEBUG
	nskb->nf_debug = 0;
#endif

	tcph = (struct tcphdr *)((u_int32_t*)nskb->nh.iph + nskb->nh.iph->ihl);

	/* Swap source and dest */
	nskb->nh.iph->daddr = xchg(&nskb->nh.iph->saddr, nskb->nh.iph->daddr);
	tmp = tcph->source;
	tcph->source = tcph->dest;
	tcph->dest = tmp;

	/* Truncate to length (no data) */
	tcph->doff = sizeof(struct tcphdr)/4;
	skb_trim(nskb, nskb->nh.iph->ihl*4 + sizeof(struct tcphdr));
	nskb->nh.iph->tot_len = htons(nskb->len);

	if (tcph->ack) {
		needs_ack = 0;
		tcph->seq = otcph->ack_seq;
		tcph->ack_seq = 0;
	} else {
		needs_ack = 1;
		tcph->ack_seq = htonl(ntohl(otcph->seq) + otcph->syn + otcph->fin
				      + otcplen - (otcph->doff<<2));
		tcph->seq = 0;
	}

	/* Reset flags */
	((u_int8_t *)tcph)[13] = 0;
	tcph->rst = 1;
	tcph->ack = needs_ack;

	tcph->window = 0;
	tcph->urg_ptr = 0;

	/* Adjust TCP checksum */
	tcph->check = 0;
	tcph->check = tcp_v4_check(tcph, sizeof(struct tcphdr),
				   nskb->nh.iph->saddr,
				   nskb->nh.iph->daddr,
				   csum_partial((char *)tcph,
						sizeof(struct tcphdr), 0));

	/* Adjust IP TTL, DF */
	nskb->nh.iph->ttl = MAXTTL;
	/* Set DF, id = 0 */
	nskb->nh.iph->frag_off = htons(IP_DF);
	nskb->nh.iph->id = 0;

	/* Adjust IP checksum */
	nskb->nh.iph->check = 0;
	nskb->nh.iph->check = ip_fast_csum((unsigned char *)nskb->nh.iph, 
					   nskb->nh.iph->ihl);

	/* Routing: if not headed for us, route won't like source */
	if (ip_route_output(&rt, nskb->nh.iph->daddr,
			    local ? nskb->nh.iph->saddr : 0,
			    RT_TOS(nskb->nh.iph->tos) | RTO_CONN,
			    0) != 0)
		goto free_nskb;

	dst_release(nskb->dst);
	nskb->dst = &rt->u.dst;

	/* "Never happens" */
	if (nskb->len > nskb->dst->pmtu)
		goto free_nskb;

	NF_HOOK(PF_INET, NF_IP_LOCAL_OUT, nskb, NULL, nskb->dst->dev,
		ip_finish_output);
	return;

 free_nskb:
	kfree_skb(nskb);
}

static unsigned int reject(struct sk_buff **pskb,
			   unsigned int hooknum,
			   const struct net_device *in,
			   const struct net_device *out,
			   const void *targinfo,
			   void *userinfo)
{
	const struct ipt_reject_info *reject = targinfo;

	/* WARNING: This code causes reentry within iptables.
	   This means that the iptables jump stack is now crap.  We
	   must return an absolute verdict. --RR */
    	switch (reject->with) {
    	case IPT_ICMP_NET_UNREACHABLE:
    		icmp_send(*pskb, ICMP_DEST_UNREACH, ICMP_NET_UNREACH, 0);
    		break;
    	case IPT_ICMP_HOST_UNREACHABLE:
    		icmp_send(*pskb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);
    		break;
    	case IPT_ICMP_PROT_UNREACHABLE:
    		icmp_send(*pskb, ICMP_DEST_UNREACH, ICMP_PROT_UNREACH, 0);
    		break;
    	case IPT_ICMP_PORT_UNREACHABLE:
    		icmp_send(*pskb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);
    		break;
    	case IPT_ICMP_NET_PROHIBITED:
    		icmp_send(*pskb, ICMP_DEST_UNREACH, ICMP_NET_ANO, 0);
    		break;
	case IPT_ICMP_HOST_PROHIBITED:
    		icmp_send(*pskb, ICMP_DEST_UNREACH, ICMP_HOST_ANO, 0);
    		break;
    	case IPT_ICMP_ECHOREPLY: {
		struct icmphdr *icmph  = (struct icmphdr *)
			((u_int32_t *)(*pskb)->nh.iph + (*pskb)->nh.iph->ihl);
		unsigned int datalen = (*pskb)->len - (*pskb)->nh.iph->ihl * 4;

		/* Not non-head frags, or truncated */
		if (((ntohs((*pskb)->nh.iph->frag_off) & IP_OFFSET) == 0)
		    && datalen >= 4) {
			/* Usually I don't like cut & pasting code,
                           but dammit, my party is starting in 45
                           mins! --RR */
			struct icmp_bxm icmp_param;

			icmp_param.icmph=*icmph;
			icmp_param.icmph.type=ICMP_ECHOREPLY;
			icmp_param.data_ptr=(icmph+1);
			icmp_param.data_len=datalen;
			icmp_reply(&icmp_param, *pskb);
		}
	}
	break;
	case IPT_TCP_RESET:
		send_reset(*pskb, hooknum == NF_IP_LOCAL_IN);
		break;
	}

	return NF_DROP;
}

static inline int find_ping_match(const struct ipt_entry_match *m)
{
	const struct ipt_icmp *icmpinfo = (const struct ipt_icmp *)m->data;

	if (strcmp(m->u.kernel.match->name, "icmp") == 0
	    && icmpinfo->type == ICMP_ECHO
	    && !(icmpinfo->invflags & IPT_ICMP_INV))
		return 1;

	return 0;
}

static int check(const char *tablename,
		 const struct ipt_entry *e,
		 void *targinfo,
		 unsigned int targinfosize,
		 unsigned int hook_mask)
{
 	const struct ipt_reject_info *rejinfo = targinfo;

 	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_icmp))) {
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
		/* Must specify that it's an ICMP ping packet. */
		if (e->ip.proto != IPPROTO_ICMP
		    || (e->ip.invflags & IPT_INV_PROTO)) {
			DEBUGP("REJECT: ECHOREPLY illegal for non-icmp\n");
			return 0;
		}
		/* Must contain ICMP match. */
		if (IPT_MATCH_ITERATE(e, find_ping_match) == 0) {
			DEBUGP("REJECT: ECHOREPLY illegal for non-ping\n");
			return 0;
		}
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

static struct ipt_target ipt_reject_reg
= { { NULL, NULL }, "REJECT", reject, check, NULL, THIS_MODULE };

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
