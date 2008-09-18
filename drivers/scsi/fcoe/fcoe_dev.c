/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

/*
 * FCOE protocol file
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/kthread.h>
#include <linux/crc32.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <net/rtnetlink.h>

#include <scsi/fc/fc_encaps.h>

#include <scsi/libfc/libfc.h>
#include <scsi/libfc/fc_frame.h>

#include <scsi/fc/fc_fcoe.h>
#include "fcoe_def.h"

#define FCOE_MAX_QUEUE_DEPTH  256

/* destination address mode */
#define FCOE_GW_ADDR_MODE	    0x00
#define FCOE_FCOUI_ADDR_MODE	    0x01

/* Function Prototyes */
static int fcoe_check_wait_queue(struct fc_lport *);
static void fcoe_insert_wait_queue_head(struct fc_lport *, struct sk_buff *);
static void fcoe_insert_wait_queue(struct fc_lport *, struct sk_buff *);
static void fcoe_recv_flogi(struct fcoe_softc *, struct fc_frame *, u8 *);

/*
 * this is the fcoe receive function
 * called by NET_RX_SOFTIRQ
 * this function will receive the packet and
 * build fc frame and pass it up
 */
int fcoe_rcv(struct sk_buff *skb, struct net_device *dev,
	     struct packet_type *ptype, struct net_device *olddev)
{
	struct fc_lport *lp;
	struct fcoe_rcv_info *fr;
	struct fcoe_softc *fc;
	struct fcoe_dev_stats *stats;
	u8 *data;
	struct fc_frame_header *fh;
	unsigned short oxid;
	int cpu_idx;
	struct fcoe_percpu_s *fps;
	struct fcoe_info *fci = &fcoei;

	fc = container_of(ptype, struct fcoe_softc, fcoe_packet_type);
	lp = fc->lp;
	if (unlikely(lp == NULL)) {
		FC_DBG("cannot find hba structure");
		goto err2;
	}

	if (unlikely(debug_fcoe)) {
		FC_DBG("skb_info: len:%d data_len:%d head:%p data:%p tail:%p "
		       "end:%p sum:%d dev:%s", skb->len, skb->data_len,
		       skb->head, skb->data, skb_tail_pointer(skb),
		       skb_end_pointer(skb), skb->csum,
		       skb->dev ? skb->dev->name : "<NULL>");

	}

	/* check for FCOE packet type */
	if (unlikely(eth_hdr(skb)->h_proto != htons(ETH_P_FCOE))) {
		FC_DBG("wrong FC type frame");
		goto err;
	}
	data = skb->data;
	data += sizeof(struct fcoe_hdr);
	fh = (struct fc_frame_header *)data;
	oxid = ntohs(fh->fh_ox_id);

	fr = fcoe_dev_from_skb(skb);
	fr->fr_dev = lp;
	fr->ptype = ptype;
	cpu_idx = 0;
#ifdef CONFIG_SMP
	/*
	 * The exchange ID are ANDed with num of online CPUs,
	 * so that will have the least lock contention in
	 * handling the exchange. if there is no thread
	 * for a given idx then use first online cpu.
	 */
	cpu_idx = oxid & (num_online_cpus() >> 1);
	if (fci->fcoe_percpu[cpu_idx] == NULL)
		cpu_idx = first_cpu(cpu_online_map);
#endif
	fps = fci->fcoe_percpu[cpu_idx];

	spin_lock_bh(&fps->fcoe_rx_list.lock);
	__skb_queue_tail(&fps->fcoe_rx_list, skb);
	if (fps->fcoe_rx_list.qlen == 1)
		wake_up_process(fps->thread);

	spin_unlock_bh(&fps->fcoe_rx_list.lock);

	return 0;
err:
#ifdef CONFIG_SMP
	stats = lp->dev_stats[smp_processor_id()];
#else
	stats = lp->dev_stats[0];
#endif
	stats->ErrorFrames++;

err2:
	kfree_skb(skb);
	return -1;
}

static inline int fcoe_start_io(struct sk_buff *skb)
{
	int rc;

	skb_get(skb);
	rc = dev_queue_xmit(skb);
	if (rc != 0)
		return rc;
	kfree_skb(skb);
	return 0;
}

static int fcoe_get_paged_crc_eof(struct sk_buff *skb, int tlen)
{
	struct fcoe_info *fci = &fcoei;
	struct fcoe_percpu_s *fps;
	struct page *page;
	int cpu_idx;

	cpu_idx = get_cpu();
	fps = fci->fcoe_percpu[cpu_idx];
	page = fps->crc_eof_page;
	if (!page) {
		page = alloc_page(GFP_ATOMIC);
		if (!page) {
			put_cpu();
			return -ENOMEM;
		}
		fps->crc_eof_page = page;
		WARN_ON(fps->crc_eof_offset != 0);
	}

	get_page(page);
	skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags, page,
			   fps->crc_eof_offset, tlen);
	skb->len += tlen;
	skb->data_len += tlen;
	skb->truesize += tlen;
	fps->crc_eof_offset += sizeof(struct fcoe_crc_eof);

	if (fps->crc_eof_offset >= PAGE_SIZE) {
		fps->crc_eof_page = NULL;
		fps->crc_eof_offset = 0;
		put_page(page);
	}
	put_cpu();
	return 0;
}

/*
 * this is the frame xmit routine
 */
int fcoe_xmit(struct fc_lport *lp, struct fc_frame *fp)
{
	int indx;
	int wlen, rc = 0;
	u32 crc;
	struct ethhdr *eh;
	struct fcoe_crc_eof *cp;
	struct sk_buff *skb;
	struct fcoe_dev_stats *stats;
	struct fc_frame_header *fh;
	unsigned int hlen;		/* header length implies the version */
	unsigned int tlen;		/* trailer length */
	int flogi_in_progress = 0;
	struct fcoe_softc *fc;
	void *data;
	u8 sof, eof;
	struct fcoe_hdr *hp;

	WARN_ON((fr_len(fp) % sizeof(u32)) != 0);

	fc = (struct fcoe_softc *)lp->drv_priv;
	/*
	 * if it is a flogi then we need to learn gw-addr
	 * and my own fcid
	 */
	fh = fc_frame_header_get(fp);
	if (unlikely(fh->fh_r_ctl == FC_RCTL_ELS_REQ)) {
		if (fc_frame_payload_op(fp) == ELS_FLOGI) {
			fc->flogi_oxid = ntohs(fh->fh_ox_id);
			fc->address_mode = FCOE_FCOUI_ADDR_MODE;
			fc->flogi_progress = 1;
			flogi_in_progress = 1;
		} else if (fc->flogi_progress && ntoh24(fh->fh_s_id) != 0) {
			/*
			 * Here we must've gotten an SID by accepting an FLOGI
			 * from a point-to-point connection.  Switch to using
			 * the source mac based on the SID.  The destination
			 * MAC in this case would have been set by receving the
			 * FLOGI.
			 */
			fc_fcoe_set_mac(fc->data_src_addr, fh->fh_s_id);
			fc->flogi_progress = 0;
		}
	}

	skb = fp_skb(fp);
	sof = fr_sof(fp);
	eof = fr_eof(fp);

	crc = ~0;
	crc = crc32(crc, skb->data, skb_headlen(skb));

	for (indx = 0; indx < skb_shinfo(skb)->nr_frags; indx++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[indx];
		unsigned long off = frag->page_offset;
		unsigned long len = frag->size;

		while (len > 0) {
			unsigned long clen;

			clen = min(len, PAGE_SIZE - (off & ~PAGE_MASK));
			data = kmap_atomic(frag->page + (off >> PAGE_SHIFT),
					   KM_SKB_DATA_SOFTIRQ);
			crc = crc32(crc, data + (off & ~PAGE_MASK),
				    clen);
			kunmap_atomic(data, KM_SKB_DATA_SOFTIRQ);
			off += clen;
			len -= clen;
		}
	}

	/*
	 * Get header and trailer lengths.
	 * This is temporary code until we get rid of the old protocol.
	 * Both versions have essentially the same trailer layout but T11
	 * has padding afterwards.
	 */
	hlen = sizeof(struct fcoe_hdr);
	tlen = sizeof(struct fcoe_crc_eof);

	/*
	 * copy fc crc and eof to the skb buff
	 * Use utility buffer in the fc_frame part of the sk_buff for the
	 * trailer.
	 * We don't do a get_page for this frag, since that page may not be
	 * managed that way.  So that skb_free() doesn't do that either, we
	 * setup the destructor to remove this frag.
	 */
	if (skb_is_nonlinear(skb)) {
		skb_frag_t *frag;
		if (fcoe_get_paged_crc_eof(skb, tlen)) {
			kfree(skb);
			return -ENOMEM;
		}
		frag = &skb_shinfo(skb)->frags[skb_shinfo(skb)->nr_frags - 1];
		cp = kmap_atomic(frag->page, KM_SKB_DATA_SOFTIRQ)
			+ frag->page_offset;
	} else {
		cp = (struct fcoe_crc_eof *)skb_put(skb, tlen);
	}

	cp->fcoe_eof = eof;
	cp->fcoe_crc32 = cpu_to_le32(~crc);
	if (tlen == sizeof(*cp))
		memset(cp->fcoe_resvd, 0, sizeof(cp->fcoe_resvd));
	wlen = (skb->len - tlen + sizeof(crc)) / FCOE_WORD_TO_BYTE;

	if (skb_is_nonlinear(skb)) {
		kunmap_atomic(cp, KM_SKB_DATA_SOFTIRQ);
		cp = NULL;
	}

	/*
	 *	Fill in the control structures
	 */
	skb->ip_summed = CHECKSUM_NONE;
	eh = (struct ethhdr *)skb_push(skb, hlen + sizeof(struct ethhdr));
	if (fc->address_mode == FCOE_FCOUI_ADDR_MODE)
		fc_fcoe_set_mac(eh->h_dest, fh->fh_d_id);
	else
		/* insert GW address */
		memcpy(eh->h_dest, fc->dest_addr, ETH_ALEN);

	if (unlikely(flogi_in_progress))
		memcpy(eh->h_source, fc->ctl_src_addr, ETH_ALEN);
	else
		memcpy(eh->h_source, fc->data_src_addr, ETH_ALEN);

	eh->h_proto = htons(ETH_P_FCOE);
	skb->protocol = htons(ETH_P_802_3);
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);

	hp = (struct fcoe_hdr *)(eh + 1);
	memset(hp, 0, sizeof(*hp));
	if (FC_FCOE_VER)
		FC_FCOE_ENCAPS_VER(hp, FC_FCOE_VER);
	hp->fcoe_sof = sof;

	stats = lp->dev_stats[smp_processor_id()];
	stats->TxFrames++;
	stats->TxWords += wlen;
	skb->dev = fc->real_dev;

	fr_dev(fp) = lp;
	if (fc->fcoe_pending_queue.qlen)
		rc = fcoe_check_wait_queue(lp);

	if (rc == 0)
		rc = fcoe_start_io(skb);

	if (rc) {
		fcoe_insert_wait_queue(lp, skb);
		if (fc->fcoe_pending_queue.qlen > FCOE_MAX_QUEUE_DEPTH)
			fc_pause(lp);
	}

	return 0;
}

int fcoe_percpu_receive_thread(void *arg)
{
	struct fcoe_percpu_s *p = arg;
	u32 fr_len;
	unsigned int hlen;
	unsigned int tlen;
	struct fc_lport *lp;
	struct fcoe_rcv_info *fr;
	struct fcoe_dev_stats *stats;
	struct fc_frame_header *fh;
	struct sk_buff *skb;
	struct fcoe_crc_eof *cp;
	enum fc_sof sof;
	struct fc_frame *fp;
	u8 *mac = NULL;
	struct fcoe_softc *fc;
	struct fcoe_hdr *hp;

	set_user_nice(current, 19);

	while (!kthread_should_stop()) {

		spin_lock_bh(&p->fcoe_rx_list.lock);
		while ((skb = __skb_dequeue(&p->fcoe_rx_list)) == NULL) {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_bh(&p->fcoe_rx_list.lock);
			schedule();
			set_current_state(TASK_RUNNING);
			if (kthread_should_stop())
				return 0;
			spin_lock_bh(&p->fcoe_rx_list.lock);
		}
		spin_unlock_bh(&p->fcoe_rx_list.lock);
		fr = fcoe_dev_from_skb(skb);
		lp = fr->fr_dev;
		if (unlikely(lp == NULL)) {
			FC_DBG("invalid HBA Structure");
			kfree_skb(skb);
			continue;
		}

		stats = lp->dev_stats[smp_processor_id()];

		if (unlikely(debug_fcoe)) {
			FC_DBG("skb_info: len:%d data_len:%d head:%p data:%p "
			       "tail:%p end:%p sum:%d dev:%s",
			       skb->len, skb->data_len,
			       skb->head, skb->data, skb_tail_pointer(skb),
			       skb_end_pointer(skb), skb->csum,
			       skb->dev ? skb->dev->name : "<NULL>");
		}

		/*
		 * Save source MAC address before discarding header.
		 */
		fc = lp->drv_priv;
		if (unlikely(fc->flogi_progress))
			mac = eth_hdr(skb)->h_source;

		if (skb_is_nonlinear(skb))
			skb_linearize(skb);	/* not ideal */

		/*
		 * Check the header and pull it off.
		 */
		hlen = sizeof(struct fcoe_hdr);

		hp = (struct fcoe_hdr *)skb->data;
		if (unlikely(FC_FCOE_DECAPS_VER(hp) != FC_FCOE_VER)) {
			if (stats->ErrorFrames < 5)
				FC_DBG("unknown FCoE version %x",
				       FC_FCOE_DECAPS_VER(hp));
			stats->ErrorFrames++;
			kfree_skb(skb);
			continue;
		}
		sof = hp->fcoe_sof;
		skb_pull(skb, sizeof(*hp));
		fr_len = skb->len - sizeof(struct fcoe_crc_eof);
		skb_trim(skb, fr_len);
		tlen = sizeof(struct fcoe_crc_eof);

		if (unlikely(fr_len > skb->len)) {
			if (stats->ErrorFrames < 5)
				FC_DBG("length error fr_len 0x%x skb->len 0x%x",
				       fr_len, skb->len);
			stats->ErrorFrames++;
			kfree_skb(skb);
			continue;
		}
		stats->RxFrames++;
		stats->RxWords += fr_len / FCOE_WORD_TO_BYTE;

		fp = (struct fc_frame *) skb;
		fc_frame_init(fp);
		cp = (struct fcoe_crc_eof *)(skb->data + fr_len);
		fr_eof(fp) = cp->fcoe_eof;
		fr_sof(fp) = sof;
		fr_dev(fp) = lp;

		/*
		 * Check the CRC here, unless it's solicited data for SCSI.
		 * In that case, the SCSI layer can check it during the copy,
		 * and it'll be more cache-efficient.
		 */
		fh = fc_frame_header_get(fp);
		if (fh->fh_r_ctl == FC_RCTL_DD_SOL_DATA &&
		    fh->fh_type == FC_TYPE_FCP) {
			fr_flags(fp) |= FCPHF_CRC_UNCHECKED;
			fc_exch_recv(lp, lp->emp, fp);
		} else if (le32_to_cpu(cp->fcoe_crc32) ==
			   ~crc32(~0, skb->data, fr_len)) {
			if (unlikely(fc->flogi_progress))
				fcoe_recv_flogi(fc, fp, mac);
			fc_exch_recv(lp, lp->emp, fp);
		} else {
			if (debug_fcoe || stats->InvalidCRCCount < 5) {
				printk(KERN_WARNING \
				       "fcoe: dropping frame with CRC error");
			}
			stats->InvalidCRCCount++;
			stats->ErrorFrames++;
			fc_frame_free(fp);
		}
	}
	return 0;
}

/*
 * Snoop potential response to FLOGI or even incoming FLOGI.
 */
static void fcoe_recv_flogi(struct fcoe_softc *fc, struct fc_frame *fp, u8 *sa)
{
	struct fc_frame_header *fh;
	u8 op;

	fh = fc_frame_header_get(fp);
	if (fh->fh_type != FC_TYPE_ELS)
		return;
	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC && fh->fh_r_ctl == FC_RCTL_ELS_REP &&
	    fc->flogi_oxid == ntohs(fh->fh_ox_id)) {
		/*
		 * FLOGI accepted.
		 * If the src mac addr is FC_OUI-based, then we mark the
		 * address_mode flag to use FC_OUI-based Ethernet DA.
		 * Otherwise we use the FCoE gateway addr
		 */
		if (!compare_ether_addr(sa, (u8[6]) FC_FCOE_FLOGI_MAC)) {
			fc->address_mode = FCOE_FCOUI_ADDR_MODE;
		} else {
			memcpy(fc->dest_addr, sa, ETH_ALEN);
			fc->address_mode = FCOE_GW_ADDR_MODE;
		}

		/*
		 * Remove any previously-set unicast MAC filter.
		 * Add secondary FCoE MAC address filter for our OUI.
		 */
		rtnl_lock();
		if (compare_ether_addr(fc->data_src_addr, (u8[6]) { 0 }))
			dev_unicast_delete(fc->real_dev, fc->data_src_addr,
					   ETH_ALEN);
		fc_fcoe_set_mac(fc->data_src_addr, fh->fh_d_id);
		dev_unicast_add(fc->real_dev, fc->data_src_addr, ETH_ALEN);
		rtnl_unlock();

		fc->flogi_progress = 0;
	} else if (op == ELS_FLOGI && fh->fh_r_ctl == FC_RCTL_ELS_REQ && sa) {
		/*
		 * Save source MAC for point-to-point responses.
		 */
		memcpy(fc->dest_addr, sa, ETH_ALEN);
		fc->address_mode = FCOE_GW_ADDR_MODE;
	}
}

void fcoe_watchdog(ulong vp)
{
	struct fc_lport *lp;
	struct fcoe_softc *fc;
	struct fcoe_info *fci = &fcoei;
	int paused = 0;

	read_lock(&fci->fcoe_hostlist_lock);
	list_for_each_entry(fc, &fci->fcoe_hostlist, list) {
		lp = fc->lp;
		if (lp) {
			if (fc->fcoe_pending_queue.qlen > FCOE_MAX_QUEUE_DEPTH)
				paused = 1;
			if (fcoe_check_wait_queue(lp) <	 FCOE_MAX_QUEUE_DEPTH) {
				if (paused)
					fc_unpause(lp);
			}
		}
	}
	read_unlock(&fci->fcoe_hostlist_lock);

	fci->timer.expires = jiffies + (1 * HZ);
	add_timer(&fci->timer);
}

/*
 * the wait_queue is used when the skb transmit fails. skb will go
 * in the wait_queue which will be emptied by the time function OR
 * by the next skb transmit.
 *
 */

/*
 * Function name : fcoe_check_wait_queue()
 *
 * Return Values : 0 or error
 *
 * Description	 : empties the wait_queue
 *		   dequeue the head of the wait_queue queue and
 *		   calls fcoe_start_io() for each packet
 *		   if all skb have been transmitted, return 0
 *		   if a error occurs, then restore wait_queue and try again
 *		   later
 *
 */

static int fcoe_check_wait_queue(struct fc_lport *lp)
{
	int rc, unpause = 0;
	int paused = 0;
	struct sk_buff *skb;
	struct fcoe_softc *fc;

	fc = (struct fcoe_softc *)lp->drv_priv;
	spin_lock_bh(&fc->fcoe_pending_queue.lock);

	/*
	 * is this interface paused?
	 */
	if (fc->fcoe_pending_queue.qlen > FCOE_MAX_QUEUE_DEPTH)
		paused = 1;
	if (fc->fcoe_pending_queue.qlen) {
		while ((skb = __skb_dequeue(&fc->fcoe_pending_queue)) != NULL) {
			spin_unlock_bh(&fc->fcoe_pending_queue.lock);
			rc = fcoe_start_io(skb);
			if (rc) {
				fcoe_insert_wait_queue_head(lp, skb);
				return rc;
			}
			spin_lock_bh(&fc->fcoe_pending_queue.lock);
		}
		if (fc->fcoe_pending_queue.qlen < FCOE_MAX_QUEUE_DEPTH)
			unpause = 1;
	}
	spin_unlock_bh(&fc->fcoe_pending_queue.lock);
	if ((unpause) && (paused))
		fc_unpause(lp);
	return fc->fcoe_pending_queue.qlen;
}

static void fcoe_insert_wait_queue_head(struct fc_lport *lp,
					struct sk_buff *skb)
{
	struct fcoe_softc *fc;

	fc = (struct fcoe_softc *)lp->drv_priv;
	spin_lock_bh(&fc->fcoe_pending_queue.lock);
	__skb_queue_head(&fc->fcoe_pending_queue, skb);
	spin_unlock_bh(&fc->fcoe_pending_queue.lock);
}

static void fcoe_insert_wait_queue(struct fc_lport *lp,
				   struct sk_buff *skb)
{
	struct fcoe_softc *fc;

	fc = (struct fcoe_softc *)lp->drv_priv;
	spin_lock_bh(&fc->fcoe_pending_queue.lock);
	__skb_queue_tail(&fc->fcoe_pending_queue, skb);
	spin_unlock_bh(&fc->fcoe_pending_queue.lock);
}
