#ifndef __XEN_NETUTIL_H__
#define __XEN_NETUTIL_H__

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/ip.h>
#include <net/ip6_checksum.h>

static inline int _maybe_pull_tail(struct sk_buff *skb, unsigned int len,
				   unsigned int max)
{
	if (skb_headlen(skb) >= len)
		return 0;

	/*
	 * If we need to pullup then pullup to the max, so we won't need to
	 * do it again.
	 */
	if (max > skb->len)
		max = skb->len;

	if (!__pskb_pull_tail(skb, max - skb_headlen(skb)))
		return -ENOMEM;

	if (skb_headlen(skb) < len)
		return -EPROTO;

	return 0;
}

/*
 * This value should be large enough to cover a tagged ethernet header plus
 * maximally sized IP and TCP or UDP headers.
 */
#define MAX_IP_HDR_LEN 128

static inline int _checksum_setup_ip(struct sk_buff *skb, __be16 *csum)
{
 	const struct iphdr *iph;
	unsigned int off;
	int err = _maybe_pull_tail(skb, sizeof(*iph), MAX_IP_HDR_LEN);

	if (err)
		return err;
 	iph = ip_hdr(skb);
	if (iph->frag_off & htons(IP_OFFSET | IP_MF)) {
		net_err_ratelimited("%s\n", "Packet is a fragment");
		return -EPROTO;
	}
	off = 4 * iph->ihl;

	switch (iph->protocol) {
	case IPPROTO_TCP:
		err = _maybe_pull_tail(skb, off + sizeof(struct tcphdr),
				       off + sizeof(struct tcphdr));
		if (err || !csum)
			return err;
		if (!skb_partial_csum_set(skb, off,
					  offsetof(struct tcphdr, check)))
			return -EPROTO;
		csum = &tcp_hdr(skb)->check;
		break;
	case IPPROTO_UDP:
		err = _maybe_pull_tail(skb, off + sizeof(struct udphdr),
				       off + sizeof(struct udphdr));
		if (err || !csum)
			return err;
		if (!skb_partial_csum_set(skb, off,
					  offsetof(struct udphdr, check)))
			return -EPROTO;
		csum = &udp_hdr(skb)->check;
		break;
	default:
		net_err_ratelimited("Attempting to checksum a non-TCP/UDP packet,"
				    " dropping a protocol %d packet\n",
				    iph->protocol);
		return -EPROTO;
	}

 	iph = ip_hdr(skb);
	*csum = ~csum_tcpudp_magic(iph->saddr, iph->daddr,
				   skb->len - off, iph->protocol, 0);

	return 0;
}

/*
 * This value should be large enough to cover a tagged ethernet header plus
 * an IPv6 header, all options, and a maximal TCP or UDP header.
 */
#define MAX_IPV6_HDR_LEN 256

static inline int _checksum_setup_ipv6(struct sk_buff *skb, __be16 *csum)
{
#define OPT_HDR(type, skb, off) ((type *)(skb_network_header(skb) + (off)))
	const struct ipv6hdr *ipv6h;
	unsigned int off = sizeof(*ipv6h);
	u8 nexthdr;
	bool done = false, fragment = false;
	int err = _maybe_pull_tail(skb, off, MAX_IPV6_HDR_LEN);

	if (err)
		return err;
	ipv6h = ipv6_hdr(skb);
	nexthdr = ipv6h->nexthdr;

	while ((off <= sizeof(*ipv6h) + ntohs(ipv6h->payload_len)) && !done) {
		switch (nexthdr) {
		case IPPROTO_DSTOPTS:
		case IPPROTO_HOPOPTS:
		case IPPROTO_ROUTING: {
			const struct ipv6_opt_hdr *hp;

			err = _maybe_pull_tail(skb, off + sizeof(*hp),
					       MAX_IPV6_HDR_LEN);
			if (err)
				return err;
			hp = OPT_HDR(struct ipv6_opt_hdr, skb, off);
			nexthdr = hp->nexthdr;
			off += ipv6_optlen(hp);
			break;
		}
		case IPPROTO_AH: {
			const struct ip_auth_hdr *hp;

			err = _maybe_pull_tail(skb, off + sizeof(*hp),
					       MAX_IPV6_HDR_LEN);
			if (err)
				return err;
			hp = OPT_HDR(struct ip_auth_hdr, skb, off);
			nexthdr = hp->nexthdr;
			off += ipv6_authlen(hp);
			break;
		}
		case IPPROTO_FRAGMENT: {
			const struct frag_hdr *hp;

			err = _maybe_pull_tail(skb, off + sizeof(*hp),
					       MAX_IPV6_HDR_LEN);
			if (err < 0)
				return err;
			hp = OPT_HDR(struct frag_hdr, skb, off);
			if (hp->frag_off & htons(IP6_OFFSET | IP6_MF))
				fragment = true;
 			nexthdr = hp->nexthdr;
			off += sizeof(*hp);
			break;
		}
		default:
			done = true;
			break;
		}
		ipv6h = ipv6_hdr(skb);
	}

	if (!done || fragment) {
		net_err_ratelimited("%s\n",
				    done ? "Failed to parse packet header"
					 : "Packet is a v6 fragment");
		return -EPROTO;
	}

	switch (nexthdr) {
	case IPPROTO_TCP:
		err = _maybe_pull_tail(skb, off + sizeof(struct tcphdr),
				       off + sizeof(struct tcphdr));
		if (err || !csum)
			return err;
		if (!skb_partial_csum_set(skb, off,
					  offsetof(struct tcphdr, check)))
			return -EPROTO;
		csum = &tcp_hdr(skb)->check;
		break;
	case IPPROTO_UDP:
		err = _maybe_pull_tail(skb, off + sizeof(struct udphdr),
				       off + sizeof(struct udphdr));
		if (err || !csum)
			return err;
		if (!skb_partial_csum_set(skb, off,
					  offsetof(struct udphdr, check)))
			return -EPROTO;
		csum = &udp_hdr(skb)->check;
		break;
	default:
		net_err_ratelimited("Attempting to checksum a non-TCPv6/UDPv6 packet,"
				    " dropping a protocol %d packet\n",
				    nexthdr);
		return -EPROTO;
	}

	ipv6h = ipv6_hdr(skb);
	*csum = ~csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr,
				 skb->len - off, nexthdr, 0);

	return 0;
#undef OPT_HDR
}

static inline int skb_checksum_setup(struct sk_buff *skb,
				     unsigned long *fixup_counter)
{
	__be16 *csum = NULL;
	int err = -EPROTO;

	skb_reset_network_header(skb);
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		/* A non-CHECKSUM_PARTIAL SKB does not require setup. */
		if (!skb_is_gso(skb))
			return 0;

		/*
		 * A GSO SKB must be CHECKSUM_PARTIAL. However some buggy
		 * peers can fail to set NETRXF_csum_blank when sending a GSO
		 * frame. In this case force the SKB to CHECKSUM_PARTIAL and
		 * recalculate the partial checksum.
		 */
		++*fixup_counter;
		--csum;
	}

	if (skb->protocol == htons(ETH_P_IP))
		err = _checksum_setup_ip(skb, csum);
	else if (skb->protocol == htons(ETH_P_IPV6))
		err = _checksum_setup_ipv6(skb, csum);
	if (!err)
		skb_probe_transport_header(skb, 0);

	return err;
}

#endif /* __XEN_NETUTIL_H__ */
