#ifndef __XEN_NETUTIL_H__
#define __XEN_NETUTIL_H__

#include <linux/kernel.h>
#include <linux/skbuff.h>

static inline int xennet_checksum_setup(struct sk_buff *skb,
					unsigned long *fixup_counter)
{
	bool recalc = false;
	int err;

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
		recalc = true;
	}

	err = skb_checksum_setup(skb, recalc);
	if (!err)
		skb_probe_transport_header(skb, 0);

	return err;
}

#endif /* __XEN_NETUTIL_H__ */
