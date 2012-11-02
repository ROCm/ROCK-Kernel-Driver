/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#ifndef NETFRONT_ACCEL_SSR_H
#define NETFRONT_ACCEL_SSR_H

#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/list.h>

#include "accel.h"

/** State for Soft Segment Reassembly (SSR). */

struct netfront_accel_ssr_conn {
	struct list_head link;

	unsigned saddr, daddr;
	unsigned short source, dest;

	/** Number of in-order packets we've seen with payload. */
	unsigned n_in_order_pkts;

	/** Next in-order sequence number. */
	unsigned next_seq;

	/** Time we last saw a packet on this connection. */
	unsigned long last_pkt_jiffies;

	/** The SKB we are currently holding.  If NULL, then all following
	 * fields are undefined.
	 */
	struct sk_buff *skb;

	/** The tail of the frag_list of SKBs we're holding.  Only valid
	 * after at least one merge.
	 */
	struct sk_buff *skb_tail;

	/** The IP header of the skb we are holding. */
	struct iphdr *iph;
	
	/** The TCP header of the skb we are holding. */
	struct tcphdr *th;
};

extern void netfront_accel_ssr_init(struct netfront_accel_ssr_state *st);
extern void netfront_accel_ssr_fini(netfront_accel_vnic *vnic,
				    struct netfront_accel_ssr_state *st);

extern void
__netfront_accel_ssr_end_of_burst(netfront_accel_vnic *vnic,
				  struct netfront_accel_ssr_state *st);

extern int  netfront_accel_ssr_skb(netfront_accel_vnic *vnic,
				   struct netfront_accel_ssr_state *st,
				   struct sk_buff *skb);

static inline void
netfront_accel_ssr_end_of_burst (netfront_accel_vnic *vnic,
				 struct netfront_accel_ssr_state *st) {
	if ( ! list_empty(&st->conns) )
		__netfront_accel_ssr_end_of_burst(vnic, st);
}

#endif /* NETFRONT_ACCEL_SSR_H */
