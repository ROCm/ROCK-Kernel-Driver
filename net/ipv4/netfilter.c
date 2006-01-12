/* IPv4 specific functions of netfilter core */

#include <linux/config.h>
#ifdef CONFIG_NETFILTER

#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <net/route.h>
#include <net/xfrm.h>
#include <net/ip.h>
#include <linux/ip.h>

#ifdef CONFIG_XFRM
inline int nf_rcv_postxfrm_nonlocal(struct sk_buff *skb)
{
        skb->sp->decap_done = 1;
        dst_release(skb->dst);
        skb->dst = NULL;
        nf_reset(skb);
        return netif_rx(skb);
}

int nf_rcv_postxfrm_local(struct sk_buff *skb)
{
        __skb_push(skb, skb->data - skb->nh.raw);
        /* Fix header len and checksum if last xfrm was transport mode */
        if (!skb->sp->x[skb->sp->len - 1].xvec->props.mode) {
                skb->nh.iph->tot_len = htons(skb->len);
        }
	/* Unconditionally do the checksum; the packet
	 * may have been fragmented. Icky. */
	ip_send_check(skb->nh.iph);
        return nf_rcv_postxfrm_nonlocal(skb);
}

EXPORT_SYMBOL_GPL(nf_rcv_postxfrm_local);
#endif CONFIG_XFRM

#ifdef CONFIG_XFRM
#ifdef CONFIG_IP_NF_NAT_NEEDED
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_nat.h>

void nf_nat_decode_session4(struct sk_buff *skb, struct flowi *fl)
{
	struct ip_conntrack *ct;
	struct ip_conntrack_tuple *t;
	enum ip_conntrack_info ctinfo;
	enum ip_conntrack_dir dir;
	int known_proto;
	int statusbit;

	ct = ip_conntrack_get(skb, &ctinfo);
	if (ct == NULL || !(ct->status & IPS_NAT_MASK))
		return;

	dir = CTINFO2DIR(ctinfo);
	t = &ct->tuplehash[dir].tuple;
	known_proto = t->dst.protonum == IPPROTO_TCP ||
	              t->dst.protonum == IPPROTO_UDP;

	if (dir == IP_CT_DIR_REPLY)
		statusbit = IPS_SRC_NAT;
        else
		statusbit = IPS_DST_NAT;
	if (ct->status & statusbit) {
		fl->fl4_dst = t->dst.ip;
		if (known_proto)
			fl->fl_ip_dport = t->dst.u.tcp.port;
	}

	statusbit ^= IPS_NAT_MASK;
	if (ct->status & statusbit) {
		fl->fl4_src = t->src.ip;
		if (known_proto)
                        fl->fl_ip_sport = t->src.u.tcp.port;
        }
}
#endif /* CONFIG_IP_NF_NAT_NEEDED */
#endif

/* route_me_harder function, used by iptable_nat, iptable_mangle + ip_queue */
int ip_route_me_harder(struct sk_buff **pskb)
{
	struct iphdr *iph = (*pskb)->nh.iph;
	struct rtable *rt;
	struct flowi fl = {};
	struct dst_entry *odst;
	unsigned int hh_len;

	/* some non-standard hacks like ipt_REJECT.c:send_reset() can cause
	 * packets with foreign saddr to appear on the NF_IP_LOCAL_OUT hook.
	 */
	if (inet_addr_type(iph->saddr) == RTN_LOCAL) {
		fl.nl_u.ip4_u.daddr = iph->daddr;
		fl.nl_u.ip4_u.saddr = iph->saddr;
		fl.nl_u.ip4_u.tos = RT_TOS(iph->tos);
		fl.oif = (*pskb)->sk ? (*pskb)->sk->sk_bound_dev_if : 0;
#ifdef CONFIG_IP_ROUTE_FWMARK
		fl.nl_u.ip4_u.fwmark = (*pskb)->nfmark;
#endif
		if (ip_route_output_key(&rt, &fl) != 0)
			return -1;

		/* Drop old route. */
		dst_release((*pskb)->dst);
		(*pskb)->dst = &rt->u.dst;
	} else {
		/* non-local src, find valid iif to satisfy
		 * rp-filter when calling ip_route_input. */
		fl.nl_u.ip4_u.daddr = iph->saddr;
		if (ip_route_output_key(&rt, &fl) != 0)
			return -1;

		odst = (*pskb)->dst;
		if (ip_route_input(*pskb, iph->daddr, iph->saddr,
				   RT_TOS(iph->tos), rt->u.dst.dev) != 0) {
			dst_release(&rt->u.dst);
			return -1;
		}
		dst_release(&rt->u.dst);
		dst_release(odst);
	}
	
	if ((*pskb)->dst->error)
		return -1;

#ifdef CONFIG_XFRM
	if (!(IPCB(*pskb)->flags & IPSKB_XFRM_TRANSFORMED)) {
		struct xfrm_policy_afinfo *afinfo;

		afinfo = xfrm_policy_get_afinfo(AF_INET);
		if (afinfo != NULL) {
			afinfo->decode_session(*pskb, &fl);
			xfrm_policy_put_afinfo(afinfo);
			if (xfrm_lookup(&(*pskb)->dst, &fl, (*pskb)->sk, 0) != 0)
				return -1;
		}
	}
#endif

	/* Change in oif may mean change in hh_len. */
	hh_len = (*pskb)->dst->dev->hard_header_len;
	if (skb_headroom(*pskb) < hh_len) {
		struct sk_buff *nskb;

		nskb = skb_realloc_headroom(*pskb, hh_len);
		if (!nskb) 
			return -1;
		if ((*pskb)->sk)
			skb_set_owner_w(nskb, (*pskb)->sk);
		kfree_skb(*pskb);
		*pskb = nskb;
	}

	return 0;
}
EXPORT_SYMBOL(ip_route_me_harder);

/*
 * Extra routing may needed on local out, as the QUEUE target never
 * returns control to the table.
 */

struct ip_rt_info {
	u_int32_t daddr;
	u_int32_t saddr;
	u_int8_t tos;
};

static void queue_save(const struct sk_buff *skb, struct nf_info *info)
{
	struct ip_rt_info *rt_info = nf_info_reroute(info);

	if (info->hook == NF_IP_LOCAL_OUT) {
		const struct iphdr *iph = skb->nh.iph;

		rt_info->tos = iph->tos;
		rt_info->daddr = iph->daddr;
		rt_info->saddr = iph->saddr;
	}
}

static int queue_reroute(struct sk_buff **pskb, const struct nf_info *info)
{
	const struct ip_rt_info *rt_info = nf_info_reroute(info);

	if (info->hook == NF_IP_LOCAL_OUT) {
		struct iphdr *iph = (*pskb)->nh.iph;

		if (!(iph->tos == rt_info->tos
		      && iph->daddr == rt_info->daddr
		      && iph->saddr == rt_info->saddr))
			return ip_route_me_harder(pskb);
	}
	return 0;
}

static struct nf_queue_rerouter ip_reroute = {
	.rer_size	= sizeof(struct ip_rt_info),
	.save		= queue_save,
	.reroute	= queue_reroute,
};

static int init(void)
{
	return nf_register_queue_rerouter(PF_INET, &ip_reroute);
}

static void fini(void)
{
	nf_unregister_queue_rerouter(PF_INET);
}

module_init(init);
module_exit(fini);

#endif /* CONFIG_NETFILTER */
