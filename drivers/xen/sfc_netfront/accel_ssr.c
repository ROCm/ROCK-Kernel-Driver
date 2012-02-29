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

#include <linux/socket.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/list.h>
#include <net/ip.h>
#include <net/checksum.h>

#include "accel.h"
#include "accel_util.h"
#include "accel_bufs.h"

#include "accel_ssr.h"

static inline int list_valid(struct list_head *lh) {
	return(lh->next != NULL);
}

static void netfront_accel_ssr_deliver (struct netfront_accel_vnic *vnic,
					struct netfront_accel_ssr_state *st,
					struct netfront_accel_ssr_conn *c);

/** Construct an efx_ssr_state.
 *
 * @v st     The SSR state (per channel per port)
 * @v port   The port.
 */
void netfront_accel_ssr_init(struct netfront_accel_ssr_state *st) {
	unsigned i;

	INIT_LIST_HEAD(&st->conns);
	INIT_LIST_HEAD(&st->free_conns);
	for (i = 0; i < 8; ++i) {
		struct netfront_accel_ssr_conn *c = 
			kmalloc(sizeof(*c), GFP_KERNEL);
		if (c == NULL)  break;
		c->n_in_order_pkts = 0;
		c->skb = NULL;
		list_add(&c->link, &st->free_conns);
	}

}


/** Destructor for an efx_ssr_state.
 *
 * @v st     The SSR state (per channel per port)
 */
void netfront_accel_ssr_fini(netfront_accel_vnic *vnic, 
			     struct netfront_accel_ssr_state *st) {
	struct netfront_accel_ssr_conn *c;

	/* Return cleanly if efx_ssr_init() not previously called */
	BUG_ON(list_valid(&st->conns) != list_valid(&st->free_conns));
	if (! list_valid(&st->conns))
		return;

	while ( ! list_empty(&st->free_conns)) {
		c = list_entry(st->free_conns.prev, 
			       struct netfront_accel_ssr_conn, link);
		list_del(&c->link);
		BUG_ON(c->skb != NULL);
		kfree(c);
	}
	while ( ! list_empty(&st->conns)) {
		c = list_entry(st->conns.prev, 
			       struct netfront_accel_ssr_conn, link);
		list_del(&c->link);
		if (c->skb)
			netfront_accel_ssr_deliver(vnic, st, c);
		kfree(c);
	}
}


/** Calc IP checksum and deliver to the OS
 *
 * @v st     The SSR state (per channel per port)
 * @v c	     The SSR connection state
 */
static void netfront_accel_ssr_deliver(netfront_accel_vnic *vnic,
				       struct netfront_accel_ssr_state *st,
				       struct netfront_accel_ssr_conn *c) {
	BUG_ON(c->skb == NULL);

	/*
	 * If we've chained packets together, recalculate the IP
	 * checksum.
	 */
	if (skb_shinfo(c->skb)->frag_list) {
		NETFRONT_ACCEL_STATS_OP(++vnic->stats.ssr_bursts);
		c->iph->check = 0;
		c->iph->check = ip_fast_csum((unsigned char *) c->iph, 
					     c->iph->ihl);
	}

	VPRINTK("%s: %d\n", __FUNCTION__, c->skb->len);

	netif_receive_skb(c->skb); 
	c->skb = NULL;
}


/** Push held skbs down into network stack.
 *
 * @v st       SSR state
 *
 * Only called if we are tracking one or more connections.
 */
void __netfront_accel_ssr_end_of_burst(netfront_accel_vnic *vnic, 
				       struct netfront_accel_ssr_state *st) {
	struct netfront_accel_ssr_conn *c;

	BUG_ON(list_empty(&st->conns));

	list_for_each_entry(c, &st->conns, link)
		if (c->skb)
			netfront_accel_ssr_deliver(vnic, st, c);

	/* Time-out connections that have received no traffic for 20ms. */
	c = list_entry(st->conns.prev, struct netfront_accel_ssr_conn,
		       link);
	if (jiffies - c->last_pkt_jiffies > (HZ / 50 + 1)) {
		NETFRONT_ACCEL_STATS_OP(++vnic->stats.ssr_drop_stream);
		list_del(&c->link);
		list_add(&c->link, &st->free_conns);
	}
}


/** Process SKB and decide whether to dispatch it to the stack now or
 * later.
 *
 * @v st	 SSR state
 * @v skb	SKB to exmaine
 * @ret rc       0 => deliver SKB to kernel now, otherwise the SKB belongs
 *	       us.
 */
int netfront_accel_ssr_skb(struct netfront_accel_vnic *vnic,
			   struct netfront_accel_ssr_state *st,
			   struct sk_buff *skb) {
	int data_length, dont_merge;
	struct netfront_accel_ssr_conn *c;
	struct iphdr *iph;
	struct tcphdr *th;
	unsigned th_seq;

	BUG_ON(skb_shinfo(skb)->frag_list != NULL);
	BUG_ON(skb->next != NULL);

	/* We're not interested if it isn't TCP over IPv4. */
	iph = (struct iphdr *) skb->data;
	if (skb->protocol != htons(ETH_P_IP) ||
	    iph->protocol != IPPROTO_TCP) {
		return 0;
	}

	/* Ignore segments that fail csum or are fragmented. */
	if (unlikely((skb->ip_summed - CHECKSUM_UNNECESSARY) |
		     (iph->frag_off & htons(IP_MF | IP_OFFSET)))) {
		return 0;
	}

	th = (struct tcphdr*)(skb->data + iph->ihl * 4);
	data_length = ntohs(iph->tot_len) - iph->ihl * 4 - th->doff * 4;
	th_seq = ntohl(th->seq);
	dont_merge = (data_length == 0) | th->urg | th->syn | th->rst;

	list_for_each_entry(c, &st->conns, link) {
		if ((c->saddr  - iph->saddr) |
		    (c->daddr  - iph->daddr) |
		    (c->source - th->source) |
		    (c->dest   - th->dest  ))
			continue;

		/* Re-insert at head of list to reduce lookup time. */
		list_del(&c->link);
		list_add(&c->link, &st->conns);
		c->last_pkt_jiffies = jiffies;

		if (unlikely(th_seq - c->next_seq)) {
			/* Out-of-order, so start counting again. */
			if (c->skb)
				netfront_accel_ssr_deliver(vnic, st, c);
			c->n_in_order_pkts = 0;
			c->next_seq = th_seq + data_length;
			NETFRONT_ACCEL_STATS_OP(++vnic->stats.ssr_misorder);
			return 0;
		}
		c->next_seq = th_seq + data_length;

		if (++c->n_in_order_pkts < 300) {
			/* May be in slow-start, so don't merge. */
			NETFRONT_ACCEL_STATS_OP(++vnic->stats.ssr_slow_start);
			return 0;
		}

		if (unlikely(dont_merge)) {
			if (c->skb)
				netfront_accel_ssr_deliver(vnic, st, c);
			return 0;
		}

		if (c->skb) {
			c->iph->tot_len = ntohs(c->iph->tot_len);
			c->iph->tot_len += data_length;
			c->iph->tot_len = htons(c->iph->tot_len);
			c->th->ack_seq = th->ack_seq;
			c->th->fin |= th->fin;
			c->th->psh |= th->psh;
			c->th->window = th->window;

			/* Remove the headers from this skb. */
			skb_pull(skb, skb->len - data_length);

			/*
			 * Tack the new skb onto the head skb's frag_list.
			 * This is exactly the format that fragmented IP
			 * datagrams are reassembled into.
			 */
			BUG_ON(skb->next != 0);
			if ( ! skb_shinfo(c->skb)->frag_list)
				skb_shinfo(c->skb)->frag_list = skb;
			else
				c->skb_tail->next = skb;
			c->skb_tail = skb;
			c->skb->len += skb->len;
			c->skb->data_len += skb->len;
			c->skb->truesize += skb->truesize;

			NETFRONT_ACCEL_STATS_OP(++vnic->stats.ssr_merges);

			/*
			 * If the next packet might push this super-packet
			 * over the limit for an IP packet, deliver it now.
			 * This is slightly conservative, but close enough.
			 */
			if (c->skb->len + 
			    (PAGE_SIZE / NETFRONT_ACCEL_BUFS_PER_PAGE)
			    > 16384)
				netfront_accel_ssr_deliver(vnic, st, c);

			return 1;
		}
		else {
			c->iph = iph;
			c->th = th;
			c->skb = skb;
			return 1;
		}
	}

	/* We're not yet tracking this connection. */

	if (dont_merge) {
		return 0;
	}

	if (list_empty(&st->free_conns)) {
		c = list_entry(st->conns.prev, 
			       struct netfront_accel_ssr_conn,
			       link);
		if (c->skb) {
			NETFRONT_ACCEL_STATS_OP(++vnic->stats.ssr_too_many);
			return 0;
		}
	}
	else {
		c = list_entry(st->free_conns.next,
			       struct netfront_accel_ssr_conn,
			       link);
	}
	list_del(&c->link);
	list_add(&c->link, &st->conns);
	c->saddr = iph->saddr;
	c->daddr = iph->daddr;
	c->source = th->source;
	c->dest = th->dest;
	c->next_seq = th_seq + data_length;
	c->n_in_order_pkts = 0;
	BUG_ON(c->skb != NULL);
	NETFRONT_ACCEL_STATS_OP(++vnic->stats.ssr_new_stream);
	return 0;
}
