/*
 * This is the 1999 rewrite of IP Firewalling, aiming for kernel 2.3.x.
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/route.h>
#include <linux/ip.h>

#define MANGLE_VALID_HOOKS ((1 << NF_IP_PRE_ROUTING) | (1 << NF_IP_LOCAL_OUT))

/* Standard entry. */
struct ipt_standard
{
	struct ipt_entry entry;
	struct ipt_standard_target target;
};

struct ipt_error_target
{
	struct ipt_entry_target target;
	char errorname[IPT_FUNCTION_MAXNAMELEN];
};

struct ipt_error
{
	struct ipt_entry entry;
	struct ipt_error_target target;
};

static struct
{
	struct ipt_replace repl;
	struct ipt_standard entries[2];
	struct ipt_error term;
} initial_table __initdata
= { { "mangle", MANGLE_VALID_HOOKS, 3,
      sizeof(struct ipt_standard) * 2 + sizeof(struct ipt_error),
      { [NF_IP_PRE_ROUTING] 0,
	[NF_IP_LOCAL_OUT] sizeof(struct ipt_standard) },
      { [NF_IP_PRE_ROUTING] 0,
	[NF_IP_LOCAL_OUT] sizeof(struct ipt_standard) },
      0, NULL, { } },
    {
	    /* PRE_ROUTING */
	    { { { { 0 }, { 0 }, { 0 }, { 0 }, "", "", { 0 }, { 0 }, 0, 0, 0 },
		0,
		sizeof(struct ipt_entry),
		sizeof(struct ipt_standard),
		0, { 0, 0 }, { } },
	      { { { { sizeof(struct ipt_standard_target), "" } }, { } },
		-NF_ACCEPT - 1 } },
	    /* LOCAL_OUT */
	    { { { { 0 }, { 0 }, { 0 }, { 0 }, "", "", { 0 }, { 0 }, 0, 0, 0 },
		0,
		sizeof(struct ipt_entry),
		sizeof(struct ipt_standard),
		0, { 0, 0 }, { } },
	      { { { { sizeof(struct ipt_standard_target), "" } }, { } },
		-NF_ACCEPT - 1 } }
    },
    /* ERROR */
    { { { { 0 }, { 0 }, { 0 }, { 0 }, "", "", { 0 }, { 0 }, 0, 0, 0 },
	0,
	sizeof(struct ipt_entry),
	sizeof(struct ipt_error),
	0, { 0, 0 }, { } },
      { { { { sizeof(struct ipt_error_target), IPT_ERROR_TARGET } },
	  { } },
	"ERROR"
      }
    }
};

static struct ipt_table packet_mangler
= { { NULL, NULL }, "mangle", &initial_table.repl,
    MANGLE_VALID_HOOKS, RW_LOCK_UNLOCKED, NULL };

/* The work comes in here from netfilter.c. */
static unsigned int
ipt_hook(unsigned int hook,
	 struct sk_buff **pskb,
	 const struct net_device *in,
	 const struct net_device *out,
	 int (*okfn)(struct sk_buff *))
{
	return ipt_do_table(pskb, hook, in, out, &packet_mangler, NULL);
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
ipt_local_out_hook(unsigned int hook,
		   struct sk_buff **pskb,
		   const struct net_device *in,
		   const struct net_device *out,
		   int (*okfn)(struct sk_buff *))
{
	unsigned int ret;
	u_int8_t tos;
	u_int32_t saddr, daddr;
	unsigned long nfmark;

	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct iphdr)
	    || (*pskb)->nh.iph->ihl * 4 < sizeof(struct iphdr)) {
		if (net_ratelimit())
			printk("ipt_hook: happy cracking.\n");
		return NF_ACCEPT;
	}

	/* Save things which could affect route */
	nfmark = (*pskb)->nfmark;
	saddr = (*pskb)->nh.iph->saddr;
	daddr = (*pskb)->nh.iph->daddr;
	tos = (*pskb)->nh.iph->tos;

	ret = ipt_do_table(pskb, hook, in, out, &packet_mangler, NULL);
	/* Reroute for ANY change. */
	if (ret != NF_DROP && ret != NF_STOLEN
	    && ((*pskb)->nh.iph->saddr != saddr
		|| (*pskb)->nh.iph->daddr != daddr
		|| (*pskb)->nfmark != nfmark
		|| (*pskb)->nh.iph->tos != tos))
		return route_me_harder(*pskb) == 0 ? ret : NF_DROP;

	return ret;
}

static struct nf_hook_ops ipt_ops[]
= { { { NULL, NULL }, ipt_hook, PF_INET, NF_IP_PRE_ROUTING, NF_IP_PRI_MANGLE },
    { { NULL, NULL }, ipt_local_out_hook, PF_INET, NF_IP_LOCAL_OUT,
		NF_IP_PRI_MANGLE }
};

static int __init init(void)
{
	int ret;

	/* Register table */
	ret = ipt_register_table(&packet_mangler);
	if (ret < 0)
		return ret;

	/* Register hooks */
	ret = nf_register_hook(&ipt_ops[0]);
	if (ret < 0)
		goto cleanup_table;

	ret = nf_register_hook(&ipt_ops[1]);
	if (ret < 0)
		goto cleanup_hook0;

	return ret;

 cleanup_hook0:
	nf_unregister_hook(&ipt_ops[0]);
 cleanup_table:
	ipt_unregister_table(&packet_mangler);

	return ret;
}

static void __exit fini(void)
{
	unsigned int i;

	for (i = 0; i < sizeof(ipt_ops)/sizeof(struct nf_hook_ops); i++)
		nf_unregister_hook(&ipt_ops[i]);

	ipt_unregister_table(&packet_mangler);
}

module_init(init);
module_exit(fini);
