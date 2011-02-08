#ifndef __XEN_NETUTIL_H__
#define __XEN_NETUTIL_H__

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/ip.h>

static inline int skb_checksum_setup(struct sk_buff *skb)
{
	struct iphdr *iph;
	unsigned char *th;
	int err = -EPROTO;

	if (skb->protocol != htons(ETH_P_IP))
		goto out;

	iph = (void *)skb->data;
	th = skb->data + 4 * iph->ihl;
	if (th >= skb_tail_pointer(skb))
		goto out;

	skb->csum_start = th - skb->head;
	switch (iph->protocol) {
	case IPPROTO_TCP:
		skb->csum_offset = offsetof(struct tcphdr, check);
		break;
	case IPPROTO_UDP:
		skb->csum_offset = offsetof(struct udphdr, check);
		break;
	default:
		if (net_ratelimit())
			pr_err("Attempting to checksum a non-"
			       "TCP/UDP packet, dropping a protocol"
			       " %d packet\n", iph->protocol);
		goto out;
	}

	if ((th + skb->csum_offset + 2) > skb_tail_pointer(skb))
		goto out;

	err = 0;
out:
	return err;
}

#endif /* __XEN_NETUTIL_H__ */
