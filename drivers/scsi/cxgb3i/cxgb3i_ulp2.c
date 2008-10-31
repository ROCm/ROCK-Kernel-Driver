/*
 * cxgb3i_ulp2.c: Chelsio S3xx iSCSI driver.
 *
 * Copyright (c) 2008 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#include <linux/skbuff.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <linux/crypto.h>

#include "cxgb3i.h"
#include "cxgb3i_ulp2.h"

#ifdef __DEBUG_CXGB3I_RX__
#define cxgb3i_rx_debug		cxgb3i_log_debug
#else
#define cxgb3i_rx_debug(fmt...)
#endif

#ifdef __DEBUG_CXGB3I_TX__
#define cxgb3i_tx_debug		cxgb3i_log_debug
#else
#define cxgb3i_tx_debug(fmt...)
#endif

#ifdef __DEBUG_CXGB3I_TAG__
#define cxgb3i_tag_debug	cxgb3i_log_debug
#else
#define cxgb3i_tag_debug(fmt...)
#endif

#ifdef __DEBUG_CXGB3I_DDP__
#define cxgb3i_ddp_debug	cxgb3i_log_debug
#else
#define cxgb3i_ddp_debug(fmt...)
#endif

static struct page *pad_page;

#define ULP2_PGIDX_MAX		4
#define ULP2_4K_PAGE_SHIFT	12
#define ULP2_4K_PAGE_MASK	(~((1UL << ULP2_4K_PAGE_SHIFT) - 1))
static unsigned char ddp_page_order[ULP2_PGIDX_MAX];
static unsigned long ddp_page_size[ULP2_PGIDX_MAX];
static unsigned char ddp_page_shift[ULP2_PGIDX_MAX];
static unsigned char sw_tag_idx_bits;
static unsigned char sw_tag_age_bits;

static void cxgb3i_ddp_page_init(void)
{
	int i;
	unsigned long n = PAGE_SIZE >> ULP2_4K_PAGE_SHIFT;

	if (PAGE_SIZE & (~ULP2_4K_PAGE_MASK)) {
		cxgb3i_log_debug("PAGE_SIZE 0x%lx is not multiple of 4K, "
				"ddp disabled.\n", PAGE_SIZE);
		return;
	}
	n = __ilog2_u32(n);
	for (i = 0; i < ULP2_PGIDX_MAX; i++, n++) {
		ddp_page_order[i] = n;
		ddp_page_shift[i] = ULP2_4K_PAGE_SHIFT + n;
		ddp_page_size[i] = 1 << ddp_page_shift[i];
		cxgb3i_log_debug("%d, order %u, shift %u, size 0x%lx.\n", i,
				 ddp_page_order[i], ddp_page_shift[i],
				 ddp_page_size[i]);
	}

	sw_tag_idx_bits = (__ilog2_u32(ISCSI_ITT_MASK)) + 1;
	sw_tag_age_bits = (__ilog2_u32(ISCSI_AGE_MASK)) + 1;
}

static inline void ulp_mem_io_set_hdr(struct sk_buff *skb, unsigned int addr)
{
	struct ulp_mem_io *req = (struct ulp_mem_io *)skb->head;

	req->wr.wr_lo = 0;
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_BYPASS));
	req->cmd_lock_addr = htonl(V_ULP_MEMIO_ADDR(addr >> 5) |
				   V_ULPTX_CMD(ULP_MEM_WRITE));
	req->len = htonl(V_ULP_MEMIO_DATA_LEN(PPOD_SIZE >> 5) |
			 V_ULPTX_NFLITS((PPOD_SIZE >> 3) + 1));
}

static int set_ddp_map(struct cxgb3i_adapter *snic, struct pagepod_hdr *hdr,
		       unsigned int idx, unsigned int npods,
		       struct scatterlist *sgl, unsigned int sgcnt)
{
	struct cxgb3i_ddp_info *ddp = &snic->ddp;
	struct scatterlist *sg = sgl;
	unsigned int pm_addr = (idx << PPOD_SIZE_SHIFT) + ddp->llimit;
	int i;

	for (i = 0; i < npods; i++, pm_addr += PPOD_SIZE) {
		struct sk_buff *skb;
		struct pagepod *ppod;
		int j, k;
		skb =
		    alloc_skb(sizeof(struct ulp_mem_io) + PPOD_SIZE,
			      GFP_ATOMIC);
		if (!skb) {
			cxgb3i_log_debug("skb OMM.\n");
			return -ENOMEM;
		}
		skb_put(skb, sizeof(struct ulp_mem_io) + PPOD_SIZE);

		ulp_mem_io_set_hdr(skb, pm_addr);
		ppod =
		    (struct pagepod *)(skb->head + sizeof(struct ulp_mem_io));
		memcpy(&(ppod->hdr), hdr, sizeof(struct pagepod));
		for (j = 0, k = i * 4; j < 5; j++, k++) {
			if (k < sgcnt) {
				ppod->addr[j] = cpu_to_be64(sg_dma_address(sg));
				if (j < 4)
					sg = sg_next(sg);
			} else
				ppod->addr[j] = 0UL;
		}

		skb->priority = CPL_PRIORITY_CONTROL;
		cxgb3_ofld_send(snic->tdev, skb);
	}
	return 0;
}

static int clear_ddp_map(struct cxgb3i_adapter *snic, unsigned int idx,
			 unsigned int npods)
{
	struct cxgb3i_ddp_info *ddp = &snic->ddp;
	unsigned int pm_addr = (idx << PPOD_SIZE_SHIFT) + ddp->llimit;
	int i;

	for (i = 0; i < npods; i++, pm_addr += PPOD_SIZE) {
		struct sk_buff *skb;
		skb =
		    alloc_skb(sizeof(struct ulp_mem_io) + PPOD_SIZE,
			      GFP_ATOMIC);
		if (!skb)
			return -ENOMEM;
		skb_put(skb, sizeof(struct ulp_mem_io) + PPOD_SIZE);
		memset((skb->head + sizeof(struct ulp_mem_io)), 0, PPOD_SIZE);
		ulp_mem_io_set_hdr(skb, pm_addr);
		skb->priority = CPL_PRIORITY_CONTROL;
		cxgb3_ofld_send(snic->tdev, skb);
	}
	return 0;
}

static int cxgb3i_ddp_sgl_check(struct scatterlist *sgl, unsigned int sgcnt)
{
	struct scatterlist *sg;
	int i;

	/* make sure the sgl is fit for ddp:
	 *      each has the same page size, and
	 *      first & last page do not need to be used completely, and
	 *      the rest of page must be used completely
	 */
	for_each_sg(sgl, sg, sgcnt, i) {
		if ((i && sg->offset) ||
		    ((i != sgcnt - 1) &&
		     (sg->length + sg->offset) != PAGE_SIZE)) {
			cxgb3i_tag_debug("sg %u/%u, off %u, len %u.\n",
					 i, sgcnt, sg->offset, sg->length);
			return -EINVAL;
		}
	}

	return 0;
}

static inline int ddp_find_unused_entries(struct cxgb3i_ddp_info *ddp,
					  int start, int max, int count)
{
	unsigned int i, j;

	spin_lock(&ddp->map_lock);
	for (i = start; i <= max;) {
		for (j = 0; j < count; j++) {
			if (ddp->map[i + j])
				break;
		}
		if (j == count) {
			memset(&ddp->map[i], 1, count);
			spin_unlock(&ddp->map_lock);
			return i;
		}
		i += j + 1;
	}
	spin_unlock(&ddp->map_lock);
	return -EBUSY;
}

static inline void ddp_unmark_entries(struct cxgb3i_ddp_info *ddp,
				      int start, int count)
{
	spin_lock(&ddp->map_lock);
	memset(&ddp->map[start], 0, count);
	spin_unlock(&ddp->map_lock);
}

static inline int sgl_map(struct cxgb3i_adapter *snic, 
			  struct scatterlist *sgl, unsigned int sgcnt)
{
	struct scatterlist *sg;
	int i, err;

	for_each_sg(sgl, sg, sgcnt, i) {
		err = pci_map_sg(snic->pdev, sg, 1, PCI_DMA_FROMDEVICE);
		if (err <= 0) {
			cxgb3i_tag_debug("sgcnt %d/%u, pci map failed %d.\n",
				 	 i, sgcnt, err);
			return err;
		}
	}
	return sgcnt;
}

static inline void sgl_unmap(struct cxgb3i_adapter *snic, 
			     struct scatterlist *sgl, unsigned int sgcnt)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, sgcnt, i) {
		if (sg_dma_address(sg))
			pci_unmap_sg(snic->pdev, sg, 1, PCI_DMA_FROMDEVICE);
		else
			break;
	}
}

u32 cxgb3i_ddp_tag_reserve(struct cxgb3i_adapter *snic, unsigned int tid,
			   u32 sw_tag, unsigned int xferlen,
			   struct scatterlist *sgl, unsigned int sgcnt)
{
	struct cxgb3i_ddp_info *ddp = &snic->ddp;
	struct pagepod_hdr hdr;
	unsigned int npods;
	int idx = -1, idx_max;
	u32 tag;
	int err;

	if (!ddp || !sgcnt || xferlen < PAGE_SIZE) {
		cxgb3i_tag_debug("sgcnt %u, xferlen %u < %lu, NO DDP.\n",
				 sgcnt, xferlen, PAGE_SIZE);
		return RESERVED_ITT;
	}

	err = cxgb3i_ddp_sgl_check(sgl, sgcnt);
	if (err < 0) {
		cxgb3i_tag_debug("sgcnt %u, xferlen %u, SGL check fail.\n",
				 sgcnt, xferlen);
		return RESERVED_ITT;
	}

	npods = (sgcnt + PPOD_PAGES_MAX - 1) >> PPOD_PAGES_SHIFT;
	idx_max = ddp->nppods - npods + 1;

	if (ddp->idx_last == ddp->nppods)
		idx = ddp_find_unused_entries(ddp, 0, idx_max, npods);
	else {
		idx = ddp_find_unused_entries(ddp, ddp->idx_last + 1, idx_max,
					      npods);
		if ((idx < 0) && (ddp->idx_last >= npods))
			idx = ddp_find_unused_entries(ddp, 0,
						      ddp->idx_last - npods + 1,
						      npods);
	}
	if (idx < 0) {
		cxgb3i_tag_debug("sgcnt %u, xferlen %u, npods %u NO DDP.\n",
				 sgcnt, xferlen, npods);
		return RESERVED_ITT;
	}

	err = sgl_map(snic, sgl, sgcnt);
	if (err < sgcnt)
		goto unmap_sgl;
	
	tag = sw_tag | (idx << snic->tag_format.rsvd_shift);

	hdr.rsvd = 0;
	hdr.vld_tid = htonl(F_PPOD_VALID | V_PPOD_TID(tid));
	hdr.pgsz_tag_clr = htonl(tag & snic->tag_format.rsvd_tag_mask);
	hdr.maxoffset = htonl(xferlen);
	hdr.pgoffset = htonl(sgl->offset);

	if (set_ddp_map(snic, &hdr, idx, npods, sgl, sgcnt) < 0)
		goto unmap_sgl;

	ddp->idx_last = idx;
	cxgb3i_tag_debug("tid 0x%x, xfer %u, 0x%x -> ddp 0x%x (0x%x, %u).\n",
			 tid, xferlen, sw_tag, tag, idx, npods);
	return tag;

unmap_sgl:
	sgl_unmap(snic, sgl, sgcnt);
	ddp_unmark_entries(ddp, idx, npods);
	return RESERVED_ITT;
}

void cxgb3i_ddp_tag_release(struct cxgb3i_adapter *snic, u32 tag,
			    struct scatterlist *sgl, unsigned int sgcnt)
{
	u32 idx = (tag >> snic->tag_format.rsvd_shift) &
		  snic->tag_format.rsvd_mask;
	unsigned int npods = (sgcnt + PPOD_PAGES_MAX - 1) >> PPOD_PAGES_SHIFT;

	if (idx < snic->tag_format.rsvd_mask) {
		cxgb3i_tag_debug("ddp tag 0x%x, release idx 0x%x, npods %u.\n",
				 tag, idx, npods);
		clear_ddp_map(snic, idx, npods);
		ddp_unmark_entries(&snic->ddp, idx, npods);
		sgl_unmap(snic, sgl, sgcnt);
	}
}

int cxgb3i_conn_ulp_setup(struct cxgb3i_conn *cconn, int hcrc, int dcrc)
{
	struct iscsi_tcp_conn *tcp_conn = cconn->conn->dd_data;
	struct s3_conn *c3cn = (struct s3_conn *)(tcp_conn->sock);
	struct sk_buff *skb = alloc_skb(sizeof(struct cpl_set_tcb_field),
					GFP_KERNEL | __GFP_NOFAIL);
	struct cpl_set_tcb_field *req;
	u32 submode = (hcrc ? 1 : 0) | (dcrc ? 2 : 0);

	/* set up ulp submode and page size */
	req = (struct cpl_set_tcb_field *)skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, c3cn->tid));
	req->reply = V_NO_REPLY(1);
	req->cpu_idx = 0;
	req->word = htons(31);
	req->mask = cpu_to_be64(0xFF000000);
	/* the connection page size is always the same as ddp-pgsz0 */
	req->val = cpu_to_be64(submode << 24);
	skb->priority = CPL_PRIORITY_CONTROL;

	cxgb3_ofld_send(c3cn->cdev, skb);
	return 0;
}

static int cxgb3i_conn_read_pdu_skb(struct iscsi_conn *conn,
				    struct sk_buff *skb)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_segment *segment = &tcp_conn->in.segment;
	struct iscsi_hdr *hdr = (struct iscsi_hdr *)tcp_conn->in.hdr_buf;
	unsigned char *buf = (unsigned char *)hdr;
	unsigned int offset = sizeof(struct iscsi_hdr);
	int err;

	cxgb3i_rx_debug("conn 0x%p, skb 0x%p, len %u, flag 0x%x.\n",
			conn, skb, skb->len, skb_ulp_mode(skb));

	/* read bhs */
	err = skb_copy_bits(skb, 0, buf, sizeof(struct iscsi_hdr));
	if (err < 0)
		return err;
	segment->copied = sizeof(struct iscsi_hdr);
	/* read ahs */
	if (hdr->hlength) {
		unsigned int ahslen = hdr->hlength << 2;
		/* Make sure we don't overflow */
		if (sizeof(*hdr) + ahslen > sizeof(tcp_conn->in.hdr_buf))
			return -ISCSI_ERR_AHSLEN;
		err = skb_copy_bits(skb, offset, buf + offset, ahslen);
		if (err < 0)
			return err;
		offset += ahslen;
	}
	/* header digest */
	if (conn->hdrdgst_en)
		offset += ISCSI_DIGEST_SIZE;

	/* check header digest */
	segment->status = (conn->hdrdgst_en &&
			   (skb_ulp_mode(skb) & ULP2_FLAG_HCRC_ERROR)) ?
	    ISCSI_SEGMENT_DGST_ERR : 0;

	hdr->itt = ntohl(hdr->itt);
	segment->total_copied = segment->total_size;
	tcp_conn->in.hdr = hdr;
	err = iscsi_tcp_hdr_dissect(conn, hdr);
	if (err)
		return err;

	if (tcp_conn->in.datalen) {
		segment = &tcp_conn->in.segment;
		segment->status = (conn->datadgst_en &&
				   (skb_ulp_mode(skb) & ULP2_FLAG_DCRC_ERROR)) ?
		    ISCSI_SEGMENT_DGST_ERR : 0;
		if (skb_ulp_mode(skb) & ULP2_FLAG_DATA_DDPED) {
			cxgb3i_ddp_debug("opcode 0x%x, data %u, ddp'ed.\n",
					 hdr->opcode & ISCSI_OPCODE_MASK,
					 tcp_conn->in.datalen);
			segment->total_copied = segment->total_size;
		} else
			offset += sizeof(struct cpl_iscsi_hdr_norss);

		while (segment->total_copied < segment->total_size) {
			iscsi_tcp_segment_map(segment, 1);
			err = skb_copy_bits(skb, offset, segment->data,
					    segment->size);
			iscsi_tcp_segment_unmap(segment);
			if (err)
				return err;
			segment->total_copied += segment->size;
			offset += segment->size;

			if (segment->total_copied < segment->total_size)
				iscsi_tcp_segment_init_sg(segment,
							  sg_next(segment->sg),
							  0);
		}
		err = segment->done(tcp_conn, segment);
	}
	return err;
}

static inline void tx_skb_setmode(struct sk_buff *skb, int hcrc, int dcrc)
{
	u8 submode = 0;

	if (hcrc)
		submode |= 1;
	if (dcrc)
		submode |= 2;
	skb_ulp_mode(skb) = (ULP_MODE_ISCSI << 4) | submode;
}

int cxgb3i_conn_ulp2_xmit(struct iscsi_conn *conn)
{
	struct iscsi_tcp_conn *tcp_conn = conn->dd_data;
	struct iscsi_segment *hdr_seg = &tcp_conn->out.segment;
	struct iscsi_segment *data_seg = &tcp_conn->out.data_segment;
	unsigned int hdrlen = hdr_seg->total_size;
	unsigned int datalen = data_seg->total_size;
	unsigned int padlen = iscsi_padding(datalen);
	unsigned int copymax = SKB_MAX_HEAD(TX_HEADER_LEN);
	unsigned int copylen;
	struct sk_buff *skb;
	unsigned char *dst;
	int err = -EAGAIN;

	if (data_seg->data && ((datalen + padlen) < copymax))
		copylen = hdrlen + datalen + padlen;
	else
		copylen = hdrlen;

	/* supports max. 16K pdus, so one skb is enough to hold all the data */
	skb = alloc_skb(TX_HEADER_LEN + copylen, GFP_ATOMIC);
	if (!skb)
		return -EAGAIN;

	skb_reserve(skb, TX_HEADER_LEN);
	skb_put(skb, copylen);
	dst = skb->data;

	tx_skb_setmode(skb, conn->hdrdgst_en, datalen ? conn->datadgst_en : 0);

	memcpy(dst, hdr_seg->data, hdrlen);
	dst += hdrlen;

	if (!datalen)
		goto send_pdu;

	if (data_seg->data) {
		/* data is in a linear buffer */
		if (copylen > hdrlen) {
			/* data fits in the skb's headroom */
			memcpy(dst, data_seg->data, datalen);
			dst += datalen;
			if (padlen)
				memset(dst, 0, padlen);
		} else {
			unsigned int offset = 0;
			while (datalen) {
				struct page *page = alloc_page(GFP_ATOMIC);
				int idx = skb_shinfo(skb)->nr_frags;
				skb_frag_t *frag = &skb_shinfo(skb)->frags[idx];

				if (!page)
					goto free_skb;

				frag->page = page;
				frag->page_offset = 0;
				if (datalen > PAGE_SIZE)
					frag->size = PAGE_SIZE;
				else
					frag->size = datalen;
				memcpy(page_address(page),
				       data_seg->data + offset, frag->size);

				skb_shinfo(skb)->nr_frags++;
				datalen -= frag->size;
				offset += frag->size;
			}
		}
	} else {
		struct scatterlist *sg = data_seg->sg;
		unsigned int offset = data_seg->sg_offset;
		while (datalen) {
			int idx = skb_shinfo(skb)->nr_frags;
			skb_frag_t *frag = &skb_shinfo(skb)->frags[idx];
			struct page *pg = sg_page(sg);

			get_page(pg);
			frag->page = pg;
			frag->page_offset = offset + sg->offset;
			frag->size = min(sg->length, datalen);

			offset = 0;
			skb_shinfo(skb)->nr_frags++;
			datalen -= frag->size;
			sg = sg_next(sg);
		}
	}

	if (skb_shinfo(skb)->nr_frags) {
		if (padlen) {
			int idx = skb_shinfo(skb)->nr_frags;
			skb_frag_t *frag = &skb_shinfo(skb)->frags[idx];
			frag->page = pad_page;
			frag->page_offset = 0;
			frag->size = padlen;
			skb_shinfo(skb)->nr_frags++;
		}
		datalen = data_seg->total_size + padlen;
		skb->data_len += datalen;
		skb->truesize += datalen;
		skb->len += datalen;
	}

send_pdu:
	err = cxgb3i_c3cn_send_pdus((struct s3_conn *)tcp_conn->sock,
				    skb, MSG_DONTWAIT | MSG_NOSIGNAL);
	if (err > 0) {
		int pdulen = hdrlen + datalen + padlen;
		if (conn->hdrdgst_en)
			pdulen += ISCSI_DIGEST_SIZE;
		if (datalen && conn->datadgst_en)
			pdulen += ISCSI_DIGEST_SIZE;

		hdr_seg->total_copied = hdr_seg->total_size;
		if (datalen)
			data_seg->total_copied = data_seg->total_size;
		conn->txdata_octets += pdulen;
		return pdulen;
	}

free_skb:
	kfree_skb(skb);
	if (err < 0 && err != -EAGAIN) {
		cxgb3i_log_error("conn 0x%p, xmit err %d.\n", conn, err);
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
		return err;
	}
	return -EAGAIN;
}

int cxgb3i_ulp2_init(void)
{
	pad_page = alloc_page(GFP_KERNEL);
	if (!pad_page)
		return -ENOMEM;
	memset(page_address(pad_page), 0, PAGE_SIZE);
	cxgb3i_ddp_page_init();
	return 0;
}

void cxgb3i_ulp2_cleanup(void)
{
	if (pad_page) {
		__free_page(pad_page);
		pad_page = NULL;
	}
}

void cxgb3i_conn_pdu_ready(struct s3_conn *c3cn)
{
	struct sk_buff *skb;
	unsigned int read = 0;
	struct iscsi_conn *conn = c3cn->user_data;
	int err = 0;

	cxgb3i_rx_debug("cn 0x%p.\n", c3cn);

	read_lock(&c3cn->callback_lock);
	if (unlikely(!conn || conn->suspend_rx)) {
		cxgb3i_rx_debug("conn 0x%p, id %d, suspend_rx %lu!\n",
				conn, conn ? conn->id : 0xFF,
				conn ? conn->suspend_rx : 0xFF);
		read_unlock(&c3cn->callback_lock);
		return;
	}
	skb = skb_peek(&c3cn->receive_queue);
	while (!err && skb) {
		__skb_unlink(skb, &c3cn->receive_queue);
		read += skb_ulp_pdulen(skb);
		err = cxgb3i_conn_read_pdu_skb(conn, skb);
		__kfree_skb(skb);
		skb = skb_peek(&c3cn->receive_queue);
	}
	read_unlock(&c3cn->callback_lock);
	if (c3cn) {
		c3cn->copied_seq += read;
		cxgb3i_c3cn_rx_credits(c3cn, read);
	}
	conn->rxdata_octets += read;

	if (err) {
		cxgb3i_log_info("conn 0x%p rx failed err %d.\n", conn, err);
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
	}
}

void cxgb3i_conn_tx_open(struct s3_conn *c3cn)
{
	struct iscsi_conn *conn = c3cn->user_data;
	struct iscsi_tcp_conn *tcp_conn;

	cxgb3i_tx_debug("cn 0x%p.\n", c3cn);
	if (conn) {
		cxgb3i_tx_debug("cn 0x%p, cid %d.\n", c3cn, conn->id);
		tcp_conn = conn->dd_data;
		scsi_queue_work(conn->session->host, &conn->xmitwork);
	}
}

void cxgb3i_conn_closing(struct s3_conn *c3cn)
{
	struct iscsi_conn *conn;

	read_lock(&c3cn->callback_lock);
	conn = c3cn->user_data;
	if (conn && c3cn->state != C3CN_STATE_ESTABLISHED)
		iscsi_conn_failure(conn, ISCSI_ERR_CONN_FAILED);
	read_unlock(&c3cn->callback_lock);
}

int cxgb3i_adapter_ulp_init(struct cxgb3i_adapter *snic)
{
	struct t3cdev *tdev = snic->tdev;
	struct cxgb3i_ddp_info *ddp = &snic->ddp;
	struct ulp_iscsi_info uinfo;
	unsigned int ppmax, bits, max_bits;
	int i, err;

	spin_lock_init(&ddp->map_lock);

	err = tdev->ctl(tdev, ULP_ISCSI_GET_PARAMS, &uinfo);
	if (err < 0) {
		cxgb3i_log_error("%s, failed to get iscsi param err=%d.\n",
				 tdev->name, err);
		return err;
	}

	ppmax = (uinfo.ulimit - uinfo.llimit + 1) >> PPOD_SIZE_SHIFT;
	max_bits = min(PPOD_IDX_MAX_SIZE,
		       (32 - sw_tag_idx_bits - sw_tag_age_bits));
	bits = __ilog2_u32(ppmax) + 1;
	if (bits > max_bits)
		bits = max_bits;
	ppmax = (1 << bits) - 1;

	snic->tx_max_size = min_t(unsigned int,
				  uinfo.max_txsz, ULP2_MAX_PKT_SIZE);
	snic->rx_max_size = min_t(unsigned int,
				  uinfo.max_rxsz, ULP2_MAX_PKT_SIZE);

	snic->tag_format.idx_bits = sw_tag_idx_bits;
	snic->tag_format.age_bits = sw_tag_age_bits;
	snic->tag_format.rsvd_bits = bits;
	snic->tag_format.rsvd_shift = PPOD_IDX_SHIFT;
	snic->tag_format.rsvd_mask = (1 << snic->tag_format.rsvd_bits) - 1;
	snic->tag_format.rsvd_tag_mask =
		(1 << (snic->tag_format.rsvd_bits + PPOD_IDX_SHIFT)) - 1;

	ddp->map = cxgb3i_alloc_big_mem(ppmax);
	if (!ddp->map) {
		cxgb3i_log_warn("snic unable to alloc ddp ppod 0x%u, "
				"ddp disabled.\n", ppmax);
		return 0;
	}
	ddp->llimit = uinfo.llimit;
	ddp->ulimit = uinfo.ulimit;

	uinfo.tagmask =
	    snic->tag_format.rsvd_mask << snic->tag_format.rsvd_shift;
	for (i = 0; i < ULP2_PGIDX_MAX; i++)
		uinfo.pgsz_factor[i] = ddp_page_order[i];

	uinfo.ulimit = uinfo.llimit + (ppmax << PPOD_SIZE_SHIFT);

	err = tdev->ctl(tdev, ULP_ISCSI_SET_PARAMS, &uinfo);
	if (err < 0) {
		cxgb3i_log_warn("snic unable to set iscsi param err=%d, "
				"ddp disabled.\n", err);
		goto free_ppod_map;
	}

	ddp->nppods = ppmax;
	ddp->idx_last = ppmax;

	tdev->ulp_iscsi = ddp;

	cxgb3i_log_info("snic nppods %u (0x%x ~ 0x%x), rsvd shift %u, "
			"bits %u, mask 0x%x, 0x%x, pkt %u,%u.\n",
			ppmax, ddp->llimit, ddp->ulimit,
			snic->tag_format.rsvd_shift,
			snic->tag_format.rsvd_bits,
			snic->tag_format.rsvd_mask, uinfo.tagmask,
			snic->tx_max_size, snic->rx_max_size);

	return 0;

free_ppod_map:
	cxgb3i_free_big_mem(ddp->map);
	return 0;
}

void cxgb3i_adapter_ulp_cleanup(struct cxgb3i_adapter *snic)
{
	u8 *map = snic->ddp.map;

	if (map) {
		snic->tdev->ulp_iscsi = NULL;
		spin_lock(&snic->lock);
		snic->ddp.map = NULL;
		spin_unlock(&snic->lock);
		cxgb3i_free_big_mem(map);
	}
}
