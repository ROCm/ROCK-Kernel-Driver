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

#include <linux/if_ether.h>
#include <linux/ip.h>
#include <net/checksum.h>
#include <asm/io.h>

#include "accel.h"
#include "accel_util.h"
#include "accel_bufs.h"
#include "accel_tso.h"
#include "accel_ssr.h"
#include "netfront.h"

#include "etherfabric/ef_vi.h"

/*
 * Max available space in a buffer for data once meta-data has taken
 * its place
 */
#define NETFRONT_ACCEL_TX_BUF_LENGTH					\
	((PAGE_SIZE / NETFRONT_ACCEL_BUFS_PER_PAGE)			\
	 - sizeof(struct netfront_accel_tso_buffer))

#define ACCEL_TX_MAX_BUFFERS (6)
#define ACCEL_VI_POLL_EVENTS (8)

static
int netfront_accel_vi_init_fini(netfront_accel_vnic *vnic, 
				struct net_accel_msg_hw *hw_msg)
{
	struct ef_vi_nic_type nic_type;
	struct net_accel_hw_falcon_b *hw_info;
	void *io_kva, *evq_base, *rx_dma_kva, *tx_dma_kva, *doorbell_kva;
	u32 *evq_gnts;
	u32 evq_order;
	int vi_state_size;
	u8 vi_data[VI_MAPPINGS_SIZE];

	if (hw_msg == NULL)
		goto fini;

	/* And create the local macs table lock */
	spin_lock_init(&vnic->table_lock);
	
	/* Create fastpath table, initial size 8, key length 8 */
	if (cuckoo_hash_init(&vnic->fastpath_table, 3, 8)) {
		EPRINTK("failed to allocate fastpath table\n");
		goto fail_cuckoo;
	}

	vnic->hw.falcon.type = hw_msg->type;

	switch (hw_msg->type) {
	case NET_ACCEL_MSG_HWTYPE_FALCON_A:
		hw_info = &hw_msg->resources.falcon_a.common;
		/* Need the extra rptr register page on A1 */
		io_kva = net_accel_map_iomem_page
			(vnic->dev, hw_msg->resources.falcon_a.evq_rptr_gnt,
			 &vnic->hw.falcon.evq_rptr_mapping);
		if (io_kva == NULL) {
			EPRINTK("%s: evq_rptr permission failed\n", __FUNCTION__);
			goto evq_rptr_fail;
		}

		vnic->hw.falcon.evq_rptr = io_kva + 
			(hw_info->evq_rptr & (PAGE_SIZE - 1));
		break;
	case NET_ACCEL_MSG_HWTYPE_FALCON_B:
	case NET_ACCEL_MSG_HWTYPE_SIENA_A:
		hw_info = &hw_msg->resources.falcon_b;
		break;
	default:
		goto bad_type;
	}

	/**** Event Queue ****/

	/* Map the event queue pages */
	evq_gnts = hw_info->evq_mem_gnts;
	evq_order = hw_info->evq_order;

	EPRINTK_ON(hw_info->evq_offs != 0);

	DPRINTK("Will map evq %d pages\n", 1 << evq_order);

	evq_base =
		net_accel_map_grants_contig(vnic->dev, evq_gnts, 1 << evq_order,
					    &vnic->evq_mapping);
	if (evq_base == NULL) {
		EPRINTK("%s: evq_base failed\n", __FUNCTION__);
		goto evq_fail;
	}

	/**** Doorbells ****/
	/* Set up the doorbell mappings. */
	doorbell_kva = 
		net_accel_map_iomem_page(vnic->dev, hw_info->doorbell_gnt,
					 &vnic->hw.falcon.doorbell_mapping);
	if (doorbell_kva == NULL) {
		EPRINTK("%s: doorbell permission failed\n", __FUNCTION__);
		goto doorbell_fail;
	}
	vnic->hw.falcon.doorbell = doorbell_kva;

	/* On Falcon_B and Siena we get the rptr from the doorbell page */
	if (hw_msg->type == NET_ACCEL_MSG_HWTYPE_FALCON_B ||
	    hw_msg->type == NET_ACCEL_MSG_HWTYPE_SIENA_A) {
		vnic->hw.falcon.evq_rptr = 
			(u32 *)((char *)vnic->hw.falcon.doorbell 
				+ hw_info->evq_rptr);
	}

	/**** DMA Queue ****/

	/* Set up the DMA Queues from the message. */
	tx_dma_kva = net_accel_map_grants_contig
		(vnic->dev, &(hw_info->txdmaq_gnt), 1, 
		 &vnic->hw.falcon.txdmaq_mapping);
	if (tx_dma_kva == NULL) {
		EPRINTK("%s: TX dma failed\n", __FUNCTION__);
		goto tx_dma_fail;
	}

	rx_dma_kva = net_accel_map_grants_contig
		(vnic->dev, &(hw_info->rxdmaq_gnt), 1, 
		 &vnic->hw.falcon.rxdmaq_mapping);
	if (rx_dma_kva == NULL) {
		EPRINTK("%s: RX dma failed\n", __FUNCTION__);
		goto rx_dma_fail;
	}

	/* Full confession */
	DPRINTK("Mapped H/W"
		"  Tx DMAQ grant %x -> %p\n"
		"  Rx DMAQ grant %x -> %p\n"
		"  EVQ grant %x -> %p\n",
		hw_info->txdmaq_gnt, tx_dma_kva,
		hw_info->rxdmaq_gnt, rx_dma_kva,
		evq_gnts[0], evq_base
		);

	memset(vi_data, 0, sizeof(vi_data));
	
	/* TODO BUG11305: convert efhw_arch to ef_vi_arch
	 * e.g.
	 * arch = ef_vi_arch_from_efhw_arch(hw_info->nic_arch);
	 * assert(arch >= 0);
	 * nic_type.arch = arch;
	 */
	nic_type.arch = (unsigned char)hw_info->nic_arch;
	nic_type.variant = (char)hw_info->nic_variant;
	nic_type.revision = (unsigned char)hw_info->nic_revision;
	
	ef_vi_init_mapping_evq(vi_data, nic_type, hw_info->instance, 
			       1 << (evq_order + PAGE_SHIFT), evq_base, 
			       (void *)0xdeadbeef);

	ef_vi_init_mapping_vi(vi_data, nic_type, hw_info->rx_capacity, 
			      hw_info->tx_capacity, hw_info->instance, 
			      doorbell_kva, rx_dma_kva, tx_dma_kva, 0);

	vi_state_size = ef_vi_calc_state_bytes(hw_info->rx_capacity,
					       hw_info->tx_capacity);
	vnic->vi_state = (ef_vi_state *)kmalloc(vi_state_size, GFP_KERNEL);
	if (vnic->vi_state == NULL) {
		EPRINTK("%s: kmalloc for VI state failed\n", __FUNCTION__);
		goto vi_state_fail;
	}
	ef_vi_init(&vnic->vi, vi_data, vnic->vi_state, &vnic->evq_state, 0);

	ef_eventq_state_init(&vnic->vi);

	ef_vi_state_init(&vnic->vi);

	return 0;

fini:
	kfree(vnic->vi_state);
	vnic->vi_state = NULL;
vi_state_fail:
	net_accel_unmap_grants_contig(vnic->dev, vnic->hw.falcon.rxdmaq_mapping);
rx_dma_fail:
	net_accel_unmap_grants_contig(vnic->dev, vnic->hw.falcon.txdmaq_mapping);
tx_dma_fail:
	net_accel_unmap_iomem_page(vnic->dev, vnic->hw.falcon.doorbell_mapping);
	vnic->hw.falcon.doorbell = NULL;
doorbell_fail:
	net_accel_unmap_grants_contig(vnic->dev, vnic->evq_mapping);
evq_fail:
	if (vnic->hw.falcon.type == NET_ACCEL_MSG_HWTYPE_FALCON_A)
		net_accel_unmap_iomem_page(vnic->dev, 
					   vnic->hw.falcon.evq_rptr_mapping);
	vnic->hw.falcon.evq_rptr = NULL;
evq_rptr_fail:
bad_type:
	cuckoo_hash_destroy(&vnic->fastpath_table);
fail_cuckoo:
	return -EIO;
}


void netfront_accel_vi_ctor(netfront_accel_vnic *vnic)
{
	/* Just mark the VI as uninitialised. */
	vnic->vi_state = NULL;
}


int netfront_accel_vi_init(netfront_accel_vnic *vnic, struct net_accel_msg_hw *hw_msg)
{
	BUG_ON(hw_msg == NULL);
	return netfront_accel_vi_init_fini(vnic, hw_msg);
}


void netfront_accel_vi_dtor(netfront_accel_vnic *vnic)
{
	if (vnic->vi_state != NULL)
		netfront_accel_vi_init_fini(vnic, NULL);
}


static
void netfront_accel_vi_post_rx(netfront_accel_vnic *vnic, u16 id,
			       netfront_accel_pkt_desc *buf)
{

	int idx = vnic->rx_dma_batched;

#if 0
	VPRINTK("Posting buffer %d (0x%08x) for rx at index %d, space is %d\n",
		id, buf->pkt_buff_addr, idx, ef_vi_receive_space(&vnic->vi));
#endif
	/* Set up a virtual buffer descriptor */
	ef_vi_receive_init(&vnic->vi, buf->pkt_buff_addr, id,
			   /*rx_bytes=max*/0);

	idx++;

	vnic->rx_dma_level++;
	
	/* 
	 * Only push the descriptor to the card if we've reached the
	 * batch size.  Otherwise, the descriptors can sit around for
	 * a while.  There will be plenty available.
	 */
	if (idx >= NETFRONT_ACCEL_RX_DESC_BATCH ||
	    vnic->rx_dma_level < NETFRONT_ACCEL_RX_DESC_BATCH) {
#if 0
		VPRINTK("Flushing %d rx descriptors.\n", idx);
#endif

		/* Push buffer to hardware */
		ef_vi_receive_push(&vnic->vi);
		
		idx = 0;
	}
	
	vnic->rx_dma_batched = idx;
}


inline
void netfront_accel_vi_post_rx_or_free(netfront_accel_vnic *vnic, u16 id,
				       netfront_accel_pkt_desc *buf)
{

	VPRINTK("%s: %d\n", __FUNCTION__, id);

	if (ef_vi_receive_space(&vnic->vi) <= vnic->rx_dma_batched) {
		VPRINTK("RX space is full\n");
		netfront_accel_buf_put(vnic->rx_bufs, id);
		return;
	}

	VPRINTK("Completed buffer %d is reposted\n", id);
	netfront_accel_vi_post_rx(vnic, id, buf);
	
	/*
	 * Let's see if there's any more to be pushed out to the NIC
	 * while we're here
	 */
	while (ef_vi_receive_space(&vnic->vi) > vnic->rx_dma_batched) {
		/* Try to allocate a buffer. */
		buf = netfront_accel_buf_get(vnic->rx_bufs);
		if (buf == NULL)
			break;
		
		/* Add it to the rx dma queue. */
		netfront_accel_vi_post_rx(vnic, buf->buf_id, buf);	
	}
}


void netfront_accel_vi_add_bufs(netfront_accel_vnic *vnic, int is_rx)
{

	while (is_rx && 
	       ef_vi_receive_space(&vnic->vi) > vnic->rx_dma_batched) {
		netfront_accel_pkt_desc *buf;
		
		VPRINTK("%s: %d\n", __FUNCTION__, vnic->rx_dma_level);
		
		/* Try to allocate a buffer. */
		buf = netfront_accel_buf_get(vnic->rx_bufs);

		if (buf == NULL)
			break;
		
		/* Add it to the rx dma queue. */
		netfront_accel_vi_post_rx(vnic, buf->buf_id, buf);
	}

	VPRINTK("%s: done\n", __FUNCTION__);
}


struct netfront_accel_multi_state {
	unsigned remaining_len;

	unsigned buffers;

	struct netfront_accel_tso_buffer *output_buffers;

	/* Where we are in the current fragment of the SKB. */
	struct {
		/* address of current position */
		void *addr;
		/* remaining length */	  
		unsigned int len;
	} ifc; /*  == Input Fragment Cursor */
};


static inline void multi_post_start(struct netfront_accel_multi_state *st, 
				    struct sk_buff *skb)
{
	st->remaining_len = skb->len;
	st->output_buffers = NULL;
	st->buffers = 0;
	st->ifc.len = skb_headlen(skb);
	st->ifc.addr = skb->data;
}

static int multi_post_start_new_buffer(netfront_accel_vnic *vnic, 
				       struct netfront_accel_multi_state *st)
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
	tso_buf = (struct netfront_accel_tso_buffer *)
		(buf->pkt_kva + NETFRONT_ACCEL_TX_BUF_LENGTH);

	tso_buf->buf = buf;

	tso_buf->length = 0;
	
	tso_buf->next = st->output_buffers;
	st->output_buffers = tso_buf;
	st->buffers++;

	BUG_ON(st->buffers >= ACCEL_TX_MAX_BUFFERS);

	/*
	 * Store the context, set to NULL, last packet buffer will get
	 * non-NULL later
	 */
	tso_buf->buf->skb = NULL;
	
	return 0;
}


static void
multi_post_fill_buffer_with_fragment(netfront_accel_vnic *vnic,
				     struct netfront_accel_multi_state *st)
{
	struct netfront_accel_tso_buffer *tso_buf;
	unsigned n, space;

	BUG_ON(st->output_buffers == NULL);
	tso_buf = st->output_buffers;

	if (st->ifc.len == 0) return;
	if (tso_buf->length == NETFRONT_ACCEL_TX_BUF_LENGTH) return;

	BUG_ON(tso_buf->length > NETFRONT_ACCEL_TX_BUF_LENGTH);

	space = NETFRONT_ACCEL_TX_BUF_LENGTH - tso_buf->length;
	n = min(st->ifc.len, space);

	memcpy(tso_buf->buf->pkt_kva + tso_buf->length, st->ifc.addr, n);

	st->remaining_len -= n;
	st->ifc.len -= n;
	tso_buf->length += n;
	st->ifc.addr += n;

	BUG_ON(tso_buf->length > NETFRONT_ACCEL_TX_BUF_LENGTH);

	return;
}


static inline void multi_post_unwind(netfront_accel_vnic *vnic,
				     struct netfront_accel_multi_state *st)
{
	struct netfront_accel_tso_buffer *tso_buf;

	DPRINTK("%s\n", __FUNCTION__);

	while (st->output_buffers != NULL) {
		tso_buf = st->output_buffers;
		st->output_buffers = tso_buf->next;
		st->buffers--;
		netfront_accel_buf_put(vnic->tx_bufs, tso_buf->buf->buf_id);
	}
	BUG_ON(st->buffers != 0);
}


static enum netfront_accel_post_status
netfront_accel_enqueue_skb_multi(netfront_accel_vnic *vnic, struct sk_buff *skb)
{
	struct netfront_accel_tso_buffer *tso_buf;
	struct netfront_accel_multi_state state;
	ef_iovec iovecs[ACCEL_TX_MAX_BUFFERS];
	skb_frag_t *f;
	int frag_i, rc, dma_id;

	multi_post_start(&state, skb);

	frag_i = -1;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		/* Set to zero to encourage falcon to work it out for us */
		*(u16*)(skb->head + skb->csum_start + skb->csum_offset) = 0;
	}

	if (multi_post_start_new_buffer(vnic, &state)) {
		DPRINTK("%s: out of buffers\n", __FUNCTION__);
		goto unwind;
	}

	while (1) {
		multi_post_fill_buffer_with_fragment(vnic, &state);

		/* Move onto the next fragment? */
		if (state.ifc.len == 0) {
			if (++frag_i >= skb_shinfo(skb)->nr_frags)
				/* End of payload reached. */
				break;
			f = &skb_shinfo(skb)->frags[frag_i];
			state.ifc.len = f->size;
			state.ifc.addr = page_address(f->page) + f->page_offset;
		}

		/* Start a new buffer? */
		if ((state.output_buffers->length == 
		     NETFRONT_ACCEL_TX_BUF_LENGTH) &&
		    multi_post_start_new_buffer(vnic, &state)) {
			DPRINTK("%s: out of buffers\n", __FUNCTION__);
			goto unwind;
		}
	}

	/* Check for space */
	if (ef_vi_transmit_space(&vnic->vi) < state.buffers) {
		DPRINTK("%s: Not enough TX space (%d)\n", __FUNCTION__, state.buffers);
		goto unwind;
	}

	/* Store the skb in what will be the last buffer's context */
	state.output_buffers->buf->skb = skb;
	/* Remember dma_id of what will be the last buffer */ 
	dma_id = state.output_buffers->buf->buf_id;

	/*
	 * Make an iovec of the buffers in the list, reversing the
	 * buffers as we go as they are constructed on a stack
	 */
	tso_buf = state.output_buffers;
	for (frag_i = state.buffers-1; frag_i >= 0; frag_i--) {
		iovecs[frag_i].iov_base = tso_buf->buf->pkt_buff_addr;
		iovecs[frag_i].iov_len = tso_buf->length;
		tso_buf = tso_buf->next;
	}
	
	rc = ef_vi_transmitv(&vnic->vi, iovecs, state.buffers, dma_id);

	/* Track number of tx fastpath stats */
	vnic->netdev_stats.fastpath_tx_bytes += skb->len;
	vnic->netdev_stats.fastpath_tx_pkts ++;
#if NETFRONT_ACCEL_STATS
	{
		u32 n;
		n = vnic->netdev_stats.fastpath_tx_pkts -
			(u32)vnic->stats.fastpath_tx_completions;
		if (n > vnic->stats.fastpath_tx_pending_max)
			vnic->stats.fastpath_tx_pending_max = n;
	}
#endif
	return NETFRONT_ACCEL_STATUS_GOOD;

unwind:
	multi_post_unwind(vnic, &state);

	NETFRONT_ACCEL_STATS_OP(vnic->stats.fastpath_tx_busy++);

	return NETFRONT_ACCEL_STATUS_BUSY;
}


static enum netfront_accel_post_status 
netfront_accel_enqueue_skb_single(netfront_accel_vnic *vnic, struct sk_buff *skb)
{
	struct netfront_accel_tso_buffer *tso_buf;
	struct netfront_accel_pkt_desc *buf;
	u8 *kva;
	int rc;

	if (ef_vi_transmit_space(&vnic->vi) < 1) {
		DPRINTK("%s: No TX space\n", __FUNCTION__);
		NETFRONT_ACCEL_STATS_OP(vnic->stats.fastpath_tx_busy++);
		return NETFRONT_ACCEL_STATUS_BUSY;
	}

	buf = netfront_accel_buf_get(vnic->tx_bufs);
	if (buf == NULL) {
		DPRINTK("%s: No buffer for TX\n", __FUNCTION__);
		NETFRONT_ACCEL_STATS_OP(vnic->stats.fastpath_tx_busy++);
		return NETFRONT_ACCEL_STATUS_BUSY;
	}

	/* Track number of tx fastpath stats */
	vnic->netdev_stats.fastpath_tx_pkts++;
	vnic->netdev_stats.fastpath_tx_bytes += skb->len;

#if NETFRONT_ACCEL_STATS
	{
		u32 n;
		n = vnic->netdev_stats.fastpath_tx_pkts - 
			(u32)vnic->stats.fastpath_tx_completions;
		if (n > vnic->stats.fastpath_tx_pending_max)
			vnic->stats.fastpath_tx_pending_max = n;
	}
#endif
	
	/* Store the context */
	buf->skb = skb;
	
	kva = buf->pkt_kva;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		/* Set to zero to encourage falcon to work it out for us */
		*(u16*)(skb->head + skb->csum_start + skb->csum_offset) = 0;
	}
	NETFRONT_ACCEL_PKTBUFF_FOR_EACH_FRAGMENT
		(skb, idx, frag_data, frag_len, {
			/* Copy in payload */
			VPRINTK("*** Copying %d bytes to %p\n", frag_len, kva);
			memcpy(kva, frag_data, frag_len);
			kva += frag_len;
		});

	VPRINTK("%s: id %d pkt %p kva %p buff_addr 0x%08x\n", __FUNCTION__,
		buf->buf_id, buf, buf->pkt_kva, buf->pkt_buff_addr);


	/* Set up the TSO meta-data for a single buffer/packet */
	tso_buf = (struct netfront_accel_tso_buffer *)
		(buf->pkt_kva + NETFRONT_ACCEL_TX_BUF_LENGTH);
	tso_buf->next = NULL;
	tso_buf->buf = buf;
	tso_buf->length = skb->len;

	rc = ef_vi_transmit(&vnic->vi, buf->pkt_buff_addr, skb->len,
			    buf->buf_id);
	/* We checked for space already, so it really should succeed */
	BUG_ON(rc != 0);

	return NETFRONT_ACCEL_STATUS_GOOD;
}


enum netfront_accel_post_status 
netfront_accel_vi_tx_post(netfront_accel_vnic *vnic, struct sk_buff *skb)
{
	struct ethhdr *pkt_eth_hdr;
	struct iphdr *pkt_ipv4_hdr;
	int value, try_fastpath;

	/*
	 * This assumes that the data field points to the dest mac
	 * address.
	 */
	cuckoo_hash_mac_key key = cuckoo_mac_to_key(skb->data);

	/*
	 * NB very important that all things that could return "CANT"
	 * are tested before things that return "BUSY" as if it it
	 * returns "BUSY" it is assumed that it won't return "CANT"
	 * next time it is tried
	 */

	/*
	 * Do a fastpath send if fast path table lookup returns true.
	 * We do this without the table lock and so may get the wrong
	 * answer, but current opinion is that's not a big problem 
	 */
	try_fastpath = cuckoo_hash_lookup(&vnic->fastpath_table, 
					  (cuckoo_hash_key *)(&key), &value);

	if (!try_fastpath) {
		VPRINTK("try fast path false for mac: %pM\n", skb->data);
		
		return NETFRONT_ACCEL_STATUS_CANT;
	}

	/* Check to see if the packet can be sent. */
	if (skb_headlen(skb) < sizeof(*pkt_eth_hdr) + sizeof(*pkt_ipv4_hdr)) {
		EPRINTK("%s: Packet header is too small\n", __FUNCTION__);
		return NETFRONT_ACCEL_STATUS_CANT;
	}

	pkt_eth_hdr  = (void*)skb->data;
	pkt_ipv4_hdr = (void*)(pkt_eth_hdr+1);

	if (be16_to_cpu(pkt_eth_hdr->h_proto) != ETH_P_IP) {
		DPRINTK("%s: Packet is not IPV4 (ether_type=0x%04x)\n", __FUNCTION__,
			be16_to_cpu(pkt_eth_hdr->h_proto));
		return NETFRONT_ACCEL_STATUS_CANT;
	}
	
	if (pkt_ipv4_hdr->protocol != IPPROTO_TCP &&
	    pkt_ipv4_hdr->protocol != IPPROTO_UDP) {
		DPRINTK("%s: Packet is not TCP/UDP (ip_protocol=0x%02x)\n",
			__FUNCTION__, pkt_ipv4_hdr->protocol);
		return NETFRONT_ACCEL_STATUS_CANT;
	}
	
	VPRINTK("%s: %d bytes, gso %d\n", __FUNCTION__, skb->len, 
		skb_shinfo(skb)->gso_size);
	
	if (skb_shinfo(skb)->gso_size) {
		return netfront_accel_enqueue_skb_tso(vnic, skb);
	}

	if (skb->len <= NETFRONT_ACCEL_TX_BUF_LENGTH) {
		return netfront_accel_enqueue_skb_single(vnic, skb);
	}

	return netfront_accel_enqueue_skb_multi(vnic, skb);
}


/*
 * Copy the data to required end destination. NB. len is the total new
 * length of the socket buffer, not the amount of data to copy
 */
inline
int ef_vnic_copy_to_skb(netfront_accel_vnic *vnic, struct sk_buff *skb, 
			struct netfront_accel_pkt_desc *buf, int len)
{
	int i, extra = len - skb->len;
	char c;
	int pkt_stride = vnic->rx_pkt_stride;
	int skb_stride = vnic->rx_skb_stride;
	char *skb_start;
	
	/*
	 * This pulls stuff into the cache - have seen performance
	 * benefit in this, but disabled by default
	 */
	skb_start = skb->data;
	if (pkt_stride) {
		for (i = 0; i < len; i += pkt_stride) {
			c += ((volatile char*)(buf->pkt_kva))[i];
		}
	}
	if (skb_stride) {
		for (i = skb->len; i < len ; i += skb_stride) {
			c += ((volatile char*)(skb_start))[i];
		}
	}

	if (skb_tailroom(skb) >= extra) {
		memcpy(skb_put(skb, extra), buf->pkt_kva, extra);
		return 0;
	}

	return -ENOSPC;
}


static void discard_jumbo_state(netfront_accel_vnic *vnic) 
{

	if (vnic->jumbo_state.skb != NULL) {
		dev_kfree_skb_any(vnic->jumbo_state.skb);

		vnic->jumbo_state.skb = NULL;
	}
	vnic->jumbo_state.in_progress = 0;
}


static void  netfront_accel_vi_rx_complete(netfront_accel_vnic *vnic,
					   struct sk_buff *skb)
{
	cuckoo_hash_mac_key key;
	unsigned long flags;
	int value;
	struct net_device *net_dev;


	key = cuckoo_mac_to_key(skb->data + ETH_ALEN);

	/*
	 * If this is a MAC address that we want to do fast path TX
	 * to, and we don't already, add it to the fastpath table.
	 * The initial lookup is done without the table lock and so
	 * may get the wrong answer, but current opinion is that's not
	 * a big problem
	 */
	if (is_valid_ether_addr(skb->data + ETH_ALEN) &&
	    !cuckoo_hash_lookup(&vnic->fastpath_table, (cuckoo_hash_key *)&key,
				&value)) {
		spin_lock_irqsave(&vnic->table_lock, flags);
		   
		cuckoo_hash_add_check(&vnic->fastpath_table,
				      (cuckoo_hash_key *)&key,
				      1, 1);
		
		spin_unlock_irqrestore(&vnic->table_lock, flags);
	}

	if (compare_ether_addr(skb->data, vnic->mac)) {
		struct iphdr *ip = (struct iphdr *)(skb->data + ETH_HLEN);
		u16 port;

		DPRINTK("%s: saw wrong MAC address %pM\n",
			__FUNCTION__, skb->data);

		if (ip->protocol == IPPROTO_TCP) {
			struct tcphdr *tcp = (struct tcphdr *)
				((char *)ip + 4 * ip->ihl);
			port = tcp->dest;
		} else {
			struct udphdr *udp = (struct udphdr *)
				((char *)ip + 4 * ip->ihl);
			EPRINTK_ON(ip->protocol != IPPROTO_UDP);
			port = udp->dest;
		}

		netfront_accel_msg_tx_fastpath(vnic, skb->data,
					       ip->daddr, port,
					       ip->protocol);
	}

	net_dev = vnic->net_dev;
	skb->dev = net_dev;
	skb->protocol = eth_type_trans(skb, net_dev);
	/* CHECKSUM_UNNECESSARY as hardware has done it already */
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (!netfront_accel_ssr_skb(vnic, &vnic->ssr_state, skb))
		netif_receive_skb(skb);
}


static int netfront_accel_vi_poll_process_rx(netfront_accel_vnic *vnic, 
					     ef_event *ev)
{
	struct netfront_accel_bufinfo *bufinfo = vnic->rx_bufs;
	struct netfront_accel_pkt_desc *buf = NULL;
	struct sk_buff *skb;
	int id, len, sop = 0, cont = 0;

	VPRINTK("Rx event.\n");
	/*
	 * Complete the receive operation, and get the request id of
	 * the buffer
	 */
	id = ef_vi_receive_done(&vnic->vi, ev);

	if (id < 0 || id >= bufinfo->npages*NETFRONT_ACCEL_BUFS_PER_PAGE) {
		EPRINTK("Rx packet %d is invalid\n", id);
		/* Carry on round the loop if more events */
		goto bad_packet;
	}
	/* Get our buffer descriptor */
	buf = netfront_accel_buf_find(bufinfo, id);

	len = EF_EVENT_RX_BYTES(*ev);

	/* An RX buffer has been removed from the DMA ring. */
	vnic->rx_dma_level--;

	if (EF_EVENT_TYPE(*ev) == EF_EVENT_TYPE_RX) {
		sop = EF_EVENT_RX_SOP(*ev);
		cont = EF_EVENT_RX_CONT(*ev);

		skb = vnic->jumbo_state.skb;

		VPRINTK("Rx packet %d: %d bytes so far; sop %d; cont %d\n", 
			id, len, sop, cont);

		if (sop) {
			if (!vnic->jumbo_state.in_progress) {
				vnic->jumbo_state.in_progress = 1;
				BUG_ON(vnic->jumbo_state.skb != NULL);
			} else {
				/*
				 * This fragment shows a missing tail in 
				 * previous one, but is itself possibly OK
				 */
				DPRINTK("sop and in_progress => no tail\n");

				/* Release the socket buffer we already had */
				discard_jumbo_state(vnic);

				/* Now start processing this fragment */
				vnic->jumbo_state.in_progress = 1;
				skb = NULL;
			}
		} else if (!vnic->jumbo_state.in_progress) {
			DPRINTK("!sop and !in_progress => missing head\n");
			goto missing_head;
		}

		if (!cont) {
			/* Update state for next time */
			vnic->jumbo_state.in_progress = 0;
			vnic->jumbo_state.skb = NULL;
		} else if (!vnic->jumbo_state.in_progress) {
			DPRINTK("cont and !in_progress => missing head\n");
			goto missing_head;
		}

		if (skb == NULL) {
			BUG_ON(!sop);

			if (!cont)
				skb = alloc_skb(len+NET_IP_ALIGN, GFP_ATOMIC);
			else
				skb = alloc_skb(vnic->net_dev->mtu+NET_IP_ALIGN, 
						GFP_ATOMIC);

			if (skb == NULL) {
				DPRINTK("%s: Couldn't get an rx skb.\n",
					__FUNCTION__);
				netfront_accel_vi_post_rx_or_free(vnic, (u16)id, buf);
				/*
				 * Dropping this fragment means we
				 * should discard the rest too
				 */
				discard_jumbo_state(vnic);

				/* Carry on round the loop if more events */
				return 0;
			}

		}
		
		/* Copy the data to required end destination */
		if (ef_vnic_copy_to_skb(vnic, skb, buf, len) != 0) {
			/*
			 * No space in the skb - suggests > MTU packet
			 * received
			 */
			EPRINTK("%s: Rx packet too large (%d)\n",
				__FUNCTION__, len);
			netfront_accel_vi_post_rx_or_free(vnic, (u16)id, buf);
			discard_jumbo_state(vnic);
			return 0;
		}
		
		/* Put the buffer back in the DMA queue. */
		netfront_accel_vi_post_rx_or_free(vnic, (u16)id, buf);

		if (cont) {
			vnic->jumbo_state.skb = skb;

			return 0;
		} else {
			/* Track number of rx fastpath packets */
			vnic->netdev_stats.fastpath_rx_pkts++;
			vnic->netdev_stats.fastpath_rx_bytes += len;

			netfront_accel_vi_rx_complete(vnic, skb);

			return 1;
		}
	} else {
		BUG_ON(EF_EVENT_TYPE(*ev) != EF_EVENT_TYPE_RX_DISCARD);

		if (EF_EVENT_RX_DISCARD_TYPE(*ev) 
		    == EF_EVENT_RX_DISCARD_TRUNC) {
			DPRINTK("%s: " EF_EVENT_FMT 
				" buffer %d FRM_TRUNC q_id %d\n",
				__FUNCTION__, EF_EVENT_PRI_ARG(*ev), id,
				EF_EVENT_RX_DISCARD_Q_ID(*ev) );
			NETFRONT_ACCEL_STATS_OP(++vnic->stats.fastpath_frm_trunc);
		} else if (EF_EVENT_RX_DISCARD_TYPE(*ev) 
			  == EF_EVENT_RX_DISCARD_OTHER) {
			DPRINTK("%s: " EF_EVENT_FMT 
				" buffer %d RX_DISCARD_OTHER q_id %d\n",
				__FUNCTION__, EF_EVENT_PRI_ARG(*ev), id,
				EF_EVENT_RX_DISCARD_Q_ID(*ev) );
			NETFRONT_ACCEL_STATS_OP(++vnic->stats.fastpath_discard_other);
		} else if (EF_EVENT_RX_DISCARD_TYPE(*ev) ==
			   EF_EVENT_RX_DISCARD_CSUM_BAD) {
			DPRINTK("%s: " EF_EVENT_FMT 
				" buffer %d DISCARD CSUM_BAD q_id %d\n",
				__FUNCTION__, EF_EVENT_PRI_ARG(*ev), id,
				EF_EVENT_RX_DISCARD_Q_ID(*ev) );
			NETFRONT_ACCEL_STATS_OP(++vnic->stats.fastpath_csum_bad);
		} else if (EF_EVENT_RX_DISCARD_TYPE(*ev) ==
			   EF_EVENT_RX_DISCARD_CRC_BAD) {
			DPRINTK("%s: " EF_EVENT_FMT 
				" buffer %d DISCARD CRC_BAD q_id %d\n",
				__FUNCTION__, EF_EVENT_PRI_ARG(*ev), id,
				EF_EVENT_RX_DISCARD_Q_ID(*ev) );
			NETFRONT_ACCEL_STATS_OP(++vnic->stats.fastpath_crc_bad);
		} else {
			BUG_ON(EF_EVENT_RX_DISCARD_TYPE(*ev) !=
			       EF_EVENT_RX_DISCARD_RIGHTS);
			DPRINTK("%s: " EF_EVENT_FMT 
				" buffer %d DISCARD RIGHTS q_id %d\n",
				__FUNCTION__, EF_EVENT_PRI_ARG(*ev), id,
				EF_EVENT_RX_DISCARD_Q_ID(*ev) );
			NETFRONT_ACCEL_STATS_OP(++vnic->stats.fastpath_rights_bad);
		}
	}

	/* discard type drops through here */

bad_packet:
	/* Release the socket buffer we already had */
	discard_jumbo_state(vnic);

missing_head:
	BUG_ON(vnic->jumbo_state.in_progress != 0);
	BUG_ON(vnic->jumbo_state.skb != NULL);

	if (id >= 0 && id < bufinfo->npages*NETFRONT_ACCEL_BUFS_PER_PAGE)
		/* Put the buffer back in the DMA queue. */
		netfront_accel_vi_post_rx_or_free(vnic, (u16)id, buf);

	vnic->netdev_stats.fastpath_rx_errors++;

	DPRINTK("%s experienced bad packet/missing fragment error: %d \n",
		__FUNCTION__, ev->rx.flags);

	return 0;
}


static void netfront_accel_vi_not_busy(netfront_accel_vnic *vnic)
{
	struct netfront_info *np = ((struct netfront_info *)
				    netdev_priv(vnic->net_dev));
	int handled;
	unsigned long flags;

	/*
	 * We hold the vnic tx_lock which is sufficient to exclude
	 * writes to tx_skb
	 */

	if (vnic->tx_skb != NULL) {
		DPRINTK("%s trying to send spare buffer\n", __FUNCTION__);
		
		handled = netfront_accel_vi_tx_post(vnic, vnic->tx_skb);
		
		if (handled != NETFRONT_ACCEL_STATUS_BUSY) {
			DPRINTK("%s restarting tx\n", __FUNCTION__);

			/* Need netfront tx_lock and vnic tx_lock to
			 * write tx_skb */
			spin_lock_irqsave(&np->tx_lock, flags);

			vnic->tx_skb = NULL;

			if (netfront_check_queue_ready(vnic->net_dev)) {
				netif_wake_queue(vnic->net_dev);
				NETFRONT_ACCEL_STATS_OP
					(vnic->stats.queue_wakes++);
			}
			spin_unlock_irqrestore(&np->tx_lock, flags);

		}
		
		/*
		 * Should never get a CANT, as it checks that before
		 * deciding it was BUSY first time round 
		 */
		BUG_ON(handled == NETFRONT_ACCEL_STATUS_CANT);
	}
}


static void netfront_accel_vi_tx_complete(netfront_accel_vnic *vnic, 
					  struct netfront_accel_tso_buffer *tso_buf,
					  int is_last)
{
	struct netfront_accel_tso_buffer *next;

	/* 
	 * We get a single completion for every call to
	 * ef_vi_transmitv so handle any other buffers which are part
	 * of the same packet 
	 */
	while (tso_buf != NULL) {
		if (tso_buf->buf->skb != NULL) {
			dev_kfree_skb_any(tso_buf->buf->skb);
			tso_buf->buf->skb = NULL;
		}

		next = tso_buf->next;

		netfront_accel_buf_put(vnic->tx_bufs, tso_buf->buf->buf_id);

		tso_buf = next;
	}

	/*
	 * If this was the last one in the batch, we try and send any
	 * pending tx_skb. There should now be buffers and
	 * descriptors
	 */
	if (is_last)
		netfront_accel_vi_not_busy(vnic);
}


static void netfront_accel_vi_poll_process_tx(netfront_accel_vnic *vnic,
					      ef_event *ev)
{
	struct netfront_accel_pkt_desc *buf;
	struct netfront_accel_tso_buffer *tso_buf;
	ef_request_id ids[EF_VI_TRANSMIT_BATCH];
	int i, n_ids;
	unsigned long flags;

	/* Get the request ids for this tx completion event. */
	n_ids = ef_vi_transmit_unbundle(&vnic->vi, ev, ids);

	/* Take the tx buffer spin lock and hold for the duration */
	spin_lock_irqsave(&vnic->tx_lock, flags);

	for (i = 0; i < n_ids; ++i) {
		VPRINTK("Tx packet %d complete\n", ids[i]);
		buf = netfront_accel_buf_find(vnic->tx_bufs, ids[i]);
		NETFRONT_ACCEL_STATS_OP(vnic->stats.fastpath_tx_completions++);

		tso_buf = (struct netfront_accel_tso_buffer *)
			(buf->pkt_kva + NETFRONT_ACCEL_TX_BUF_LENGTH);
		BUG_ON(tso_buf->buf != buf);

		netfront_accel_vi_tx_complete(vnic, tso_buf, i == (n_ids-1));
	}

	spin_unlock_irqrestore(&vnic->tx_lock, flags);
}


int netfront_accel_vi_poll(netfront_accel_vnic *vnic, int rx_packets)
{
	ef_event ev[ACCEL_VI_POLL_EVENTS];
	int rx_remain = rx_packets, rc, events, i;
#if NETFRONT_ACCEL_STATS
	int n_evs_polled = 0, rx_evs_polled = 0, tx_evs_polled = 0;
#endif
	BUG_ON(rx_packets <= 0);

	events = ef_eventq_poll(&vnic->vi, ev, 
				min(rx_remain, ACCEL_VI_POLL_EVENTS));
	i = 0;
	NETFRONT_ACCEL_STATS_OP(n_evs_polled += events);

	VPRINTK("%s: %d events\n", __FUNCTION__, events);

	/* Loop over each event */
	while (events) {
		VPRINTK("%s: Event "EF_EVENT_FMT", index %lu\n", __FUNCTION__, 
			EF_EVENT_PRI_ARG(ev[i]),	
			(unsigned long)(vnic->vi.evq_state->evq_ptr));

		if ((EF_EVENT_TYPE(ev[i]) == EF_EVENT_TYPE_RX) ||
		    (EF_EVENT_TYPE(ev[i]) == EF_EVENT_TYPE_RX_DISCARD)) {
			rc = netfront_accel_vi_poll_process_rx(vnic, &ev[i]);
			rx_remain -= rc;
			BUG_ON(rx_remain < 0);
			NETFRONT_ACCEL_STATS_OP(rx_evs_polled++);
		} else if (EF_EVENT_TYPE(ev[i]) == EF_EVENT_TYPE_TX) {
			netfront_accel_vi_poll_process_tx(vnic, &ev[i]);
			NETFRONT_ACCEL_STATS_OP(tx_evs_polled++);
		} else if (EF_EVENT_TYPE(ev[i]) == 
			   EF_EVENT_TYPE_RX_NO_DESC_TRUNC) {
			DPRINTK("%s: RX_NO_DESC_TRUNC " EF_EVENT_FMT "\n",
				__FUNCTION__, EF_EVENT_PRI_ARG(ev[i]));
			discard_jumbo_state(vnic);
			NETFRONT_ACCEL_STATS_OP(vnic->stats.rx_no_desc_trunc++);
		} else {
			EPRINTK("Unexpected event " EF_EVENT_FMT "\n", 
				EF_EVENT_PRI_ARG(ev[i]));
			NETFRONT_ACCEL_STATS_OP(vnic->stats.bad_event_count++);
		}

		i++;

		/* Carry on round the loop if more events and more space */
		if (i == events) {
			if (rx_remain == 0)
				break;

			events = ef_eventq_poll(&vnic->vi, ev, 
						min(rx_remain, 
						    ACCEL_VI_POLL_EVENTS));
			i = 0;
			NETFRONT_ACCEL_STATS_OP(n_evs_polled += events);
		}
	}
	
#if NETFRONT_ACCEL_STATS
	vnic->stats.event_count += n_evs_polled;
	vnic->stats.event_count_since_irq += n_evs_polled;
	if (n_evs_polled > vnic->stats.events_per_poll_max)
		vnic->stats.events_per_poll_max = n_evs_polled;
	if (rx_evs_polled > vnic->stats.events_per_poll_rx_max)
		vnic->stats.events_per_poll_rx_max = rx_evs_polled;
	if (tx_evs_polled > vnic->stats.events_per_poll_tx_max)
		vnic->stats.events_per_poll_tx_max = tx_evs_polled;
#endif

	return rx_packets - rx_remain;
}


int netfront_accel_vi_enable_interrupts(netfront_accel_vnic *vnic)
{
	u32 sw_evq_ptr;

	VPRINTK("%s: checking for event on %p\n", __FUNCTION__, &vnic->vi.evq_state);

	BUG_ON(vnic == NULL);
	BUG_ON(vnic->vi.evq_state == NULL);

	/* Do a quick check for an event. */
	if (ef_eventq_has_event(&vnic->vi)) {
		VPRINTK("%s: found event\n",  __FUNCTION__);
		return 0;
	}

	VPRINTK("evq_ptr=0x%08x	 evq_mask=0x%08x\n",
		vnic->evq_state.evq_ptr, vnic->vi.evq_mask);
  
	/* Request a wakeup from the hardware. */
	sw_evq_ptr = vnic->evq_state.evq_ptr & vnic->vi.evq_mask;

	BUG_ON(vnic->hw.falcon.evq_rptr == NULL);

	VPRINTK("Requesting wakeup at 0x%08x, rptr %p\n", sw_evq_ptr,
		vnic->hw.falcon.evq_rptr);
	*(volatile u32 *)(vnic->hw.falcon.evq_rptr) = (sw_evq_ptr >> 3);

	return 1;
}
