/*
 *	IPv6 output functions
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: ip6_output.c,v 1.34 2002/02/01 22:01:04 davem Exp $
 *
 *	Based on linux/net/ipv4/ip_output.c
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	Changes:
 *	A.N.Kuznetsov	:	airthmetics in fragmentation.
 *				extension headers are implemented.
 *				route changes now work.
 *				ip6_forward does not confuse sniffers.
 *				etc.
 *
 *      H. von Brand    :       Added missing #include <linux/string.h>
 *	Imran Patel	: 	frag id should be in NBO
 *      Kazunori MIYAZAWA @USAGI
 *			:       add ip6_append_data and related functions
 *				for datagram xmit
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/tcp.h>
#include <linux/route.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/rawv6.h>
#include <net/icmp.h>
#include <net/xfrm.h>

static int ip6_fragment(struct sk_buff *skb, int (*output)(struct sk_buff*));

static __inline__ void ipv6_select_ident(struct sk_buff *skb, struct frag_hdr *fhdr)
{
	static u32 ipv6_fragmentation_id = 1;
	static spinlock_t ip6_id_lock = SPIN_LOCK_UNLOCKED;

	spin_lock_bh(&ip6_id_lock);
	fhdr->identification = htonl(ipv6_fragmentation_id);
	if (++ipv6_fragmentation_id == 0)
		ipv6_fragmentation_id = 1;
	spin_unlock_bh(&ip6_id_lock);
}

static inline int ip6_output_finish(struct sk_buff *skb)
{

	struct dst_entry *dst = skb->dst;
	struct hh_cache *hh = dst->hh;

	if (hh) {
		int hh_alen;

		read_lock_bh(&hh->hh_lock);
		hh_alen = HH_DATA_ALIGN(hh->hh_len);
		memcpy(skb->data - hh_alen, hh->hh_data, hh_alen);
		read_unlock_bh(&hh->hh_lock);
	        skb_push(skb, hh->hh_len);
		return hh->hh_output(skb);
	} else if (dst->neighbour)
		return dst->neighbour->output(skb);

	kfree_skb(skb);
	return -EINVAL;

}

/* dev_loopback_xmit for use with netfilter. */
static int ip6_dev_loopback_xmit(struct sk_buff *newskb)
{
	newskb->mac.raw = newskb->data;
	__skb_pull(newskb, newskb->nh.raw - newskb->data);
	newskb->pkt_type = PACKET_LOOPBACK;
	newskb->ip_summed = CHECKSUM_UNNECESSARY;
	BUG_TRAP(newskb->dst);

	netif_rx(newskb);
	return 0;
}


int ip6_output2(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct net_device *dev = dst->dev;

	skb->protocol = htons(ETH_P_IPV6);
	skb->dev = dev;

	if (ipv6_addr_is_multicast(&skb->nh.ipv6h->daddr)) {
		struct ipv6_pinfo* np = skb->sk ? inet6_sk(skb->sk) : NULL;

		if (!(dev->flags & IFF_LOOPBACK) && (!np || np->mc_loop) &&
		    ipv6_chk_mcast_addr(dev, &skb->nh.ipv6h->daddr,
				&skb->nh.ipv6h->saddr)) {
			struct sk_buff *newskb = skb_clone(skb, GFP_ATOMIC);

			/* Do not check for IFF_ALLMULTI; multicast routing
			   is not supported in any case.
			 */
			if (newskb)
				NF_HOOK(PF_INET6, NF_IP6_POST_ROUTING, newskb, NULL,
					newskb->dev,
					ip6_dev_loopback_xmit);

			if (skb->nh.ipv6h->hop_limit == 0) {
				kfree_skb(skb);
				return 0;
			}
		}

		IP6_INC_STATS(Ip6OutMcastPkts);
	}

	return NF_HOOK(PF_INET6, NF_IP6_POST_ROUTING, skb,NULL, skb->dev,ip6_output_finish);
}

int ip6_output(struct sk_buff *skb)
{
	if ((skb->len > dst_pmtu(skb->dst) || skb_shinfo(skb)->frag_list))
		return ip6_fragment(skb, ip6_output2);
	else
		return ip6_output2(skb);
}

#ifdef CONFIG_NETFILTER
int ip6_route_me_harder(struct sk_buff *skb)
{
	struct ipv6hdr *iph = skb->nh.ipv6h;
	struct dst_entry *dst;
	struct flowi fl = {
		.oif = skb->sk ? skb->sk->sk_bound_dev_if : 0,
		.nl_u =
		{ .ip6_u =
		  { .daddr = iph->daddr,
		    .saddr = iph->saddr, } },
		.proto = iph->nexthdr,
	};

	dst = ip6_route_output(skb->sk, &fl);

	if (dst->error) {
		if (net_ratelimit())
			printk(KERN_DEBUG "ip6_route_me_harder: No more route.\n");
		dst_release(dst);
		return -EINVAL;
	}

	/* Drop old route. */
	dst_release(skb->dst);

	skb->dst = dst;
	return 0;
}
#endif

static inline int ip6_maybe_reroute(struct sk_buff *skb)
{
#ifdef CONFIG_NETFILTER
	if (skb->nfcache & NFC_ALTERED){
		if (ip6_route_me_harder(skb) != 0){
			kfree_skb(skb);
			return -EINVAL;
		}
	}
#endif /* CONFIG_NETFILTER */
	return dst_output(skb);
}

/*
 *	xmit an sk_buff (used by TCP)
 */

int ip6_xmit(struct sock *sk, struct sk_buff *skb, struct flowi *fl,
	     struct ipv6_txoptions *opt, int ipfragok)
{
	struct ipv6_pinfo *np = sk ? inet6_sk(sk) : NULL;
	struct in6_addr *first_hop = &fl->fl6_dst;
	struct dst_entry *dst = skb->dst;
	struct ipv6hdr *hdr;
	u8  proto = fl->proto;
	int seg_len = skb->len;
	int hlimit;
	u32 mtu;

	if (opt) {
		int head_room;

		/* First: exthdrs may take lots of space (~8K for now)
		   MAX_HEADER is not enough.
		 */
		head_room = opt->opt_nflen + opt->opt_flen;
		seg_len += head_room;
		head_room += sizeof(struct ipv6hdr) + ((dst->dev->hard_header_len + 15)&~15);

		if (skb_headroom(skb) < head_room) {
			struct sk_buff *skb2 = skb_realloc_headroom(skb, head_room);
			kfree_skb(skb);
			skb = skb2;
			if (skb == NULL)
				return -ENOBUFS;
			if (sk)
				skb_set_owner_w(skb, sk);
		}
		if (opt->opt_flen)
			ipv6_push_frag_opts(skb, opt, &proto);
		if (opt->opt_nflen)
			ipv6_push_nfrag_opts(skb, opt, &proto, &first_hop);
	}

	hdr = skb->nh.ipv6h = (struct ipv6hdr*)skb_push(skb, sizeof(struct ipv6hdr));

	/*
	 *	Fill in the IPv6 header
	 */

	*(u32*)hdr = htonl(0x60000000) | fl->fl6_flowlabel;
	hlimit = -1;
	if (np)
		hlimit = np->hop_limit;
	if (hlimit < 0)
		hlimit = dst_metric(dst, RTAX_HOPLIMIT);

	hdr->payload_len = htons(seg_len);
	hdr->nexthdr = proto;
	hdr->hop_limit = hlimit;

	ipv6_addr_copy(&hdr->saddr, &fl->fl6_src);
	ipv6_addr_copy(&hdr->daddr, first_hop);

	mtu = dst_pmtu(dst);
	if ((skb->len <= mtu) || ipfragok) {
		IP6_INC_STATS(Ip6OutRequests);
		return NF_HOOK(PF_INET6, NF_IP6_LOCAL_OUT, skb, NULL, dst->dev, ip6_maybe_reroute);
	}

	if (net_ratelimit())
		printk(KERN_DEBUG "IPv6: sending pkt_too_big to self\n");
	skb->dev = dst->dev;
	icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu, skb->dev);
	kfree_skb(skb);
	return -EMSGSIZE;
}

/*
 *	To avoid extra problems ND packets are send through this
 *	routine. It's code duplication but I really want to avoid
 *	extra checks since ipv6_build_header is used by TCP (which
 *	is for us performance critical)
 */

int ip6_nd_hdr(struct sock *sk, struct sk_buff *skb, struct net_device *dev,
	       struct in6_addr *saddr, struct in6_addr *daddr,
	       int proto, int len)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6hdr *hdr;
	int totlen;

	skb->protocol = htons(ETH_P_IPV6);
	skb->dev = dev;

	totlen = len + sizeof(struct ipv6hdr);

	hdr = (struct ipv6hdr *) skb_put(skb, sizeof(struct ipv6hdr));
	skb->nh.ipv6h = hdr;

	*(u32*)hdr = htonl(0x60000000);

	hdr->payload_len = htons(len);
	hdr->nexthdr = proto;
	hdr->hop_limit = np->hop_limit;

	ipv6_addr_copy(&hdr->saddr, saddr);
	ipv6_addr_copy(&hdr->daddr, daddr);

	return 0;
}

static struct ipv6hdr * ip6_bld_1(struct sock *sk, struct sk_buff *skb, struct flowi *fl,
				  int hlimit, unsigned pktlength)
{
	struct ipv6hdr *hdr;
	
	skb->nh.raw = skb_put(skb, sizeof(struct ipv6hdr));
	hdr = skb->nh.ipv6h;
	
	*(u32*)hdr = fl->fl6_flowlabel | htonl(0x60000000);

	hdr->payload_len = htons(pktlength - sizeof(struct ipv6hdr));
	hdr->hop_limit = hlimit;
	hdr->nexthdr = fl->proto;

	ipv6_addr_copy(&hdr->saddr, &fl->fl6_src);
	ipv6_addr_copy(&hdr->daddr, &fl->fl6_dst);
	return hdr;
}

static __inline__ u8 * ipv6_build_fraghdr(struct sk_buff *skb, u8* prev_hdr, unsigned offset)
{
	struct frag_hdr *fhdr;

	fhdr = (struct frag_hdr *) skb_put(skb, sizeof(struct frag_hdr));

	fhdr->nexthdr  = *prev_hdr;
	*prev_hdr = NEXTHDR_FRAGMENT;
	prev_hdr = &fhdr->nexthdr;

	fhdr->reserved = 0;
	fhdr->frag_off = htons(offset);
	ipv6_select_ident(skb, fhdr);
	return &fhdr->nexthdr;
}

static int ip6_frag_xmit(struct sock *sk, inet_getfrag_t getfrag,
			 const void *data, struct dst_entry *dst,
			 struct flowi *fl, struct ipv6_txoptions *opt,
			 struct in6_addr *final_dst,
			 int hlimit, int flags, unsigned length, int mtu)
{
	struct ipv6hdr *hdr;
	struct sk_buff *last_skb;
	u8 *prev_hdr;
	int unfrag_len;
	int frag_len;
	int last_len;
	int nfrags;
	int fhdr_dist;
	int frag_off;
	int data_off;
	int err;

	/*
	 *	Fragmentation
	 *
	 *	Extension header order:
	 *	Hop-by-hop -> Dest0 -> Routing -> Fragment -> Auth -> Dest1 -> rest (...)
	 *	
	 *	We must build the non-fragmented part that
	 *	will be in every packet... this also means
	 *	that other extension headers (Dest, Auth, etc)
	 *	must be considered in the data to be fragmented
	 */

	unfrag_len = sizeof(struct ipv6hdr) + sizeof(struct frag_hdr);
	last_len = length;

	if (opt) {
		unfrag_len += opt->opt_nflen;
		last_len += opt->opt_flen;
	}

	/*
	 *	Length of fragmented part on every packet but 
	 *	the last must be an:
	 *	"integer multiple of 8 octets".
	 */

	frag_len = (mtu - unfrag_len) & ~0x7;

	/* Unfragmentable part exceeds mtu. */
	if (frag_len <= 0) {
		ipv6_local_error(sk, EMSGSIZE, fl, mtu);
		return -EMSGSIZE;
	}

	nfrags = last_len / frag_len;

	/*
	 *	We must send from end to start because of 
	 *	UDP/ICMP checksums. We do a funny trick:
	 *	fill the last skb first with the fixed
	 *	header (and its data) and then use it
	 *	to create the following segments and send it
	 *	in the end. If the peer is checking the M_flag
	 *	to trigger the reassembly code then this 
	 *	might be a good idea.
	 */

	frag_off = nfrags * frag_len;
	last_len -= frag_off;

	if (last_len == 0) {
		last_len = frag_len;
		frag_off -= frag_len;
		nfrags--;
	}
	data_off = frag_off;

	/* And it is implementation problem: for now we assume, that
	   all the exthdrs will fit to the first fragment.
	 */
	if (opt) {
		if (frag_len < opt->opt_flen) {
			ipv6_local_error(sk, EMSGSIZE, fl, mtu);
			return -EMSGSIZE;
		}
		data_off = frag_off - opt->opt_flen;
	}

	if (flags&MSG_PROBE)
		return 0;

	last_skb = sock_alloc_send_skb(sk, unfrag_len + frag_len +
				       dst->dev->hard_header_len + 15,
				       flags & MSG_DONTWAIT, &err);

	if (last_skb == NULL)
		return err;

	last_skb->dst = dst_clone(dst);

	skb_reserve(last_skb, (dst->dev->hard_header_len + 15) & ~15);

	hdr = ip6_bld_1(sk, last_skb, fl, hlimit, frag_len+unfrag_len);
	prev_hdr = &hdr->nexthdr;

	if (opt && opt->opt_nflen)
		prev_hdr = ipv6_build_nfrag_opts(last_skb, prev_hdr, opt, final_dst, 0);

	prev_hdr = ipv6_build_fraghdr(last_skb, prev_hdr, frag_off);
	fhdr_dist = prev_hdr - last_skb->data;

	err = getfrag(data, &hdr->saddr, last_skb->tail, data_off, last_len);

	if (!err) {
		while (nfrags--) {
			struct sk_buff *skb;
			
			struct frag_hdr *fhdr2;
				
			skb = skb_copy(last_skb, sk->sk_allocation);

			if (skb == NULL) {
				IP6_INC_STATS(Ip6FragFails);
				kfree_skb(last_skb);
				return -ENOMEM;
			}

			frag_off -= frag_len;
			data_off -= frag_len;

			fhdr2 = (struct frag_hdr *) (skb->data + fhdr_dist);

			/* more flag on */
			fhdr2->frag_off = htons(frag_off | 1);

			/* Write fragmentable exthdrs to the first chunk */
			if (nfrags == 0 && opt && opt->opt_flen) {
				ipv6_build_frag_opts(skb, &fhdr2->nexthdr, opt);
				frag_len -= opt->opt_flen;
				data_off = 0;
			}

			err = getfrag(data, &hdr->saddr,skb_put(skb, frag_len),
				      data_off, frag_len);

			if (err) {
				kfree_skb(skb);
				break;
			}

			IP6_INC_STATS(Ip6FragCreates);
			IP6_INC_STATS(Ip6OutRequests);
			err = NF_HOOK(PF_INET6,NF_IP6_LOCAL_OUT, skb, NULL, dst->dev, ip6_maybe_reroute);
			if (err) {
				kfree_skb(last_skb);
				return err;
			}
		}
	}

	if (err) {
		IP6_INC_STATS(Ip6FragFails);
		kfree_skb(last_skb);
		return -EFAULT;
	}

	hdr->payload_len = htons(unfrag_len + last_len - sizeof(struct ipv6hdr));

	/*
	 *	update last_skb to reflect the getfrag we did
	 *	on start.
	 */

	skb_put(last_skb, last_len);

	IP6_INC_STATS(Ip6FragCreates);
	IP6_INC_STATS(Ip6FragOKs);
	IP6_INC_STATS(Ip6OutRequests);
	return NF_HOOK(PF_INET6, NF_IP6_LOCAL_OUT, last_skb, NULL,dst->dev, ip6_maybe_reroute);
}

int ip6_build_xmit(struct sock *sk, inet_getfrag_t getfrag, const void *data,
		   struct flowi *fl, unsigned length,
		   struct ipv6_txoptions *opt, int hlimit, int flags)
{
	struct inet_opt *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct in6_addr final_dst_buf, *final_dst = NULL;
	struct dst_entry *dst;
	int err = 0;
	unsigned int pktlength, jumbolen, mtu;

	if (opt && opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *) opt->srcrt;
		ipv6_addr_copy(&final_dst_buf, &fl->fl6_dst);
		final_dst = &final_dst_buf;
		ipv6_addr_copy(&fl->fl6_dst, rt0->addr);
	}

	if (!fl->oif && ipv6_addr_is_multicast(&fl->fl6_dst))
		fl->oif = np->mcast_oif;

	dst = __sk_dst_check(sk, np->dst_cookie);
	if (dst) {
		struct rt6_info *rt = (struct rt6_info*)dst;

			/* Yes, checking route validity in not connected
			   case is not very simple. Take into account,
			   that we do not support routing by source, TOS,
			   and MSG_DONTROUTE 		--ANK (980726)

			   1. If route was host route, check that
			      cached destination is current.
			      If it is network route, we still may
			      check its validity using saved pointer
			      to the last used address: daddr_cache.
			      We do not want to save whole address now,
			      (because main consumer of this service
			       is tcp, which has not this problem),
			      so that the last trick works only on connected
			      sockets.
			   2. oif also should be the same.
			 */

		if (((rt->rt6i_dst.plen != 128 ||
		      ipv6_addr_cmp(&fl->fl6_dst, &rt->rt6i_dst.addr))
		     && (np->daddr_cache == NULL ||
			 ipv6_addr_cmp(&fl->fl6_dst, np->daddr_cache)))
		    || (fl->oif && fl->oif != dst->dev->ifindex)) {
			dst = NULL;
		} else
			dst_hold(dst);
	}

	if (dst == NULL)
		dst = ip6_route_output(sk, fl);

	if (dst->error) {
		IP6_INC_STATS(Ip6OutNoRoutes);
		dst_release(dst);
		return -ENETUNREACH;
	}

	if (ipv6_addr_any(&fl->fl6_src)) {
		err = ipv6_get_saddr(dst, &fl->fl6_dst, &fl->fl6_src);

		if (err) {
#if IP6_DEBUG >= 2
			printk(KERN_DEBUG "ip6_build_xmit: "
			       "no available source address\n");
#endif
			goto out;
		}
	}
	pktlength = length;

        if (dst) {
		if ((err = xfrm_lookup(&dst, fl, sk, 0)) < 0) {
			dst_release(dst);	
			return -ENETUNREACH;
		}
        }

	if (hlimit < 0) {
		if (ipv6_addr_is_multicast(&fl->fl6_dst))
			hlimit = np->mcast_hops;
		else
			hlimit = np->hop_limit;
		if (hlimit < 0)
			hlimit = dst_metric(dst, RTAX_HOPLIMIT);
	}

	jumbolen = 0;

	if (!inet->hdrincl) {
		pktlength += sizeof(struct ipv6hdr);
		if (opt)
			pktlength += opt->opt_flen + opt->opt_nflen;

		if (pktlength > sizeof(struct ipv6hdr) + IPV6_MAXPLEN) {
			/* Jumbo datagram.
			   It is assumed, that in the case of hdrincl
			   jumbo option is supplied by user.
			 */
			pktlength += 8;
			jumbolen = pktlength - sizeof(struct ipv6hdr);
		}
	}

	mtu = dst_pmtu(dst);
	if (np->frag_size < mtu) {
		if (np->frag_size)
			mtu = np->frag_size;
		else if (np->pmtudisc == IPV6_PMTUDISC_DONT)
			mtu = IPV6_MIN_MTU;
	}

	/* Critical arithmetic overflow check.
	   FIXME: may gcc optimize it out? --ANK (980726)
	 */
	if (pktlength < length) {
		ipv6_local_error(sk, EMSGSIZE, fl, mtu);
		err = -EMSGSIZE;
		goto out;
	}

	if (flags&MSG_CONFIRM)
		dst_confirm(dst);

	if (pktlength <= mtu) {
		struct sk_buff *skb;
		struct ipv6hdr *hdr;
		struct net_device *dev = dst->dev;

		err = 0;
		if (flags&MSG_PROBE)
			goto out;
		/* alloc skb with mtu as we do in the IPv4 stack for IPsec */
		skb = sock_alloc_send_skb(sk, mtu + LL_RESERVED_SPACE(dev),
					  flags & MSG_DONTWAIT, &err);

		if (skb == NULL) {
			IP6_INC_STATS(Ip6OutDiscards);
			goto out;
		}

		skb->dst = dst_clone(dst);

		skb_reserve(skb, (dev->hard_header_len + 15) & ~15);

		hdr = (struct ipv6hdr *) skb->tail;
		skb->nh.ipv6h = hdr;

		if (!inet->hdrincl) {
			ip6_bld_1(sk, skb, fl, hlimit,
				  jumbolen ? sizeof(struct ipv6hdr) : pktlength);

			if (opt || jumbolen) {
				u8 *prev_hdr = &hdr->nexthdr;
				prev_hdr = ipv6_build_nfrag_opts(skb, prev_hdr, opt, final_dst, jumbolen);
				if (opt && opt->opt_flen)
					ipv6_build_frag_opts(skb, prev_hdr, opt);
			}
		}

		skb_put(skb, length);
		err = getfrag(data, &hdr->saddr,
			      ((char *) hdr) + (pktlength - length),
			      0, length);
		if (!opt || !opt->dst1opt)
			skb->h.raw = ((char *) hdr) + (pktlength - length);

		if (!err) {
			IP6_INC_STATS(Ip6OutRequests);
			err = NF_HOOK(PF_INET6, NF_IP6_LOCAL_OUT, skb, NULL, dst->dev, ip6_maybe_reroute);
		} else {
			err = -EFAULT;
			kfree_skb(skb);
		}
	} else {
		if (inet->hdrincl || jumbolen ||
		    np->pmtudisc == IPV6_PMTUDISC_DO) {
			ipv6_local_error(sk, EMSGSIZE, fl, mtu);
			err = -EMSGSIZE;
			goto out;
		}

		err = ip6_frag_xmit(sk, getfrag, data, dst, fl, opt, final_dst, hlimit,
				    flags, length, mtu);
	}

	/*
	 *	cleanup
	 */
out:
	ip6_dst_store(sk, dst,
		      !ipv6_addr_cmp(&fl->fl6_dst, &np->daddr) ?
		      &np->daddr : NULL);
	if (err > 0)
		err = np->recverr ? net_xmit_errno(err) : 0;
	return err;
}

int ip6_call_ra_chain(struct sk_buff *skb, int sel)
{
	struct ip6_ra_chain *ra;
	struct sock *last = NULL;

	read_lock(&ip6_ra_lock);
	for (ra = ip6_ra_chain; ra; ra = ra->next) {
		struct sock *sk = ra->sk;
		if (sk && ra->sel == sel) {
			if (last) {
				struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
				if (skb2)
					rawv6_rcv(last, skb2);
			}
			last = sk;
		}
	}

	if (last) {
		rawv6_rcv(last, skb);
		read_unlock(&ip6_ra_lock);
		return 1;
	}
	read_unlock(&ip6_ra_lock);
	return 0;
}

static inline int ip6_forward_finish(struct sk_buff *skb)
{
	return dst_output(skb);
}

int ip6_forward(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct ipv6hdr *hdr = skb->nh.ipv6h;
	struct inet6_skb_parm *opt =(struct inet6_skb_parm*)skb->cb;
	
	if (ipv6_devconf.forwarding == 0)
		goto error;

	if (!xfrm6_policy_check(NULL, XFRM_POLICY_FWD, skb))
		goto drop;

	skb->ip_summed = CHECKSUM_NONE;

	/*
	 *	We DO NOT make any processing on
	 *	RA packets, pushing them to user level AS IS
	 *	without ane WARRANTY that application will be able
	 *	to interpret them. The reason is that we
	 *	cannot make anything clever here.
	 *
	 *	We are not end-node, so that if packet contains
	 *	AH/ESP, we cannot make anything.
	 *	Defragmentation also would be mistake, RA packets
	 *	cannot be fragmented, because there is no warranty
	 *	that different fragments will go along one path. --ANK
	 */
	if (opt->ra) {
		u8 *ptr = skb->nh.raw + opt->ra;
		if (ip6_call_ra_chain(skb, (ptr[2]<<8) + ptr[3]))
			return 0;
	}

	/*
	 *	check and decrement ttl
	 */
	if (hdr->hop_limit <= 1) {
		/* Force OUTPUT device used as source address */
		skb->dev = dst->dev;
		icmpv6_send(skb, ICMPV6_TIME_EXCEED, ICMPV6_EXC_HOPLIMIT,
			    0, skb->dev);

		kfree_skb(skb);
		return -ETIMEDOUT;
	}

	if (!xfrm6_route_forward(skb))
		goto drop;

	/* IPv6 specs say nothing about it, but it is clear that we cannot
	   send redirects to source routed frames.
	 */
	if (skb->dev == dst->dev && dst->neighbour && opt->srcrt == 0) {
		struct in6_addr *target = NULL;
		struct rt6_info *rt;
		struct neighbour *n = dst->neighbour;

		/*
		 *	incoming and outgoing devices are the same
		 *	send a redirect.
		 */

		rt = (struct rt6_info *) dst;
		if ((rt->rt6i_flags & RTF_GATEWAY))
			target = (struct in6_addr*)&n->primary_key;
		else
			target = &hdr->daddr;

		/* Limit redirects both by destination (here)
		   and by source (inside ndisc_send_redirect)
		 */
		if (xrlim_allow(dst, 1*HZ))
			ndisc_send_redirect(skb, n, target);
	} else if (ipv6_addr_type(&hdr->saddr)&(IPV6_ADDR_MULTICAST|IPV6_ADDR_LOOPBACK
						|IPV6_ADDR_LINKLOCAL)) {
		/* This check is security critical. */
		goto error;
	}

	if (skb->len > dst_pmtu(dst)) {
		/* Again, force OUTPUT device used as source address */
		skb->dev = dst->dev;
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, dst_pmtu(dst), skb->dev);
		IP6_INC_STATS_BH(Ip6InTooBigErrors);
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	if (skb_cow(skb, dst->dev->hard_header_len))
		goto drop;

	hdr = skb->nh.ipv6h;

	/* Mangling hops number delayed to point after skb COW */
 
	hdr->hop_limit--;

	IP6_INC_STATS_BH(Ip6OutForwDatagrams);
	return NF_HOOK(PF_INET6,NF_IP6_FORWARD, skb, skb->dev, dst->dev, ip6_forward_finish);

error:
	IP6_INC_STATS_BH(Ip6InAddrErrors);
drop:
	kfree_skb(skb);
	return -EINVAL;
}

static void ip6_copy_metadata(struct sk_buff *to, struct sk_buff *from)
{
	to->pkt_type = from->pkt_type;
	to->priority = from->priority;
	to->protocol = from->protocol;
	to->security = from->security;
	to->dst = dst_clone(from->dst);
	to->dev = from->dev;

#ifdef CONFIG_NET_SCHED
	to->tc_index = from->tc_index;
#endif
#ifdef CONFIG_NETFILTER
	to->nfmark = from->nfmark;
	/* Connection association is same as pre-frag packet */
	to->nfct = from->nfct;
	nf_conntrack_get(to->nfct);
#ifdef CONFIG_BRIDGE_NETFILTER
	to->nf_bridge = from->nf_bridge;
	nf_bridge_get(to->nf_bridge);
#endif
#ifdef CONFIG_NETFILTER_DEBUG
	to->nf_debug = from->nf_debug;
#endif
#endif
}

int ip6_find_1stfragopt(struct sk_buff *skb, u8 **nexthdr)
{
	u16 offset = sizeof(struct ipv6hdr);
	struct ipv6_opt_hdr *exthdr = (struct ipv6_opt_hdr*)(skb->nh.ipv6h + 1);
	unsigned int packet_len = skb->tail - skb->nh.raw;
	int found_rhdr = 0;
	*nexthdr = &skb->nh.ipv6h->nexthdr;

	while (offset + 1 <= packet_len) {

		switch (**nexthdr) {

		case NEXTHDR_HOP:
		case NEXTHDR_ROUTING:
		case NEXTHDR_DEST:
			if (**nexthdr == NEXTHDR_ROUTING) found_rhdr = 1;
			if (**nexthdr == NEXTHDR_DEST && found_rhdr) return offset;
			offset += ipv6_optlen(exthdr);
			*nexthdr = &exthdr->nexthdr;
			exthdr = (struct ipv6_opt_hdr*)(skb->nh.raw + offset);
			break;
		default :
			return offset;
		}
	}

	return offset;
}

static int ip6_fragment(struct sk_buff *skb, int (*output)(struct sk_buff*))
{
	struct net_device *dev;
	struct rt6_info *rt = (struct rt6_info*)skb->dst;
	struct sk_buff *frag;
	struct ipv6hdr *tmp_hdr;
	struct frag_hdr *fh;
	unsigned int mtu, hlen, left, len;
	u32 frag_id = 0;
	int ptr, offset = 0, err=0;
	u8 *prevhdr, nexthdr = 0;

	dev = rt->u.dst.dev;
	hlen = ip6_find_1stfragopt(skb, &prevhdr);
	nexthdr = *prevhdr;

	mtu = dst_pmtu(&rt->u.dst) - hlen - sizeof(struct frag_hdr);

	if (skb_shinfo(skb)->frag_list) {
		int first_len = skb_pagelen(skb);

		if (first_len - hlen > mtu ||
		    ((first_len - hlen) & 7) ||
		    skb_cloned(skb))
			goto slow_path;

		for (frag = skb_shinfo(skb)->frag_list; frag; frag = frag->next) {
			/* Correct geometry. */
			if (frag->len > mtu ||
			    ((frag->len & 7) && frag->next) ||
			    skb_headroom(frag) < hlen)
			    goto slow_path;

			/* Correct socket ownership. */
			if (frag->sk == NULL)
				goto slow_path;

			/* Partially cloned skb? */
			if (skb_shared(frag))
				goto slow_path;
		}

		err = 0;
		offset = 0;
		frag = skb_shinfo(skb)->frag_list;
		skb_shinfo(skb)->frag_list = 0;
		/* BUILD HEADER */

		tmp_hdr = kmalloc(hlen, GFP_ATOMIC);
		if (!tmp_hdr) {
			IP6_INC_STATS(Ip6FragFails);
			return -ENOMEM;
		}

		*prevhdr = NEXTHDR_FRAGMENT;
		memcpy(tmp_hdr, skb->nh.raw, hlen);
		__skb_pull(skb, hlen);
		fh = (struct frag_hdr*)__skb_push(skb, sizeof(struct frag_hdr));
		skb->nh.raw = __skb_push(skb, hlen);
		memcpy(skb->nh.raw, tmp_hdr, hlen);

		ipv6_select_ident(skb, fh);
		fh->nexthdr = nexthdr;
		fh->reserved = 0;
		fh->frag_off = htons(IP6_MF);
		frag_id = fh->identification;

		first_len = skb_pagelen(skb);
		skb->data_len = first_len - skb_headlen(skb);
		skb->len = first_len;
		skb->nh.ipv6h->payload_len = htons(first_len - sizeof(struct ipv6hdr));
 

		for (;;) {
			/* Prepare header of the next frame,
			 * before previous one went down. */
			if (frag) {
				frag->h.raw = frag->data;
				fh = (struct frag_hdr*)__skb_push(frag, sizeof(struct frag_hdr));
				frag->nh.raw = __skb_push(frag, hlen);
				memcpy(frag->nh.raw, tmp_hdr, hlen);
				offset += skb->len - hlen - sizeof(struct frag_hdr);
				fh->nexthdr = nexthdr;
				fh->reserved = 0;
				fh->frag_off = htons(offset);
				if (frag->next != NULL)
					fh->frag_off |= htons(IP6_MF);
				fh->identification = frag_id;
				frag->nh.ipv6h->payload_len = htons(frag->len - sizeof(struct ipv6hdr));
				ip6_copy_metadata(frag, skb);
			}
			err = output(skb);

			if (err || !frag)
				break;

			skb = frag;
			frag = skb->next;
			skb->next = NULL;
		}

		if (tmp_hdr)
			kfree(tmp_hdr);

		if (err == 0) {
			IP6_INC_STATS(Ip6FragOKs);
			return 0;
		}

		while (frag) {
			skb = frag->next;
			kfree_skb(frag);
			frag = skb;
		}

		IP6_INC_STATS(Ip6FragFails);
		return err;
	}

slow_path:
	left = skb->len - hlen;		/* Space per frame */
	ptr = hlen;			/* Where to start from */

	/*
	 *	Fragment the datagram.
	 */

	*prevhdr = NEXTHDR_FRAGMENT;

	/*
	 *	Keep copying data until we run out.
	 */
	while(left > 0)	{
		len = left;
		/* IF: it doesn't fit, use 'mtu' - the data space left */
		if (len > mtu)
			len = mtu;
		/* IF: we are not sending upto and including the packet end
		   then align the next start on an eight byte boundary */
		if (len < left)	{
			len &= ~7;
		}
		/*
		 *	Allocate buffer.
		 */

		if ((frag = alloc_skb(len+hlen+sizeof(struct frag_hdr)+LL_RESERVED_SPACE(rt->u.dst.dev), GFP_ATOMIC)) == NULL) {
			NETDEBUG(printk(KERN_INFO "IPv6: frag: no memory for new fragment!\n"));
			err = -ENOMEM;
			goto fail;
		}

		/*
		 *	Set up data on packet
		 */

		ip6_copy_metadata(frag, skb);
		skb_reserve(frag, LL_RESERVED_SPACE(rt->u.dst.dev));
		skb_put(frag, len + hlen + sizeof(struct frag_hdr));
		frag->nh.raw = frag->data;
		fh = (struct frag_hdr*)(frag->data + hlen);
		frag->h.raw = frag->data + hlen + sizeof(struct frag_hdr);

		/*
		 *	Charge the memory for the fragment to any owner
		 *	it might possess
		 */
		if (skb->sk)
			skb_set_owner_w(frag, skb->sk);

		/*
		 *	Copy the packet header into the new buffer.
		 */
		memcpy(frag->nh.raw, skb->data, hlen);

		/*
		 *	Build fragment header.
		 */
		fh->nexthdr = nexthdr;
		fh->reserved = 0;
		if (frag_id) {
			ipv6_select_ident(skb, fh);
			frag_id = fh->identification;
		} else
			fh->identification = frag_id;

		/*
		 *	Copy a block of the IP datagram.
		 */
		if (skb_copy_bits(skb, ptr, frag->h.raw, len))
			BUG();
		left -= len;

		fh->frag_off = htons(offset);
		if (left > 0)
			fh->frag_off |= htons(IP6_MF);
		frag->nh.ipv6h->payload_len = htons(frag->len - sizeof(struct ipv6hdr));

		ptr += len;
		offset += len;

		/*
		 *	Put this fragment into the sending queue.
		 */

		IP6_INC_STATS(Ip6FragCreates);

		err = output(frag);
		if (err)
			goto fail;
	}
	kfree_skb(skb);
	IP6_INC_STATS(Ip6FragOKs);
	return err;

fail:
	kfree_skb(skb); 
	IP6_INC_STATS(Ip6FragFails);
	return err;
}

int ip6_dst_lookup(struct sock *sk, struct dst_entry **dst, struct flowi *fl)
{
	int err = 0;

	if (sk) {
		struct ipv6_pinfo *np = inet6_sk(sk);
	
		*dst = __sk_dst_check(sk, np->dst_cookie);
		if (*dst) {
			struct rt6_info *rt = (struct rt6_info*)*dst;
	
				/* Yes, checking route validity in not connected
				   case is not very simple. Take into account,
				   that we do not support routing by source, TOS,
				   and MSG_DONTROUTE 		--ANK (980726)
	
				   1. If route was host route, check that
				      cached destination is current.
				      If it is network route, we still may
				      check its validity using saved pointer
				      to the last used address: daddr_cache.
				      We do not want to save whole address now,
				      (because main consumer of this service
				       is tcp, which has not this problem),
				      so that the last trick works only on connected
				      sockets.
				   2. oif also should be the same.
				 */
	
			if (((rt->rt6i_dst.plen != 128 ||
			      ipv6_addr_cmp(&fl->fl6_dst, &rt->rt6i_dst.addr))
			     && (np->daddr_cache == NULL ||
				 ipv6_addr_cmp(&fl->fl6_dst, np->daddr_cache)))
			    || (fl->oif && fl->oif != (*dst)->dev->ifindex)) {
				*dst = NULL;
			} else
				dst_hold(*dst);
		}
	}

	if (*dst == NULL)
		*dst = ip6_route_output(sk, fl);

	if ((err = (*dst)->error))
		goto out_err_release;

	if (ipv6_addr_any(&fl->fl6_src)) {
		err = ipv6_get_saddr(*dst, &fl->fl6_dst, &fl->fl6_src);

		if (err) {
#if IP6_DEBUG >= 2
			printk(KERN_DEBUG "ip6_dst_lookup: "
			       "no available source address\n");
#endif
			goto out_err_release;
		}
	}
	if ((err = xfrm_lookup(dst, fl, sk, 0)) < 0) {
		err = -ENETUNREACH;
		goto out_err_release;
        }

	return 0;

out_err_release:
	dst_release(*dst);
	*dst = NULL;
	return err;
}

int ip6_append_data(struct sock *sk, int getfrag(void *from, char *to, int offset, int len, int odd, struct sk_buff *skb),
		    void *from, int length, int transhdrlen,
		    int hlimit, struct ipv6_txoptions *opt, struct flowi *fl, struct rt6_info *rt,
		    unsigned int flags)
{
	struct inet_opt *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sk_buff *skb;
	unsigned int maxfraglen, fragheaderlen;
	int exthdrlen;
	int hh_len;
	int mtu;
	int copy = 0;
	int err;
	int offset = 0;
	int csummode = CHECKSUM_NONE;

	if (flags&MSG_PROBE)
		return 0;
	if (skb_queue_empty(&sk->sk_write_queue)) {
		/*
		 * setup for corking
		 */
		if (opt) {
			if (np->cork.opt == NULL)
				np->cork.opt = kmalloc(opt->tot_len,
						       sk->sk_allocation);
			memcpy(np->cork.opt, opt, opt->tot_len);
			inet->cork.flags |= IPCORK_OPT;
			/* need source address above miyazawa*/
		}
		dst_hold(&rt->u.dst);
		np->cork.rt = rt;
		inet->cork.fl = *fl;
		np->cork.hop_limit = hlimit;
		inet->cork.fragsize = mtu = dst_pmtu(&rt->u.dst);
		inet->cork.length = 0;
		inet->sndmsg_page = NULL;
		inet->sndmsg_off = 0;
		exthdrlen = rt->u.dst.header_len + (opt ? opt->opt_flen : 0);
		length += exthdrlen;
		transhdrlen += exthdrlen;
	} else {
		rt = np->cork.rt;
		fl = &inet->cork.fl;
		if (inet->cork.flags & IPCORK_OPT)
			opt = np->cork.opt;
		transhdrlen = 0;
		exthdrlen = 0;
		mtu = inet->cork.fragsize;
	}

	hh_len = (rt->u.dst.dev->hard_header_len&~15) + 16;

	fragheaderlen = sizeof(struct ipv6hdr) + (opt ? opt->opt_nflen : 0);
	maxfraglen = ((mtu - fragheaderlen) & ~7) + fragheaderlen - sizeof(struct frag_hdr);

	if (mtu <= sizeof(struct ipv6hdr) + IPV6_MAXPLEN) {
		if (inet->cork.length + length > sizeof(struct ipv6hdr) + IPV6_MAXPLEN - fragheaderlen) {
			ipv6_local_error(sk, EMSGSIZE, fl, mtu-exthdrlen);
			return -EMSGSIZE;
		}
	}

	inet->cork.length += length;

	if ((skb = skb_peek_tail(&sk->sk_write_queue)) == NULL)
		goto alloc_new_skb;

	while (length > 0) {
		if ((copy = maxfraglen - skb->len) <= 0) {
			char *data;
			unsigned int datalen;
			unsigned int fraglen;
			unsigned int alloclen;
			BUG_TRAP(copy == 0);
alloc_new_skb:
			datalen = maxfraglen - fragheaderlen;
			if (datalen > length)
				datalen = length;
			fraglen = datalen + fragheaderlen;
			if ((flags & MSG_MORE) &&
			    !(rt->u.dst.dev->features&NETIF_F_SG))
				alloclen = maxfraglen;
			else
				alloclen = fraglen;
			alloclen += sizeof(struct frag_hdr);
			if (transhdrlen) {
				skb = sock_alloc_send_skb(sk,
						alloclen + hh_len + 15,
						(flags & MSG_DONTWAIT), &err);
			} else {
				skb = NULL;
				if (atomic_read(&sk->sk_wmem_alloc) <=
				    2 * sk->sk_sndbuf)
					skb = sock_wmalloc(sk,
							   alloclen + hh_len + 15, 1,
							   sk->sk_allocation);
				if (unlikely(skb == NULL))
					err = -ENOBUFS;
			}
			if (skb == NULL)
				goto error;
			/*
			 *	Fill in the control structures
			 */
			skb->ip_summed = csummode;
			skb->csum = 0;
			/* reserve 8 byte for fragmentation */
			skb_reserve(skb, hh_len+sizeof(struct frag_hdr));

			/*
			 *	Find where to start putting bytes
			 */
			data = skb_put(skb, fraglen);
			skb->nh.raw = data + exthdrlen;
			data += fragheaderlen;
			skb->h.raw = data + exthdrlen;
			copy = datalen - transhdrlen;
			if (copy > 0 && getfrag(from, data + transhdrlen, offset, copy, 0, skb) < 0) {
				err = -EFAULT;
				kfree_skb(skb);
				goto error;
			}

			offset += copy;
			length -= datalen;
			transhdrlen = 0;
			exthdrlen = 0;
			csummode = CHECKSUM_NONE;

			/*
			 * Put the packet on the pending queue
			 */
			__skb_queue_tail(&sk->sk_write_queue, skb);
			continue;
		}

		if (copy > length)
			copy = length;

		if (!(rt->u.dst.dev->features&NETIF_F_SG)) {
			unsigned int off;

			off = skb->len;
			if (getfrag(from, skb_put(skb, copy),
						offset, copy, off, skb) < 0) {
				__skb_trim(skb, off);
				err = -EFAULT;
				goto error;
			}
		} else {
			int i = skb_shinfo(skb)->nr_frags;
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i-1];
			struct page *page = inet->sndmsg_page;
			int off = inet->sndmsg_off;
			unsigned int left;

			if (page && (left = PAGE_SIZE - off) > 0) {
				if (copy >= left)
					copy = left;
				if (page != frag->page) {
					if (i == MAX_SKB_FRAGS) {
						err = -EMSGSIZE;
						goto error;
					}
					get_page(page);
					skb_fill_page_desc(skb, i, page, inet->sndmsg_off, 0);
					frag = &skb_shinfo(skb)->frags[i];
				}
			} else if(i < MAX_SKB_FRAGS) {
				if (copy > PAGE_SIZE)
					copy = PAGE_SIZE;
				page = alloc_pages(sk->sk_allocation, 0);
				if (page == NULL) {
					err = -ENOMEM;
					goto error;
				}
				inet->sndmsg_page = page;
				inet->sndmsg_off = 0;

				skb_fill_page_desc(skb, i, page, 0, 0);
				frag = &skb_shinfo(skb)->frags[i];
				skb->truesize += PAGE_SIZE;
				atomic_add(PAGE_SIZE, &sk->sk_wmem_alloc);
			} else {
				err = -EMSGSIZE;
				goto error;
			}
			if (getfrag(from, page_address(frag->page)+frag->page_offset+frag->size, offset, copy, skb->len, skb) < 0) {
				err = -EFAULT;
				goto error;
			}
			inet->sndmsg_off += copy;
			frag->size += copy;
			skb->len += copy;
			skb->data_len += copy;
		}
		offset += copy;
		length -= copy;
	}
	return 0;
error:
	inet->cork.length -= length;
	IP6_INC_STATS(Ip6OutDiscards);
	return err;
}

int ip6_push_pending_frames(struct sock *sk)
{
	struct sk_buff *skb, *tmp_skb;
	struct sk_buff **tail_skb;
	struct in6_addr final_dst_buf, *final_dst = &final_dst_buf;
	struct inet_opt *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6hdr *hdr;
	struct ipv6_txoptions *opt = np->cork.opt;
	struct rt6_info *rt = np->cork.rt;
	struct flowi *fl = &inet->cork.fl;
	unsigned char proto = fl->proto;
	int err = 0;

	if ((skb = __skb_dequeue(&sk->sk_write_queue)) == NULL)
		goto out;
	tail_skb = &(skb_shinfo(skb)->frag_list);

	/* move skb->data to ip header from ext header */
	if (skb->data < skb->nh.raw)
		__skb_pull(skb, skb->nh.raw - skb->data);
	while ((tmp_skb = __skb_dequeue(&sk->sk_write_queue)) != NULL) {
		__skb_pull(tmp_skb, skb->h.raw - skb->nh.raw);
		*tail_skb = tmp_skb;
		tail_skb = &(tmp_skb->next);
		skb->len += tmp_skb->len;
		skb->data_len += tmp_skb->len;
#if 0 /* Logically correct, but useless work, ip_fragment() will have to undo */
		skb->truesize += tmp_skb->truesize;
		__sock_put(tmp_skb->sk);
		tmp_skb->destructor = NULL;
		tmp_skb->sk = NULL;
#endif
	}

	ipv6_addr_copy(final_dst, &fl->fl6_dst);
	__skb_pull(skb, skb->h.raw - skb->nh.raw);
	if (opt && opt->opt_flen)
		ipv6_push_frag_opts(skb, opt, &proto);
	if (opt && opt->opt_nflen)
		ipv6_push_nfrag_opts(skb, opt, &proto, &final_dst);

	skb->nh.ipv6h = hdr = (struct ipv6hdr*) skb_push(skb, sizeof(struct ipv6hdr));
	
	*(u32*)hdr = fl->fl6_flowlabel | htonl(0x60000000);

	if (skb->len <= sizeof(struct ipv6hdr) + IPV6_MAXPLEN)
		hdr->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
	else
		hdr->payload_len = 0;
	hdr->hop_limit = np->cork.hop_limit;
	hdr->nexthdr = proto;
	ipv6_addr_copy(&hdr->saddr, &fl->fl6_src);
	ipv6_addr_copy(&hdr->daddr, final_dst);

	skb->dst = dst_clone(&rt->u.dst);
	err = NF_HOOK(PF_INET6, NF_IP6_LOCAL_OUT, skb, NULL, skb->dst->dev, dst_output);
	if (err) {
		if (err > 0)
			err = inet->recverr ? net_xmit_errno(err) : 0;
		if (err)
			goto error;
	}

out:
	inet->cork.flags &= ~IPCORK_OPT;
	if (np->cork.opt) {
		kfree(np->cork.opt);
		np->cork.opt = NULL;
	}
	if (np->cork.rt) {
		dst_release(&np->cork.rt->u.dst);
		np->cork.rt = NULL;
	}
	memset(&inet->cork.fl, 0, sizeof(inet->cork.fl));
	return err;
error:
	goto out;
}

void ip6_flush_pending_frames(struct sock *sk)
{
	struct inet_opt *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sk_buff *skb;

	while ((skb = __skb_dequeue_tail(&sk->sk_write_queue)) != NULL)
		kfree_skb(skb);

	inet->cork.flags &= ~IPCORK_OPT;

	if (np->cork.opt) {
		kfree(np->cork.opt);
		np->cork.opt = NULL;
	}
	if (np->cork.rt) {
		dst_release(&np->cork.rt->u.dst);
		np->cork.rt = NULL;
	}
	memset(&inet->cork.fl, 0, sizeof(inet->cork.fl));
}
