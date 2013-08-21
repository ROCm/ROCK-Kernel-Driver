/******************************************************************************
 * drivers/xen/netback/netback.c
 * 
 * Back-end of the driver for virtual network devices. This portion of the
 * driver exports a 'unified' network-device interface that can be accessed
 * by any operating system that implements a compatible front end. A 
 * reference front-end implementation can be found in:
 *  drivers/xen/netfront/netfront.c
 * 
 * Copyright (c) 2002-2005, K A Fraser
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "common.h"
#include <linux/if_vlan.h>
#include <linux/kthread.h>
#include <linux/pfn.h>
#include <linux/vmalloc.h>
#include <net/tcp.h>
#include <xen/balloon.h>
#include <xen/evtchn.h>
#include <xen/gnttab.h>
#include <xen/interface/memory.h>
#include <xen/net-util.h>

/*define NETBE_DEBUG_INTERRUPT*/

struct xen_netbk *__read_mostly xen_netbk;
unsigned int __read_mostly netbk_nr_groups;
static bool __read_mostly use_kthreads = true;
static bool __initdata bind_threads;

#define GET_GROUP_INDEX(netif) ((netif)->group)

struct netbk_rx_cb {
	unsigned int nr_frags;
	unsigned int nr_slots;
};
#define netbk_rx_cb(skb) ((struct netbk_rx_cb *)skb->cb)

struct netbk_tx_cb {
	u16 copy_slots;
	u16 pending_idx[1 + XEN_NETIF_NR_SLOTS_MIN];
};
#define netbk_tx_cb(skb) ((struct netbk_tx_cb *)skb->cb)

static void netif_idx_release(struct xen_netbk *, u16 pending_idx);
static bool make_tx_response(netif_t *, const netif_tx_request_t *, s8 st,
			     netif_t **);
static netif_rx_response_t *make_rx_response(netif_t *netif, 
					     u16      id, 
					     s8       st,
					     u16      offset,
					     u16      size,
					     u16      flags);

static void net_tx_action(unsigned long group);
static void net_rx_action(unsigned long group);

/* Discriminate from any valid pending_idx value. */
#define INVALID_PENDING_IDX 0xffff

static inline unsigned long idx_to_pfn(struct xen_netbk *netbk, u16 idx)
{
	return page_to_pfn(netbk->tx.mmap_pages[idx]);
}

static inline unsigned long idx_to_kaddr(struct xen_netbk *netbk, u16 idx)
{
	return (unsigned long)pfn_to_kaddr(idx_to_pfn(netbk, idx));
}

/* extra field used in struct page */
union page_ext {
	struct {
#if BITS_PER_LONG < 64
#define GROUP_WIDTH (BITS_PER_LONG - CONFIG_XEN_NETDEV_TX_SHIFT)
#define MAX_GROUPS ((1U << GROUP_WIDTH) - 1)
		unsigned int grp:GROUP_WIDTH;
		unsigned int idx:CONFIG_XEN_NETDEV_TX_SHIFT;
#else
#define MAX_GROUPS UINT_MAX
		unsigned int grp, idx;
#endif
	} e;
	void *mapping;
};

static inline void netif_set_page_ext(struct page *pg, unsigned int group,
				      unsigned int idx)
{
	union page_ext ext = { .e = { .grp = group + 1, .idx = idx } };

	BUILD_BUG_ON(sizeof(ext) > sizeof(ext.mapping));
	pg->mapping = ext.mapping;
}

#define netif_get_page_ext(pg, netbk, index) do { \
	const struct page *pg__ = (pg); \
	union page_ext ext__ = { .mapping = pg__->mapping }; \
	unsigned int grp__ = ext__.e.grp - 1; \
	unsigned int idx__ = index = ext__.e.idx; \
	netbk = grp__ < netbk_nr_groups ? &xen_netbk[grp__] : NULL; \
	if (!PageForeign(pg__) || idx__ >= MAX_PENDING_REQS || \
	    (netbk && netbk->tx.mmap_pages[idx__] != pg__)) \
		netbk = NULL; \
} while (0)

static u16 frag_get_pending_idx(const skb_frag_t *frag)
{
	return (u16)frag->page_offset;
}

static void frag_set_pending_idx(skb_frag_t *frag, u16 pending_idx)
{
	frag->page_offset = pending_idx;
}

/*
 * This is the amount of packet we copy rather than map, so that the
 * guest can't fiddle with the contents of the headers while we do
 * packet processing on them (netfilter, routing, etc).
 */
#define PKT_PROT_LEN    (ETH_HLEN + VLAN_HLEN + \
			 sizeof(struct iphdr) + MAX_IPOPTLEN + \
			 sizeof(struct tcphdr) + MAX_TCP_OPTION_SPACE)

#define MASK_PEND_IDX(_i) ((_i)&(MAX_PENDING_REQS-1))

static inline pending_ring_idx_t nr_pending_reqs(const struct xen_netbk *netbk)
{
	return MAX_PENDING_REQS -
		netbk->tx.pending_prod + netbk->tx.pending_cons;
}

/*
 * This is the maximum slots a TX request can have. If a guest sends a TX
 * request which exceeds this limit it is considered malicious.
 */
static unsigned int max_tx_slots = XEN_NETIF_NR_SLOTS_MIN;
module_param(max_tx_slots, uint, 0444);
MODULE_PARM_DESC(max_tx_slots, "Maximum number of slots accepted in netfront TX requests");

/* Setting this allows the safe use of this driver without netloop. */
static bool MODPARM_copy_skb = true;
module_param_named(copy_skb, MODPARM_copy_skb, bool, 0);
MODULE_PARM_DESC(copy_skb, "Copy data received from netfront without netloop");
static bool MODPARM_permute_returns;
module_param_named(permute_returns, MODPARM_permute_returns, bool, S_IRUSR|S_IWUSR);
MODULE_PARM_DESC(permute_returns, "Randomly permute the order in which TX responses are sent to the frontend");
module_param_named(groups, netbk_nr_groups, uint, 0);
MODULE_PARM_DESC(groups, "Specify the number of tasklet pairs/threads to use");
module_param_named(tasklets, use_kthreads, invbool, 0);
MODULE_PARM_DESC(tasklets, "Use tasklets instead of kernel threads");
module_param_named(bind, bind_threads, bool, 0);
MODULE_PARM_DESC(bind, "Bind kernel threads to (v)CPUs");

int netbk_copy_skb_mode;

static inline unsigned long alloc_mfn(struct xen_netbk_rx *netbk)
{
	BUG_ON(netbk->alloc_index == 0);
	return netbk->mfn_list[--netbk->alloc_index];
}

static int check_mfn(struct xen_netbk_rx *netbk, unsigned int nr)
{
	struct xen_memory_reservation reservation = {
		.extent_order = 0,
		.domid        = DOMID_SELF
	};
	int rc;

	if (likely(netbk->alloc_index >= nr))
		return 0;

	set_xen_guest_handle(reservation.extent_start,
			     netbk->mfn_list + netbk->alloc_index);
	reservation.nr_extents = MAX_MFN_ALLOC - netbk->alloc_index;
	rc = HYPERVISOR_memory_op(XENMEM_increase_reservation, &reservation);
	if (likely(rc > 0))
		netbk->alloc_index += rc;

	return netbk->alloc_index >= nr ? 0 : -ENOMEM;
}

static void flush_notify_list(netif_t *list, unsigned int idx,
			      multicall_entry_t mcl[],
			      unsigned int limit)
{
	unsigned int used = 0;

	do {
		netif_t *cur = list;

		list = cur->notify_link[idx];
		cur->notify_link[idx] = NULL;

		if ((!used && !list) ||
		    multi_notify_remote_via_irq(mcl + used, cur->irq))
			notify_remote_via_irq(cur->irq);
		else if (++used == limit) {
			if (HYPERVISOR_multicall(mcl, used))
				BUG();
			used = 0;
		}
		netif_put(cur);
	} while (list);
	if (used && HYPERVISOR_multicall(mcl, used))
		BUG();
}

static void netbk_rx_schedule(struct xen_netbk_rx *netbk)
{
	if (use_kthreads)
		wake_up(&container_of(netbk, struct xen_netbk, rx)->action_wq);
	else
		tasklet_schedule(&netbk->tasklet);
}

static void netbk_tx_schedule(struct xen_netbk *netbk)
{
	if (use_kthreads)
		wake_up(&netbk->action_wq);
	else
		tasklet_schedule(&netbk->tx.tasklet);
}

static inline void maybe_schedule_tx_action(netif_t *netif)
{
	struct xen_netbk *netbk = &xen_netbk[GET_GROUP_INDEX(netif)];

	smp_mb();
	if ((nr_pending_reqs(netbk) < (MAX_PENDING_REQS/2)) &&
	    !list_empty(&netbk->tx.schedule_list))
		netbk_tx_schedule(netbk);
}

static struct sk_buff *netbk_copy_skb(struct sk_buff *skb)
{
	struct skb_shared_info *ninfo;
	struct sk_buff *nskb;
	unsigned long offset;
	int ret;
	int len;
	int headlen;

	BUG_ON(skb_shinfo(skb)->frag_list != NULL);

	nskb = alloc_skb(SKB_MAX_HEAD(0), GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!nskb))
		goto err;

	skb_reserve(nskb, 16 + NET_IP_ALIGN);
	headlen = skb_end_pointer(nskb) - nskb->data;
	if (headlen > skb_headlen(skb))
		headlen = skb_headlen(skb);
	ret = skb_copy_bits(skb, 0, __skb_put(nskb, headlen), headlen);
	BUG_ON(ret);

	ninfo = skb_shinfo(nskb);
	ninfo->gso_size = skb_shinfo(skb)->gso_size;
	ninfo->gso_type = skb_shinfo(skb)->gso_type;

	offset = headlen;
	len = skb->len - headlen;

	nskb->len = skb->len;
	nskb->data_len = len;
	nskb->truesize += len;

	while (len) {
		struct page *page;
		int copy;
		int zero;

		if (unlikely(ninfo->nr_frags >= MAX_SKB_FRAGS)) {
			dump_stack();
			goto err_free;
		}

		copy = len >= PAGE_SIZE ? PAGE_SIZE : len;
		zero = len >= PAGE_SIZE ? 0 : __GFP_ZERO;

		page = alloc_page(GFP_ATOMIC | __GFP_NOWARN | zero);
		if (unlikely(!page))
			goto err_free;

		ret = skb_copy_bits(skb, offset, page_address(page), copy);
		BUG_ON(ret);

		__skb_fill_page_desc(nskb, ninfo->nr_frags, page, 0, copy);
		ninfo->nr_frags++;

		offset += copy;
		len -= copy;
	}

#ifdef NET_SKBUFF_DATA_USES_OFFSET
	offset = 0;
#else
	offset = nskb->data - skb->data;
#endif

	nskb->transport_header = skb->transport_header + offset;
	nskb->network_header   = skb->network_header   + offset;
	nskb->mac_header       = skb->mac_header       + offset;

	return nskb;

 err_free:
	kfree_skb(nskb);
 err:
	return NULL;
}

static inline unsigned int netbk_max_required_rx_slots(const netif_t *netif)
{
	return netif->can_sg || netif->gso
	       ? max_t(unsigned int, XEN_NETIF_NR_SLOTS_MIN,
		       MAX_SKB_FRAGS + 2/* header + extra_info + frags */)
	       : 1; /* all in one */
}

static inline bool netbk_queue_full(const netif_t *netif)
{
	RING_IDX peek   = netif->rx_req_cons_peek;
	RING_IDX needed = netbk_max_required_rx_slots(netif);

	return ((netif->rx.sring->req_prod - peek) < needed) ||
	       ((netif->rx.rsp_prod_pvt + NET_RX_RING_SIZE - peek) < needed);
}

static void tx_queue_callback(unsigned long data)
{
	netif_t *netif = (netif_t *)data;
	if (netif_schedulable(netif))
		netif_wake_queue(netif->dev);
}

static unsigned int netbk_count_slots(const struct skb_shared_info *shinfo,
				      bool copying)
{
	unsigned int i, slots;

	for (slots = i = 0; i < shinfo->nr_frags; ++i) {
		const skb_frag_t *frag = shinfo->frags + i;
		unsigned int len = skb_frag_size(frag), offs;

		if (!len)
			continue;
		offs = copying ? 0 : offset_in_page(frag->page_offset);
		slots += PFN_UP(offs + len);
	}

	return slots;
}

int netif_be_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	netif_t *netif = netdev_priv(dev);
	unsigned int group = GET_GROUP_INDEX(netif);
	struct xen_netbk_rx *netbk;

	BUG_ON(skb->dev != dev);

	if (unlikely(group >= netbk_nr_groups)) {
		BUG_ON(group != UINT_MAX);
		goto drop;
	}

	/* Drop the packet if the target domain has no receive buffers. */
	if (unlikely(!netif_schedulable(netif) || netbk_queue_full(netif)))
		goto drop;

	/*
	 * Copy the packet here if it's destined for a flipping interface
	 * but isn't flippable (e.g. extra references to data).
	 * XXX For now we also copy skbuffs whose head crosses a page
	 * boundary, because netbk_gop_skb can't handle them.
	 */
	if (!netif->copying_receiver ||
	    ((skb_headlen(skb) + offset_in_page(skb->data)) >= PAGE_SIZE)) {
		struct sk_buff *nskb = netbk_copy_skb(skb);
		if ( unlikely(nskb == NULL) )
			goto drop;
		/* Copy only the header fields we use in this driver. */
		nskb->dev = skb->dev;
		nskb->ip_summed = skb->ip_summed;
		dev_kfree_skb(skb);
		skb = nskb;
	}

	netbk_rx_cb(skb)->nr_frags = skb_shinfo(skb)->nr_frags;
	netbk_rx_cb(skb)->nr_slots = 1 + !!skb_shinfo(skb)->gso_size +
				     netbk_count_slots(skb_shinfo(skb),
						       netif->copying_receiver);
	netif->rx_req_cons_peek += netbk_rx_cb(skb)->nr_slots;
	netif_get(netif);

	if (netbk_can_queue(dev) && netbk_queue_full(netif)) {
		netif->rx.sring->req_event = netif->rx_req_cons_peek +
			netbk_max_required_rx_slots(netif);
		mb(); /* request notification /then/ check & stop the queue */
		if (netbk_queue_full(netif)) {
			netif_stop_queue(dev);
			/*
			 * Schedule 500ms timeout to restart the queue, thus
			 * ensuring that an inactive queue will be drained.
			 * Packets will be immediately be dropped until more
			 * receive buffers become available (see
			 * netbk_queue_full() check above).
			 */
			netif->tx_queue_timeout.data = (unsigned long)netif;
			netif->tx_queue_timeout.function = tx_queue_callback;
			mod_timer(&netif->tx_queue_timeout, jiffies + HZ/2);
		}
	}

	netbk = &xen_netbk[group].rx;
	skb_queue_tail(&netbk->queue, skb);
	netbk_rx_schedule(netbk);

	return NETDEV_TX_OK;

 drop:
	dev->stats.tx_dropped++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

#if 0
static void xen_network_done_notify(void)
{
	static struct net_device *eth0_dev = NULL;
	if (unlikely(eth0_dev == NULL))
		eth0_dev = __dev_get_by_name(&init_net, "eth0");
	napi_schedule(???);
}
/* 
 * Add following to poll() function in NAPI driver (Tigon3 is example):
 *  if ( xen_network_done() )
 *      tg3_enable_ints(tp);
 */
int xen_network_done(void)
{
	return skb_queue_empty(&netbk->rx.queue);
}
#endif

struct netrx_pending_operations {
	unsigned trans_prod, trans_cons;
	unsigned mmu_prod, mmu_mcl;
	unsigned mcl_prod, mcl_cons;
	unsigned copy_prod, copy_cons;
	unsigned meta_prod, meta_cons;
	mmu_update_t *mmu;
	gnttab_transfer_t *trans;
	gnttab_copy_t *copy;
	multicall_entry_t *mcl;
	struct netbk_rx_meta *meta;
};

/* Set up the grant operations for this fragment.  If it's a flipping
   interface, we also set up the unmap request from here. */
static void netbk_gop_frag(netif_t *netif, struct netbk_rx_meta *meta,
			   unsigned int i,
			   struct netrx_pending_operations *npo,
			   struct page *page, unsigned int size,
			   unsigned int offset)
{
	mmu_update_t *mmu;
	gnttab_transfer_t *gop;
	gnttab_copy_t *copy_gop;
	multicall_entry_t *mcl;
	netif_rx_request_t *req;

	req = RING_GET_REQUEST(&netif->rx, netif->rx.req_cons + i);
	if (netif->copying_receiver) {
		struct xen_netbk *netbk;
		unsigned int idx;

		/* The fragment needs to be copied rather than
		   flipped. */
		meta->copy++;
		copy_gop = npo->copy + npo->copy_prod++;
		copy_gop->flags = GNTCOPY_dest_gref;
		netif_get_page_ext(page, netbk, idx);
		if (netbk) {
			struct pending_tx_info *src_pend;
			unsigned int grp;

			src_pend = &netbk->tx.pending_info[idx];
			grp = GET_GROUP_INDEX(src_pend->netif);
			BUG_ON(netbk != &xen_netbk[grp] && grp != UINT_MAX);
			copy_gop->source.domid = src_pend->netif->domid;
			copy_gop->source.u.ref = src_pend->req.gref;
			copy_gop->flags |= GNTCOPY_source_gref;
		} else {
			copy_gop->source.domid = DOMID_SELF;
			copy_gop->source.u.gmfn = virt_to_mfn(page_address(page));
		}
		copy_gop->source.offset = offset;
		copy_gop->dest.domid = netif->domid;
		copy_gop->dest.offset = i ? meta->frag.size : 0;
		copy_gop->dest.u.ref = req->gref;
		copy_gop->len = size;
	} else {
		if (!xen_feature(XENFEAT_auto_translated_physmap)) {
			unsigned long new_mfn =
				alloc_mfn(&xen_netbk[GET_GROUP_INDEX(netif)].rx);

			/*
			 * Set the new P2M table entry before
			 * reassigning the old data page. Heed the
			 * comment in pgtable-2level.h:pte_page(). :-)
			 */
			set_phys_to_machine(page_to_pfn(page), new_mfn);

			mcl = npo->mcl + npo->mcl_prod++;
			MULTI_update_va_mapping(mcl,
					     (unsigned long)page_address(page),
					     pfn_pte_ma(new_mfn, PAGE_KERNEL),
					     0);

			mmu = npo->mmu + npo->mmu_prod++;
			mmu->ptr = ((maddr_t)new_mfn << PAGE_SHIFT) |
				MMU_MACHPHYS_UPDATE;
			mmu->val = page_to_pfn(page);
		}

		gop = npo->trans - npo->trans_prod++;
		gop->mfn = virt_to_mfn(page_address(page));
		gop->domid = netif->domid;
		gop->ref = req->gref;
	}
	meta->id = req->id;
}

static unsigned int netbk_gop_skb(struct sk_buff *skb,
				  struct netrx_pending_operations *npo)
{
	netif_t *netif = netdev_priv(skb->dev);
	unsigned int i, n, nr_frags = netbk_rx_cb(skb)->nr_frags;
	struct netbk_rx_meta *head_meta, *meta;

	head_meta = npo->meta + npo->meta_prod++;
	head_meta->frag.page_offset = skb_shinfo(skb)->gso_type;
	head_meta->frag.size = skb_shinfo(skb)->gso_size;
	head_meta->copy = 0;
	n = !!head_meta->frag.size + 1;

	for (i = 0; i < nr_frags; i++, n++) {
		const skb_frag_t *frag = skb_shinfo(skb)->frags + i;
		unsigned int offset = frag->page_offset;
		unsigned int len = skb_frag_size(frag);
		struct page *frag_page = skb_frag_page(frag);
		struct page *page = frag_page + PFN_DOWN(offset);

		if (!len)
			continue;
		for (meta = NULL, offset &= ~PAGE_MASK; len; ) {
			unsigned int bytes = PAGE_SIZE - offset;

			if (bytes > len)
				bytes = len;
			/*
			 * Try to reduce the number of slots needed (at the
			 * expense of more copy operations), so that frontends
			 * only coping with the minimum slot count required to
			 * be supported have a better chance of receiving this
			 * packet.
			 */
			else if (meta && meta->copy &&
				 (bytes > PAGE_SIZE - meta->frag.size) &&
				 (offset_in_page(len) + meta->frag.size <=
				  PAGE_SIZE))
				bytes = PAGE_SIZE - meta->frag.size;
			if (!meta || !meta->copy ||
			    bytes > PAGE_SIZE - meta->frag.size) {
				if (meta)
					n++;
				meta = npo->meta + npo->meta_prod++;
				__skb_frag_set_page(&meta->frag, frag_page);
				frag_page = NULL;
				meta->frag.page_offset = offset;
				meta->frag.size = 0;
				meta->copy = 0;
				meta->tail = 0;
			}
			netbk_gop_frag(netif, meta, n, npo, page, bytes,
				       offset);
			meta->frag.size += bytes;
			len -= bytes;
			if ((offset += bytes) == PAGE_SIZE) {
				++page;
				offset = 0;
			}
		}
		meta->tail = 1;
	}

	/*
	 * This must occur at the end to ensure that we don't trash skb_shinfo
	 * until we're done. We know that the head doesn't cross a page
	 * boundary because such packets get copied in netif_be_start_xmit.
	 */
	netbk_gop_frag(netif, head_meta, 0, npo, virt_to_page(skb->data),
		       skb_headlen(skb), offset_in_page(skb->data));
	head_meta->tail = 1;

	netif->rx.req_cons += n;
	return n;
}

static inline void netbk_free_pages(int nr_frags, struct netbk_rx_meta *meta)
{
	int i;

	for (i = 0; i < nr_frags; meta++) {
		struct page *page = skb_frag_page(&meta->frag);

		if (page) {
			put_page(page);
			i++;
		}
	}
}

/* This is a twin to netbk_gop_skb.  Assume that netbk_gop_skb was
   used to set up the operations on the top of
   netrx_pending_operations, which have since been done.  Check that
   they didn't give any errors and advance over them. */
static int netbk_check_gop(unsigned int nr_frags, const netif_t *netif,
			   struct netrx_pending_operations *npo)
{
	multicall_entry_t *mcl;
	gnttab_transfer_t *gop;
	gnttab_copy_t     *copy_op;
	int status = XEN_NETIF_RSP_OKAY;
	int i;
	const struct netbk_rx_meta *meta = npo->meta + npo->meta_cons;

	for (i = 0; i <= nr_frags; i += meta++->tail) {
		unsigned int copy = meta->copy;

		if (copy) {
			do {
				copy_op = npo->copy + npo->copy_cons++;
				if (unlikely(copy_op->status == GNTST_eagain))
					gnttab_check_GNTST_eagain_while(GNTTABOP_copy, copy_op);
				if (unlikely(copy_op->status != GNTST_okay)) {
					netdev_dbg(netif->dev,
						   "Bad status %d from copy to DOM%d.\n",
						   copy_op->status, netif->domid);
					status = XEN_NETIF_RSP_ERROR;
				}
			} while (--copy);
		} else {
			if (!xen_feature(XENFEAT_auto_translated_physmap)) {
				mcl = npo->mcl + npo->mcl_cons++;
				/* The update_va_mapping() must not fail. */
				BUG_ON(mcl->result != 0);
			}

			gop = npo->trans - npo->trans_cons++;
			/* Check the reassignment error code. */
			if (unlikely(gop->status != GNTST_okay)) {
				netdev_dbg(netif->dev,
					   "Bad status %d from grant transfer to DOM%u\n",
					   gop->status, netif->domid);
				/*
				 * Page no longer belongs to us unless
				 * GNTST_bad_page, but that should be
				 * a fatal error anyway.
				 */
				BUG_ON(gop->status == GNTST_bad_page);
				status = XEN_NETIF_RSP_ERROR;
			}
		}
	}

	return status;
}

static unsigned int netbk_add_frag_responses(netif_t *netif, int status,
					     const struct netbk_rx_meta *meta,
					     unsigned int nr_frags)
{
	unsigned int i, n;

	for (n = i = 0; i < nr_frags; meta++, n++) {
		int flags = (meta->tail && ++i == nr_frags)
			    ? 0 : XEN_NETRXF_more_data;

		make_rx_response(netif, meta->id, status,
				 meta->copy ? 0 : meta->frag.page_offset,
				 meta->frag.size, flags);
	}

	return n;
}

static void net_rx_action(unsigned long group)
{
	netif_t *netif, *notify_head = NULL, *notify_tail = NULL;
	s8 status;
	u16 id, flags;
	netif_rx_response_t *resp;
	multicall_entry_t *mcl;
	struct sk_buff_head rxq;
	struct sk_buff *skb;
	int ret;
	int nr_frags;
	int count;
	unsigned long offset;
	struct xen_netbk_rx *netbk = &xen_netbk[group].rx;

	struct netrx_pending_operations npo = {
		.mmu   = netbk->mmu,
		.trans = &netbk->grant_trans_op,
		.copy  = netbk->grant_copy_op,
		.mcl   = netbk->mcl,
		.meta  = netbk->meta,
	};

	BUILD_BUG_ON(sizeof(skb->cb) < sizeof(struct netbk_rx_cb));

	skb_queue_head_init(&rxq);

	count = 0;

	while ((skb = skb_dequeue(&netbk->queue)) != NULL) {
		nr_frags = netbk_rx_cb(skb)->nr_slots;

		/* Filled the batch queue? */
		if (count + nr_frags > NET_RX_RING_SIZE) {
			skb_queue_head(&netbk->queue, skb);
			break;
		}

		if (!xen_feature(XENFEAT_auto_translated_physmap) &&
		    !((netif_t *)netdev_priv(skb->dev))->copying_receiver &&
		    check_mfn(netbk, nr_frags)) {
			/* Memory squeeze? Back off for an arbitrary while. */
			if ( net_ratelimit() )
				netdev_warn(skb->dev, "memory squeeze\n");
			mod_timer(&netbk->timer, jiffies + HZ);
			skb_queue_head(&netbk->queue, skb);
			break;
		}

		count += netbk_gop_skb(skb, &npo);

		__skb_queue_tail(&rxq, skb);
	}

	BUG_ON(npo.meta_prod > ARRAY_SIZE(netbk->meta));

	npo.mmu_mcl = npo.mcl_prod;
	if (npo.mcl_prod) {
		BUG_ON(xen_feature(XENFEAT_auto_translated_physmap));
		BUG_ON(npo.mmu_prod > ARRAY_SIZE(netbk->mmu));
		mcl = npo.mcl + npo.mcl_prod++;

		BUG_ON(mcl[-1].op != __HYPERVISOR_update_va_mapping);
		mcl[-1].args[MULTI_UVMFLAGS_INDEX] = UVMF_TLB_FLUSH|UVMF_ALL;

		MULTI_mmu_update(mcl, netbk->mmu, npo.mmu_prod, 0,
				 DOMID_SELF);
	}

	BUILD_BUG_ON(sizeof(netbk->grant_trans_op)
		     > sizeof(*netbk->grant_copy_op));
	BUG_ON(npo.copy_prod + npo.trans_prod
	       > ARRAY_SIZE(netbk->grant_copy_op) + 1);
	if (npo.trans_prod)
		MULTI_grant_table_op(npo.mcl + npo.mcl_prod++,
				     GNTTABOP_transfer,
				     npo.trans + 1 - npo.trans_prod,
				     npo.trans_prod);

	if (npo.copy_prod)
		MULTI_grant_table_op(npo.mcl + npo.mcl_prod++,
				     GNTTABOP_copy, npo.copy, npo.copy_prod);

	/* Nothing to do? */
	if (!npo.mcl_prod)
		return;

	BUG_ON(npo.mcl_prod > ARRAY_SIZE(netbk->mcl));

	ret = HYPERVISOR_multicall(npo.mcl, npo.mcl_prod);
	BUG_ON(ret != 0);
	/* The mmu_machphys_update() must not fail. */
	BUG_ON(npo.mmu_mcl && npo.mcl[npo.mmu_mcl].result != 0);

	while ((skb = __skb_dequeue(&rxq)) != NULL) {
		nr_frags = netbk_rx_cb(skb)->nr_frags;

		netif = netdev_priv(skb->dev);

		status = netbk_check_gop(nr_frags, netif, &npo);

		/* We can't rely on skb_release_data to release the
		   pages used by fragments for us, since it tries to
		   touch the pages in the fraglist.  If we're in
		   flipping mode, that doesn't work.  In copying mode,
		   we still have access to all of the pages, and so
		   it's safe to let release_data deal with it. */
		/* (Freeing the fragments is safe since we copy
		   non-linear skbs destined for flipping interfaces) */
		if (!netif->copying_receiver) {
			atomic_set(&(skb_shinfo(skb)->dataref), 1);
			skb_shinfo(skb)->frag_list = NULL;
			skb_shinfo(skb)->nr_frags = 0;
			netbk_free_pages(nr_frags,
					 netbk->meta + npo.meta_cons + 1);
		}

		skb->dev->stats.tx_bytes += skb->len;
		skb->dev->stats.tx_packets++;

		id = netbk->meta[npo.meta_cons].id;
		flags = nr_frags ? XEN_NETRXF_more_data : 0;

		switch (skb->ip_summed) {
		case CHECKSUM_PARTIAL: /* local packet? */
			flags |= XEN_NETRXF_csum_blank |
				 XEN_NETRXF_data_validated;
			break;
		case CHECKSUM_UNNECESSARY: /* remote but checksummed? */
			flags |= XEN_NETRXF_data_validated;
			break;
		}

		if (netbk->meta[npo.meta_cons].copy)
			offset = 0;
		else
			offset = offset_in_page(skb->data);
		resp = make_rx_response(netif, id, status, offset,
					skb_headlen(skb), flags);

		if (netbk->meta[npo.meta_cons].frag.size) {
			struct netif_extra_info *gso =
				(struct netif_extra_info *)
				RING_GET_RESPONSE(&netif->rx,
						  netif->rx.rsp_prod_pvt++);

			resp->flags |= XEN_NETRXF_extra_info;

			gso->u.gso.size = netbk->meta[npo.meta_cons].frag.size;
			gso->u.gso.type = XEN_NETIF_GSO_TYPE_TCPV4;
			gso->u.gso.pad = 0;
			gso->u.gso.features = 0;

			gso->type = XEN_NETIF_EXTRA_TYPE_GSO;
			gso->flags = 0;
		}

		nr_frags = netbk_add_frag_responses(netif, status,
						    netbk->meta + npo.meta_cons + 1,
						    nr_frags);

		RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&netif->rx, ret);

		if (netif_queue_stopped(netif->dev) &&
		    netif_schedulable(netif) &&
		    !netbk_queue_full(netif))
			netif_wake_queue(netif->dev);

		if (ret && netif != notify_tail && !netif->rx_notify_link) {
			if (notify_tail)
				notify_tail->rx_notify_link = netif;
			else
				notify_head = netif;
			notify_tail = netif;
		} else
			netif_put(netif);

		dev_kfree_skb(skb);

		npo.meta_cons += nr_frags + 1;
	}

	if (notify_head)
		flush_notify_list(notify_head, RX_IDX, netbk->mcl,
				  ARRAY_SIZE(netbk->mcl));

	/* More work to do? */
	if (!skb_queue_empty(&netbk->queue) &&
	    !timer_pending(&netbk->timer))
		netbk_rx_schedule(netbk);
#if 0
	else
		xen_network_done_notify();
#endif
}

static void net_alarm(unsigned long group)
{
	netbk_rx_schedule(&xen_netbk[group].rx);
}

static void netbk_tx_pending_timeout(unsigned long group)
{
	netbk_tx_schedule(&xen_netbk[group]);
}

static int __on_net_schedule_list(netif_t *netif)
{
	return netif->list.next != NULL;
}

/* Must be called with netbk->tx.schedule_list_lock held. */
static void remove_from_net_schedule_list(netif_t *netif)
{
	if (likely(__on_net_schedule_list(netif))) {
		list_del(&netif->list);
		netif->list.next = NULL;
		netif_put(netif);
	}
}

static netif_t *poll_net_schedule_list(struct xen_netbk *netbk)
{
	netif_t *netif = NULL;

	spin_lock_irq(&netbk->tx.schedule_list_lock);
	if (!list_empty(&netbk->tx.schedule_list)) {
		netif = list_first_entry(&netbk->tx.schedule_list,
					 netif_t, list);
		netif_get(netif);
		remove_from_net_schedule_list(netif);
	}
	spin_unlock_irq(&netbk->tx.schedule_list_lock);
	return netif;
}

static void add_to_net_schedule_list_tail(netif_t *netif)
{
	struct xen_netbk *netbk = &xen_netbk[GET_GROUP_INDEX(netif)];
	unsigned long flags;

	if (__on_net_schedule_list(netif))
		return;

	spin_lock_irqsave(&netbk->tx.schedule_list_lock, flags);
	if (!__on_net_schedule_list(netif) &&
	    likely(netif_schedulable(netif))) {
		list_add_tail(&netif->list, &netbk->tx.schedule_list);
		netif_get(netif);
	}
	spin_unlock_irqrestore(&netbk->tx.schedule_list_lock, flags);
}

/*
 * Note on CONFIG_XEN_NETDEV_PIPELINED_TRANSMITTER:
 * If this driver is pipelining transmit requests then we can be very
 * aggressive in avoiding new-packet notifications -- frontend only needs to
 * send a notification if there are no outstanding unreceived responses.
 * If we may be buffer transmit buffers for any reason then we must be rather
 * more conservative and treat this as the final check for pending work.
 */
void netif_schedule_work(netif_t *netif)
{
	int more_to_do;

#ifdef CONFIG_XEN_NETDEV_PIPELINED_TRANSMITTER
	more_to_do = RING_HAS_UNCONSUMED_REQUESTS(&netif->tx);
#else
	RING_FINAL_CHECK_FOR_REQUESTS(&netif->tx, more_to_do);
#endif

	if (more_to_do && likely(!netif->busted)) {
		add_to_net_schedule_list_tail(netif);
		maybe_schedule_tx_action(netif);
	}
}

void netif_deschedule_work(netif_t *netif)
{
	struct xen_netbk *netbk = &xen_netbk[GET_GROUP_INDEX(netif)];

	spin_lock_irq(&netbk->tx.schedule_list_lock);
	remove_from_net_schedule_list(netif);
	spin_unlock_irq(&netbk->tx.schedule_list_lock);
}


static void tx_add_credit(netif_t *netif)
{
	unsigned long max_burst, max_credit;

	/*
	 * Allow a burst big enough to transmit a jumbo packet of up to 128kB.
	 * Otherwise the interface can seize up due to insufficient credit.
	 */
	max_burst = RING_GET_REQUEST(&netif->tx, netif->tx.req_cons)->size;
	max_burst = min(max_burst, 131072UL);
	max_burst = max(max_burst, netif->credit_bytes);

	/* Take care that adding a new chunk of credit doesn't wrap to zero. */
	max_credit = netif->remaining_credit + netif->credit_bytes;
	if (max_credit < netif->remaining_credit)
		max_credit = ULONG_MAX; /* wrapped: clamp to ULONG_MAX */

	netif->remaining_credit = min(max_credit, max_burst);
}

static void tx_credit_callback(unsigned long data)
{
	netif_t *netif = (netif_t *)data;
	tx_add_credit(netif);
	netif_schedule_work(netif);
}

static inline int copy_pending_req(struct xen_netbk *netbk,
				   pending_ring_idx_t pending_idx)
{
	return gnttab_copy_grant_page(netbk->tx.pending_info[pending_idx].grant_handle,
				      &netbk->tx.mmap_pages[pending_idx]);
}

static void permute_dealloc_ring(u16 *dealloc_ring, pending_ring_idx_t dc,
				 pending_ring_idx_t dp)
{
	static unsigned random_src = 0x12345678;
	unsigned dst_offset;
	pending_ring_idx_t dest;
	u16 tmp;

	while (dc != dp) {
		dst_offset = (random_src / 256) % (dp - dc);
		dest = dc + dst_offset;
		tmp = dealloc_ring[MASK_PEND_IDX(dest)];
		dealloc_ring[MASK_PEND_IDX(dest)] =
			dealloc_ring[MASK_PEND_IDX(dc)];
		dealloc_ring[MASK_PEND_IDX(dc)] = tmp;
		dc++;
		random_src *= 68389;
	}
}

inline static void net_tx_action_dealloc(struct xen_netbk *netbk)
{
	struct pending_tx_info *pending_tx_info = netbk->tx.pending_info;
	struct netbk_tx_pending_inuse *inuse, *n;
	gnttab_unmap_grant_ref_t *gop;
	u16 pending_idx;
	pending_ring_idx_t dc, dp;
	netif_t *netif, *notify_head = NULL, *notify_tail = NULL;
	LIST_HEAD(list);

	dc = netbk->tx.dealloc_cons;
	gop = netbk->tx.unmap_ops;

	/*
	 * Free up any grants we have finished using
	 */
	do {
		dp = netbk->tx.dealloc_prod;

		/* Ensure we see all indices enqueued by netif_idx_release(). */
		smp_rmb();

		if (MODPARM_permute_returns && netbk_nr_groups == 1)
			permute_dealloc_ring(netbk->tx.dealloc_ring, dc, dp);

		while (dc != dp) {
			unsigned long pfn;
			struct netbk_tx_pending_inuse *pending_inuse =
					netbk->tx.pending_inuse;

			pending_idx = netbk->tx.dealloc_ring[MASK_PEND_IDX(dc++)];
			list_move_tail(&pending_inuse[pending_idx].list, &list);

			pfn = idx_to_pfn(netbk, pending_idx);
			/* Already unmapped? */
			if (!phys_to_machine_mapping_valid(pfn))
				continue;

			gnttab_set_unmap_op(gop, idx_to_kaddr(netbk, pending_idx),
					    GNTMAP_host_map,
					    netbk->tx.pending_info[pending_idx].grant_handle);
			gop++;
		}

	} while (dp != netbk->tx.dealloc_prod);

	netbk->tx.dealloc_cons = dc;

	if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
				      netbk->tx.unmap_ops,
				      gop - netbk->tx.unmap_ops))
		BUG();

	/* Copy any entries that have been pending for too long. */
	if (netbk_copy_skb_mode == NETBK_DELAYED_COPY_SKB &&
	    !list_empty(&netbk->tx.pending_inuse_head)) {
		list_for_each_entry_safe(inuse, n, &netbk->tx.pending_inuse_head, list) {
			if (time_after(inuse->alloc_time + HZ / 2, jiffies))
				break;

			pending_idx = inuse - netbk->tx.pending_inuse;

			pending_tx_info[pending_idx].netif->nr_copied_skbs++;

			switch (copy_pending_req(netbk, pending_idx)) {
			case 0:
				list_move_tail(&inuse->list, &list);
				continue;
			case -EBUSY:
				list_del_init(&inuse->list);
				continue;
			case -ENOENT:
				continue;
			}

			break;
		}
	}

	list_for_each_entry_safe(inuse, n, &list, list) {
		pending_idx = inuse - netbk->tx.pending_inuse;
		netif = pending_tx_info[pending_idx].netif;

		if (!make_tx_response(netif, &pending_tx_info[pending_idx].req,
				      XEN_NETIF_RSP_OKAY, &notify_tail))
			netif_put(netif);
		else if (!notify_head)
			notify_head = netif;

		/* Ready for next use. */
		gnttab_reset_grant_page(netbk->tx.mmap_pages[pending_idx]);

		netbk->tx.pending_ring[MASK_PEND_IDX(netbk->tx.pending_prod++)] =
			pending_idx;

		list_del_init(&inuse->list);
	}
	if (notify_head)
		flush_notify_list(notify_head, TX_IDX, netbk->tx.mcl,
				  sizeof(netbk->tx.map_ops)
				  / sizeof(*netbk->tx.mcl));
}

static void netbk_tx_err(netif_t *netif, netif_tx_request_t *txp, RING_IDX end)
{
	RING_IDX cons = netif->tx.req_cons;

	do {
		make_tx_response(netif, txp, XEN_NETIF_RSP_ERROR, NULL);
		if (cons == end)
			break;
		txp = RING_GET_REQUEST(&netif->tx, cons++);
	} while (1);
	netif->tx.req_cons = cons;
	netif->dev->stats.rx_errors++;
	netif_schedule_work(netif);
	netif_put(netif);
}

static void netbk_fatal_tx_err(netif_t *netif)
{
	netdev_err(netif->dev, "fatal error; disabling device\n");
	netif->busted = 1;
	disable_irq(netif->irq);
	netif_deschedule_work(netif);
	netif_put(netif);
}

static int netbk_count_requests(netif_t *netif, netif_tx_request_t *first,
				netif_tx_request_t *txp, int work_to_do)
{
	RING_IDX cons = netif->tx.req_cons;
	int slots = 0, drop_err = 0;

	if (!(first->flags & XEN_NETTXF_more_data))
		return 0;

	do {
		if (slots >= work_to_do) {
			netdev_err(netif->dev, "Need more slots\n");
			netbk_fatal_tx_err(netif);
			return -ENODATA;
		}

		if (unlikely(slots >= max_tx_slots)) {
			netdev_err(netif->dev, "Too many slots\n");
			netbk_fatal_tx_err(netif);
			return -E2BIG;
		}

		/*
		 * The Xen network protocol had an implicit dependency on
		 * MAX_SKB_FRAGS. XEN_NETIF_NR_SLOTS_MIN is set to the
		 * historical MAX_SKB_FRAGS value 18 to honor the same
		 * behavior as before. Any packet using more than 18 slots
		 * but less than max_tx_slots slots is dropped.
		 */
		switch (slots) {
		case 0 ... XEN_NETIF_NR_SLOTS_MIN - 1:
			break;
		case XEN_NETIF_NR_SLOTS_MIN:
			if (net_ratelimit())
				netdev_dbg(netif->dev,
					   "slot count exceeding limit of %d, dropping packet\n",
					   XEN_NETIF_NR_SLOTS_MIN);
			if (!drop_err)
				drop_err = -E2BIG;
			/* fall through */
		default:
			--txp;
			break;
		}

		*txp = *RING_GET_REQUEST(&netif->tx, cons + slots);

		/*
		 * If the guest submitted a frame >= 64 KiB then first->size
		 * overflowed and following slots will appear to be larger
		 * than the frame. This cannot be fatal error as there are
		 * buggy frontends that do this.
		 *
		 * Consume all slots and drop the packet.
		 */
		if (!drop_err && txp->size > first->size) {
			if (net_ratelimit())
				netdev_dbg(netif->dev,
					   "Invalid tx request (slot size %u > remaining size %u)\n",
					   txp->size, first->size);
			drop_err = -EIO;
		}

		first->size -= txp->size;
		slots++;

		if (unlikely((txp->offset + txp->size) > PAGE_SIZE)) {
			netdev_err(netif->dev, "txp->offset: %x, size: %u\n",
				   txp->offset, txp->size);
			netbk_fatal_tx_err(netif);
			return -EINVAL;
		}
	} while ((txp++)->flags & XEN_NETTXF_more_data);

	if (drop_err) {
		netbk_tx_err(netif, first, cons + slots);
		return drop_err;
	}

	return slots;
}

struct netbk_tx_gop {
	gnttab_map_grant_ref_t *map;
	gnttab_copy_t *copy;
	union {
		void *ptr;
		struct {
			netif_t *head, *tail;
		} notify;
	};
};

static void netbk_fill_tx_copy(const netif_tx_request_t *txreq,
			       struct netbk_tx_gop *gop, domid_t domid)
{
	gop->copy--;
	gop->copy->source.u.ref = txreq->gref;
	gop->copy->source.domid = domid;
	gop->copy->source.offset = txreq->offset;
	gop->copy->dest.u.gmfn = virt_to_mfn(gop->ptr);
	gop->copy->dest.domid = DOMID_SELF;
	gop->copy->dest.offset = offset_in_page(gop->ptr);
	gop->copy->flags = GNTCOPY_source_gref;

	if (gop->copy->dest.offset + txreq->size > PAGE_SIZE) {
		unsigned int first = PAGE_SIZE - gop->copy->dest.offset;

		gop->copy->len = first;
		gop->ptr += first;

		gop->copy--;
		gop->copy->source = gop->copy[-1].source;
		gop->copy->source.offset += first;
		gop->copy->dest.u.gmfn = virt_to_mfn(gop->ptr);
		gop->copy->dest.domid = DOMID_SELF;
		gop->copy->dest.offset = 0;
		gop->copy->flags = GNTCOPY_source_gref;
		gop->copy->len = txreq->size - first;
	} else
		gop->copy->len = txreq->size;

	gop->ptr += gop->copy->len;
}

void netbk_get_requests(struct xen_netbk *netbk, netif_t *netif,
			struct sk_buff *skb, struct netbk_tx_gop *gop)
{
	netif_tx_request_t *txp = netbk->tx.slots;
	struct pending_tx_info *pending_tx_info = netbk->tx.pending_info;
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	skb_frag_t *frags = shinfo->frags;
	u16 pending_idx = netbk_tx_cb(skb)->pending_idx[0];
	pending_ring_idx_t index;
	int i, start;

	/* Skip first skb fragment if it is on same page as header fragment. */
	start = (frag_get_pending_idx(frags) == pending_idx);

	for (i = 0; i < netbk_tx_cb(skb)->copy_slots; ++i, txp++) {
		index = MASK_PEND_IDX(netbk->tx.pending_cons++);
		pending_idx = netbk->tx.pending_ring[index];

		netbk_fill_tx_copy(txp, gop, netif->domid);

		pending_tx_info[pending_idx].req = *txp;
		netif_get(netif);
		pending_tx_info[pending_idx].netif = netif;
		netbk_tx_cb(skb)->pending_idx[1 + i] = pending_idx;
	}

	for (i = start; i < shinfo->nr_frags; i++, txp++) {
		index = MASK_PEND_IDX(netbk->tx.pending_cons++);
		pending_idx = netbk->tx.pending_ring[index];

		gnttab_set_map_op(gop->map++, idx_to_kaddr(netbk, pending_idx),
				  GNTMAP_host_map | GNTMAP_readonly,
				  txp->gref, netif->domid);

		memcpy(&pending_tx_info[pending_idx].req, txp, sizeof(*txp));
		netif_get(netif);
		pending_tx_info[pending_idx].netif = netif;
		frag_set_pending_idx(&frags[i], pending_idx);
	}

	if ((void *)gop->map > (void *)gop->copy)
		net_warn_ratelimited("%s: Grant op overrun (%p > %p)\n",
				     netdev_name(netif->dev),
				     gop->map, gop->copy);
}

static int netbk_tx_check_gop(struct xen_netbk *netbk, struct sk_buff *skb,
			      struct netbk_tx_gop *gop, bool hdr_copied)
{
	gnttab_copy_t *cop = gop->copy;
	gnttab_map_grant_ref_t *mop = gop->map;
	u16 pending_idx = netbk_tx_cb(skb)->pending_idx[0];
	pending_ring_idx_t index;
	struct pending_tx_info *pending_tx_info = netbk->tx.pending_info;
	netif_t *netif = pending_tx_info[pending_idx].netif;
	netif_tx_request_t *txp = &pending_tx_info[pending_idx].req;
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	int nr_frags = shinfo->nr_frags;
	int i, err, start;

	/* Check status of header. */
	if (hdr_copied) {
		err = (--cop)->status;
		if (txp->size > cop->len)
			cmpxchg_local(&err, GNTST_okay, (--cop)->status);
		if (!make_tx_response(netif, txp,
				      err == GNTST_okay ? XEN_NETIF_RSP_OKAY
							: XEN_NETIF_RSP_ERROR,
				      &gop->notify.tail))
			netif_put(netif);
		else if (!gop->notify.head)
			gop->notify.head = netif;
		index = MASK_PEND_IDX(netbk->tx.pending_prod++);
		netbk->tx.pending_ring[index] = pending_idx;
	} else if (unlikely((err = mop->status) != GNTST_okay)) {
		++mop;
		make_tx_response(netif, txp, XEN_NETIF_RSP_ERROR, NULL);
		index = MASK_PEND_IDX(netbk->tx.pending_prod++);
		netbk->tx.pending_ring[index] = pending_idx;
		netif_put(netif);
	} else {
		set_phys_to_machine(idx_to_pfn(netbk, pending_idx),
			FOREIGN_FRAME(mop->dev_bus_addr >> PAGE_SHIFT));
		container_of(txp, struct pending_tx_info, req)->grant_handle
			= mop++->handle;
	}

	/* Skip first skb fragment if it is on same page as header fragment. */
	start = (frag_get_pending_idx(shinfo->frags) == pending_idx);

	for (i = 0; i < netbk_tx_cb(skb)->copy_slots; ++i) {
		int newerr = (--cop)->status;

		pending_idx = netbk_tx_cb(skb)->pending_idx[1 + i];
		txp = &pending_tx_info[pending_idx].req;
		if (txp->size > cop->len)
			cmpxchg_local(&newerr, GNTST_okay, (--cop)->status);
		if (!make_tx_response(netif, txp,
				      newerr == GNTST_okay ? XEN_NETIF_RSP_OKAY
							   : XEN_NETIF_RSP_ERROR,
				      &gop->notify.tail))
			netif_put(netif);
		else if (!gop->notify.head)
			gop->notify.head = netif;
		cmpxchg_local(&err, GNTST_okay, newerr);
		index = MASK_PEND_IDX(netbk->tx.pending_prod++);
		netbk->tx.pending_ring[index] = pending_idx;
	}

	for (i = start; i < nr_frags; i++, mop++) {
		int j, newerr;

		pending_idx = frag_get_pending_idx(&shinfo->frags[i]);
		txp = &pending_tx_info[pending_idx].req;

		/* Check error status: if okay then remember grant handle. */
		newerr = mop->status;
		if (likely(newerr == GNTST_okay)) {
			set_phys_to_machine(idx_to_pfn(netbk, pending_idx),
				FOREIGN_FRAME(mop->dev_bus_addr>>PAGE_SHIFT));
			container_of(txp, struct pending_tx_info, req)->grant_handle
				= mop->handle;
			/* Had a previous error? Invalidate this fragment. */
			if (unlikely(err != GNTST_okay))
				netif_idx_release(netbk, pending_idx);
			continue;
		}

		/* Error on this fragment: respond to client with an error. */
		make_tx_response(netif, txp, XEN_NETIF_RSP_ERROR, NULL);
		index = MASK_PEND_IDX(netbk->tx.pending_prod++);
		netbk->tx.pending_ring[index] = pending_idx;
		netif_put(netif);

		/* Not the first error? Preceding frags already invalidated. */
		if (err != GNTST_okay)
			continue;

		/* First error: invalidate header and preceding fragments. */
		if (!hdr_copied) {
			pending_idx = netbk_tx_cb(skb)->pending_idx[0];
			netif_idx_release(netbk, pending_idx);
		}
		for (j = start; j < i; j++) {
			pending_idx = frag_get_pending_idx(&shinfo->frags[j]);
			netif_idx_release(netbk, pending_idx);
		}

		/* Remember the error: invalidate all subsequent fragments. */
		err = newerr;
	}

	gop->map = mop;
	gop->copy = cop;
	if ((void *)mop > (void *)cop)
		net_warn_ratelimited("%s: Grant op check overrun (%p > %p)\n",
				     netdev_name(netif->dev), mop, cop);
	return err;
}

static void netbk_fill_frags(struct xen_netbk *netbk, struct sk_buff *skb)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	int nr_frags = shinfo->nr_frags;
	int i;

	for (i = 0; i < nr_frags; i++) {
		netif_tx_request_t *txp;
		u16 pending_idx = frag_get_pending_idx(shinfo->frags + i);

		netbk->tx.pending_inuse[pending_idx].alloc_time = jiffies;
		list_add_tail(&netbk->tx.pending_inuse[pending_idx].list,
			      &netbk->tx.pending_inuse_head);

		txp = &netbk->tx.pending_info[pending_idx].req;
		__skb_fill_page_desc(skb, i, netbk->tx.mmap_pages[pending_idx],
				     txp->offset, txp->size);

		skb->len += txp->size;
		skb->data_len += txp->size;
		skb->truesize += txp->size;
	}
}

int netbk_get_extras(netif_t *netif, struct netif_extra_info *extras,
		     int work_to_do)
{
	struct netif_extra_info extra;
	RING_IDX cons = netif->tx.req_cons;

	do {
		if (unlikely(work_to_do-- <= 0)) {
			netdev_err(netif->dev, "Missing extra info\n");
			netbk_fatal_tx_err(netif);
			return -EBADR;
		}

		memcpy(&extra, RING_GET_REQUEST(&netif->tx, cons),
		       sizeof(extra));
		if (unlikely(!extra.type ||
			     extra.type >= XEN_NETIF_EXTRA_TYPE_MAX)) {
			netif->tx.req_cons = ++cons;
			netdev_dbg(netif->dev, "Invalid extra type: %d\n",
				   extra.type);
			netbk_fatal_tx_err(netif);
			return -EINVAL;
		}

		memcpy(&extras[extra.type - 1], &extra, sizeof(extra));
		netif->tx.req_cons = ++cons;
	} while (extra.flags & XEN_NETIF_EXTRA_FLAG_MORE);

	return work_to_do;
}

static int netbk_set_skb_gso(netif_t *netif, struct sk_buff *skb,
			     struct netif_extra_info *gso)
{
	if (!gso->u.gso.size) {
		netdev_err(skb->dev, "GSO size must not be zero.\n");
		netbk_fatal_tx_err(netif);
		return -EINVAL;
	}

	/* Currently only TCPv4 S.O. is supported. */
	if (gso->u.gso.type != XEN_NETIF_GSO_TYPE_TCPV4) {
		netdev_err(skb->dev, "Bad GSO type %d.\n", gso->u.gso.type);
		netbk_fatal_tx_err(netif);
		return -EINVAL;
	}

	skb_shinfo(skb)->gso_size = gso->u.gso.size;
	skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;

	/* Header must be checked, and gso_segs computed. */
	skb_shinfo(skb)->gso_type |= SKB_GSO_DODGY;
	skb_shinfo(skb)->gso_segs = 0;

	return 0;
}

/* Called after netfront has transmitted */
static void net_tx_action(unsigned long group)
{
	struct xen_netbk *netbk = &xen_netbk[group];
	struct sk_buff *skb;
	netif_t *netif;
	netif_tx_request_t txreq, *txslot;
	struct netif_extra_info extras[XEN_NETIF_EXTRA_TYPE_MAX - 1];
	u16 pending_idx;
	RING_IDX i;
	struct netbk_tx_gop gop;
	multicall_entry_t mcl[2];
	unsigned int data_len;
	int ret, work_to_do;

	BUILD_BUG_ON(sizeof(skb->cb) < sizeof(struct netbk_tx_cb));

	net_tx_action_dealloc(netbk);

	gop.map = netbk->tx.map_ops;
	gop.copy = &netbk->tx.copy_op + 1;
	while (nr_pending_reqs(netbk) + XEN_NETIF_NR_SLOTS_MIN < MAX_PENDING_REQS
	       && !list_empty(&netbk->tx.schedule_list)) {
		/* Get a netif from the list with work to do. */
		netif = poll_net_schedule_list(netbk);
		/*
		 * This can sometimes happen because the test of
		 * list_empty(net_schedule_list) at the top of the
		 * loop is unlocked.  Just go back and have another
		 * look.
		 */
		if (!netif)
			continue;

		if (unlikely(netif->busted)) {
			netif_put(netif);
			continue;
		}

		if (netif->tx.sring->req_prod - netif->tx.req_cons >
		    NET_TX_RING_SIZE) {
			netdev_err(netif->dev,
				   "Impossible number of requests"
				   " (prod=%u cons=%u size=%lu)\n",
				   netif->tx.sring->req_prod,
				   netif->tx.req_cons, NET_TX_RING_SIZE);
			netbk_fatal_tx_err(netif);
			continue;
		}

		RING_FINAL_CHECK_FOR_REQUESTS(&netif->tx, work_to_do);
		if (!work_to_do) {
			netif_put(netif);
			continue;
		}

		i = netif->tx.req_cons;
		rmb(); /* Ensure that we see the request before we copy it. */
		memcpy(&txreq, RING_GET_REQUEST(&netif->tx, i), sizeof(txreq));

		/* Credit-based scheduling. */
		if (txreq.size > netif->remaining_credit) {
			unsigned long now = jiffies;
			unsigned long next_credit = 
				netif->credit_timeout.expires +
				msecs_to_jiffies(netif->credit_usec / 1000);

			/* Timer could already be pending in rare cases. */
			if (timer_pending(&netif->credit_timeout)) {
				netif_put(netif);
				continue;
			}

			/* Passed the point where we can replenish credit? */
			if (time_after_eq(now, next_credit)) {
				netif->credit_timeout.expires = now;
				tx_add_credit(netif);
			}

			/* Still too big to send right now? Set a callback. */
			if (txreq.size > netif->remaining_credit) {
				netif->credit_timeout.data     =
					(unsigned long)netif;
				netif->credit_timeout.function =
					tx_credit_callback;
				mod_timer(&netif->credit_timeout, next_credit);
				netif_put(netif);
				continue;
			}
		}
		netif->remaining_credit -= txreq.size;

		work_to_do--;
		netif->tx.req_cons = ++i;

		memset(extras, 0, sizeof(extras));
		if (txreq.flags & XEN_NETTXF_extra_info) {
			work_to_do = netbk_get_extras(netif, extras,
						      work_to_do);
			i = netif->tx.req_cons;
			if (unlikely(work_to_do < 0))
				continue;
		}

		txslot = netbk->tx.slots;
		ret = netbk_count_requests(netif, &txreq, txslot, work_to_do);
		if (unlikely(ret < 0))
			continue;

		i += ret;

		if (unlikely(txreq.size < ETH_HLEN)) {
			netdev_dbg(netif->dev, "Bad packet size: %d\n",
				   txreq.size);
			netbk_tx_err(netif, &txreq, i);
			continue;
		}

		/* No crossing a page as the payload mustn't fragment. */
		if (unlikely((txreq.offset + txreq.size) > PAGE_SIZE)) {
			netdev_err(netif->dev,
				   "txreq.offset: %x, size: %u, end: %lu\n",
				   txreq.offset, txreq.size,
				   (txreq.offset & ~PAGE_MASK) + txreq.size);
			netbk_fatal_tx_err(netif);
			continue;
		}

		pending_idx = netbk->tx.pending_ring[MASK_PEND_IDX(netbk->tx.pending_cons)];

		data_len = (txreq.size > PKT_PROT_LEN &&
			    ret < MAX_SKB_FRAGS) ?
			PKT_PROT_LEN : txreq.size;
		while (ret > MAX_SKB_FRAGS ||
		       (ret && (data_len + txslot->size <= PKT_PROT_LEN ||
				netbk_copy_skb_mode == NETBK_ALWAYS_COPY_SKB))) {
			data_len += txslot++->size;
			--ret;
		}

		skb = alloc_skb(data_len + 16 + NET_IP_ALIGN,
				GFP_ATOMIC | __GFP_NOWARN);
		if (unlikely(skb == NULL)) {
			netdev_dbg(netif->dev,
				   "Can't allocate a skb in start_xmit.\n");
			netbk_tx_err(netif, &txreq, i);
			break;
		}

		/* Packets passed to netif_rx() must have some headroom. */
		skb_reserve(skb, 16 + NET_IP_ALIGN);
		skb->dev = netif->dev;

		if (extras[XEN_NETIF_EXTRA_TYPE_GSO - 1].type) {
			struct netif_extra_info *gso;
			gso = &extras[XEN_NETIF_EXTRA_TYPE_GSO - 1];

			if (netbk_set_skb_gso(netif, skb, gso)) {
				/* Failure in netbk_set_skb_gso is fatal. */
				kfree_skb(skb);
				continue;
			}
		}

		netbk->tx.pending_info[pending_idx].req = txreq;
		netbk->tx.pending_info[pending_idx].netif = netif;
		netbk_tx_cb(skb)->pending_idx[0] = pending_idx;
		netbk_tx_cb(skb)->copy_slots = txslot - netbk->tx.slots;

		__skb_put(skb, data_len);
		gop.ptr = skb->data;

		skb_shinfo(skb)->nr_frags = ret;
		if (data_len < txreq.size) {
			gnttab_set_map_op(gop.map++,
					  idx_to_kaddr(netbk, pending_idx),
					  GNTMAP_host_map | GNTMAP_readonly,
					  txreq.gref, netif->domid);
			skb_shinfo(skb)->nr_frags++;
		} else {
			netbk_fill_tx_copy(&txreq, &gop, netif->domid);
			pending_idx = INVALID_PENDING_IDX;
		}
		frag_set_pending_idx(skb_shinfo(skb)->frags, pending_idx);

		__skb_queue_tail(&netbk->tx.queue, skb);

		netbk->tx.pending_cons++;

		netbk_get_requests(netbk, netif, skb, &gop);

		netif->tx.req_cons = i;
		netif_schedule_work(netif);
	}

	if (skb_queue_empty(&netbk->tx.queue))
		goto out;

    /* NOTE: some maps may fail with GNTST_eagain, which could be successfully
     * retried in the backend after a delay. However, we can also fail the tx
     * req and let the frontend resend the relevant packet again. This is fine
     * because it is unlikely that a network buffer will be paged out or shared,
     * and therefore it is unlikely to fail with GNTST_eagain. */
	MULTI_grant_table_op(&mcl[0], GNTTABOP_copy, gop.copy,
			     &netbk->tx.copy_op + 1 - gop.copy);
	MULTI_grant_table_op(&mcl[1], GNTTABOP_map_grant_ref,
			     netbk->tx.map_ops, gop.map - netbk->tx.map_ops);
	if (HYPERVISOR_multicall_check(mcl, 2, NULL))
		BUG();

	gop.map = netbk->tx.map_ops;
	gop.copy = &netbk->tx.copy_op + 1;
	gop.notify.head = NULL;
	gop.notify.tail = NULL;
	while ((skb = __skb_dequeue(&netbk->tx.queue)) != NULL) {
		struct net_device *dev;
		netif_tx_request_t *txp;

		pending_idx = netbk_tx_cb(skb)->pending_idx[0];
		netif       = netbk->tx.pending_info[pending_idx].netif;
		dev         = netif->dev;
		txp         = &netbk->tx.pending_info[pending_idx].req;
		data_len    = skb->len;

		/* Check the remap/copy error code. */
		if (unlikely(netbk_tx_check_gop(netbk, skb, &gop,
						data_len >= txp->size))) {
			netdev_dbg(dev, "netback grant failed.\n");
			skb_shinfo(skb)->nr_frags = 0;
			kfree_skb(skb);
			dev->stats.rx_errors++;
			continue;
		}

		if (data_len < txp->size) {
			memcpy(skb->data,
			       (void *)(idx_to_kaddr(netbk, pending_idx)
					+ txp->offset),
			       data_len);
			/* Append the packet payload as a fragment. */
			txp->offset += data_len;
			txp->size -= data_len;
		}

		if (txp->flags & XEN_NETTXF_csum_blank)
			skb->ip_summed = CHECKSUM_PARTIAL;
		else if (txp->flags & XEN_NETTXF_data_validated)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;

		netbk_fill_frags(netbk, skb);

		/*
		 * If the initial fragment was < PKT_PROT_LEN then
		 * pull through some bytes from the other fragments to
		 * increase the linear region to PKT_PROT_LEN bytes.
		 */
		if (skb_headlen(skb) < PKT_PROT_LEN && skb_is_nonlinear(skb)) {
			int target = min_t(int, skb->len, PKT_PROT_LEN);
			__pskb_pull_tail(skb, target - skb_headlen(skb));
		}

		skb->protocol = eth_type_trans(skb, dev);

		if (skb_checksum_setup(skb, &netif->rx_gso_csum_fixups)) {
			netdev_dbg(dev,
				   "Can't setup checksum in net_tx_action\n");
			kfree_skb(skb);
			dev->stats.rx_dropped++;
			continue;
		}

		skb_probe_transport_header(skb, 0);

		dev->stats.rx_bytes += skb->len;
		dev->stats.rx_packets++;

		if (use_kthreads)
			netif_rx_ni(skb);
		else
			netif_rx(skb);
	}

	if (gop.notify.head)
		flush_notify_list(gop.notify.head, TX_IDX, netbk->tx.mcl,
				  sizeof(netbk->tx.map_ops)
				  / sizeof(*netbk->tx.mcl));

 out:
	if (netbk_copy_skb_mode == NETBK_DELAYED_COPY_SKB &&
	    !list_empty(&netbk->tx.pending_inuse_head)) {
		struct netbk_tx_pending_inuse *oldest;

		oldest = list_entry(netbk->tx.pending_inuse_head.next,
				    struct netbk_tx_pending_inuse, list);
		mod_timer(&netbk->tx.pending_timer, oldest->alloc_time + HZ);
	}
}

static void netif_idx_release(struct xen_netbk *netbk, u16 pending_idx)
{
	unsigned long flags;

	spin_lock_irqsave(&netbk->tx.release_lock, flags);
	netbk->tx.dealloc_ring[MASK_PEND_IDX(netbk->tx.dealloc_prod)] = pending_idx;
	/* Sync with net_tx_action_dealloc: insert idx /then/ incr producer. */
	smp_wmb();
	netbk->tx.dealloc_prod++;
	spin_unlock_irqrestore(&netbk->tx.release_lock, flags);

	netbk_tx_schedule(netbk);
}

static void netif_page_release(struct page *page, unsigned int order)
{
	struct xen_netbk *netbk;
	unsigned int idx;

	BUG_ON(order);
	netif_get_page_ext(page, netbk, idx);
	BUG_ON(!netbk);
	netif_idx_release(netbk, idx);
}

irqreturn_t netif_be_int(int irq, void *dev_id)
{
	netif_t *netif = dev_id;
	unsigned int group = GET_GROUP_INDEX(netif);

	if (unlikely(group >= netbk_nr_groups)) {
		/*
		 * Short of having a way to bind the IRQ in disabled mode
		 * (IRQ_NOAUTOEN), we have to ignore the first invocation(s)
		 * (before we got assigned to a group).
		 */
		BUG_ON(group != UINT_MAX);
		return IRQ_HANDLED;
	}

	add_to_net_schedule_list_tail(netif);
	maybe_schedule_tx_action(netif);

	if (netif_schedulable(netif) && !netbk_queue_full(netif))
		netif_wake_queue(netif->dev);

	return IRQ_HANDLED;
}

static bool make_tx_response(netif_t *netif, const netif_tx_request_t *txp,
			     s8 st, netif_t **tailp)
{
	RING_IDX i = netif->tx.rsp_prod_pvt;
	netif_tx_response_t *resp;
	int notify;

	resp = RING_GET_RESPONSE(&netif->tx, i);
	resp->id     = txp->id;
	resp->status = st;

	if (txp->flags & XEN_NETTXF_extra_info)
		RING_GET_RESPONSE(&netif->tx, ++i)->status = XEN_NETIF_RSP_NULL;

	netif->tx.rsp_prod_pvt = ++i;
	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&netif->tx, notify);
	if (notify) {
		if (!tailp)
			notify_remote_via_irq(netif->irq);
		else if (netif != *tailp && !netif->tx_notify_link) {
			if (*tailp)
				(*tailp)->tx_notify_link = netif;
			*tailp = netif;
		} else
			notify = false;
	}

#ifdef CONFIG_XEN_NETDEV_PIPELINED_TRANSMITTER
	if (i == netif->tx.req_cons) {
		int more_to_do;
		RING_FINAL_CHECK_FOR_REQUESTS(&netif->tx, more_to_do);
		if (more_to_do)
			add_to_net_schedule_list_tail(netif);
	}
#endif

	return notify;
}

static netif_rx_response_t *make_rx_response(netif_t *netif, 
					     u16      id, 
					     s8       st,
					     u16      offset,
					     u16      size,
					     u16      flags)
{
	RING_IDX i = netif->rx.rsp_prod_pvt;
	netif_rx_response_t *resp;

	resp = RING_GET_RESPONSE(&netif->rx, i);
	resp->offset     = offset;
	resp->flags      = flags;
	resp->id         = id;
	resp->status     = (s16)size;
	if (st < 0)
		resp->status = (s16)st;

	netif->rx.rsp_prod_pvt = ++i;

	return resp;
}

#ifdef NETBE_DEBUG_INTERRUPT
static irqreturn_t netif_be_dbg(int irq, void *dev_id)
{
	netif_t *netif;
	unsigned int i = 0, group;

	pr_alert("netif_schedule_list:\n");

	for (group = 0; group < netbk_nr_groups; ++group) {
		struct xen_netbk *netbk = &xen_netbk[group];

		spin_lock_irq(&netbk->tx.schedule_list_lock);

		list_for_each_entry(netif, &netbk->tx.schedule_list, list) {
			pr_alert(" %d: private(rx_req_cons=%08x "
				 "rx_resp_prod=%08x\n", i,
				 netif->rx.req_cons, netif->rx.rsp_prod_pvt);
			pr_alert("   tx_req_cons=%08x tx_resp_prod=%08x)\n",
				 netif->tx.req_cons, netif->tx.rsp_prod_pvt);
			pr_alert("   shared(rx_req_prod=%08x "
				 "rx_resp_prod=%08x\n",
				 netif->rx.sring->req_prod,
				 netif->rx.sring->rsp_prod);
			pr_alert("   rx_event=%08x tx_req_prod=%08x\n",
				 netif->rx.sring->rsp_event,
				 netif->tx.sring->req_prod);
			pr_alert("   tx_resp_prod=%08x, tx_event=%08x)\n",
				 netif->tx.sring->rsp_prod,
				 netif->tx.sring->rsp_event);
			i++;
		}

		spin_unlock_irq(&netbk->tx.schedule_list_lock);
	}

	pr_alert(" ** End of netif_schedule_list **\n");

	return IRQ_HANDLED;
}

static struct irqaction netif_be_dbg_action = {
	.handler = netif_be_dbg,
	.flags   = IRQF_SHARED,
	.name    = "net-be-dbg"
};
#endif

static inline int rx_work_todo(struct xen_netbk *netbk)
{
	return !skb_queue_empty(&netbk->rx.queue);
}

static inline int tx_work_todo(struct xen_netbk *netbk)
{
	if (netbk->tx.dealloc_cons != netbk->tx.dealloc_prod)
		return 1;

	if (netbk_copy_skb_mode == NETBK_DELAYED_COPY_SKB &&
	    !list_empty(&netbk->tx.pending_inuse_head))
		return 1;

	if (nr_pending_reqs(netbk) + XEN_NETIF_NR_SLOTS_MIN < MAX_PENDING_REQS
	    && !list_empty(&netbk->tx.schedule_list))
		return 1;

	return 0;
}

static int netbk_action_thread(void *index)
{
	unsigned long group = (unsigned long)index;
	struct xen_netbk *netbk = &xen_netbk[group];

	while (!kthread_should_stop()) {
		wait_event_interruptible(netbk->action_wq,
					 rx_work_todo(netbk) ||
					 tx_work_todo(netbk) ||
					 kthread_should_stop());
		cond_resched();

		if (rx_work_todo(netbk))
			net_rx_action(group);

		if (tx_work_todo(netbk))
			net_tx_action(group);
	}

	return 0;
}


static int __init netback_init(void)
{
	unsigned int i, group;
	int rc;
	struct page *page;

	if (!is_running_on_xen())
		return -ENODEV;

	BUILD_BUG_ON(XEN_NETIF_NR_SLOTS_MIN >= MAX_PENDING_REQS);
	if (max_tx_slots < XEN_NETIF_NR_SLOTS_MIN) {
		pr_info("netback: max_tx_slots too small (%u), using XEN_NETIF_NR_SLOTS_MIN (%d)\n",
			max_tx_slots, XEN_NETIF_NR_SLOTS_MIN);
		max_tx_slots = XEN_NETIF_NR_SLOTS_MIN;
	}

	group = netbk_nr_groups;
	if (!netbk_nr_groups)
		netbk_nr_groups = (num_online_cpus() + 1) / 2;
	if (netbk_nr_groups > MAX_GROUPS)
		netbk_nr_groups = MAX_GROUPS;

	do {
		xen_netbk = vzalloc(netbk_nr_groups * sizeof(*xen_netbk));
	} while (!xen_netbk && (netbk_nr_groups >>= 1));
	if (!xen_netbk)
		return -ENOMEM;
	if (group && netbk_nr_groups != group)
		pr_warn("netback: only using %u (instead of %u) groups\n",
			netbk_nr_groups, group);

	/* We can increase reservation by this much in net_rx_action(). */
	balloon_update_driver_allowance(netbk_nr_groups * NET_RX_RING_SIZE);

	for (group = 0; group < netbk_nr_groups; group++) {
		struct xen_netbk *netbk = &xen_netbk[group];

		skb_queue_head_init(&netbk->rx.queue);
		skb_queue_head_init(&netbk->tx.queue);

		init_timer(&netbk->rx.timer);
		netbk->rx.timer.data = group;
		netbk->rx.timer.function = net_alarm;

		init_timer(&netbk->tx.pending_timer);
		netbk->tx.pending_timer.data = group;
		netbk->tx.pending_timer.function =
			netbk_tx_pending_timeout;

		netbk->tx.pending_prod = MAX_PENDING_REQS;

		INIT_LIST_HEAD(&netbk->tx.pending_inuse_head);
		INIT_LIST_HEAD(&netbk->tx.schedule_list);

		spin_lock_init(&netbk->tx.schedule_list_lock);
		spin_lock_init(&netbk->tx.release_lock);

		netbk->tx.mmap_pages =
			alloc_empty_pages_and_pagevec(MAX_PENDING_REQS);
		if (netbk->tx.mmap_pages == NULL) {
			pr_err("%s: out of memory\n", __func__);
			rc = -ENOMEM;
			goto failed_init;
		}

		for (i = 0; i < MAX_PENDING_REQS; i++) {
			page = netbk->tx.mmap_pages[i];
			SetPageForeign(page, netif_page_release);
			netif_set_page_ext(page, group, i);
			netbk->tx.pending_ring[i] = i;
			INIT_LIST_HEAD(&netbk->tx.pending_inuse[i].list);
		}

		if (use_kthreads) {
			init_waitqueue_head(&netbk->action_wq);
			netbk->task = kthread_create(netbk_action_thread,
						     (void *)(long)group,
						     "netback/%u", group);

			if (IS_ERR(netbk->task)) {
				pr_err("netback: kthread_create() failed\n");
				rc = PTR_ERR(netbk->task);
				goto failed_init;
			}
			if (bind_threads)
				kthread_bind(netbk->task,
					     group % num_online_cpus());
			wake_up_process(netbk->task);
		} else {
			tasklet_init(&netbk->tx.tasklet, net_tx_action, group);
			tasklet_init(&netbk->rx.tasklet, net_rx_action, group);
		}
	}

	netbk_copy_skb_mode = NETBK_DONT_COPY_SKB;
	if (MODPARM_copy_skb) {
#if CONFIG_XEN_COMPAT < 0x030200
		if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_and_replace,
					      NULL, 0))
			netbk_copy_skb_mode = NETBK_ALWAYS_COPY_SKB;
		else
#endif
			netbk_copy_skb_mode = NETBK_DELAYED_COPY_SKB;
	}

	netif_accel_init();

	netif_xenbus_init();

#ifdef NETBE_DEBUG_INTERRUPT
	(void)bind_virq_to_irqaction(VIRQ_DEBUG,
				     0,
				     &netif_be_dbg_action);
#endif

	return 0;

failed_init:
	do {
		struct xen_netbk *netbk = &xen_netbk[group];

		if (use_kthreads && netbk->task && !IS_ERR(netbk->task))
			kthread_stop(netbk->task);
		if (netbk->tx.mmap_pages)
			free_empty_pages_and_pagevec(netbk->tx.mmap_pages,
						     MAX_PENDING_REQS);
	} while (group--);
	vfree(xen_netbk);
	balloon_update_driver_allowance(-(long)netbk_nr_groups
					* NET_RX_RING_SIZE);

	return rc;
}

module_init(netback_init);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("xen-backend:vif");
