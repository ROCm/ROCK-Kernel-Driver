/*
 *	Neighbour Discovery for IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *	Mike Shaver		<shaver@ingenia.com>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Changes:
 *
 *	Lars Fenneberg			:	fixed MTU setting on receipt
 *						of an RA.
 *
 *	Janos Farkas			:	kmalloc failure checks
 *	Alexey Kuznetsov		:	state machine reworked
 *						and moved to net/core.
 */

/* Set to 3 to get tracing... */
#define ND_DEBUG 1

#define ND_PRINTK(x...) printk(KERN_DEBUG x)
#define ND_NOPRINTK(x...) do { ; } while(0)
#define ND_PRINTK0 ND_PRINTK
#define ND_PRINTK1 ND_NOPRINTK
#define ND_PRINTK2 ND_NOPRINTK
#if ND_DEBUG >= 1
#undef ND_PRINTK1
#define ND_PRINTK1 ND_PRINTK
#endif
#if ND_DEBUG >= 2
#undef ND_PRINTK2
#define ND_PRINTK2 ND_PRINTK
#endif

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/route.h>
#include <linux/init.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/icmp.h>

#include <net/checksum.h>
#include <linux/proc_fs.h>

static struct socket *ndisc_socket;

static u32 ndisc_hash(const void *pkey, const struct net_device *dev);
static int ndisc_constructor(struct neighbour *neigh);
static void ndisc_solicit(struct neighbour *neigh, struct sk_buff *skb);
static void ndisc_error_report(struct neighbour *neigh, struct sk_buff *skb);
static int pndisc_constructor(struct pneigh_entry *n);
static void pndisc_destructor(struct pneigh_entry *n);
static void pndisc_redo(struct sk_buff *skb);

static struct neigh_ops ndisc_generic_ops =
{
	AF_INET6,
	NULL,
	ndisc_solicit,
	ndisc_error_report,
	neigh_resolve_output,
	neigh_connected_output,
	dev_queue_xmit,
	dev_queue_xmit
};

static struct neigh_ops ndisc_hh_ops =
{
	AF_INET6,
	NULL,
	ndisc_solicit,
	ndisc_error_report,
	neigh_resolve_output,
	neigh_resolve_output,
	dev_queue_xmit,
	dev_queue_xmit
};


static struct neigh_ops ndisc_direct_ops =
{
	AF_INET6,
	NULL,
	NULL,
	NULL,
	dev_queue_xmit,
	dev_queue_xmit,
	dev_queue_xmit,
	dev_queue_xmit
};

struct neigh_table nd_tbl =
{
	NULL,
	AF_INET6,
	sizeof(struct neighbour) + sizeof(struct in6_addr),
	sizeof(struct in6_addr),
	ndisc_hash,
	ndisc_constructor,
	pndisc_constructor,
	pndisc_destructor,
	pndisc_redo,
	"ndisc_cache",
        { NULL, NULL, &nd_tbl, 0, NULL, NULL,
		  30*HZ, 1*HZ, 60*HZ, 30*HZ, 5*HZ, 3, 3, 0, 3, 1*HZ, (8*HZ)/10, 64, 0 },
	30*HZ, 128, 512, 1024,
};

#define NDISC_OPT_SPACE(len) (((len)+2+7)&~7)

static u8 *ndisc_fill_option(u8 *opt, int type, void *data, int data_len)
{
	int space = NDISC_OPT_SPACE(data_len);

	opt[0] = type;
	opt[1] = space>>3;
	memcpy(opt+2, data, data_len);
	data_len += 2;
	opt += data_len;
	if ((space -= data_len) > 0)
		memset(opt, 0, space);
	return opt + space;
}

int ndisc_mc_map(struct in6_addr *addr, char *buf, struct net_device *dev, int dir)
{
	switch (dev->type) {
	case ARPHRD_ETHER:
	case ARPHRD_IEEE802:	/* Not sure. Check it later. --ANK */
	case ARPHRD_FDDI:
		ipv6_eth_mc_map(addr, buf);
		return 0;
	case ARPHRD_IEEE802_TR:
		ipv6_tr_mc_map(addr,buf);
		return 0;
	default:
		if (dir) {
			memcpy(buf, dev->broadcast, dev->addr_len);
			return 0;
		}
	}
	return -EINVAL;
}

static u32 ndisc_hash(const void *pkey, const struct net_device *dev)
{
	u32 hash_val;

	hash_val = *(u32*)(pkey + sizeof(struct in6_addr) - 4);
	hash_val ^= (hash_val>>16);
	hash_val ^= hash_val>>8;
	hash_val ^= hash_val>>3;
	hash_val = (hash_val^dev->ifindex)&NEIGH_HASHMASK;

	return hash_val;
}

static int ndisc_constructor(struct neighbour *neigh)
{
	struct in6_addr *addr = (struct in6_addr*)&neigh->primary_key;
	struct net_device *dev = neigh->dev;
	struct inet6_dev *in6_dev = in6_dev_get(dev);
	int addr_type;

	if (in6_dev == NULL)
		return -EINVAL;

	addr_type = ipv6_addr_type(addr);
	if (in6_dev->nd_parms)
		neigh->parms = in6_dev->nd_parms;

	if (addr_type&IPV6_ADDR_MULTICAST)
		neigh->type = RTN_MULTICAST;
	else
		neigh->type = RTN_UNICAST;
	if (dev->hard_header == NULL) {
		neigh->nud_state = NUD_NOARP;
		neigh->ops = &ndisc_direct_ops;
		neigh->output = neigh->ops->queue_xmit;
	} else {
		if (addr_type&IPV6_ADDR_MULTICAST) {
			neigh->nud_state = NUD_NOARP;
			ndisc_mc_map(addr, neigh->ha, dev, 1);
		} else if (dev->flags&(IFF_NOARP|IFF_LOOPBACK)) {
			neigh->nud_state = NUD_NOARP;
			memcpy(neigh->ha, dev->dev_addr, dev->addr_len);
			if (dev->flags&IFF_LOOPBACK)
				neigh->type = RTN_LOCAL;
		} else if (dev->flags&IFF_POINTOPOINT) {
			neigh->nud_state = NUD_NOARP;
			memcpy(neigh->ha, dev->broadcast, dev->addr_len);
		}
		if (dev->hard_header_cache)
			neigh->ops = &ndisc_hh_ops;
		else
			neigh->ops = &ndisc_generic_ops;
		if (neigh->nud_state&NUD_VALID)
			neigh->output = neigh->ops->connected_output;
		else
			neigh->output = neigh->ops->output;
	}
	in6_dev_put(in6_dev);
	return 0;
}

static int pndisc_constructor(struct pneigh_entry *n)
{
	struct in6_addr *addr = (struct in6_addr*)&n->key;
	struct in6_addr maddr;
	struct net_device *dev = n->dev;

	if (dev == NULL || __in6_dev_get(dev) == NULL)
		return -EINVAL;
#ifndef CONFIG_IPV6_NO_PB
	addrconf_addr_solict_mult_old(addr, &maddr);
	ipv6_dev_mc_inc(dev, &maddr);
#endif
#ifdef CONFIG_IPV6_EUI64
	addrconf_addr_solict_mult_new(addr, &maddr);
	ipv6_dev_mc_inc(dev, &maddr);
#endif
	return 0;
}

static void pndisc_destructor(struct pneigh_entry *n)
{
	struct in6_addr *addr = (struct in6_addr*)&n->key;
	struct in6_addr maddr;
	struct net_device *dev = n->dev;

	if (dev == NULL || __in6_dev_get(dev) == NULL)
		return;
#ifndef CONFIG_IPV6_NO_PB
	addrconf_addr_solict_mult_old(addr, &maddr);
	ipv6_dev_mc_dec(dev, &maddr);
#endif
#ifdef CONFIG_IPV6_EUI64
	addrconf_addr_solict_mult_new(addr, &maddr);
	ipv6_dev_mc_dec(dev, &maddr);
#endif
}



static int
ndisc_build_ll_hdr(struct sk_buff *skb, struct net_device *dev,
		   struct in6_addr *daddr, struct neighbour *neigh, int len)
{
	unsigned char ha[MAX_ADDR_LEN];
	unsigned char *h_dest = NULL;

	skb_reserve(skb, (dev->hard_header_len + 15) & ~15);

	if (dev->hard_header) {
		if (ipv6_addr_type(daddr) & IPV6_ADDR_MULTICAST) {
			ndisc_mc_map(daddr, ha, dev, 1);
			h_dest = ha;
		} else if (neigh) {
			read_lock_bh(&neigh->lock);
			if (neigh->nud_state&NUD_VALID) {
				memcpy(ha, neigh->ha, dev->addr_len);
				h_dest = ha;
			}
			read_unlock_bh(&neigh->lock);
		} else {
			neigh = neigh_lookup(&nd_tbl, daddr, dev);
			if (neigh) {
				read_lock_bh(&neigh->lock);
				if (neigh->nud_state&NUD_VALID) {
					memcpy(ha, neigh->ha, dev->addr_len);
					h_dest = ha;
				}
				read_unlock_bh(&neigh->lock);
				neigh_release(neigh);
			}
		}

		if (dev->hard_header(skb, dev, ETH_P_IPV6, h_dest, NULL, len) < 0)
			return 0;
	}

	return 1;
}


/*
 *	Send a Neighbour Advertisement
 */

void ndisc_send_na(struct net_device *dev, struct neighbour *neigh,
		   struct in6_addr *daddr, struct in6_addr *solicited_addr,
		   int router, int solicited, int override, int inc_opt) 
{
        struct sock *sk = ndisc_socket->sk;
        struct nd_msg *msg;
        int len;
        struct sk_buff *skb;
	int err;

	len = sizeof(struct icmp6hdr) + sizeof(struct in6_addr);

	if (inc_opt) {
		if (dev->addr_len)
			len += NDISC_OPT_SPACE(dev->addr_len);
		else
			inc_opt = 0;
	}

	skb = sock_alloc_send_skb(sk, MAX_HEADER + len + dev->hard_header_len + 15,
				  0, 0, &err);

	if (skb == NULL) {
		ND_PRINTK1("send_na: alloc skb failed\n");
		return;
	}

	if (ndisc_build_ll_hdr(skb, dev, daddr, neigh, len) == 0) {
		kfree_skb(skb);
		return;
	}

	ip6_nd_hdr(sk, skb, dev, solicited_addr, daddr, IPPROTO_ICMPV6, len);

	msg = (struct nd_msg *) skb_put(skb, len);

        msg->icmph.icmp6_type = NDISC_NEIGHBOUR_ADVERTISEMENT;
        msg->icmph.icmp6_code = 0;
        msg->icmph.icmp6_cksum = 0;

        msg->icmph.icmp6_unused = 0;
        msg->icmph.icmp6_router    = router;
        msg->icmph.icmp6_solicited = solicited;
        msg->icmph.icmp6_override  = !!override;

        /* Set the target address. */
	ipv6_addr_copy(&msg->target, solicited_addr);

	if (inc_opt)
		ndisc_fill_option((void*)&msg->opt, ND_OPT_TARGET_LL_ADDR, dev->dev_addr, dev->addr_len);

	/* checksum */
	msg->icmph.icmp6_cksum = csum_ipv6_magic(solicited_addr, daddr, len, 
						 IPPROTO_ICMPV6,
						 csum_partial((__u8 *) msg, 
							      len, 0));

	dev_queue_xmit(skb);

	ICMP6_INC_STATS(Icmp6OutNeighborAdvertisements);
	ICMP6_INC_STATS(Icmp6OutMsgs);
}        

void ndisc_send_ns(struct net_device *dev, struct neighbour *neigh,
		   struct in6_addr *solicit,
		   struct in6_addr *daddr, struct in6_addr *saddr) 
{
        struct sock *sk = ndisc_socket->sk;
        struct sk_buff *skb;
        struct nd_msg *msg;
	struct in6_addr addr_buf;
        int len;
	int err;

	len = sizeof(struct icmp6hdr) + sizeof(struct in6_addr);
	if (dev->addr_len)
		len += NDISC_OPT_SPACE(dev->addr_len);

	skb = sock_alloc_send_skb(sk, MAX_HEADER + len + dev->hard_header_len + 15,
				  0, 0, &err);
	if (skb == NULL) {
		ND_PRINTK1("send_ns: alloc skb failed\n");
		return;
	}

	if (saddr == NULL) {
		if (ipv6_get_lladdr(dev, &addr_buf)) {
			kfree_skb(skb);
			return;
		}
		saddr = &addr_buf;
	}

	if (ndisc_build_ll_hdr(skb, dev, daddr, neigh, len) == 0) {
		kfree_skb(skb);
		return;
	}

	ip6_nd_hdr(sk, skb, dev, saddr, daddr, IPPROTO_ICMPV6, len);

	msg = (struct nd_msg *)skb_put(skb, len);
	msg->icmph.icmp6_type = NDISC_NEIGHBOUR_SOLICITATION;
	msg->icmph.icmp6_code = 0;
	msg->icmph.icmp6_cksum = 0;
	msg->icmph.icmp6_unused = 0;

	/* Set the target address. */
	ipv6_addr_copy(&msg->target, solicit);

	if (dev->addr_len)
		ndisc_fill_option((void*)&msg->opt, ND_OPT_SOURCE_LL_ADDR, dev->dev_addr, dev->addr_len);

	/* checksum */
	msg->icmph.icmp6_cksum = csum_ipv6_magic(&skb->nh.ipv6h->saddr,
						 daddr, len, 
						 IPPROTO_ICMPV6,
						 csum_partial((__u8 *) msg, 
							      len, 0));
	/* send it! */
	dev_queue_xmit(skb);

	ICMP6_INC_STATS(Icmp6OutNeighborSolicits);
	ICMP6_INC_STATS(Icmp6OutMsgs);
}

void ndisc_send_rs(struct net_device *dev, struct in6_addr *saddr,
		   struct in6_addr *daddr)
{
	struct sock *sk = ndisc_socket->sk;
        struct sk_buff *skb;
        struct icmp6hdr *hdr;
	__u8 * opt;
        int len;
	int err;

	len = sizeof(struct icmp6hdr);
	if (dev->addr_len)
		len += NDISC_OPT_SPACE(dev->addr_len);

        skb = sock_alloc_send_skb(sk, MAX_HEADER + len + dev->hard_header_len + 15,
				  0, 0, &err);
	if (skb == NULL) {
		ND_PRINTK1("send_ns: alloc skb failed\n");
		return;
	}

	if (ndisc_build_ll_hdr(skb, dev, daddr, NULL, len) == 0) {
		kfree_skb(skb);
		return;
	}

	ip6_nd_hdr(sk, skb, dev, saddr, daddr, IPPROTO_ICMPV6, len);

        hdr = (struct icmp6hdr *) skb_put(skb, len);
        hdr->icmp6_type = NDISC_ROUTER_SOLICITATION;
        hdr->icmp6_code = 0;
        hdr->icmp6_cksum = 0;
        hdr->icmp6_unused = 0;

	opt = (u8*) (hdr + 1);

	if (dev->addr_len)
		ndisc_fill_option(opt, ND_OPT_SOURCE_LL_ADDR, dev->dev_addr, dev->addr_len);

	/* checksum */
	hdr->icmp6_cksum = csum_ipv6_magic(&skb->nh.ipv6h->saddr, daddr, len,
					   IPPROTO_ICMPV6,
					   csum_partial((__u8 *) hdr, len, 0));

	/* send it! */
	dev_queue_xmit(skb);

	ICMP6_INC_STATS(Icmp6OutRouterSolicits);
	ICMP6_INC_STATS(Icmp6OutMsgs);
}
		   

static u8 * ndisc_find_option(u8 *opt, int opt_len, int len, int option)
{
	while (opt_len <= len) {
		int l = opt[1]<<3;

		if (opt[0] == option && l >= opt_len)
			return opt + 2;

		if (l == 0) {
			if (net_ratelimit())
			    printk(KERN_WARNING "ndisc: option has 0 len\n");
			return NULL;
		}

		opt += l;
		len -= l;
	}
	return NULL;
}


static void ndisc_error_report(struct neighbour *neigh, struct sk_buff *skb)
{
	/*
	 *	"The sender MUST return an ICMP
	 *	 destination unreachable"
	 */
	dst_link_failure(skb);
	kfree_skb(skb);
}

/* Called with locked neigh: either read or both */

static void ndisc_solicit(struct neighbour *neigh, struct sk_buff *skb)
{
	struct in6_addr *saddr = NULL;
	struct in6_addr mcaddr;
	struct net_device *dev = neigh->dev;
	struct in6_addr *target = (struct in6_addr *)&neigh->primary_key;
	int probes = atomic_read(&neigh->probes);

	if (skb && ipv6_chk_addr(&skb->nh.ipv6h->saddr, dev))
		saddr = &skb->nh.ipv6h->saddr;

	if ((probes -= neigh->parms->ucast_probes) < 0) {
		if (!(neigh->nud_state&NUD_VALID))
			ND_PRINTK1("trying to ucast probe in NUD_INVALID\n");
		ndisc_send_ns(dev, neigh, target, target, saddr);
	} else if ((probes -= neigh->parms->app_probes) < 0) {
#ifdef CONFIG_ARPD
		neigh_app_ns(neigh);
#endif
	} else {
#ifdef CONFIG_IPV6_EUI64
		addrconf_addr_solict_mult_new(target, &mcaddr);
		ndisc_send_ns(dev, NULL, target, &mcaddr, saddr);
#endif
#ifndef CONFIG_IPV6_NO_PB
		addrconf_addr_solict_mult_old(target, &mcaddr);
		ndisc_send_ns(dev, NULL, target, &mcaddr, saddr);
#endif
	}
}


static void ndisc_update(struct neighbour *neigh, u8* opt, int len, int type)
{
	opt = ndisc_find_option(opt, neigh->dev->addr_len+2, len, type);
	neigh_update(neigh, opt, NUD_STALE, 1, 1);
}

static void ndisc_router_discovery(struct sk_buff *skb)
{
        struct ra_msg *ra_msg = (struct ra_msg *) skb->h.raw;
	struct neighbour *neigh;
	struct inet6_dev *in6_dev;
	struct rt6_info *rt;
	int lifetime;
	int optlen;

	__u8 * opt = (__u8 *)(ra_msg + 1);

	optlen = (skb->tail - skb->h.raw) - sizeof(struct ra_msg);

	if (skb->nh.ipv6h->hop_limit != 255) {
		printk(KERN_INFO
		       "NDISC: fake router advertisment received\n");
		return;
	}

	/*
	 *	set the RA_RECV flag in the interface
	 */

	in6_dev = in6_dev_get(skb->dev);
	if (in6_dev == NULL) {
		ND_PRINTK1("RA: can't find in6 device\n");
		return;
	}
	if (in6_dev->cnf.forwarding || !in6_dev->cnf.accept_ra) {
		in6_dev_put(in6_dev);
		return;
	}

	if (in6_dev->if_flags & IF_RS_SENT) {
		/*
		 *	flag that an RA was received after an RS was sent
		 *	out on this interface.
		 */
		in6_dev->if_flags |= IF_RA_RCVD;
	}

	lifetime = ntohs(ra_msg->icmph.icmp6_rt_lifetime);

	rt = rt6_get_dflt_router(&skb->nh.ipv6h->saddr, skb->dev);

	if (rt && lifetime == 0) {
		ip6_del_rt(rt);
		rt = NULL;
	}

	if (rt == NULL && lifetime) {
		ND_PRINTK2("ndisc_rdisc: adding default router\n");

		rt = rt6_add_dflt_router(&skb->nh.ipv6h->saddr, skb->dev);
		if (rt == NULL) {
			ND_PRINTK1("route_add failed\n");
			in6_dev_put(in6_dev);
			return;
		}

		neigh = rt->rt6i_nexthop;
		if (neigh == NULL) {
			ND_PRINTK1("nd: add default router: null neighbour\n");
			dst_release(&rt->u.dst);
			in6_dev_put(in6_dev);
			return;
		}
		neigh->flags |= NTF_ROUTER;

		/*
		 *	If we where using an "all destinations on link" route
		 *	delete it
		 */

		rt6_purge_dflt_routers(RTF_ALLONLINK);
	}

	if (rt)
		rt->rt6i_expires = jiffies + (HZ * lifetime);

	if (ra_msg->icmph.icmp6_hop_limit)
		in6_dev->cnf.hop_limit = ra_msg->icmph.icmp6_hop_limit;

	/*
	 *	Update Reachable Time and Retrans Timer
	 */

	if (in6_dev->nd_parms) {
		__u32 rtime = ntohl(ra_msg->retrans_timer);

		if (rtime && rtime/1000 < MAX_SCHEDULE_TIMEOUT/HZ) {
			rtime = (rtime*HZ)/1000;
			if (rtime < HZ/10)
				rtime = HZ/10;
			in6_dev->nd_parms->retrans_time = rtime;
		}

		rtime = ntohl(ra_msg->reachable_time);
		if (rtime && rtime/1000 < MAX_SCHEDULE_TIMEOUT/(3*HZ)) {
			rtime = (rtime*HZ)/1000;

			if (rtime < HZ/10)
				rtime = HZ/10;

			if (rtime != in6_dev->nd_parms->base_reachable_time) {
				in6_dev->nd_parms->base_reachable_time = rtime;
				in6_dev->nd_parms->gc_staletime = 3 * rtime;
				in6_dev->nd_parms->reachable_time = neigh_rand_reach_time(rtime);
			}
		}
	}

	/*
	 *	Process options.
	 */

        while (optlen > 0) {
                int len = (opt[1] << 3);

		if (len == 0) {
			ND_PRINTK0("RA: opt has 0 len\n");
			break;
		}

                switch(*opt) {
                case ND_OPT_SOURCE_LL_ADDR:

			if (rt == NULL)
				break;
			
			if ((neigh = rt->rt6i_nexthop) != NULL &&
			    skb->dev->addr_len + 2 >= len)
				neigh_update(neigh, opt+2, NUD_STALE, 1, 1);
			break;

                case ND_OPT_PREFIX_INFO:
			addrconf_prefix_rcv(skb->dev, opt, len);
                        break;

                case ND_OPT_MTU:
			{
				int mtu;
				
				mtu = htonl(*(__u32 *)(opt+4));

				if (mtu < IPV6_MIN_MTU || mtu > skb->dev->mtu) {
					ND_PRINTK0("NDISC: router "
						   "announcement with mtu = %d\n",
						   mtu);
					break;
				}

				if (in6_dev->cnf.mtu6 != mtu) {
					in6_dev->cnf.mtu6 = mtu;

					if (rt)
						rt->u.dst.pmtu = mtu;

					rt6_mtu_change(skb->dev, mtu);
				}
			}
                        break;

		case ND_OPT_TARGET_LL_ADDR:
		case ND_OPT_REDIRECT_HDR:
			ND_PRINTK0("got illegal option with RA");
			break;
		default:
			ND_PRINTK0("unkown option in RA\n");
                };
                optlen -= len;
                opt += len;
        }
	if (rt)
		dst_release(&rt->u.dst);
	in6_dev_put(in6_dev);
}

static void ndisc_redirect_rcv(struct sk_buff *skb)
{
	struct inet6_dev *in6_dev;
	struct icmp6hdr *icmph;
	struct in6_addr *dest;
	struct in6_addr *target;	/* new first hop to destination */
	struct neighbour *neigh;
	int on_link = 0;
	int optlen;

	if (skb->nh.ipv6h->hop_limit != 255) {
		printk(KERN_WARNING "NDISC: fake ICMP redirect received\n");
		return;
	}

	if (!(ipv6_addr_type(&skb->nh.ipv6h->saddr) & IPV6_ADDR_LINKLOCAL)) {
		printk(KERN_WARNING "ICMP redirect: source address is not linklocal\n");
		return;
	}

	optlen = skb->tail - skb->h.raw;
	optlen -= sizeof(struct icmp6hdr) + 2 * sizeof(struct in6_addr);

	if (optlen < 0) {
		printk(KERN_WARNING "ICMP redirect: packet too small\n");
		return;
	}

	icmph = (struct icmp6hdr *) skb->h.raw;
	target = (struct in6_addr *) (icmph + 1);
	dest = target + 1;

	if (ipv6_addr_type(dest) & IPV6_ADDR_MULTICAST) {
		printk(KERN_WARNING "ICMP redirect for multicast addr\n");
		return;
	}

	if (ipv6_addr_cmp(dest, target) == 0) {
		on_link = 1;
	} else if (!(ipv6_addr_type(target) & IPV6_ADDR_LINKLOCAL)) {
		printk(KERN_WARNING "ICMP redirect: target address is not linklocal\n");
		return;
	}

	in6_dev = in6_dev_get(skb->dev);
	if (!in6_dev)
		return;
	if (in6_dev->cnf.forwarding || !in6_dev->cnf.accept_redirects) {
		in6_dev_put(in6_dev);
		return;
	}

	/* passed validation tests */

	/*
	   We install redirect only if nexthop state is valid.
	 */

	neigh = __neigh_lookup(&nd_tbl, target, skb->dev, 1);
	if (neigh) {
		ndisc_update(neigh, (u8*)(dest + 1), optlen, ND_OPT_TARGET_LL_ADDR);
		if (neigh->nud_state&NUD_VALID)
			rt6_redirect(dest, &skb->nh.ipv6h->saddr, neigh, on_link);
		else
			__neigh_event_send(neigh, NULL);
		neigh_release(neigh);
	}
	in6_dev_put(in6_dev);
}

void ndisc_send_redirect(struct sk_buff *skb, struct neighbour *neigh,
			 struct in6_addr *target)
{
	struct sock *sk = ndisc_socket->sk;
	int len = sizeof(struct icmp6hdr) + 2 * sizeof(struct in6_addr);
	struct sk_buff *buff;
	struct icmp6hdr *icmph;
	struct in6_addr saddr_buf;
	struct in6_addr *addrp;
	struct net_device *dev;
	struct rt6_info *rt;
	u8 *opt;
	int rd_len;
	int err;
	int hlen;

	dev = skb->dev;
	rt = rt6_lookup(&skb->nh.ipv6h->saddr, NULL, dev->ifindex, 1);

	if (rt == NULL)
		return;

	if (rt->rt6i_flags & RTF_GATEWAY) {
		ND_PRINTK1("ndisc_send_redirect: not a neighbour\n");
		dst_release(&rt->u.dst);
		return;
	}
	if (!xrlim_allow(&rt->u.dst, 1*HZ)) {
		dst_release(&rt->u.dst);
		return;
	}
	dst_release(&rt->u.dst);

	if (dev->addr_len) {
		if (neigh->nud_state&NUD_VALID) {
			len  += NDISC_OPT_SPACE(dev->addr_len);
		} else {
			/* If nexthop is not valid, do not redirect!
			   We will make it later, when will be sure,
			   that it is alive.
			 */
			return;
		}
	}

	rd_len = min(IPV6_MIN_MTU-sizeof(struct ipv6hdr)-len, skb->len + 8);
	rd_len &= ~0x7;
	len += rd_len;

	if (ipv6_get_lladdr(dev, &saddr_buf)) {
 		ND_PRINTK1("redirect: no link_local addr for dev\n");
 		return;
 	}

	buff = sock_alloc_send_skb(sk, MAX_HEADER + len + dev->hard_header_len + 15,
				   0, 0, &err);
	if (buff == NULL) {
		ND_PRINTK1("ndisc_send_redirect: alloc_skb failed\n");
		return;
	}

	hlen = 0;

	if (ndisc_build_ll_hdr(buff, dev, &skb->nh.ipv6h->saddr, NULL, len) == 0) {
		kfree_skb(buff);
		return;
	}

	ip6_nd_hdr(sk, buff, dev, &saddr_buf, &skb->nh.ipv6h->saddr,
		   IPPROTO_ICMPV6, len);

	icmph = (struct icmp6hdr *) skb_put(buff, len);

	memset(icmph, 0, sizeof(struct icmp6hdr));
	icmph->icmp6_type = NDISC_REDIRECT;

	/*
	 *	copy target and destination addresses
	 */

	addrp = (struct in6_addr *)(icmph + 1);
	ipv6_addr_copy(addrp, target);
	addrp++;
	ipv6_addr_copy(addrp, &skb->nh.ipv6h->daddr);

	opt = (u8*) (addrp + 1);

	/*
	 *	include target_address option
	 */

	if (dev->addr_len)
		opt = ndisc_fill_option(opt, ND_OPT_TARGET_LL_ADDR, neigh->ha, dev->addr_len);

	/*
	 *	build redirect option and copy skb over to the new packet.
	 */

	memset(opt, 0, 8);	
	*(opt++) = ND_OPT_REDIRECT_HDR;
	*(opt++) = (rd_len >> 3);
	opt += 6;

	memcpy(opt, skb->nh.ipv6h, rd_len - 8);

	icmph->icmp6_cksum = csum_ipv6_magic(&saddr_buf, &skb->nh.ipv6h->saddr,
					     len, IPPROTO_ICMPV6,
					     csum_partial((u8 *) icmph, len, 0));

	dev_queue_xmit(buff);

	ICMP6_INC_STATS(Icmp6OutRedirects);
	ICMP6_INC_STATS(Icmp6OutMsgs);
}

static __inline__ struct neighbour *
ndisc_recv_ns(struct in6_addr *saddr, struct sk_buff *skb)
{
	u8 *opt;

	opt = skb->h.raw;
	opt += sizeof(struct icmp6hdr) + sizeof(struct in6_addr);
	opt = ndisc_find_option(opt, skb->dev->addr_len+2, skb->tail - opt, ND_OPT_SOURCE_LL_ADDR);

	return neigh_event_ns(&nd_tbl, opt, saddr, skb->dev);
}

static __inline__ int ndisc_recv_na(struct neighbour *neigh, struct sk_buff *skb)
{
	struct nd_msg *msg = (struct nd_msg *) skb->h.raw;
	u8 *opt;

	opt = skb->h.raw;
	opt += sizeof(struct icmp6hdr) + sizeof(struct in6_addr);
	opt = ndisc_find_option(opt, skb->dev->addr_len+2, skb->tail - opt, ND_OPT_TARGET_LL_ADDR);

	return neigh_update(neigh, opt,
			    msg->icmph.icmp6_solicited ? NUD_REACHABLE : NUD_STALE,
			    msg->icmph.icmp6_override, 1);
}

static void pndisc_redo(struct sk_buff *skb)
{
	ndisc_rcv(skb, skb->len);
	kfree_skb(skb);
}

int ndisc_rcv(struct sk_buff *skb, unsigned long len)
{
	struct net_device *dev = skb->dev;
	struct in6_addr *saddr = &skb->nh.ipv6h->saddr;
	struct in6_addr *daddr = &skb->nh.ipv6h->daddr;
	struct nd_msg *msg = (struct nd_msg *) skb->h.raw;
	struct neighbour *neigh;
	struct inet6_ifaddr *ifp;

	switch (msg->icmph.icmp6_type) {
	case NDISC_NEIGHBOUR_SOLICITATION:
		if ((ifp = ipv6_get_ifaddr(&msg->target, dev)) != NULL) {
			int addr_type = ipv6_addr_type(saddr);

			if (ifp->flags & IFA_F_TENTATIVE) {
				/* Address is tentative. If the source
				   is unspecified address, it is someone
				   does DAD, otherwise we ignore solicitations
				   until DAD timer expires.
				 */
				if (addr_type == IPV6_ADDR_ANY) {
					if (dev->type == ARPHRD_IEEE802_TR) { 
						unsigned char *sadr = skb->mac.raw ;
						if (((sadr[8] &0x7f) != (dev->dev_addr[0] & 0x7f)) ||
						(sadr[9] != dev->dev_addr[1]) ||
						(sadr[10] != dev->dev_addr[2]) ||
						(sadr[11] != dev->dev_addr[3]) ||
						(sadr[12] != dev->dev_addr[4]) ||
						(sadr[13] != dev->dev_addr[5])) 
						{
							addrconf_dad_failure(ifp) ; 
						}
					} else {
						addrconf_dad_failure(ifp);
					}
				} else
					in6_ifa_put(ifp);
				return 0;
			}

			if (addr_type == IPV6_ADDR_ANY) {
				struct in6_addr maddr;

				ipv6_addr_all_nodes(&maddr);
				ndisc_send_na(dev, NULL, &maddr, &ifp->addr, 
					      ifp->idev->cnf.forwarding, 0, 1, 1);
				in6_ifa_put(ifp);
				return 0;
			}

			if (addr_type & IPV6_ADDR_UNICAST) {
				int inc = ipv6_addr_type(daddr)&IPV6_ADDR_MULTICAST;

				if (inc)
					nd_tbl.stats.rcv_probes_mcast++;
				else
					nd_tbl.stats.rcv_probes_ucast++;

				/* 
				 *	update / create cache entry
				 *	for the source adddress
				 */

				neigh = ndisc_recv_ns(saddr, skb);

				if (neigh) {
					ndisc_send_na(dev, neigh, saddr, &ifp->addr, 
						      ifp->idev->cnf.forwarding, 1, inc, inc);
					neigh_release(neigh);
				}
			}
			in6_ifa_put(ifp);
		} else {
			struct inet6_dev *in6_dev = in6_dev_get(dev);
			int addr_type = ipv6_addr_type(saddr);

			if (in6_dev && in6_dev->cnf.forwarding &&
			    (addr_type & IPV6_ADDR_UNICAST) &&
			    pneigh_lookup(&nd_tbl, &msg->target, dev, 0)) {
				int inc = ipv6_addr_type(daddr)&IPV6_ADDR_MULTICAST;

				if (skb->stamp.tv_sec == 0 ||
				    skb->pkt_type == PACKET_HOST ||
				    inc == 0 ||
				    in6_dev->nd_parms->proxy_delay == 0) {
					if (inc)
						nd_tbl.stats.rcv_probes_mcast++;
					else
						nd_tbl.stats.rcv_probes_ucast++;

					neigh = ndisc_recv_ns(saddr, skb);

					if (neigh) {
						ndisc_send_na(dev, neigh, saddr, &msg->target,
							      0, 1, 0, inc);
						neigh_release(neigh);
					}
				} else {
					struct sk_buff *n = skb_clone(skb, GFP_ATOMIC);
					if (n)
						pneigh_enqueue(&nd_tbl, in6_dev->nd_parms, n);
					in6_dev_put(in6_dev);
					return 0;
				}
			}
			if (in6_dev)
				in6_dev_put(in6_dev);
			
		}
		return 0;

	case NDISC_NEIGHBOUR_ADVERTISEMENT:
		if ((ipv6_addr_type(saddr)&IPV6_ADDR_MULTICAST) &&
		    msg->icmph.icmp6_solicited) {
			ND_PRINTK0("NDISC: solicited NA is multicasted\n");
			return 0;
		}
		if ((ifp = ipv6_get_ifaddr(&msg->target, dev))) {
			if (ifp->flags & IFA_F_TENTATIVE) {
				addrconf_dad_failure(ifp);
				return 0;
			}
			/* What should we make now? The advertisement
			   is invalid, but ndisc specs say nothing
			   about it. It could be misconfiguration, or
			   an smart proxy agent tries to help us :-)
			 */
			ND_PRINTK0("%s: someone avertise our address!\n",
				   ifp->idev->dev->name);
			in6_ifa_put(ifp);
			return 0;
		}
		neigh = neigh_lookup(&nd_tbl, &msg->target, skb->dev);

		if (neigh) {
			if (neigh->flags & NTF_ROUTER) {
				if (msg->icmph.icmp6_router == 0) {
					/*
					 *	Change: router to host
					 */
					struct rt6_info *rt;
					rt = rt6_get_dflt_router(saddr, skb->dev);
					if (rt) {
						/* It is safe only because
						   we aer in BH */
						dst_release(&rt->u.dst);
						ip6_del_rt(rt);
					}
				}
			} else {
				if (msg->icmph.icmp6_router)
					neigh->flags |= NTF_ROUTER;
			}

			ndisc_recv_na(neigh, skb);
			neigh_release(neigh);
		}
		break;

	case NDISC_ROUTER_ADVERTISEMENT:
		ndisc_router_discovery(skb);
		break;

	case NDISC_REDIRECT:
		ndisc_redirect_rcv(skb);
		break;
	};

	return 0;
}

#ifdef CONFIG_PROC_FS
#ifndef CONFIG_RTNETLINK
static int ndisc_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len=0;
	off_t pos=0;
	int size;
	unsigned long now = jiffies;
	int i;

	for (i = 0; i <= NEIGH_HASHMASK; i++) {
		struct neighbour *neigh;

		read_lock_bh(&nd_tbl.lock);
		for (neigh = nd_tbl.hash_buckets[i]; neigh; neigh = neigh->next) {
			int j;

			size = 0;
			for (j=0; j<16; j++) {
				sprintf(buffer+len+size, "%02x", neigh->primary_key[j]);
				size += 2;
			}

			read_lock(&neigh->lock);
			size += sprintf(buffer+len+size,
				       " %02x %02x %02x %02x %08lx %08lx %08x %04x %04x %04x %8s ", i,
				       128,
				       neigh->type,
				       neigh->nud_state,
				       now - neigh->used,
				       now - neigh->confirmed,
				       neigh->parms->reachable_time,
				       neigh->parms->gc_staletime,
				       atomic_read(&neigh->refcnt) - 1,
				       neigh->flags | (!neigh->hh ? 0 : (neigh->hh->hh_output==dev_queue_xmit ? 4 : 2)),
				       neigh->dev->name);

			if ((neigh->nud_state&NUD_VALID) && neigh->dev->addr_len) {
				for (j=0; j < neigh->dev->addr_len; j++) {
					sprintf(buffer+len+size, "%02x", neigh->ha[j]);
					size += 2;
				}
			} else {
                                size += sprintf(buffer+len+size, "000000000000");
			}
			read_unlock(&neigh->lock);
			size += sprintf(buffer+len+size, "\n");
			len += size;
			pos += size;
		  
			if (pos <= offset)
				len=0;
			if (pos >= offset+length) {
				read_unlock_bh(&nd_tbl.lock);
				goto done;
			}
		}
		read_unlock_bh(&nd_tbl.lock);
	}

done:

	*start = buffer+len-(pos-offset);	/* Start of wanted data */
	len = pos-offset;			/* Start slop */
	if (len>length)
		len = length;			/* Ending slop */
	if (len<0)
		len = 0;
	return len;
}

#endif
#endif	/* CONFIG_PROC_FS */


int __init ndisc_init(struct net_proto_family *ops)
{
	struct sock *sk;
        int err;

	ndisc_socket = sock_alloc();
	if (ndisc_socket == NULL) {
		printk(KERN_ERR
		       "Failed to create the NDISC control socket.\n");
		return -1;
	}
	ndisc_socket->inode->i_uid = 0;
	ndisc_socket->inode->i_gid = 0;
	ndisc_socket->type = SOCK_RAW;

	if((err = ops->create(ndisc_socket, IPPROTO_ICMPV6)) < 0) {
		printk(KERN_DEBUG 
		       "Failed to initializee the NDISC control socket (err %d).\n",
		       err);
		sock_release(ndisc_socket);
		ndisc_socket = NULL; /* For safety. */
		return err;
	}

	sk = ndisc_socket->sk;
	sk->allocation = GFP_ATOMIC;
	sk->net_pinfo.af_inet6.hop_limit = 255;
	/* Do not loopback ndisc messages */
	sk->net_pinfo.af_inet6.mc_loop = 0;
	sk->prot->unhash(sk);

        /*
         * Initialize the neighbour table
         */
	
	neigh_table_init(&nd_tbl);

#ifdef CONFIG_PROC_FS
#ifndef CONFIG_RTNETLINK
	proc_net_create("ndisc", 0, ndisc_get_info);
#endif
#endif
#ifdef CONFIG_SYSCTL
	neigh_sysctl_register(NULL, &nd_tbl.parms, NET_IPV6, NET_IPV6_NEIGH, "ipv6");
#endif

	return 0;
}

void ndisc_cleanup(void)
{
#ifdef CONFIG_PROC_FS
#ifndef CONFIG_RTNETLINK
        proc_net_remove("ndisc");
#endif
#endif
	neigh_table_clear(&nd_tbl);
	sock_release(ndisc_socket);
	ndisc_socket = NULL; /* For safety. */
}
