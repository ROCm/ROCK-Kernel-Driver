/*
 *	IPv6 over IPv6 tunnel device
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Ville Nuorvala		<vnuorval@tcs.hut.fi>	
 *
 *	$Id$
 *
 *      Based on:
 *      linux/net/ipv6/sit.c
 *
 *      RFC 2473
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/if_tunnel.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/icmpv6.h>
#include <linux/init.h>
#include <linux/route.h>
#include <linux/rtnetlink.h>

#include <asm/uaccess.h>
#include <asm/atomic.h>

#include <net/ip.h>
#include <net/sock.h>
#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/ip6_tunnel.h>

MODULE_AUTHOR("Ville Nuorvala");
MODULE_DESCRIPTION("IPv6-in-IPv6 tunnel");
MODULE_LICENSE("GPL");

#define IPV6_TLV_TEL_DST_SIZE 8

#ifdef IP6_TNL_DEBUG
#define IP6_TNL_TRACE(x...) printk(KERN_DEBUG "%s:" x "\n", __FUNCTION__)
#else
#define IP6_TNL_TRACE(x...) do {;} while(0)
#endif

#define IPV6_TCLASS_MASK (IPV6_FLOWINFO_MASK & ~IPV6_FLOWLABEL_MASK)

/* socket(s) used by ip6ip6_tnl_xmit() for resending packets */
static struct socket *__ip6_socket[NR_CPUS];
#define ip6_socket __ip6_socket[smp_processor_id()]

static void ip6_xmit_lock(void)
{
	local_bh_disable();
	if (unlikely(!spin_trylock(&ip6_socket->sk->sk_lock.slock)))
		BUG();
}

static void ip6_xmit_unlock(void)
{
	spin_unlock_bh(&ip6_socket->sk->sk_lock.slock);
}

#define HASH_SIZE  32

#define HASH(addr) (((addr)->s6_addr32[0] ^ (addr)->s6_addr32[1] ^ \
	             (addr)->s6_addr32[2] ^ (addr)->s6_addr32[3]) & \
                    (HASH_SIZE - 1))

static int ip6ip6_fb_tnl_dev_init(struct net_device *dev);
static int ip6ip6_tnl_dev_init(struct net_device *dev);

/* the IPv6 tunnel fallback device */
static struct net_device ip6ip6_fb_tnl_dev = {
	.name = "ip6tnl0",
	.init = ip6ip6_fb_tnl_dev_init
};

/* the IPv6 fallback tunnel */
static struct ip6_tnl ip6ip6_fb_tnl = {
	.dev = &ip6ip6_fb_tnl_dev,
	.parms ={.name = "ip6tnl0", .proto = IPPROTO_IPV6}
};

/* lists for storing tunnels in use */
static struct ip6_tnl *tnls_r_l[HASH_SIZE];
static struct ip6_tnl *tnls_wc[1];
static struct ip6_tnl **tnls[2] = { tnls_wc, tnls_r_l };

/* lock for the tunnel lists */
static rwlock_t ip6ip6_lock = RW_LOCK_UNLOCKED;

/**
 * ip6ip6_tnl_lookup - fetch tunnel matching the end-point addresses
 *   @remote: the address of the tunnel exit-point 
 *   @local: the address of the tunnel entry-point 
 *
 * Return:  
 *   tunnel matching given end-points if found,
 *   else fallback tunnel if its device is up, 
 *   else %NULL
 **/

struct ip6_tnl *
ip6ip6_tnl_lookup(struct in6_addr *remote, struct in6_addr *local)
{
	unsigned h0 = HASH(remote);
	unsigned h1 = HASH(local);
	struct ip6_tnl *t;

	for (t = tnls_r_l[h0 ^ h1]; t; t = t->next) {
		if (!ipv6_addr_cmp(local, &t->parms.laddr) &&
		    !ipv6_addr_cmp(remote, &t->parms.raddr) &&
		    (t->dev->flags & IFF_UP))
			return t;
	}
	if ((t = tnls_wc[0]) != NULL && (t->dev->flags & IFF_UP))
		return t;

	return NULL;
}

/**
 * ip6ip6_bucket - get head of list matching given tunnel parameters
 *   @p: parameters containing tunnel end-points 
 *
 * Description:
 *   ip6ip6_bucket() returns the head of the list matching the 
 *   &struct in6_addr entries laddr and raddr in @p.
 *
 * Return: head of IPv6 tunnel list 
 **/

static struct ip6_tnl **
ip6ip6_bucket(struct ip6_tnl_parm *p)
{
	struct in6_addr *remote = &p->raddr;
	struct in6_addr *local = &p->laddr;
	unsigned h = 0;
	int prio = 0;

	if (!ipv6_addr_any(remote) || !ipv6_addr_any(local)) {
		prio = 1;
		h = HASH(remote) ^ HASH(local);
	}
	return &tnls[prio][h];
}

/**
 * ip6ip6_tnl_link - add tunnel to hash table
 *   @t: tunnel to be added
 **/

static void
ip6ip6_tnl_link(struct ip6_tnl *t)
{
	struct ip6_tnl **tp = ip6ip6_bucket(&t->parms);

	write_lock_bh(&ip6ip6_lock);
	t->next = *tp;
	write_unlock_bh(&ip6ip6_lock);
	*tp = t;
}

/**
 * ip6ip6_tnl_unlink - remove tunnel from hash table
 *   @t: tunnel to be removed
 **/

static void
ip6ip6_tnl_unlink(struct ip6_tnl *t)
{
	struct ip6_tnl **tp;

	for (tp = ip6ip6_bucket(&t->parms); *tp; tp = &(*tp)->next) {
		if (t == *tp) {
			write_lock_bh(&ip6ip6_lock);
			*tp = t->next;
			write_unlock_bh(&ip6ip6_lock);
			break;
		}
	}
}

/**
 * ip6_tnl_create() - create a new tunnel
 *   @p: tunnel parameters
 *   @pt: pointer to new tunnel
 *
 * Description:
 *   Create tunnel matching given parameters.
 * 
 * Return: 
 *   0 on success
 **/

static int
ip6_tnl_create(struct ip6_tnl_parm *p, struct ip6_tnl **pt)
{
	struct net_device *dev;
	int err = -ENOBUFS;
	struct ip6_tnl *t;

	dev = kmalloc(sizeof (*dev) + sizeof (*t), GFP_KERNEL);
	if (!dev)
		return err;

	memset(dev, 0, sizeof (*dev) + sizeof (*t));
	dev->priv = (void *) (dev + 1);
	t = (struct ip6_tnl *) dev->priv;
	t->dev = dev;
	dev->init = ip6ip6_tnl_dev_init;
	memcpy(&t->parms, p, sizeof (*p));
	t->parms.name[IFNAMSIZ - 1] = '\0';
	if (t->parms.hop_limit > 255)
		t->parms.hop_limit = -1;
	strcpy(dev->name, t->parms.name);
	if (!dev->name[0]) {
		int i = 0;
		int exists = 0;

		do {
			sprintf(dev->name, "ip6tnl%d", ++i);
			exists = (__dev_get_by_name(dev->name) != NULL);
		} while (i < IP6_TNL_MAX && exists);

		if (i == IP6_TNL_MAX) {
			goto failed;
		}
		memcpy(t->parms.name, dev->name, IFNAMSIZ);
	}
	SET_MODULE_OWNER(dev);
	if ((err = register_netdevice(dev)) < 0) {
		goto failed;
	}
	ip6ip6_tnl_link(t);
	*pt = t;
	return 0;
failed:
	kfree(dev);
	return err;
}

/**
 * ip6_tnl_destroy() - destroy old tunnel
 *   @t: tunnel to be destroyed
 *
 * Return:
 *   whatever unregister_netdevice() returns
 **/

static inline int
ip6_tnl_destroy(struct ip6_tnl *t)
{
	return unregister_netdevice(t->dev);
}

/**
 * ip6ip6_tnl_locate - find or create tunnel matching given parameters
 *   @p: tunnel parameters 
 *   @create: != 0 if allowed to create new tunnel if no match found
 *
 * Description:
 *   ip6ip6_tnl_locate() first tries to locate an existing tunnel
 *   based on @parms. If this is unsuccessful, but @create is set a new
 *   tunnel device is created and registered for use.
 *
 * Return:
 *   0 if tunnel located or created,
 *   -EINVAL if parameters incorrect,
 *   -ENODEV if no matching tunnel available
 **/

static int
ip6ip6_tnl_locate(struct ip6_tnl_parm *p, struct ip6_tnl **pt, int create)
{
	struct in6_addr *remote = &p->raddr;
	struct in6_addr *local = &p->laddr;
	struct ip6_tnl *t;

	if (p->proto != IPPROTO_IPV6)
		return -EINVAL;

	for (t = *ip6ip6_bucket(p); t; t = t->next) {
		if (!ipv6_addr_cmp(local, &t->parms.laddr) &&
		    !ipv6_addr_cmp(remote, &t->parms.raddr)) {
			*pt = t;
			return (create ? -EEXIST : 0);
		}
	}
	if (!create) {
		return -ENODEV;
	}
	return ip6_tnl_create(p, pt);
}

/**
 * ip6ip6_tnl_dev_destructor - tunnel device destructor
 *   @dev: the device to be destroyed
 **/

static void
ip6ip6_tnl_dev_destructor(struct net_device *dev)
{
	kfree(dev);
}

/**
 * ip6ip6_tnl_dev_uninit - tunnel device uninitializer
 *   @dev: the device to be destroyed
 *   
 * Description:
 *   ip6ip6_tnl_dev_uninit() removes tunnel from its list
 **/

static void
ip6ip6_tnl_dev_uninit(struct net_device *dev)
{
	if (dev == &ip6ip6_fb_tnl_dev) {
		write_lock_bh(&ip6ip6_lock);
		tnls_wc[0] = NULL;
		write_unlock_bh(&ip6ip6_lock);
	} else {
		struct ip6_tnl *t = (struct ip6_tnl *) dev->priv;
		ip6ip6_tnl_unlink(t);
	}
}

/**
 * parse_tvl_tnl_enc_lim - handle encapsulation limit option
 *   @skb: received socket buffer
 *
 * Return: 
 *   0 if none was found, 
 *   else index to encapsulation limit
 **/

static __u16
parse_tlv_tnl_enc_lim(struct sk_buff *skb, __u8 * raw)
{
	struct ipv6hdr *ipv6h = (struct ipv6hdr *) raw;
	__u8 nexthdr = ipv6h->nexthdr;
	__u16 off = sizeof (*ipv6h);

	while (ipv6_ext_hdr(nexthdr) && nexthdr != NEXTHDR_NONE) {
		__u16 optlen = 0;
		struct ipv6_opt_hdr *hdr;
		if (raw + off + sizeof (*hdr) > skb->data &&
		    !pskb_may_pull(skb, raw - skb->data + off + sizeof (*hdr)))
			break;

		hdr = (struct ipv6_opt_hdr *) (raw + off);
		if (nexthdr == NEXTHDR_FRAGMENT) {
			struct frag_hdr *frag_hdr = (struct frag_hdr *) hdr;
			if (frag_hdr->frag_off)
				break;
			optlen = 8;
		} else if (nexthdr == NEXTHDR_AUTH) {
			optlen = (hdr->hdrlen + 2) << 2;
		} else {
			optlen = ipv6_optlen(hdr);
		}
		if (nexthdr == NEXTHDR_DEST) {
			__u16 i = off + 2;
			while (1) {
				struct ipv6_tlv_tnl_enc_lim *tel;

				/* No more room for encapsulation limit */
				if (i + sizeof (*tel) > off + optlen)
					break;

				tel = (struct ipv6_tlv_tnl_enc_lim *) &raw[i];
				/* return index of option if found and valid */
				if (tel->type == IPV6_TLV_TNL_ENCAP_LIMIT &&
				    tel->length == 1)
					return i;
				/* else jump to next option */
				if (tel->type)
					i += tel->length + 2;
				else
					i++;
			}
		}
		nexthdr = hdr->nexthdr;
		off += optlen;
	}
	return 0;
}

/**
 * ip6ip6_err - tunnel error handler
 *
 * Description:
 *   ip6ip6_err() should handle errors in the tunnel according
 *   to the specifications in RFC 2473.
 **/

void ip6ip6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		   int type, int code, int offset, __u32 info)
{
	struct ipv6hdr *ipv6h = (struct ipv6hdr *) skb->data;
	struct ip6_tnl *t;
	int rel_msg = 0;
	int rel_type = ICMPV6_DEST_UNREACH;
	int rel_code = ICMPV6_ADDR_UNREACH;
	__u32 rel_info = 0;
	__u16 len;

	/* If the packet doesn't contain the original IPv6 header we are 
	   in trouble since we might need the source address for furter 
	   processing of the error. */

	read_lock(&ip6ip6_lock);
	if ((t = ip6ip6_tnl_lookup(&ipv6h->daddr, &ipv6h->saddr)) == NULL)
		goto out;

	switch (type) {
		__u32 teli;
		struct ipv6_tlv_tnl_enc_lim *tel;
		__u32 mtu;
	case ICMPV6_DEST_UNREACH:
		if (net_ratelimit())
			printk(KERN_WARNING
			       "%s: Path to destination invalid "
			       "or inactive!\n", t->parms.name);
		rel_msg = 1;
		break;
	case ICMPV6_TIME_EXCEED:
		if (code == ICMPV6_EXC_HOPLIMIT) {
			if (net_ratelimit())
				printk(KERN_WARNING
				       "%s: Too small hop limit or "
				       "routing loop in tunnel!\n", 
				       t->parms.name);
			rel_msg = 1;
		}
		break;
	case ICMPV6_PARAMPROB:
		/* ignore if parameter problem not caused by a tunnel
		   encapsulation limit sub-option */
		if (code != ICMPV6_HDR_FIELD) {
			break;
		}
		teli = parse_tlv_tnl_enc_lim(skb, skb->data);

		if (teli && teli == info - 2) {
			tel = (struct ipv6_tlv_tnl_enc_lim *) &skb->data[teli];
			if (tel->encap_limit <= 1) {
				if (net_ratelimit())
					printk(KERN_WARNING
					       "%s: Too small encapsulation "
					       "limit or routing loop in "
					       "tunnel!\n", t->parms.name);
				rel_msg = 1;
			}
		}
		break;
	case ICMPV6_PKT_TOOBIG:
		mtu = info - offset;
		if (mtu <= IPV6_MIN_MTU) {
			mtu = IPV6_MIN_MTU;
		}
		t->dev->mtu = mtu;

		if ((len = sizeof (*ipv6h) + ipv6h->payload_len) > mtu) {
			rel_type = ICMPV6_PKT_TOOBIG;
			rel_code = 0;
			rel_info = mtu;
			rel_msg = 1;
		}
		break;
	}
	if (rel_msg &&  pskb_may_pull(skb, offset + sizeof (*ipv6h))) {
		struct rt6_info *rt;
		struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
		if (!skb2)
			goto out;

		dst_release(skb2->dst);
		skb2->dst = NULL;
		skb_pull(skb2, offset);
		skb2->nh.raw = skb2->data;

		/* Try to guess incoming interface */
		rt = rt6_lookup(&skb2->nh.ipv6h->saddr, NULL, 0, 0);

		if (rt && rt->rt6i_dev)
			skb2->dev = rt->rt6i_dev;

		icmpv6_send(skb2, rel_type, rel_code, rel_info, skb2->dev);

		if (rt)
			dst_free(&rt->u.dst);

		kfree_skb(skb2);
	}
out:
	read_unlock(&ip6ip6_lock);
}

/**
 * ip6ip6_rcv - decapsulate IPv6 packet and retransmit it locally
 *   @skb: received socket buffer
 *
 * Return: 0
 **/

int ip6ip6_rcv(struct sk_buff **pskb, unsigned int *nhoffp)
{
	struct sk_buff *skb = *pskb;
	struct ipv6hdr *ipv6h;
	struct ip6_tnl *t;

	if (!pskb_may_pull(skb, sizeof (*ipv6h)))
		goto discard;

	ipv6h = skb->nh.ipv6h;

	read_lock(&ip6ip6_lock);

	if ((t = ip6ip6_tnl_lookup(&ipv6h->saddr, &ipv6h->daddr)) != NULL) {
		if (!(t->parms.flags & IP6_TNL_F_CAP_RCV)) {
			t->stat.rx_dropped++;
			read_unlock(&ip6ip6_lock);
			goto discard;
		}
		skb->mac.raw = skb->nh.raw;
		skb->nh.raw = skb->data;
		skb->protocol = htons(ETH_P_IPV6);
		skb->pkt_type = PACKET_HOST;
		memset(skb->cb, 0, sizeof(struct inet6_skb_parm));
		skb->dev = t->dev;
		dst_release(skb->dst);
		skb->dst = NULL;
		t->stat.rx_packets++;
		t->stat.rx_bytes += skb->len;
		netif_rx(skb);
		read_unlock(&ip6ip6_lock);
		return 0;
	}
	read_unlock(&ip6ip6_lock);
	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_ADDR_UNREACH, 0, skb->dev);
discard:
	kfree_skb(skb);
	return 0;
}

/**
 * txopt_len - get necessary size for new &struct ipv6_txoptions
 *   @orig_opt: old options
 *
 * Return:
 *   Size of old one plus size of tunnel encapsulation limit option
 **/

static inline int
txopt_len(struct ipv6_txoptions *orig_opt)
{
	int len = sizeof (*orig_opt) + 8;

	if (orig_opt && orig_opt->dst0opt)
		len += ipv6_optlen(orig_opt->dst0opt);
	return len;
}

/**
 * merge_options - add encapsulation limit to original options
 *   @encap_limit: number of allowed encapsulation limits
 *   @orig_opt: original options
 * 
 * Return:
 *   Pointer to new &struct ipv6_txoptions containing the tunnel
 *   encapsulation limit
 **/

static struct ipv6_txoptions *
merge_options(struct sock *sk, __u8 encap_limit,
	      struct ipv6_txoptions *orig_opt)
{
	struct ipv6_tlv_tnl_enc_lim *tel;
	struct ipv6_txoptions *opt;
	__u8 *raw;
	__u8 pad_to = 8;
	int opt_len = txopt_len(orig_opt);

	if (!(opt = sock_kmalloc(sk, opt_len, GFP_ATOMIC))) {
		return NULL;
	}

	memset(opt, 0, opt_len);
	opt->tot_len = opt_len;
	opt->dst0opt = (struct ipv6_opt_hdr *) (opt + 1);
	opt->opt_nflen = 8;

	raw = (__u8 *) opt->dst0opt;

	tel = (struct ipv6_tlv_tnl_enc_lim *) (opt->dst0opt + 1);
	tel->type = IPV6_TLV_TNL_ENCAP_LIMIT;
	tel->length = 1;
	tel->encap_limit = encap_limit;

	if (orig_opt) {
		__u8 *orig_raw;

		opt->hopopt = orig_opt->hopopt;

		/* Keep the original destination options properly
		   aligned and merge possible old paddings to the
		   new padding option */
		if ((orig_raw = (__u8 *) orig_opt->dst0opt) != NULL) {
			__u8 type;
			int i = sizeof (struct ipv6_opt_hdr);
			pad_to += sizeof (struct ipv6_opt_hdr);
			while (i < ipv6_optlen(orig_opt->dst0opt)) {
				type = orig_raw[i++];
				if (type == IPV6_TLV_PAD0)
					pad_to++;
				else if (type == IPV6_TLV_PADN) {
					int len = orig_raw[i++];
					i += len;
					pad_to += len + 2;
				} else {
					break;
				}
			}
			opt->dst0opt->hdrlen = orig_opt->dst0opt->hdrlen + 1;
			memcpy(raw + pad_to, orig_raw + pad_to - 8,
			       opt_len - sizeof (*opt) - pad_to);
		}
		opt->srcrt = orig_opt->srcrt;
		opt->opt_nflen += orig_opt->opt_nflen;

		opt->dst1opt = orig_opt->dst1opt;
		opt->auth = orig_opt->auth;
		opt->opt_flen = orig_opt->opt_flen;
	}
	raw[5] = IPV6_TLV_PADN;

	/* subtract lengths of destination suboption header,
	   tunnel encapsulation limit and pad N header */
	raw[6] = pad_to - 7;

	return opt;
}

/**
 * ip6ip6_tnl_addr_conflict - compare packet addresses to tunnel's own
 *   @t: the outgoing tunnel device
 *   @hdr: IPv6 header from the incoming packet 
 *
 * Description:
 *   Avoid trivial tunneling loop by checking that tunnel exit-point 
 *   doesn't match source of incoming packet.
 *
 * Return: 
 *   1 if conflict,
 *   0 else
 **/

static inline int
ip6ip6_tnl_addr_conflict(struct ip6_tnl *t, struct ipv6hdr *hdr)
{
	return !ipv6_addr_cmp(&t->parms.raddr, &hdr->saddr);
}

/**
 * ip6ip6_tnl_xmit - encapsulate packet and send 
 *   @skb: the outgoing socket buffer
 *   @dev: the outgoing tunnel device 
 *
 * Description:
 *   Build new header and do some sanity checks on the packet before sending
 *   it to ip6_build_xmit().
 *
 * Return: 
 *   0
 **/

int ip6ip6_tnl_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ip6_tnl *t = (struct ip6_tnl *) dev->priv;
	struct net_device_stats *stats = &t->stat;
	struct ipv6hdr *ipv6h = skb->nh.ipv6h;
	struct ipv6_txoptions *orig_opt = NULL;
	struct ipv6_txoptions *opt = NULL;
	__u8 encap_limit = 0;
	__u16 offset;
	struct flowi fl;
	struct ip6_flowlabel *fl_lbl = NULL;
	int err = 0;
	struct dst_entry *dst;
	int link_failure = 0;
	struct sock *sk = ip6_socket->sk;
	struct ipv6_pinfo *np = inet6_sk(sk);
	int mtu;

	if (t->recursion++) {
		stats->collisions++;
		goto tx_err;
	}
	if (skb->protocol != htons(ETH_P_IPV6) ||
	    !(t->parms.flags & IP6_TNL_F_CAP_XMIT) ||
	    ip6ip6_tnl_addr_conflict(t, ipv6h)) {
		goto tx_err;
	}
	if ((offset = parse_tlv_tnl_enc_lim(skb, skb->nh.raw)) > 0) {
		struct ipv6_tlv_tnl_enc_lim *tel;
		tel = (struct ipv6_tlv_tnl_enc_lim *) &skb->nh.raw[offset];
		if (tel->encap_limit <= 1) {
			icmpv6_send(skb, ICMPV6_PARAMPROB,
				    ICMPV6_HDR_FIELD, offset + 2, skb->dev);
			goto tx_err;
		}
		encap_limit = tel->encap_limit - 1;
	} else if (!(t->parms.flags & IP6_TNL_F_IGN_ENCAP_LIMIT)) {
		encap_limit = t->parms.encap_limit;
	}
	ip6_xmit_lock();

	memcpy(&fl, &t->fl, sizeof (fl));

	if ((t->parms.flags & IP6_TNL_F_USE_ORIG_TCLASS))
		fl.fl6_flowlabel |= (*(__u32 *) ipv6h & IPV6_TCLASS_MASK);
	if ((t->parms.flags & IP6_TNL_F_USE_ORIG_FLOWLABEL))
		fl.fl6_flowlabel |= (*(__u32 *) ipv6h & IPV6_FLOWLABEL_MASK);

	if (fl.fl6_flowlabel) {
		fl_lbl = fl6_sock_lookup(sk, fl.fl6_flowlabel);
		if (fl_lbl)
			orig_opt = fl_lbl->opt;
	}
	if (encap_limit > 0) {
		if (!(opt = merge_options(sk, encap_limit, orig_opt))) {
			goto tx_err_free_fl_lbl;
		}
	} else {
		opt = orig_opt;
	}
	dst = __sk_dst_check(sk, np->dst_cookie);

	if (dst) {
		if (np->daddr_cache == NULL ||
		    ipv6_addr_cmp(&fl.fl6_dst, np->daddr_cache) ||
		    (fl.oif && fl.oif != dst->dev->ifindex)) {
			dst = NULL;
		}
	}
	if (dst == NULL) {
		dst = ip6_route_output(sk, &fl);
		if (dst->error) {
			stats->tx_carrier_errors++;
			link_failure = 1;
			goto tx_err_dst_release;
		}
		/* local routing loop */
		if (dst->dev == dev) {
			stats->collisions++;
			if (net_ratelimit())
				printk(KERN_WARNING 
				       "%s: Local routing loop detected!\n",
				       t->parms.name);
			goto tx_err_dst_release;
		}
		ipv6_addr_copy(&np->daddr, &fl.fl6_dst);
		ipv6_addr_copy(&np->saddr, &fl.fl6_src);
	}
	mtu = dst_pmtu(dst) - sizeof (*ipv6h);
	if (opt) {
		mtu -= (opt->opt_nflen + opt->opt_flen);
	}
	if (mtu < IPV6_MIN_MTU)
		mtu = IPV6_MIN_MTU;
	if (skb->dst && mtu < dst_pmtu(skb->dst)) {
		struct rt6_info *rt = (struct rt6_info *) skb->dst;
		rt->rt6i_flags |= RTF_MODIFIED;
		rt->u.dst.metrics[RTAX_MTU-1] = mtu;
	}
	if (skb->len > mtu) {
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu, dev);
		goto tx_err_opt_release;
	}
	err = ip6_append_data(sk, ip_generic_getfrag, skb->nh.raw, skb->len, 0,
			      t->parms.hop_limit, opt, &fl, 
			      (struct rt6_info *)dst, MSG_DONTWAIT);

	if (err) {
		ip6_flush_pending_frames(sk);
	} else {
		err = ip6_push_pending_frames(sk);
		err = (err < 0 ? err : 0);
	}
	if (!err) {
		stats->tx_bytes += skb->len;
		stats->tx_packets++;
	} else {
		stats->tx_errors++;
		stats->tx_aborted_errors++;
	}
	if (opt && opt != orig_opt)
		sock_kfree_s(sk, opt, opt->tot_len);

	fl6_sock_release(fl_lbl);
	ip6_dst_store(sk, dst, &np->daddr);
	ip6_xmit_unlock();
	kfree_skb(skb);
	t->recursion--;
	return 0;
tx_err_dst_release:
	dst_release(dst);
tx_err_opt_release:
	if (opt && opt != orig_opt)
		sock_kfree_s(sk, opt, opt->tot_len);
tx_err_free_fl_lbl:
	fl6_sock_release(fl_lbl);
	ip6_xmit_unlock();
	if (link_failure)
		dst_link_failure(skb);
tx_err:
	stats->tx_errors++;
	stats->tx_dropped++;
	kfree_skb(skb);
	t->recursion--;
	return 0;
}

static void ip6_tnl_set_cap(struct ip6_tnl *t)
{
	struct ip6_tnl_parm *p = &t->parms;
	struct in6_addr *laddr = &p->laddr;
	struct in6_addr *raddr = &p->raddr;
	int ltype = ipv6_addr_type(laddr);
	int rtype = ipv6_addr_type(raddr);

	p->flags &= ~(IP6_TNL_F_CAP_XMIT|IP6_TNL_F_CAP_RCV);

	if (ltype != IPV6_ADDR_ANY && rtype != IPV6_ADDR_ANY &&
	    ((ltype|rtype) &
	     (IPV6_ADDR_UNICAST|
	      IPV6_ADDR_LOOPBACK|IPV6_ADDR_LINKLOCAL|
	      IPV6_ADDR_MAPPED|IPV6_ADDR_RESERVED)) == IPV6_ADDR_UNICAST) {
		struct net_device *ldev = NULL;
		int l_ok = 1;
		int r_ok = 1;

		if (p->link)
			ldev = dev_get_by_index(p->link);
		
		if ((ltype&IPV6_ADDR_UNICAST) && !ipv6_chk_addr(laddr, ldev))
			l_ok = 0;
		
		if ((rtype&IPV6_ADDR_UNICAST) && ipv6_chk_addr(raddr, NULL))
			r_ok = 0;
		
		if (l_ok && r_ok) {
			if (ltype&IPV6_ADDR_UNICAST)
				p->flags |= IP6_TNL_F_CAP_XMIT;
			if (rtype&IPV6_ADDR_UNICAST)
				p->flags |= IP6_TNL_F_CAP_RCV;
		}
		if (ldev)
			dev_put(ldev);
	}
}


static void ip6ip6_tnl_link_config(struct ip6_tnl *t)
{
	struct net_device *dev = t->dev;
	struct ip6_tnl_parm *p = &t->parms;
	struct flowi *fl;
	/* Set up flowi template */
	fl = &t->fl;
	ipv6_addr_copy(&fl->fl6_src, &p->laddr);
	ipv6_addr_copy(&fl->fl6_dst, &p->raddr);
	fl->oif = p->link;
	fl->fl6_flowlabel = 0;

	if (!(p->flags&IP6_TNL_F_USE_ORIG_TCLASS))
		fl->fl6_flowlabel |= IPV6_TCLASS_MASK & htonl(p->flowinfo);
	if (!(p->flags&IP6_TNL_F_USE_ORIG_FLOWLABEL))
		fl->fl6_flowlabel |= IPV6_FLOWLABEL_MASK & htonl(p->flowinfo);

	ip6_tnl_set_cap(t);

	if (p->flags&IP6_TNL_F_CAP_XMIT && p->flags&IP6_TNL_F_CAP_RCV)
		dev->flags |= IFF_POINTOPOINT;
	else
		dev->flags &= ~IFF_POINTOPOINT;

	if (p->flags & IP6_TNL_F_CAP_XMIT) {
		struct rt6_info *rt = rt6_lookup(&p->raddr, &p->laddr,
						 p->link, 0);
		if (rt) {
			struct net_device *rtdev;
			if (!(rtdev = rt->rt6i_dev) ||
			    rtdev->type == ARPHRD_TUNNEL6) {
				/* as long as tunnels use the same socket 
				   for transmission, locally nested tunnels 
				   won't work */
				dst_release(&rt->u.dst);
				goto no_link;
			} else {
				dev->iflink = rtdev->ifindex;
				dev->hard_header_len = rtdev->hard_header_len +
					sizeof (struct ipv6hdr);
				dev->mtu = rtdev->mtu - sizeof (struct ipv6hdr);
				if (dev->mtu < IPV6_MIN_MTU)
					dev->mtu = IPV6_MIN_MTU;
				
				dst_release(&rt->u.dst);
			}
		}
	} else {
	no_link:
		dev->iflink = 0;
		dev->hard_header_len = LL_MAX_HEADER + sizeof (struct ipv6hdr);
		dev->mtu = ETH_DATA_LEN - sizeof (struct ipv6hdr);
	}
}

/**
 * ip6ip6_tnl_change - update the tunnel parameters
 *   @t: tunnel to be changed
 *   @p: tunnel configuration parameters
 *   @active: != 0 if tunnel is ready for use
 *
 * Description:
 *   ip6ip6_tnl_change() updates the tunnel parameters
 **/

static int
ip6ip6_tnl_change(struct ip6_tnl *t, struct ip6_tnl_parm *p)
{
	ipv6_addr_copy(&t->parms.laddr, &p->laddr);
	ipv6_addr_copy(&t->parms.raddr, &p->raddr);
	t->parms.flags = p->flags;
	t->parms.hop_limit = (p->hop_limit <= 255 ? p->hop_limit : -1);
	t->parms.encap_limit = p->encap_limit;
	t->parms.flowinfo = p->flowinfo;
	ip6ip6_tnl_link_config(t);
	return 0;
}

/**
 * ip6ip6_tnl_ioctl - configure ipv6 tunnels from userspace 
 *   @dev: virtual device associated with tunnel
 *   @ifr: parameters passed from userspace
 *   @cmd: command to be performed
 *
 * Description:
 *   ip6ip6_tnl_ioctl() is used for managing IPv6 tunnels 
 *   from userspace. 
 *
 *   The possible commands are the following:
 *     %SIOCGETTUNNEL: get tunnel parameters for device
 *     %SIOCADDTUNNEL: add tunnel matching given tunnel parameters
 *     %SIOCCHGTUNNEL: change tunnel parameters to those given
 *     %SIOCDELTUNNEL: delete tunnel
 *
 *   The fallback device "ip6tnl0", created during module 
 *   initialization, can be used for creating other tunnel devices.
 *
 * Return:
 *   0 on success,
 *   %-EFAULT if unable to copy data to or from userspace,
 *   %-EPERM if current process hasn't %CAP_NET_ADMIN set
 *   %-EINVAL if passed tunnel parameters are invalid,
 *   %-EEXIST if changing a tunnel's parameters would cause a conflict
 *   %-ENODEV if attempting to change or delete a nonexisting device
 **/

static int
ip6ip6_tnl_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int err = 0;
	int create;
	struct ip6_tnl_parm p;
	struct ip6_tnl *t = NULL;

	switch (cmd) {
	case SIOCGETTUNNEL:
		if (dev == &ip6ip6_fb_tnl_dev) {
			if (copy_from_user(&p,
					   ifr->ifr_ifru.ifru_data,
					   sizeof (p))) {
				err = -EFAULT;
				break;
			}
			if ((err = ip6ip6_tnl_locate(&p, &t, 0)) == -ENODEV)
				t = (struct ip6_tnl *) dev->priv;
			else if (err)
				break;
		} else
			t = (struct ip6_tnl *) dev->priv;

		memcpy(&p, &t->parms, sizeof (p));
		if (copy_to_user(ifr->ifr_ifru.ifru_data, &p, sizeof (p))) {
			err = -EFAULT;
		}
		break;
	case SIOCADDTUNNEL:
	case SIOCCHGTUNNEL:
		err = -EPERM;
		create = (cmd == SIOCADDTUNNEL);
		if (!capable(CAP_NET_ADMIN))
			break;
		if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof (p))) {
			err = -EFAULT;
			break;
		}
		if (!create && dev != &ip6ip6_fb_tnl_dev) {
			t = (struct ip6_tnl *) dev->priv;
		}
		if (!t && (err = ip6ip6_tnl_locate(&p, &t, create))) {
			break;
		}
		if (cmd == SIOCCHGTUNNEL) {
			if (t->dev != dev) {
				err = -EEXIST;
				break;
			}
			ip6ip6_tnl_unlink(t);
			err = ip6ip6_tnl_change(t, &p);
			ip6ip6_tnl_link(t);
			netdev_state_change(dev);
		}
		if (copy_to_user(ifr->ifr_ifru.ifru_data,
				 &t->parms, sizeof (p))) {
			err = -EFAULT;
		} else {
			err = 0;
		}
		break;
	case SIOCDELTUNNEL:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;

		if (dev == &ip6ip6_fb_tnl_dev) {
			if (copy_from_user(&p, ifr->ifr_ifru.ifru_data,
					   sizeof (p))) {
				err = -EFAULT;
				break;
			}
			err = ip6ip6_tnl_locate(&p, &t, 0);
			if (err)
				break;
			if (t == &ip6ip6_fb_tnl) {
				err = -EPERM;
				break;
			}
		} else {
			t = (struct ip6_tnl *) dev->priv;
		}
		err = ip6_tnl_destroy(t);
		break;
	default:
		err = -EINVAL;
	}
	return err;
}

/**
 * ip6ip6_tnl_get_stats - return the stats for tunnel device 
 *   @dev: virtual device associated with tunnel
 *
 * Return: stats for device
 **/

static struct net_device_stats *
ip6ip6_tnl_get_stats(struct net_device *dev)
{
	return &(((struct ip6_tnl *) dev->priv)->stat);
}

/**
 * ip6ip6_tnl_change_mtu - change mtu manually for tunnel device
 *   @dev: virtual device associated with tunnel
 *   @new_mtu: the new mtu
 *
 * Return:
 *   0 on success,
 *   %-EINVAL if mtu too small
 **/

static int
ip6ip6_tnl_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < IPV6_MIN_MTU) {
		return -EINVAL;
	}
	dev->mtu = new_mtu;
	return 0;
}

/**
 * ip6ip6_tnl_dev_init_gen - general initializer for all tunnel devices
 *   @dev: virtual device associated with tunnel
 *
 * Description:
 *   Set function pointers and initialize the &struct flowi template used
 *   by the tunnel.
 **/

static void
ip6ip6_tnl_dev_init_gen(struct net_device *dev)
{
	struct ip6_tnl *t = (struct ip6_tnl *) dev->priv;
	struct flowi *fl = &t->fl;

	memset(fl, 0, sizeof (*fl));
	fl->proto = IPPROTO_IPV6;

	dev->destructor = ip6ip6_tnl_dev_destructor;
	dev->uninit = ip6ip6_tnl_dev_uninit;
	dev->hard_start_xmit = ip6ip6_tnl_xmit;
	dev->get_stats = ip6ip6_tnl_get_stats;
	dev->do_ioctl = ip6ip6_tnl_ioctl;
	dev->change_mtu = ip6ip6_tnl_change_mtu;
	dev->type = ARPHRD_TUNNEL6;
	dev->flags |= IFF_NOARP;
	if (ipv6_addr_type(&t->parms.raddr) & IPV6_ADDR_UNICAST &&
	    ipv6_addr_type(&t->parms.laddr) & IPV6_ADDR_UNICAST)
		dev->flags |= IFF_POINTOPOINT;
	/* Hmm... MAX_ADDR_LEN is 8, so the ipv6 addresses can't be 
	   copied to dev->dev_addr and dev->broadcast, like the ipv4
	   addresses were in ipip.c, ip_gre.c and sit.c. */
	dev->addr_len = 0;
}

/**
 * ip6ip6_tnl_dev_init - initializer for all non fallback tunnel devices
 *   @dev: virtual device associated with tunnel
 **/

static int
ip6ip6_tnl_dev_init(struct net_device *dev)
{
	struct ip6_tnl *t = (struct ip6_tnl *) dev->priv;
	ip6ip6_tnl_dev_init_gen(dev);
	ip6ip6_tnl_link_config(t);
	return 0;
}

/**
 * ip6ip6_fb_tnl_dev_init - initializer for fallback tunnel device
 *   @dev: fallback device
 *
 * Return: 0
 **/

int ip6ip6_fb_tnl_dev_init(struct net_device *dev)
{
	ip6ip6_tnl_dev_init_gen(dev);
	tnls_wc[0] = &ip6ip6_fb_tnl;
	return 0;
}

static struct inet6_protocol ip6ip6_protocol = {
	.handler = ip6ip6_rcv,
	.err_handler = ip6ip6_err,
	.flags = INET6_PROTO_FINAL
};

/**
 * ip6_tunnel_init - register protocol and reserve needed resources
 *
 * Return: 0 on success
 **/

int __init ip6_tunnel_init(void)
{
	int i, j, err;
	struct sock *sk;
	struct ipv6_pinfo *np;

	ip6ip6_fb_tnl_dev.priv = (void *) &ip6ip6_fb_tnl;

	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_possible(i))
			continue;

		err = sock_create(PF_INET6, SOCK_RAW, IPPROTO_IPV6, 
				  &__ip6_socket[i]);
		if (err < 0) {
			printk(KERN_ERR 
			       "Failed to create the IPv6 tunnel socket "
			       "(err %d).\n", 
			       err);
			goto fail;
		}
		sk = __ip6_socket[i]->sk;
		sk->sk_allocation = GFP_ATOMIC;

		np = inet6_sk(sk);
		np->hop_limit = 255;
		np->mc_loop = 0;

		sk->sk_prot->unhash(sk);
	}
	if ((err = inet6_add_protocol(&ip6ip6_protocol, IPPROTO_IPV6)) < 0) {
		printk(KERN_ERR "Failed to register IPv6 protocol\n");
		goto fail;
	}

	SET_MODULE_OWNER(&ip6ip6_fb_tnl_dev);
	register_netdev(&ip6ip6_fb_tnl_dev);

	return 0;
fail:
	for (j = 0; j < i; j++) {
		if (!cpu_possible(j))
			continue;
		sock_release(__ip6_socket[j]);
		__ip6_socket[j] = NULL;
	}
	return err;
}

/**
 * ip6_tunnel_cleanup - free resources and unregister protocol
 **/

void ip6_tunnel_cleanup(void)
{
	int i;

	unregister_netdev(&ip6ip6_fb_tnl_dev);

	inet6_del_protocol(&ip6ip6_protocol, IPPROTO_IPV6);

	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_possible(i))
			continue;
		sock_release(__ip6_socket[i]);
		__ip6_socket[i] = NULL;
	}
}

#ifdef MODULE
module_init(ip6_tunnel_init);
module_exit(ip6_tunnel_cleanup);
#endif
