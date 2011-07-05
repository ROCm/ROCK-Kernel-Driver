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
	unsigned char *th;
	__be16 *csum = NULL;
	int err = -EPROTO;

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

	th = skb->data + 4 * iph->ihl;
	if (th >= skb_tail_pointer(skb))
		goto out;

	skb->csum_start = th - skb->head;
	switch (iph->protocol) {
	case IPPROTO_TCP:
		skb->csum_offset = offsetof(struct tcphdr, check);
		if (csum)
			csum = &((struct tcphdr *)th)->check;
		break;
	case IPPROTO_UDP:
		skb->csum_offset = offsetof(struct udphdr, check);
		if (csum)
			csum = &((struct udphdr *)th)->check;
		break;
	default:
		if (net_ratelimit())
			pr_err("Attempting to checksum a non-"
			       "TCP/UDP packet, dropping a protocol"
			       " %d packet\n", iph->protocol);
		goto out;
	}

	if ((th + skb->csum_offset + sizeof(*csum)) > skb_tail_pointer(skb))
		goto out;

	if (csum) {
		*csum = ~csum_tcpudp_magic(iph->saddr, iph->daddr,
					   skb->len - iph->ihl*4,
					   IPPROTO_TCP, 0);
		skb->ip_summed = CHECKSUM_PARTIAL;
	}

	err = 0;
out:
	return err;
}

#endif /* __XEN_NETUTIL_H__ */
