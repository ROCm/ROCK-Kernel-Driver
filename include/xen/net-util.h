#ifndef __XEN_NETUTIL_H__
#define __XEN_NETUTIL_H__

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/ip.h>

static inline int skb_checksum_setup(struct sk_buff *skb,
				     unsigned long *fixup_counter)
{
 	struct iphdr *iph = (void *)skb->data;
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

	if (skb->protocol != htons(ETH_P_IP))
		goto out;

	switch (iph->protocol) {
	case IPPROTO_TCP:
		if (!skb_partial_csum_set(skb, 4 * iph->ihl,
					  offsetof(struct tcphdr, check)))
			goto out;
		if (csum)
			csum = &tcp_hdr(skb)->check;
		break;
	case IPPROTO_UDP:
		if (!skb_partial_csum_set(skb, 4 * iph->ihl,
					  offsetof(struct udphdr, check)))
			goto out;
		if (csum)
			csum = &udp_hdr(skb)->check;
		break;
	default:
		net_err_ratelimited("Attempting to checksum a non-TCP/UDP packet,"
				    " dropping a protocol %d packet\n",
				    iph->protocol);
		goto out;
	}

	if (csum) {
		*csum = ~csum_tcpudp_magic(iph->saddr, iph->daddr,
					   skb->len - iph->ihl*4,
					   IPPROTO_TCP, 0);
		skb->ip_summed = CHECKSUM_PARTIAL;
	}

	skb_probe_transport_header(skb, 0);

	err = 0;
out:
	return err;
}

#endif /* __XEN_NETUTIL_H__ */
