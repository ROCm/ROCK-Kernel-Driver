/* This file contains all the functions required for the standalone
   ip_nat module.

   These are not required by the compatibility layer.
*/

/* (c) 1999 Paul `Rusty' Russell.  Licenced under the GNU General
   Public Licence. */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <net/checksum.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/brlock.h>

#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_nat_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_nat_lock)

#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_nat_protocol.h>
#include <linux/netfilter_ipv4/ip_nat_core.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

#define HOOKNAME(hooknum) ((hooknum) == NF_IP_POST_ROUTING ? "POST_ROUTING"  \
			   : ((hooknum) == NF_IP_PRE_ROUTING ? "PRE_ROUTING" \
			      : ((hooknum) == NF_IP_LOCAL_OUT ? "LOCAL_OUT"  \
				 : "*ERROR*")))

static unsigned int
ip_nat_fn(unsigned int hooknum,
	  struct sk_buff **pskb,
	  const struct net_device *in,
	  const struct net_device *out,
	  int (*okfn)(struct sk_buff *))
{
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	struct ip_nat_info *info;
	/* maniptype == SRC for postrouting. */
	enum ip_nat_manip_type maniptype = HOOK2MANIP(hooknum);

	/* We never see fragments: conntrack defrags on pre-routing
	   and local-out, and ip_nat_out protects post-routing. */
	IP_NF_ASSERT(!((*pskb)->nh.iph->frag_off
		       & __constant_htons(IP_MF|IP_OFFSET)));

	(*pskb)->nfcache |= NFC_UNKNOWN;

	/* If we had a hardware checksum before, it's now invalid */
	if ((*pskb)->pkt_type != PACKET_LOOPBACK)
		(*pskb)->ip_summed = CHECKSUM_NONE;

	ct = ip_conntrack_get(*pskb, &ctinfo);
	/* Can't track?  Maybe out of memory: this would make NAT
           unreliable. */
	if (!ct) {
		if (net_ratelimit())
			printk(KERN_DEBUG "NAT: %u dropping untracked packet %p %u %u.%u.%u.%u -> %u.%u.%u.%u\n",
			       hooknum,
			       *pskb,
			       (*pskb)->nh.iph->protocol,
			       NIPQUAD((*pskb)->nh.iph->saddr),
			       NIPQUAD((*pskb)->nh.iph->daddr));
		return NF_DROP;
	}

	switch (ctinfo) {
	case IP_CT_RELATED:
	case IP_CT_RELATED+IP_CT_IS_REPLY:
		if ((*pskb)->nh.iph->protocol == IPPROTO_ICMP) {
			return icmp_reply_translation(*pskb, ct, hooknum,
						      CTINFO2DIR(ctinfo));
		}
		/* Fall thru... (Only ICMPs can be IP_CT_IS_REPLY) */
	case IP_CT_NEW:
		info = &ct->nat.info;

		WRITE_LOCK(&ip_nat_lock);
		/* Seen it before?  This can happen for loopback, retrans,
		   or local packets.. */
		if (!(info->initialized & (1 << maniptype))) {
			int in_hashes = info->initialized;
			unsigned int ret;

			ret = ip_nat_rule_find(pskb, hooknum, in, out,
					       ct, info);
			if (ret != NF_ACCEPT) {
				WRITE_UNLOCK(&ip_nat_lock);
				return ret;
			}

			if (in_hashes) {
				IP_NF_ASSERT(info->bysource.conntrack);
				replace_in_hashes(ct, info);
			} else {
				place_in_hashes(ct, info);
			}
		} else
			DEBUGP("Already setup manip %s for ct %p\n",
			       maniptype == IP_NAT_MANIP_SRC ? "SRC" : "DST",
			       ct);
		WRITE_UNLOCK(&ip_nat_lock);
		break;

	default:
		/* ESTABLISHED */
		IP_NF_ASSERT(ctinfo == IP_CT_ESTABLISHED
			     || ctinfo == (IP_CT_ESTABLISHED+IP_CT_IS_REPLY));
		info = &ct->nat.info;
	}

	IP_NF_ASSERT(info);
	return do_bindings(ct, ctinfo, info, hooknum, pskb);
}

static unsigned int
ip_nat_out(unsigned int hooknum,
	   struct sk_buff **pskb,
	   const struct net_device *in,
	   const struct net_device *out,
	   int (*okfn)(struct sk_buff *))
{
	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct iphdr)
	    || (*pskb)->nh.iph->ihl * 4 < sizeof(struct iphdr))
		return NF_ACCEPT;

	/* We can hit fragment here; forwarded packets get
	   defragmented by connection tracking coming in, then
	   fragmented (grr) by the forward code.

	   In future: If we have nfct != NULL, AND we have NAT
	   initialized, AND there is no helper, then we can do full
	   NAPT on the head, and IP-address-only NAT on the rest.

	   I'm starting to have nightmares about fragments.  */

	if ((*pskb)->nh.iph->frag_off & __constant_htons(IP_MF|IP_OFFSET)) {
		*pskb = ip_ct_gather_frags(*pskb);

		if (!*pskb)
			return NF_STOLEN;
	}

	return ip_nat_fn(hooknum, pskb, in, out, okfn);
}

/* FIXME: change in oif may mean change in hh_len.  Check and realloc
   --RR */
static int
route_me_harder(struct sk_buff *skb)
{
	struct iphdr *iph = skb->nh.iph;
	struct rtable *rt;
	struct rt_key key = { dst:iph->daddr,
			      src:iph->saddr,
			      oif:skb->sk ? skb->sk->bound_dev_if : 0,
			      tos:RT_TOS(iph->tos)|RTO_CONN,
#ifdef CONFIG_IP_ROUTE_FWMARK
			      fwmark:skb->nfmark
#endif
			    };

	if (ip_route_output_key(&rt, &key) != 0) {
		printk("route_me_harder: No more route.\n");
		return -EINVAL;
	}

	/* Drop old route. */
	dst_release(skb->dst);

	skb->dst = &rt->u.dst;
	return 0;
}

static unsigned int
ip_nat_local_fn(unsigned int hooknum,
		struct sk_buff **pskb,
		const struct net_device *in,
		const struct net_device *out,
		int (*okfn)(struct sk_buff *))
{
	u_int32_t saddr, daddr;
	unsigned int ret;

	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct iphdr)
	    || (*pskb)->nh.iph->ihl * 4 < sizeof(struct iphdr))
		return NF_ACCEPT;

	saddr = (*pskb)->nh.iph->saddr;
	daddr = (*pskb)->nh.iph->daddr;

	ret = ip_nat_fn(hooknum, pskb, in, out, okfn);
	if (ret != NF_DROP && ret != NF_STOLEN
	    && ((*pskb)->nh.iph->saddr != saddr
		|| (*pskb)->nh.iph->daddr != daddr))
		return route_me_harder(*pskb) == 0 ? ret : NF_DROP;
	return ret;
}

/* We must be after connection tracking and before packet filtering. */

/* Before packet filtering, change destination */
static struct nf_hook_ops ip_nat_in_ops
= { { NULL, NULL }, ip_nat_fn, PF_INET, NF_IP_PRE_ROUTING, NF_IP_PRI_NAT_DST };
/* After packet filtering, change source */
static struct nf_hook_ops ip_nat_out_ops
= { { NULL, NULL }, ip_nat_out, PF_INET, NF_IP_POST_ROUTING, NF_IP_PRI_NAT_SRC};
/* Before packet filtering, change destination */
static struct nf_hook_ops ip_nat_local_out_ops
= { { NULL, NULL }, ip_nat_local_fn, PF_INET, NF_IP_LOCAL_OUT, NF_IP_PRI_NAT_DST };

/* Protocol registration. */
int ip_nat_protocol_register(struct ip_nat_protocol *proto)
{
	int ret = 0;
	struct list_head *i;

	WRITE_LOCK(&ip_nat_lock);
	for (i = protos.next; i != &protos; i = i->next) {
		if (((struct ip_nat_protocol *)i)->protonum
		    == proto->protonum) {
			ret = -EBUSY;
			goto out;
		}
	}

	list_prepend(&protos, proto);
	MOD_INC_USE_COUNT;

 out:
	WRITE_UNLOCK(&ip_nat_lock);
	return ret;
}

/* Noone stores the protocol anywhere; simply delete it. */
void ip_nat_protocol_unregister(struct ip_nat_protocol *proto)
{
	WRITE_LOCK(&ip_nat_lock);
	LIST_DELETE(&protos, proto);
	WRITE_UNLOCK(&ip_nat_lock);

	/* Someone could be still looking at the proto in a bh. */
	br_write_lock_bh(BR_NETPROTO_LOCK);
	br_write_unlock_bh(BR_NETPROTO_LOCK);

	MOD_DEC_USE_COUNT;
}

static int init_or_cleanup(int init)
{
	int ret = 0;

	if (!init) goto cleanup;

	ret = ip_nat_rule_init();
	if (ret < 0) {
		printk("ip_nat_init: can't setup rules.\n");
		goto cleanup_nothing;
	}
	ret = ip_nat_init();
	if (ret < 0) {
		printk("ip_nat_init: can't setup rules.\n");
		goto cleanup_rule_init;
	}
	ret = nf_register_hook(&ip_nat_in_ops);
	if (ret < 0) {
		printk("ip_nat_init: can't register in hook.\n");
		goto cleanup_nat;
	}
	ret = nf_register_hook(&ip_nat_out_ops);
	if (ret < 0) {
		printk("ip_nat_init: can't register out hook.\n");
		goto cleanup_inops;
	}
	ret = nf_register_hook(&ip_nat_local_out_ops);
	if (ret < 0) {
		printk("ip_nat_init: can't register local out hook.\n");
		goto cleanup_outops;
	}
	if (ip_conntrack_module)
		__MOD_INC_USE_COUNT(ip_conntrack_module);
	return ret;

 cleanup:
	if (ip_conntrack_module)
		__MOD_DEC_USE_COUNT(ip_conntrack_module);
	nf_unregister_hook(&ip_nat_local_out_ops);
 cleanup_outops:
	nf_unregister_hook(&ip_nat_out_ops);
 cleanup_inops:
	nf_unregister_hook(&ip_nat_in_ops);
 cleanup_nat:
	ip_nat_cleanup();
 cleanup_rule_init:
	ip_nat_rule_cleanup();
 cleanup_nothing:
	MUST_BE_READ_WRITE_UNLOCKED(&ip_nat_lock);
	return ret;
}

static int __init init(void)
{
	return init_or_cleanup(1);
}

static void __exit fini(void)
{
	init_or_cleanup(0);
}

module_init(init);
module_exit(fini);

#ifdef MODULE
EXPORT_SYMBOL(ip_nat_setup_info);
EXPORT_SYMBOL(ip_nat_helper_register);
EXPORT_SYMBOL(ip_nat_helper_unregister);
EXPORT_SYMBOL(ip_nat_expect_register);
EXPORT_SYMBOL(ip_nat_expect_unregister);
EXPORT_SYMBOL(ip_nat_cheat_check);
#endif
