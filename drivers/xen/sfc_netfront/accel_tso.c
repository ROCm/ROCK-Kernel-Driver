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

#include <linux/pci.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/if_ether.h>

#include "accel.h"
#include "accel_util.h"

#include "accel_tso.h"

#define ETH_HDR_LEN(skb)  skb_network_offset(skb)
#define SKB_TCP_OFF(skb)  skb_transport_offset(skb)
#define SKB_IP_OFF(skb)   skb_network_offset(skb)

/*
 * Set a maximum number of buffers in each output packet to make life
 * a little simpler - if this is reached it will just move on to
 * another packet 
 */
#define ACCEL_TSO_MAX_BUFFERS (6)

/** TSO State.
 *
 * The state used during segmentation.  It is put into this data structure
 * just to make it easy to pass into inline functions.
 */
struct netfront_accel_tso_state {
	/** bytes of data we've yet to segment */
	unsigned remaining_len;

	/** current sequence number */
	unsigned seqnum;

	/** remaining space in current packet */
	unsigned packet_space;

	/** List of packets to be output, containing the buffers and
	 *  iovecs to describe each packet 
	 */
	struct netfront_accel_tso_output_packet *output_packets;

	/** Total number of buffers in output_packets */
	unsigned buffers;

	/** Total number of packets in output_packets */
	unsigned packets;

	/** Input Fragment Cursor.
	 *
	 * Where we are in the current fragment of the incoming SKB.  These
	 * values get updated in place when we split a fragment over
	 * multiple packets.
	 */
	struct {
		/** address of current position */
		void *addr;
		/** remaining length */   
		unsigned int len;
	} ifc; /*  == ifc Input Fragment Cursor */

	/** Parameters.
	 *
	 * These values are set once at the start of the TSO send and do
	 * not get changed as the routine progresses.
	 */
	struct {
		/* the number of bytes of header */
		unsigned int header_length;

		/* The number of bytes to put in each outgoing segment. */
		int full_packet_size;
		
		/* Current IP ID, host endian. */
		unsigned ip_id;

		/* Max size of each output packet payload */
		int gso_size;
	} p;
};


/**
 * Verify that our various assumptions about sk_buffs and the conditions
 * under which TSO will be attempted hold true.
 *
 * @v skb	       The sk_buff to check.
 */
static inline void tso_check_safe(struct sk_buff *skb) {
	EPRINTK_ON(skb->protocol != htons (ETH_P_IP));
	EPRINTK_ON(((struct ethhdr*) skb->data)->h_proto != htons (ETH_P_IP));
	EPRINTK_ON(ip_hdr(skb)->protocol != IPPROTO_TCP);
	EPRINTK_ON((SKB_TCP_OFF(skb) + tcp_hdrlen(skb)) > skb_headlen(skb));
}



/** Parse the SKB header and initialise state. */
static inline void tso_start(struct netfront_accel_tso_state *st, 
			     struct sk_buff *skb) {

	/*
	 * All ethernet/IP/TCP headers combined size is TCP header size
	 * plus offset of TCP header relative to start of packet.
 	 */
	st->p.header_length = tcp_hdrlen(skb) + SKB_TCP_OFF(skb);
	st->p.full_packet_size = (st->p.header_length
				  + skb_shinfo(skb)->gso_size);
	st->p.gso_size = skb_shinfo(skb)->gso_size;

	st->p.ip_id = htons(ip_hdr(skb)->id);
	st->seqnum = ntohl(tcp_hdr(skb)->seq);

	EPRINTK_ON(tcp_hdr(skb)->urg);
	EPRINTK_ON(tcp_hdr(skb)->syn);
	EPRINTK_ON(tcp_hdr(skb)->rst);

	st->remaining_len = skb->len - st->p.header_length;

	st->output_packets = NULL;
	st->buffers = 0;
	st->packets = 0;

	VPRINTK("Starting new TSO: hl %d ps %d gso %d seq %x len %d\n",
		st->p.header_length, st->p.full_packet_size, st->p.gso_size,
		st->seqnum, skb->len);
}

/**
 * Add another NIC mapped buffer onto an output packet  
 */ 
static inline int tso_start_new_buffer(netfront_accel_vnic *vnic,
				       struct netfront_accel_tso_state *st,
				       int first)
{
	struct netfront_accel_tso_buffer *tso_buf;
	struct netfront_accel_pkt_desc *buf;

	/* Get a mapped packet buffer */
	buf = netfront_accel_buf_get(vnic->tx_bufs);
	if (buf == NULL) {
		DPRINTK("%s: No buffer for TX\n", __FUNCTION__);
		return -1;
	}

	/* Store a bit of meta-data at the end */
	tso_buf =(struct netfront_accel_tso_buffer *)
		(buf->pkt_kva + NETFRONT_ACCEL_TSO_BUF_LENGTH
		 + sizeof(struct netfront_accel_tso_output_packet));

	tso_buf->buf = buf;

	tso_buf->length = 0;
	
	if (first) {
		struct netfront_accel_tso_output_packet *output_packet 
			= (struct netfront_accel_tso_output_packet *)
			(buf->pkt_kva + NETFRONT_ACCEL_TSO_BUF_LENGTH);
		output_packet->next = st->output_packets;
		st->output_packets = output_packet;
		tso_buf->next = NULL;
		st->output_packets->tso_bufs = tso_buf;
		st->output_packets->tso_bufs_len = 1;
	} else {
		tso_buf->next = st->output_packets->tso_bufs;
		st->output_packets->tso_bufs = tso_buf;
		st->output_packets->tso_bufs_len ++;
	}

	BUG_ON(st->output_packets->tso_bufs_len > ACCEL_TSO_MAX_BUFFERS);
	
	st->buffers ++;

	/*
	 * Store the context, set to NULL, last packet buffer will get
	 * non-NULL later
	 */
	tso_buf->buf->skb = NULL;

	return 0;
}


/* Generate a new header, and prepare for the new packet.
 *
 * @v vnic	      VNIC
 * @v skb	       Socket buffer
 * @v st		TSO state
 * @ret rc	      0 on success, or -1 if failed to alloc header
 */

static inline 
int tso_start_new_packet(netfront_accel_vnic *vnic,
			 struct sk_buff *skb,
			 struct netfront_accel_tso_state *st) 
{
	struct netfront_accel_tso_buffer *tso_buf;
	struct iphdr *tsoh_iph;
	struct tcphdr *tsoh_th;
	unsigned ip_length;

	if (tso_start_new_buffer(vnic, st, 1) < 0) {
		NETFRONT_ACCEL_STATS_OP(vnic->stats.fastpath_tx_busy++);
		return -1;		
	}

	/* This has been set up by tso_start_new_buffer() */
	tso_buf = st->output_packets->tso_bufs;

	/* Copy in the header */
	memcpy(tso_buf->buf->pkt_kva, skb->data, st->p.header_length);
	tso_buf->length = st->p.header_length;

	tsoh_th = (struct tcphdr*) 
		(tso_buf->buf->pkt_kva + SKB_TCP_OFF(skb));
	tsoh_iph = (struct iphdr*) 
		(tso_buf->buf->pkt_kva + SKB_IP_OFF(skb));

	/* Set to zero to encourage falcon to fill these in */
	tsoh_th->check  = 0;
	tsoh_iph->check = 0;

	tsoh_th->seq = htonl(st->seqnum);
	st->seqnum += st->p.gso_size;

	if (st->remaining_len > st->p.gso_size) {
		/* This packet will not finish the TSO burst. */
		ip_length = st->p.full_packet_size - ETH_HDR_LEN(skb);
		tsoh_th->fin = 0;
		tsoh_th->psh = 0;
	} else {
		/* This packet will be the last in the TSO burst. */
		ip_length = (st->p.header_length - ETH_HDR_LEN(skb)
			     + st->remaining_len);
		tsoh_th->fin = tcp_hdr(skb)->fin;
		tsoh_th->psh = tcp_hdr(skb)->psh;
	}

	tsoh_iph->tot_len = htons(ip_length);

	/* Linux leaves suitable gaps in the IP ID space for us to fill. */
	tsoh_iph->id = st->p.ip_id++;
	tsoh_iph->id = htons(tsoh_iph->id);

	st->packet_space = st->p.gso_size; 

	st->packets++;

	return 0;
}



static inline void tso_get_fragment(struct netfront_accel_tso_state *st, 
				    int len, void *addr)
{
	st->ifc.len = len;
	st->ifc.addr = addr;
	return;
}


static inline void tso_unwind(netfront_accel_vnic *vnic, 
			      struct netfront_accel_tso_state *st)
{
	struct netfront_accel_tso_buffer *tso_buf;
	struct netfront_accel_tso_output_packet *output_packet;

	DPRINTK("%s\n", __FUNCTION__);

	while (st->output_packets != NULL) {
		output_packet = st->output_packets;
		st->output_packets = output_packet->next;
		while (output_packet->tso_bufs != NULL) {
			tso_buf = output_packet->tso_bufs;
			output_packet->tso_bufs = tso_buf->next;

			st->buffers --;
			output_packet->tso_bufs_len --;

			netfront_accel_buf_put(vnic->tx_bufs, 
					       tso_buf->buf->buf_id);
		}
	}
	BUG_ON(st->buffers != 0);
}



static inline
void tso_fill_packet_with_fragment(netfront_accel_vnic *vnic,
				   struct netfront_accel_tso_state *st) 
{
	struct netfront_accel_tso_buffer *tso_buf;
	int n, space;

	BUG_ON(st->output_packets == NULL);
	BUG_ON(st->output_packets->tso_bufs == NULL);

	tso_buf = st->output_packets->tso_bufs;

	if (st->ifc.len == 0)  return;
	if (st->packet_space == 0)  return;
	if (tso_buf->length == NETFRONT_ACCEL_TSO_BUF_LENGTH) return;

	n = min(st->ifc.len, st->packet_space);

	space = NETFRONT_ACCEL_TSO_BUF_LENGTH - tso_buf->length;
	n = min(n, space);

	st->packet_space -= n;
	st->remaining_len -= n;
	st->ifc.len -= n;

	memcpy(tso_buf->buf->pkt_kva + tso_buf->length, st->ifc.addr, n);

	tso_buf->length += n;

	BUG_ON(tso_buf->length > NETFRONT_ACCEL_TSO_BUF_LENGTH);

	st->ifc.addr += n;

	return;
}


int netfront_accel_enqueue_skb_tso(netfront_accel_vnic *vnic,
				   struct sk_buff *skb)
{
	struct netfront_accel_tso_state state;
	struct netfront_accel_tso_buffer *tso_buf = NULL;
	struct netfront_accel_tso_output_packet *reversed_list = NULL;
	struct netfront_accel_tso_output_packet	*tmp_pkt;
	ef_iovec iovecs[ACCEL_TSO_MAX_BUFFERS];
	int frag_i, rc, dma_id;
	skb_frag_t *f;

	tso_check_safe(skb);

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		EPRINTK("Trying to TSO send a packet without HW checksum\n");

	tso_start(&state, skb);

	/*
	 * Setup the first payload fragment.  If the skb header area
	 * contains exactly the headers and all payload is in the frag
	 * list things are little simpler
	 */
	if (skb_headlen(skb) == state.p.header_length) {
		/* Grab the first payload fragment. */
		BUG_ON(skb_shinfo(skb)->nr_frags < 1);
		frag_i = 0;
		f = &skb_shinfo(skb)->frags[frag_i];
		tso_get_fragment(&state, f->size, 
				 page_address(f->page) + f->page_offset);
	} else {
		int hl = state.p.header_length;
		tso_get_fragment(&state,  skb_headlen(skb) - hl, 
				 skb->data + hl);
		frag_i = -1;
	}

	if (tso_start_new_packet(vnic, skb, &state) < 0) {
		DPRINTK("%s: out of first start-packet memory\n",
			__FUNCTION__);
		goto unwind;
	}

	while (1) {
		tso_fill_packet_with_fragment(vnic, &state);
		
		/* Move onto the next fragment? */
		if (state.ifc.len == 0) {
			if (++frag_i >= skb_shinfo(skb)->nr_frags)
				/* End of payload reached. */
				break;
			f = &skb_shinfo(skb)->frags[frag_i];
			tso_get_fragment(&state, f->size,
					 page_address(f->page) +
					 f->page_offset);
		}

		/* Start a new buffer? */
		if ((state.output_packets->tso_bufs->length == 
		     NETFRONT_ACCEL_TSO_BUF_LENGTH) &&
		    tso_start_new_buffer(vnic, &state, 0)) {
			DPRINTK("%s: out of start-buffer memory\n",
				__FUNCTION__);
			goto unwind;
		}

		/* Start at new packet? */
		if ((state.packet_space == 0 || 
		     ((state.output_packets->tso_bufs_len >=
		       ACCEL_TSO_MAX_BUFFERS) &&
		      (state.output_packets->tso_bufs->length >= 
		       NETFRONT_ACCEL_TSO_BUF_LENGTH))) &&
		    tso_start_new_packet(vnic, skb, &state) < 0) {
			DPRINTK("%s: out of start-packet memory\n",
				__FUNCTION__);
			goto unwind;
		}

	}

	/* Check for space */
	if (ef_vi_transmit_space(&vnic->vi) < state.buffers) {
		DPRINTK("%s: Not enough TX space (%d)\n",
			__FUNCTION__, state.buffers);
		goto unwind;
	}

	/*
	 * Store the skb context in the most recent buffer (i.e. the
	 * last buffer that will be sent)
	 */
	state.output_packets->tso_bufs->buf->skb = skb;

	/* Reverse the list of packets as we construct it on a stack */
	while (state.output_packets != NULL) {
		tmp_pkt = state.output_packets;
		state.output_packets = tmp_pkt->next;
		tmp_pkt->next = reversed_list;
		reversed_list = tmp_pkt;
	}

	/* Pass off to hardware */
	while (reversed_list != NULL) {
		tmp_pkt = reversed_list;
		reversed_list = tmp_pkt->next;

		BUG_ON(tmp_pkt->tso_bufs_len > ACCEL_TSO_MAX_BUFFERS);
		BUG_ON(tmp_pkt->tso_bufs_len == 0);

		dma_id = tmp_pkt->tso_bufs->buf->buf_id;

		/*
		 * Make an iovec of the buffers in the list, reversing
		 * the buffers as we go as they are constructed on a
		 * stack
		 */
		tso_buf = tmp_pkt->tso_bufs;
		for (frag_i = tmp_pkt->tso_bufs_len - 1;
		     frag_i >= 0;
		     frag_i--) {
			iovecs[frag_i].iov_base = tso_buf->buf->pkt_buff_addr;
			iovecs[frag_i].iov_len = tso_buf->length;
			tso_buf = tso_buf->next;
		}

		rc = ef_vi_transmitv(&vnic->vi, iovecs, tmp_pkt->tso_bufs_len,
				     dma_id);
		/*
		 * We checked for space already, so it really should
		 * succeed
		 */
		BUG_ON(rc != 0);
	}

	/* Track number of tx fastpath stats */
	vnic->netdev_stats.fastpath_tx_bytes += skb->len;
	vnic->netdev_stats.fastpath_tx_pkts += state.packets;
#if NETFRONT_ACCEL_STATS
	{
		unsigned n;
		n = vnic->netdev_stats.fastpath_tx_pkts -
			vnic->stats.fastpath_tx_completions;
		if (n > vnic->stats.fastpath_tx_pending_max)
			vnic->stats.fastpath_tx_pending_max = n;
	}
#endif

	return NETFRONT_ACCEL_STATUS_GOOD;
 
 unwind:
	tso_unwind(vnic, &state);

	NETFRONT_ACCEL_STATS_OP(vnic->stats.fastpath_tx_busy++);

	return NETFRONT_ACCEL_STATUS_BUSY;
}



