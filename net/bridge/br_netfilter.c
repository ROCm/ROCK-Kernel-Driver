/*
 *	Handle firewalling
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek               <buytenh@gnu.org>
 *	Bart De Schuymer (maintainer)	<bdschuym@pandora.be>
 *
 *	Changes:
 *	Apr 29 2003: physdev module support (bdschuym)
 *	Jun 19 2003: let arptables see bridged ARP traffic (bdschuym)
 *	Oct 06 2003: filter encapsulated IP/ARP VLAN traffic on untagged bridge
 *	             (bdschuym)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Lennert dedicates this file to Kerstin Wurdinger.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/netfilter_bridge.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_arp.h>
#include <linux/in_route.h>
#include <net/ip.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include "br_private.h"


#define skb_origaddr(skb)	 (((struct bridge_skb_cb *) \
				 (skb->cb))->daddr.ipv4)
#define store_orig_dstaddr(skb)	 (skb_origaddr(skb) = (skb)->nh.iph->daddr)
#define dnat_took_place(skb)	 (skb_origaddr(skb) != (skb)->nh.iph->daddr)
#define clear_cb(skb)		 (memset(&skb_origaddr(skb), 0, \
				 sizeof(struct bridge_skb_cb)))

#define has_bridge_parent(device)	((device)->br_port != NULL)
#define bridge_parent(device)		((device)->br_port->br->dev)

#define IS_VLAN_IP (skb->protocol == __constant_htons(ETH_P_8021Q) && \
	hdr->h_vlan_encapsulated_proto == __constant_htons(ETH_P_IP))
#define IS_VLAN_ARP (skb->protocol == __constant_htons(ETH_P_8021Q) && \
	hdr->h_vlan_encapsulated_proto == __constant_htons(ETH_P_ARP))

/* We need these fake structures to make netfilter happy --
 * lots of places assume that skb->dst != NULL, which isn't
 * all that unreasonable.
 *
 * Currently, we fill in the PMTU entry because netfilter
 * refragmentation needs it, and the rt_flags entry because
 * ipt_REJECT needs it.  Future netfilter modules might
 * require us to fill additional fields.
 */
static struct net_device __fake_net_device = {
	.hard_header_len	= ETH_HLEN
};

static struct rtable __fake_rtable = {
	.u = {
		.dst = {
			.__refcnt		= ATOMIC_INIT(1),
			.dev			= &__fake_net_device,
			.path			= &__fake_rtable.u.dst,
			.metrics		= {[RTAX_MTU - 1] = 1500},
		}
	},

	.rt_flags	= 0
};


/* PF_BRIDGE/PRE_ROUTING *********************************************/
static void __br_dnat_complain(void)
{
	static unsigned long last_complaint;

	if (jiffies - last_complaint >= 5 * HZ) {
		printk(KERN_WARNING "Performing cross-bridge DNAT requires IP "
			"forwarding to be enabled\n");
		last_complaint = jiffies;
	}
}


/* This requires some explaining. If DNAT has taken place,
 * we will need to fix up the destination Ethernet address,
 * and this is a tricky process.
 *
 * There are two cases to consider:
 * 1. The packet was DNAT'ed to a device in the same bridge
 *    port group as it was received on. We can still bridge
 *    the packet.
 * 2. The packet was DNAT'ed to a different device, either
 *    a non-bridged device or another bridge port group.
 *    The packet will need to be routed.
 *
 * The correct way of distinguishing between these two cases is to
 * call ip_route_input() and to look at skb->dst->dev, which is
 * changed to the destination device if ip_route_input() succeeds.
 *
 * Let us first consider the case that ip_route_input() succeeds:
 *
 * If skb->dst->dev equals the logical bridge device the packet
 * came in on, we can consider this bridging. We then call
 * skb->dst->output() which will make the packet enter br_nf_local_out()
 * not much later. In that function it is assured that the iptables
 * FORWARD chain is traversed for the packet.
 *
 * Otherwise, the packet is considered to be routed and we just
 * change the destination MAC address so that the packet will
 * later be passed up to the IP stack to be routed.
 *
 * Let us now consider the case that ip_route_input() fails:
 *
 * After a "echo '0' > /proc/sys/net/ipv4/ip_forward" ip_route_input()
 * will fail, while __ip_route_output_key() will return success. The source
 * address for __ip_route_output_key() is set to zero, so __ip_route_output_key
 * thinks we're handling a locally generated packet and won't care
 * if IP forwarding is allowed. We send a warning message to the users's
 * log telling her to put IP forwarding on.
 *
 * ip_route_input() will also fail if there is no route available.
 * In that case we just drop the packet.
 *
 * --Lennert, 20020411
 * --Bart, 20020416 (updated)
 * --Bart, 20021007 (updated)
 */

static int br_nf_pre_routing_finish_bridge(struct sk_buff *skb)
{
#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug |= (1 << NF_BR_PRE_ROUTING) | (1 << NF_BR_FORWARD);
#endif

	if (skb->pkt_type == PACKET_OTHERHOST) {
		skb->pkt_type = PACKET_HOST;
		skb->nf_bridge->mask |= BRNF_PKT_TYPE;
	}
	skb->nf_bridge->mask ^= BRNF_NF_BRIDGE_PREROUTING;

	skb->dev = bridge_parent(skb->dev);
	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_pull(skb, VLAN_HLEN);
		skb->nh.raw += VLAN_HLEN;
	}
	skb->dst->output(skb);
	return 0;
}

static int br_nf_pre_routing_finish(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct iphdr *iph = skb->nh.iph;
	struct nf_bridge_info *nf_bridge = skb->nf_bridge;

#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug ^= (1 << NF_BR_PRE_ROUTING);
#endif

	if (nf_bridge->mask & BRNF_PKT_TYPE) {
		skb->pkt_type = PACKET_OTHERHOST;
		nf_bridge->mask ^= BRNF_PKT_TYPE;
	}
	nf_bridge->mask ^= BRNF_NF_BRIDGE_PREROUTING;

	if (dnat_took_place(skb)) {
		if (ip_route_input(skb, iph->daddr, iph->saddr, iph->tos,
		    dev)) {
			struct rtable *rt;
			struct flowi fl = { .nl_u = 
			{ .ip4_u = { .daddr = iph->daddr, .saddr = 0 ,
				     .tos = RT_TOS(iph->tos)} }, .proto = 0};

			if (!ip_route_output_key(&rt, &fl)) {
				/* Bridged-and-DNAT'ed traffic doesn't
				 * require ip_forwarding.
				 */
				if (((struct dst_entry *)rt)->dev == dev) {
					skb->dst = (struct dst_entry *)rt;
					goto bridged_dnat;
				}
				__br_dnat_complain();
				dst_release((struct dst_entry *)rt);
			}
			kfree_skb(skb);
			return 0;
		} else {
			if (skb->dst->dev == dev) {
bridged_dnat:
				/* Tell br_nf_local_out this is a
				 * bridged frame
				 */
				nf_bridge->mask |= BRNF_BRIDGED_DNAT;
				skb->dev = nf_bridge->physindev;
				clear_cb(skb);
				if (skb->protocol ==
				    __constant_htons(ETH_P_8021Q)) {
					skb_push(skb, VLAN_HLEN);
					skb->nh.raw -= VLAN_HLEN;
				}
				NF_HOOK_THRESH(PF_BRIDGE, NF_BR_PRE_ROUTING,
					       skb, skb->dev, NULL,
					       br_nf_pre_routing_finish_bridge,
					       1);
				return 0;
			}
			memcpy(skb->mac.ethernet->h_dest, dev->dev_addr,
			       ETH_ALEN);
			skb->pkt_type = PACKET_HOST;
		}
	} else {
		skb->dst = (struct dst_entry *)&__fake_rtable;
		dst_hold(skb->dst);
	}

	clear_cb(skb);
	skb->dev = nf_bridge->physindev;
	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_push(skb, VLAN_HLEN);
		skb->nh.raw -= VLAN_HLEN;
	}
	NF_HOOK_THRESH(PF_BRIDGE, NF_BR_PRE_ROUTING, skb, skb->dev, NULL,
		       br_handle_frame_finish, 1);

	return 0;
}

/* Replicate the checks that IPv4 does on packet reception.
 * Set skb->dev to the bridge device (i.e. parent of the
 * receiving device) to make netfilter happy, the REDIRECT
 * target in particular.  Save the original destination IP
 * address to be able to detect DNAT afterwards.
 */
static unsigned int br_nf_pre_routing(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	struct iphdr *iph;
	__u32 len;
	struct sk_buff *skb = *pskb;
	struct nf_bridge_info *nf_bridge;

	if (skb->protocol != __constant_htons(ETH_P_IP)) {
		struct vlan_ethhdr *hdr = (struct vlan_ethhdr *)
					  ((*pskb)->mac.ethernet);

		if (!IS_VLAN_IP)
			return NF_ACCEPT;
		if ((skb = skb_share_check(*pskb, GFP_ATOMIC)) == NULL)
			goto out;
		skb_pull(*pskb, VLAN_HLEN);
		(*pskb)->nh.raw += VLAN_HLEN;
	} else if ((skb = skb_share_check(*pskb, GFP_ATOMIC)) == NULL)
		goto out;

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto inhdr_error;

	iph = skb->nh.iph;
	if (iph->ihl < 5 || iph->version != 4)
		goto inhdr_error;

	if (!pskb_may_pull(skb, 4*iph->ihl))
		goto inhdr_error;

	iph = skb->nh.iph;
	if (ip_fast_csum((__u8 *)iph, iph->ihl) != 0)
		goto inhdr_error;

	len = ntohs(iph->tot_len);
	if (skb->len < len || len < 4*iph->ihl)
		goto inhdr_error;

	if (skb->len > len) {
		__pskb_trim(skb, len);
		if (skb->ip_summed == CHECKSUM_HW)
			skb->ip_summed = CHECKSUM_NONE;
	}

#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug ^= (1 << NF_IP_PRE_ROUTING);
#endif
 	if ((nf_bridge = nf_bridge_alloc(skb)) == NULL)
		return NF_DROP;

	if (skb->pkt_type == PACKET_OTHERHOST) {
		skb->pkt_type = PACKET_HOST;
		nf_bridge->mask |= BRNF_PKT_TYPE;
	}

	nf_bridge->mask |= BRNF_NF_BRIDGE_PREROUTING;
	nf_bridge->physindev = skb->dev;
	skb->dev = bridge_parent(skb->dev);
	store_orig_dstaddr(skb);

	NF_HOOK(PF_INET, NF_IP_PRE_ROUTING, skb, skb->dev, NULL,
		br_nf_pre_routing_finish);

	return NF_STOLEN;

inhdr_error:
//	IP_INC_STATS_BH(IpInHdrErrors);
out:
	return NF_DROP;
}


/* PF_BRIDGE/LOCAL_IN ************************************************/
/* The packet is locally destined, which requires a real
 * dst_entry, so detach the fake one.  On the way up, the
 * packet would pass through PRE_ROUTING again (which already
 * took place when the packet entered the bridge), but we
 * register an IPv4 PRE_ROUTING 'sabotage' hook that will
 * prevent this from happening.
 */
static unsigned int br_nf_local_in(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	struct sk_buff *skb = *pskb;

	if (skb->dst == (struct dst_entry *)&__fake_rtable) {
		dst_release(skb->dst);
		skb->dst = NULL;
	}

	return NF_ACCEPT;
}

/* PF_BRIDGE/FORWARD *************************************************/
static int br_nf_forward_finish(struct sk_buff *skb)
{
	struct nf_bridge_info *nf_bridge = skb->nf_bridge;
	struct net_device *in;
	struct vlan_ethhdr *hdr = (struct vlan_ethhdr *)(skb->mac.ethernet);

#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug ^= (1 << NF_BR_FORWARD);
#endif

	if (skb->protocol == __constant_htons(ETH_P_IP) || IS_VLAN_IP) {
		in = nf_bridge->physindev;
		if (nf_bridge->mask & BRNF_PKT_TYPE) {
			skb->pkt_type = PACKET_OTHERHOST;
			nf_bridge->mask ^= BRNF_PKT_TYPE;
		}
	} else {
		in = *((struct net_device **)(skb->cb));
	}
	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_push(skb, VLAN_HLEN);
		skb->nh.raw -= VLAN_HLEN;
	}
	NF_HOOK_THRESH(PF_BRIDGE, NF_BR_FORWARD, skb, in,
			skb->dev, br_forward_finish, 1);
	return 0;
}

/* This is the 'purely bridged' case.  For IP, we pass the packet to
 * netfilter with indev and outdev set to the bridge device,
 * but we are still able to filter on the 'real' indev/outdev
 * because of the ipt_physdev.c module. For ARP, indev and outdev are the
 * bridge ports.
 */
static unsigned int br_nf_forward(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	struct sk_buff *skb = *pskb;
	struct nf_bridge_info *nf_bridge;
	struct vlan_ethhdr *hdr = (struct vlan_ethhdr *)(skb->mac.ethernet);

	if (skb->protocol != __constant_htons(ETH_P_IP) &&
	    skb->protocol != __constant_htons(ETH_P_ARP)) {
		if (!IS_VLAN_IP && !IS_VLAN_ARP)
			return NF_ACCEPT;
		skb_pull(*pskb, VLAN_HLEN);
		(*pskb)->nh.raw += VLAN_HLEN;
	}

#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug ^= (1 << NF_BR_FORWARD);
#endif
	if (skb->protocol == __constant_htons(ETH_P_IP) || IS_VLAN_IP) {
		nf_bridge = skb->nf_bridge;
		if (skb->pkt_type == PACKET_OTHERHOST) {
			skb->pkt_type = PACKET_HOST;
			nf_bridge->mask |= BRNF_PKT_TYPE;
		}

		/* The physdev module checks on this */
		nf_bridge->mask |= BRNF_BRIDGED;
		nf_bridge->physoutdev = skb->dev;

		NF_HOOK(PF_INET, NF_IP_FORWARD, skb, bridge_parent(in),
			bridge_parent(out), br_nf_forward_finish);
	} else {
		struct net_device **d = (struct net_device **)(skb->cb);
		struct arphdr *arp = skb->nh.arph;

		if (arp->ar_pln != 4) {
			if (IS_VLAN_ARP) {
				skb_push(*pskb, VLAN_HLEN);
				(*pskb)->nh.raw -= VLAN_HLEN;
			}
			return NF_ACCEPT;
		}
		*d = (struct net_device *)in;
		NF_HOOK(NF_ARP, NF_ARP_FORWARD, skb, (struct net_device *)in,
			(struct net_device *)out, br_nf_forward_finish);
	}

	return NF_STOLEN;
}


/* PF_BRIDGE/LOCAL_OUT ***********************************************/
static int br_nf_local_out_finish(struct sk_buff *skb)
{
#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug &= ~(1 << NF_BR_LOCAL_OUT);
#endif
	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_push(skb, VLAN_HLEN);
		skb->nh.raw -= VLAN_HLEN;
	}

	NF_HOOK_THRESH(PF_BRIDGE, NF_BR_LOCAL_OUT, skb, NULL, skb->dev,
			br_forward_finish, NF_BR_PRI_FIRST + 1);

	return 0;
}


/* This function sees both locally originated IP packets and forwarded
 * IP packets (in both cases the destination device is a bridge
 * device). It also sees bridged-and-DNAT'ed packets.
 * To be able to filter on the physical bridge devices (with the ipt_physdev.c
 * module), we steal packets destined to a bridge device away from the
 * PF_INET/FORWARD and PF_INET/OUTPUT hook functions, and give them back later,
 * when we have determined the real output device. This is done in here.
 *
 * If (nf_bridge->mask & BRNF_BRIDGED_DNAT) then the packet is bridged
 * and we fake the PF_BRIDGE/FORWARD hook. The function br_nf_forward()
 * will then fake the PF_INET/FORWARD hook. br_nf_local_out() has priority
 * NF_BR_PRI_FIRST, so no relevant PF_BRIDGE/INPUT functions have been nor
 * will be executed.
 * Otherwise, if nf_bridge->physindev is NULL, the bridge-nf code never touched
 * this packet before, and so the packet was locally originated. We fake
 * the PF_INET/LOCAL_OUT hook.
 * Finally, if nf_bridge->physindev isn't NULL, then the packet was IP routed,
 * so we fake the PF_INET/FORWARD hook. ipv4_sabotage_out() makes sure
 * even routed packets that didn't arrive on a bridge interface have their
 * nf_bridge->physindev set.
 */

static unsigned int br_nf_local_out(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*_okfn)(struct sk_buff *))
{
	int (*okfn)(struct sk_buff *skb);
	struct net_device *realindev;
	struct sk_buff *skb = *pskb;
	struct nf_bridge_info *nf_bridge;
	struct vlan_ethhdr *hdr = (struct vlan_ethhdr *)(skb->mac.ethernet);

	if (skb->protocol != __constant_htons(ETH_P_IP) && !IS_VLAN_IP)
		return NF_ACCEPT;

	/* Sometimes we get packets with NULL ->dst here (for example,
	 * running a dhcp client daemon triggers this).
	 */
	if (skb->dst == NULL)
		return NF_ACCEPT;

	nf_bridge = skb->nf_bridge;
	nf_bridge->physoutdev = skb->dev;

	realindev = nf_bridge->physindev;

	/* Bridged, take PF_BRIDGE/FORWARD.
	 * (see big note in front of br_nf_pre_routing_finish)
	 */
	if (nf_bridge->mask & BRNF_BRIDGED_DNAT) {
		okfn = br_forward_finish;

		if (nf_bridge->mask & BRNF_PKT_TYPE) {
			skb->pkt_type = PACKET_OTHERHOST;
			nf_bridge->mask ^= BRNF_PKT_TYPE;
		}
		if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
			skb_push(skb, VLAN_HLEN);
			skb->nh.raw -= VLAN_HLEN;
		}

		NF_HOOK(PF_BRIDGE, NF_BR_FORWARD, skb, realindev,
			skb->dev, okfn);
	} else {
		struct net_device *realoutdev = bridge_parent(skb->dev);

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
		/* iptables should match -o br0.x */
		if (nf_bridge->netoutdev)
			realoutdev = nf_bridge->netoutdev;
#endif
		okfn = br_nf_local_out_finish;
		if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
			skb_pull(skb, VLAN_HLEN);
			(*pskb)->nh.raw += VLAN_HLEN;
		}
		/* IP forwarded traffic has a physindev, locally
		 * generated traffic hasn't.
		 */
		if (realindev != NULL) {
			if (((nf_bridge->mask & BRNF_DONT_TAKE_PARENT) == 0) &&
			    has_bridge_parent(realindev))
				realindev = bridge_parent(realindev);
			NF_HOOK_THRESH(PF_INET, NF_IP_FORWARD, skb, realindev,
				       realoutdev, okfn,
				       NF_IP_PRI_BRIDGE_SABOTAGE_FORWARD + 1);
		} else {
#ifdef CONFIG_NETFILTER_DEBUG
			skb->nf_debug ^= (1 << NF_IP_LOCAL_OUT);
#endif

			NF_HOOK_THRESH(PF_INET, NF_IP_LOCAL_OUT, skb, realindev,
				       realoutdev, okfn,
				       NF_IP_PRI_BRIDGE_SABOTAGE_LOCAL_OUT + 1);
		}
	}

	return NF_STOLEN;
}


/* PF_BRIDGE/POST_ROUTING ********************************************/
static unsigned int br_nf_post_routing(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	struct sk_buff *skb = *pskb;
	struct nf_bridge_info *nf_bridge = (*pskb)->nf_bridge;
	struct vlan_ethhdr *hdr = (struct vlan_ethhdr *)(skb->mac.ethernet);
	struct net_device *realoutdev = bridge_parent(skb->dev);

	/* Be very paranoid. Must be a device driver bug. */
	if (skb->mac.raw < skb->head || skb->mac.raw + ETH_HLEN > skb->data) {
		printk(KERN_CRIT "br_netfilter: Argh!! br_nf_post_routing: "
				 "bad mac.raw pointer.");
		if (skb->dev != NULL) {
			printk("[%s]", skb->dev->name);
			if (has_bridge_parent(skb->dev))
				printk("[%s]", bridge_parent(skb->dev)->name);
		}
		printk(" head:%p, raw:%p\n", skb->head, skb->mac.raw);
		return NF_ACCEPT;
	}

	if (skb->protocol != __constant_htons(ETH_P_IP) && !IS_VLAN_IP)
		return NF_ACCEPT;

	/* Sometimes we get packets with NULL ->dst here (for example,
	 * running a dhcp client daemon triggers this).
	 */
	if (skb->dst == NULL)
		return NF_ACCEPT;

#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug ^= (1 << NF_IP_POST_ROUTING);
#endif

	/* We assume any code from br_dev_queue_push_xmit onwards doesn't care
	 * about the value of skb->pkt_type.
	 */
	if (skb->pkt_type == PACKET_OTHERHOST) {
		skb->pkt_type = PACKET_HOST;
		nf_bridge->mask |= BRNF_PKT_TYPE;
	}

	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_pull(skb, VLAN_HLEN);
		skb->nh.raw += VLAN_HLEN;
	}

	nf_bridge_save_header(skb);

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
	if (nf_bridge->netoutdev)
		realoutdev = nf_bridge->netoutdev;
#endif
	NF_HOOK(PF_INET, NF_IP_POST_ROUTING, skb, NULL,
		realoutdev, br_dev_queue_push_xmit);

	return NF_STOLEN;
}


/* IPv4/SABOTAGE *****************************************************/

/* Don't hand locally destined packets to PF_INET/PRE_ROUTING
 * for the second time.
 */
static unsigned int ipv4_sabotage_in(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	if ((*pskb)->nf_bridge &&
	    !((*pskb)->nf_bridge->mask & BRNF_NF_BRIDGE_PREROUTING)) {
		okfn(*pskb);
		return NF_STOLEN;
	}

	return NF_ACCEPT;
}

/* Postpone execution of PF_INET/FORWARD, PF_INET/LOCAL_OUT
 * and PF_INET/POST_ROUTING until we have done the forwarding
 * decision in the bridge code and have determined skb->physoutdev.
 */
static unsigned int ipv4_sabotage_out(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	if ((out->hard_start_xmit == br_dev_xmit &&
	    okfn != br_nf_forward_finish &&
	    okfn != br_nf_local_out_finish &&
	    okfn != br_dev_queue_push_xmit)
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
	    || ((out->priv_flags & IFF_802_1Q_VLAN) &&
	    VLAN_DEV_INFO(out)->real_dev->hard_start_xmit == br_dev_xmit)
#endif
	    ) {
		struct sk_buff *skb = *pskb;
		struct nf_bridge_info *nf_bridge;

		if (!skb->nf_bridge && !nf_bridge_alloc(skb))
			return NF_DROP;

		nf_bridge = skb->nf_bridge;

		/* This frame will arrive on PF_BRIDGE/LOCAL_OUT and we
		 * will need the indev then. For a brouter, the real indev
		 * can be a bridge port, so we make sure br_nf_local_out()
		 * doesn't use the bridge parent of the indev by using
		 * the BRNF_DONT_TAKE_PARENT mask.
		 */
		if (hook == NF_IP_FORWARD && nf_bridge->physindev == NULL) {
			nf_bridge->mask &= BRNF_DONT_TAKE_PARENT;
			nf_bridge->physindev = (struct net_device *)in;
		}
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
		/* the iptables outdev is br0.x, not br0 */
		if (out->priv_flags & IFF_802_1Q_VLAN)
			nf_bridge->netoutdev = (struct net_device *)out;
#endif
		okfn(skb);
		return NF_STOLEN;
	}

	return NF_ACCEPT;
}

/* For br_nf_local_out we need (prio = NF_BR_PRI_FIRST), to insure that innocent
 * PF_BRIDGE/NF_BR_LOCAL_OUT functions don't get bridged traffic as input.
 * For br_nf_post_routing, we need (prio = NF_BR_PRI_LAST), because
 * ip_refrag() can return NF_STOLEN.
 */
static struct nf_hook_ops br_nf_ops[] = {
	{ .hook = br_nf_pre_routing, 
	  .owner = THIS_MODULE, 
	  .pf = PF_BRIDGE, 
	  .hooknum = NF_BR_PRE_ROUTING, 
	  .priority = NF_BR_PRI_BRNF, },
	{ .hook = br_nf_local_in,
	  .owner = THIS_MODULE,
	  .pf = PF_BRIDGE,
	  .hooknum = NF_BR_LOCAL_IN,
	  .priority = NF_BR_PRI_BRNF, },
	{ .hook = br_nf_forward,
	  .owner = THIS_MODULE,
	  .pf = PF_BRIDGE,
	  .hooknum = NF_BR_FORWARD,
	  .priority = NF_BR_PRI_BRNF, },
	{ .hook = br_nf_local_out,
	  .owner = THIS_MODULE,
	  .pf = PF_BRIDGE,
	  .hooknum = NF_BR_LOCAL_OUT,
	  .priority = NF_BR_PRI_FIRST, },
	{ .hook = br_nf_post_routing,
	  .owner = THIS_MODULE,
	  .pf = PF_BRIDGE,
	  .hooknum = NF_BR_POST_ROUTING,
	  .priority = NF_BR_PRI_LAST, },
	{ .hook = ipv4_sabotage_in,
	  .owner = THIS_MODULE,
	  .pf = PF_INET,
	  .hooknum = NF_IP_PRE_ROUTING,
	  .priority = NF_IP_PRI_FIRST, },
	{ .hook = ipv4_sabotage_out,
	  .owner = THIS_MODULE,
	  .pf = PF_INET,
	  .hooknum = NF_IP_FORWARD,
	  .priority = NF_IP_PRI_BRIDGE_SABOTAGE_FORWARD, },
	{ .hook = ipv4_sabotage_out,
	  .owner = THIS_MODULE,
	  .pf = PF_INET,
	  .hooknum = NF_IP_LOCAL_OUT,
	  .priority = NF_IP_PRI_BRIDGE_SABOTAGE_LOCAL_OUT, },
	{ .hook = ipv4_sabotage_out,
	  .owner = THIS_MODULE,
	  .pf = PF_INET,
	  .hooknum = NF_IP_POST_ROUTING,
	  .priority = NF_IP_PRI_FIRST, },
};

int br_netfilter_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(br_nf_ops); i++) {
		int ret;

		if ((ret = nf_register_hook(&br_nf_ops[i])) >= 0)
			continue;

		while (i--)
			nf_unregister_hook(&br_nf_ops[i]);

		return ret;
	}

	printk(KERN_NOTICE "Bridge firewalling registered\n");

	return 0;
}

void br_netfilter_fini(void)
{
	int i;

	for (i = ARRAY_SIZE(br_nf_ops) - 1; i >= 0; i--)
		nf_unregister_hook(&br_nf_ops[i]);
}
