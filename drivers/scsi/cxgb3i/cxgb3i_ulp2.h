/*
 * cxgb3i_ulp2.h: Chelsio S3xx iSCSI driver.
 *
 * Copyright (c) 2008 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Karen Xie (kxie@chelsio.com)
 */

#ifndef __CXGB3I_ULP2_H__
#define __CXGB3I_ULP2_H__

#define ULP2_PDU_PAYLOAD_DFLT	(16224 - ISCSI_PDU_HEADER_MAX)
#define PPOD_PAGES_MAX		4
#define PPOD_PAGES_SHIFT	2	/* 4 pages per pod */

struct pagepod_hdr {
	u32 vld_tid;
	u32 pgsz_tag_clr;
	u32 maxoffset;
	u32 pgoffset;
	u64 rsvd;
};

struct pagepod {
	struct pagepod_hdr hdr;
	u64 addr[PPOD_PAGES_MAX + 1];
};

#define PPOD_SIZE		sizeof(struct pagepod)	/* 64 */
#define PPOD_SIZE_SHIFT		6

#define PPOD_COLOR_SHIFT	0
#define PPOD_COLOR_SIZE		6
#define PPOD_COLOR_MASK		((1 << PPOD_COLOR_SIZE) - 1)

#define PPOD_IDX_SHIFT		PPOD_COLOR_SIZE
#define PPOD_IDX_MAX_SIZE	24

#define S_PPOD_TID    0
#define M_PPOD_TID    0xFFFFFF
#define V_PPOD_TID(x) ((x) << S_PPOD_TID)

#define S_PPOD_VALID    24
#define V_PPOD_VALID(x) ((x) << S_PPOD_VALID)
#define F_PPOD_VALID    V_PPOD_VALID(1U)

#define S_PPOD_COLOR    0
#define M_PPOD_COLOR    0x3F
#define V_PPOD_COLOR(x) ((x) << S_PPOD_COLOR)

#define S_PPOD_TAG    6
#define M_PPOD_TAG    0xFFFFFF
#define V_PPOD_TAG(x) ((x) << S_PPOD_TAG)

#define S_PPOD_PGSZ    30
#define M_PPOD_PGSZ    0x3
#define V_PPOD_PGSZ(x) ((x) << S_PPOD_PGSZ)

struct cpl_iscsi_hdr_norss {
	union opcode_tid ot;
	u16 pdu_len_ddp;
	u16 len;
	u32 seq;
	u16 urg;
	u8 rsvd;
	u8 status;
};

struct cpl_rx_data_ddp_norss {
	union opcode_tid ot;
	u16 urg;
	u16 len;
	u32 seq;
	u32 nxt_seq;
	u32 ulp_crc;
	u32 ddp_status;
};

#define RX_DDP_STATUS_IPP_SHIFT		27	/* invalid pagepod */
#define RX_DDP_STATUS_TID_SHIFT		26	/* tid mismatch */
#define RX_DDP_STATUS_COLOR_SHIFT	25	/* color mismatch */
#define RX_DDP_STATUS_OFFSET_SHIFT	24	/* offset mismatch */
#define RX_DDP_STATUS_ULIMIT_SHIFT	23	/* ulimit error */
#define RX_DDP_STATUS_TAG_SHIFT		22	/* tag mismatch */
#define RX_DDP_STATUS_DCRC_SHIFT	21	/* dcrc error */
#define RX_DDP_STATUS_HCRC_SHIFT	20	/* hcrc error */
#define RX_DDP_STATUS_PAD_SHIFT		19	/* pad error */
#define RX_DDP_STATUS_PPP_SHIFT		18	/* pagepod parity error */
#define RX_DDP_STATUS_LLIMIT_SHIFT	17	/* llimit error */
#define RX_DDP_STATUS_DDP_SHIFT		16	/* ddp'able */
#define RX_DDP_STATUS_PMM_SHIFT		15	/* pagepod mismatch */

#define ULP2_FLAG_DATA_READY		0x1
#define ULP2_FLAG_DATA_DDPED		0x2
#define ULP2_FLAG_HCRC_ERROR		0x10
#define ULP2_FLAG_DCRC_ERROR		0x20
#define ULP2_FLAG_PAD_ERROR		0x40

#define ULP2_MAX_PKT_SIZE		16224

void cxgb3i_conn_closing(struct s3_conn *);
void cxgb3i_conn_pdu_ready(struct s3_conn *c3cn);
void cxgb3i_conn_tx_open(struct s3_conn *c3cn);
#endif
