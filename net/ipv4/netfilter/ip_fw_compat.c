/* Compatibility framework for ipchains and ipfwadm support; designed
   to look as much like the 2.2 infrastructure as possible. */
struct notifier_block;

#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <net/icmp.h>
#include <linux/if.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/netfilter_ipv4/compat_firewall.h>
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>

/* Theoretically, we could one day use 2.4 helpers, but for now it
   just confuses depmod --RR */
EXPORT_NO_SYMBOLS;

static struct firewall_ops *fwops;

/* From ip_fw_compat_redir.c */
extern unsigned int
do_redirect(struct sk_buff *skb,
	    const struct net_device *dev,
	    u_int16_t redirpt);

extern void
check_for_redirect(struct sk_buff *skb);

extern void
check_for_unredirect(struct sk_buff *skb);

/* From ip_fw_compat_masq.c */
extern unsigned int
do_masquerade(struct sk_buff **pskb, const struct net_device *dev);

extern unsigned int
check_for_masq_error(struct sk_buff *pskb);

extern unsigned int
check_for_demasq(struct sk_buff **pskb);

extern int __init masq_init(void);
extern void masq_cleanup(void);

/* They call these; we do what they want. */
int register_firewall(int pf, struct firewall_ops *fw)
{
	if (pf != PF_INET) {
		printk("Attempt to register non-IP firewall module.\n");
		return -EINVAL;
	}
	if (fwops) {
		printk("Attempt to register multiple firewall modules.\n");
		return -EBUSY;
	}

	fwops = fw;
	return 0;
}

int unregister_firewall(int pf, struct firewall_ops *fw)
{
	fwops = NULL;
	return 0;
}

static inline void
confirm_connection(struct sk_buff *skb)
{
	if (skb->nfct) {
		struct ip_conntrack *ct
			= (struct ip_conntrack *)skb->nfct->master;
		/* ctinfo is the index of the nfct inside the conntrack */
		enum ip_conntrack_info ctinfo = skb->nfct - ct->infos;

		if ((ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED)
		    && !(ct->status & IPS_CONFIRMED))
			ip_conntrack_confirm(ct);
	}
}

static unsigned int
fw_in(unsigned int hooknum,
      struct sk_buff **pskb,
      const struct net_device *in,
      const struct net_device *out,
      int (*okfn)(struct sk_buff *))
{
	int ret = FW_BLOCK;
	u_int16_t redirpt;

	/* Assume worse case: any hook could change packet */
	(*pskb)->nfcache |= NFC_UNKNOWN | NFC_ALTERED;
	(*pskb)->ip_summed = CHECKSUM_NONE;

	switch (hooknum) {
	case NF_IP_PRE_ROUTING:
		if (fwops->fw_acct_in)
			fwops->fw_acct_in(fwops, PF_INET,
					  (struct net_device *)in,
					  (*pskb)->nh.raw, &redirpt, pskb);

		if ((*pskb)->nh.iph->frag_off & htons(IP_MF|IP_OFFSET)) {
			*pskb = ip_ct_gather_frags(*pskb);

			if (!*pskb)
				return NF_STOLEN;
		}

		ret = fwops->fw_input(fwops, PF_INET, (struct net_device *)in,
				      (*pskb)->nh.raw, &redirpt, pskb);
		break;

	case NF_IP_FORWARD:
		/* Connection will only be set if it was
                   demasqueraded: if so, skip forward chain. */
		if ((*pskb)->nfct)
			ret = FW_ACCEPT;
		else ret = fwops->fw_forward(fwops, PF_INET,
					     (struct net_device *)out,
					     (*pskb)->nh.raw, &redirpt, pskb);
		break;

	case NF_IP_POST_ROUTING:
		ret = fwops->fw_output(fwops, PF_INET,
				       (struct net_device *)out,
				       (*pskb)->nh.raw, &redirpt, pskb);
		if (ret == FW_ACCEPT || ret == FW_SKIP) {
			if (fwops->fw_acct_out)
				fwops->fw_acct_out(fwops, PF_INET,
						   (struct net_device *)in,
						   (*pskb)->nh.raw, &redirpt,
						   pskb);
			confirm_connection(*pskb);
		}
		break;
	}

	switch (ret) {
	case FW_REJECT: {
		/* Alexey says:
		 *
		 * Generally, routing is THE FIRST thing to make, when
		 * packet enters IP stack. Before packet is routed you
		 * cannot call any service routines from IP stack.  */
		struct iphdr *iph = (*pskb)->nh.iph;

		if ((*pskb)->dst != NULL
		    || ip_route_input(*pskb, iph->daddr, iph->saddr, iph->tos,
				      (struct net_device *)in) == 0)
			icmp_send(*pskb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH,
				  0);
		return NF_DROP;
	}

	case FW_ACCEPT:
	case FW_SKIP:
		if (hooknum == NF_IP_PRE_ROUTING) {
			check_for_demasq(pskb);
			check_for_redirect(*pskb);
		} else if (hooknum == NF_IP_POST_ROUTING) {
			check_for_unredirect(*pskb);
			/* Handle ICMP errors from client here */
			if ((*pskb)->nh.iph->protocol == IPPROTO_ICMP
			    && (*pskb)->nfct)
				check_for_masq_error(*pskb);
		}
		return NF_ACCEPT;

	case FW_MASQUERADE:
		if (hooknum == NF_IP_FORWARD)
			return do_masquerade(pskb, out);
		else return NF_ACCEPT;

	case FW_REDIRECT:
		if (hooknum == NF_IP_PRE_ROUTING)
			return do_redirect(*pskb, in, redirpt);
		else return NF_ACCEPT;

	default:
		/* FW_BLOCK */
		return NF_DROP;
	}
}

static unsigned int fw_confirm(unsigned int hooknum,
			       struct sk_buff **pskb,
			       const struct net_device *in,
			       const struct net_device *out,
			       int (*okfn)(struct sk_buff *))
{
	confirm_connection(*pskb);
	return NF_ACCEPT;
}

extern int ip_fw_ctl(int optval, void *user, unsigned int len);

static int sock_fn(struct sock *sk, int optval, void *user, unsigned int len)
{
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	return -ip_fw_ctl(optval, user, len);
}

static struct nf_hook_ops preroute_ops
= { { NULL, NULL }, fw_in, PF_INET, NF_IP_PRE_ROUTING, NF_IP_PRI_FILTER };

static struct nf_hook_ops postroute_ops
= { { NULL, NULL }, fw_in, PF_INET, NF_IP_POST_ROUTING, NF_IP_PRI_FILTER };

static struct nf_hook_ops forward_ops
= { { NULL, NULL }, fw_in, PF_INET, NF_IP_FORWARD, NF_IP_PRI_FILTER };

static struct nf_hook_ops local_in_ops
= { { NULL, NULL }, fw_confirm, PF_INET, NF_IP_LOCAL_IN, NF_IP_PRI_LAST - 1 };

static struct nf_sockopt_ops sock_ops
= { { NULL, NULL }, PF_INET, 64, 64 + 1024 + 1, &sock_fn, 0, 0, NULL,
    0, NULL };

extern int ipfw_init_or_cleanup(int init);

static int init_or_cleanup(int init)
{
	int ret = 0;

	if (!init) goto cleanup;

	ret = nf_register_sockopt(&sock_ops);

	if (ret < 0)
		goto cleanup_nothing;

	ret = ipfw_init_or_cleanup(1);
	if (ret < 0)
		goto cleanup_sockopt;

	ret = masq_init();
	if (ret < 0)
		goto cleanup_ipfw;

	nf_register_hook(&preroute_ops);
	nf_register_hook(&postroute_ops);
	nf_register_hook(&forward_ops);
	nf_register_hook(&local_in_ops);

	return ret;

 cleanup:
	nf_unregister_hook(&preroute_ops);
	nf_unregister_hook(&postroute_ops);
	nf_unregister_hook(&forward_ops);
	nf_unregister_hook(&local_in_ops);

	masq_cleanup();

 cleanup_ipfw:
	ipfw_init_or_cleanup(0);

 cleanup_sockopt:
	nf_unregister_sockopt(&sock_ops);

 cleanup_nothing:
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
