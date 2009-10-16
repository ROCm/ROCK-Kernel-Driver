/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains Falcon hardware support.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
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

#include <ci/driver/efab/hardware.h>
#include <ci/efhw/debug.h>
#include <ci/efhw/iopage.h>
#include <ci/efhw/falcon.h>
#include <ci/efhw/falcon_hash.h>
#include <ci/efhw/nic.h>
#include <ci/efhw/eventq.h>
#include <ci/efhw/checks.h>


/*----------------------------------------------------------------------------
 *
 * Workarounds and options
 *
 *---------------------------------------------------------------------------*/

/* Keep a software copy of the filter table and check for duplicates. */
#define FALCON_FULL_FILTER_CACHE 1

/* Read filters back from the hardware to detect corruption. */
#define FALCON_VERIFY_FILTERS    0

/* Options */
#define RX_FILTER_CTL_SRCH_LIMIT_TCP_FULL 8	/* default search limit */
#define RX_FILTER_CTL_SRCH_LIMIT_TCP_WILD 8	/* default search limit */
#define RX_FILTER_CTL_SRCH_LIMIT_UDP_FULL 8	/* default search limit */
#define RX_FILTER_CTL_SRCH_LIMIT_UDP_WILD 8	/* default search limit */

#define FALCON_MAC_SET_TYPE_BY_SPEED           0

/* FIXME: We should detect mode at runtime. */
#define FALCON_BUFFER_TABLE_FULL_MODE          1

/* "Fudge factors" - difference between programmed value and actual depth */
#define RX_FILTER_CTL_SRCH_FUDGE_WILD 3	/* increase the search limit */
#define RX_FILTER_CTL_SRCH_FUDGE_FULL 1	/* increase the search limit */
#define TX_FILTER_CTL_SRCH_FUDGE_WILD 3	/* increase the search limit */
#define TX_FILTER_CTL_SRCH_FUDGE_FULL 1	/* increase the search limit */

/*----------------------------------------------------------------------------
 *
 * Debug Macros
 *
 *---------------------------------------------------------------------------*/

#define _DEBUG_SYM_ static

 /*----------------------------------------------------------------------------
  *
  * Macros and forward declarations
  *
  *--------------------------------------------------------------------------*/

#define FALCON_REGION_NUM 4	/* number of supported memory regions */

#define FALCON_BUFFER_TBL_HALF_BYTES 4
#define FALCON_BUFFER_TBL_FULL_BYTES 8

/* Shadow buffer table - hack for testing only */
#if FALCON_BUFFER_TABLE_FULL_MODE == 0
# define FALCON_USE_SHADOW_BUFFER_TABLE 1
#else
# define FALCON_USE_SHADOW_BUFFER_TABLE 0
#endif


/*----------------------------------------------------------------------------
 *
 * Header assertion checks
 *
 *---------------------------------------------------------------------------*/

#define FALCON_ASSERT_VALID()	/* nothing yet */

/* Falcon has a 128bit register model but most registers have useful
   defaults or only implement a small number of bits. Some registers
   can be programmed 32bits UNLOCKED all others should be interlocked
   against other threads within the same protection domain.

   Aim is for software to perform the minimum number of writes and
   also to minimise the read-modify-write activity (which generally
   indicates a lack of clarity in the use model).

   Registers which are programmed in this module are listed below
   together with the method of access. Care must be taken to ensure
   remain adequate if the register spec changes.

   All 128bits programmed
    FALCON_BUFFER_TBL_HALF
    RX_FILTER_TBL
    TX_DESC_PTR_TBL
    RX_DESC_PTR_TBL
    DRV_EV_REG

   All 64bits programmed
    FALCON_BUFFER_TBL_FULL

   32 bits are programmed (UNLOCKED)
    EVQ_RPTR_REG

   Low 64bits programmed remainder are written with a random number
    RX_DC_CFG_REG
    TX_DC_CFG_REG
    SRM_RX_DC_CFG_REG
    SRM_TX_DC_CFG_REG
    BUF_TBL_CFG_REG
    BUF_TBL_UPD_REG
    SRM_UPD_EVQ_REG
    EVQ_PTR_TBL
    TIMER_CMD_REG
    TX_PACE_TBL
    FATAL_INTR_REG
    INT_EN_REG (When enabling interrupts)
    TX_FLUSH_DESCQ_REG
    RX_FLUSH_DESCQ

  Read Modify Write on low 32bits remainder are written with a random number
    INT_EN_REG (When sending a driver interrupt)
    DRIVER_REGX

  Read Modify Write on low 64bits remainder are written with a random number
   SRM_CFG_REG_OFST
   RX_CFG_REG_OFST
   RX_FILTER_CTL_REG

  Read Modify Write on full 128bits
   TXDP_RESERVED_REG  (aka TXDP_UNDOCUMENTED)
   TX_CFG_REG

*/


/*----------------------------------------------------------------------------
 *
 * DMAQ low-level register interface
 *
 *---------------------------------------------------------------------------*/

static unsigned dmaq_sizes[] = {
	512,
	EFHW_1K,
	EFHW_2K,
	EFHW_4K,
};

#define N_DMAQ_SIZES  (sizeof(dmaq_sizes) / sizeof(dmaq_sizes[0]))

static inline ulong falcon_dma_tx_q_offset(struct efhw_nic *nic, unsigned dmaq)
{
	EFHW_ASSERT(dmaq < nic->num_dmaqs);
	return TX_DESC_PTR_TBL_OFST + dmaq * FALCON_REGISTER128;
}

static inline uint falcon_dma_tx_q_size_index(uint dmaq_size)
{
	uint i;

	/* size must be one of the various options, otherwise we assert */
	for (i = 0; i < N_DMAQ_SIZES; i++) {
		if (dmaq_size == dmaq_sizes[i])
			break;
	}
	EFHW_ASSERT(i < N_DMAQ_SIZES);
	return i;
}

static void
falcon_dmaq_tx_q_init(struct efhw_nic *nic,
		      uint dmaq, uint evq_id, uint own_id,
		      uint tag, uint dmaq_size, uint buf_idx, uint flags)
{
	FALCON_LOCK_DECL;
	uint index, desc_type;
	uint64_t val1, val2, val3;
	ulong offset;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	/* Q attributes */
	int iscsi_hdig_en = ((flags & EFHW_VI_ISCSI_TX_HDIG_EN) != 0);
	int iscsi_ddig_en = ((flags & EFHW_VI_ISCSI_TX_DDIG_EN) != 0);
	int csum_ip_dis = ((flags & EFHW_VI_TX_IP_CSUM_DIS) != 0);
	int csum_tcp_dis = ((flags & EFHW_VI_TX_TCPUDP_CSUM_DIS) != 0);
	int non_ip_drop_dis = ((flags & EFHW_VI_TX_TCPUDP_ONLY) == 0);

	/* initialise the TX descriptor queue pointer table */

	/* NB physical vs buffer addressing is determined by the Queue ID. */

	offset = falcon_dma_tx_q_offset(nic, dmaq);
	index = falcon_dma_tx_q_size_index(dmaq_size);

	/* allow VI flag to override this queue's descriptor type */
	desc_type = (flags & EFHW_VI_TX_PHYS_ADDR_EN) ? 0 : 1;

	/* bug9403: It is dangerous to allow buffer-addressed queues to
	 * have owner_id=0. */
	EFHW_ASSERT((own_id > 0) || desc_type == 0);

	/* dword 1 */
	__DWCHCK(TX_DESCQ_FLUSH_LBN, TX_DESCQ_FLUSH_WIDTH);
	__DWCHCK(TX_DESCQ_TYPE_LBN, TX_DESCQ_TYPE_WIDTH);
	__DWCHCK(TX_DESCQ_SIZE_LBN, TX_DESCQ_SIZE_WIDTH);
	__DWCHCK(TX_DESCQ_LABEL_LBN, TX_DESCQ_LABEL_WIDTH);
	__DWCHCK(TX_DESCQ_OWNER_ID_LBN, TX_DESCQ_OWNER_ID_WIDTH);

	__LWCHK(TX_DESCQ_EVQ_ID_LBN, TX_DESCQ_EVQ_ID_WIDTH);

	__RANGECHCK(1, TX_DESCQ_FLUSH_WIDTH);
	__RANGECHCK(desc_type, TX_DESCQ_TYPE_WIDTH);
	__RANGECHCK(index, TX_DESCQ_SIZE_WIDTH);
	__RANGECHCK(tag, TX_DESCQ_LABEL_WIDTH);
	__RANGECHCK(own_id, TX_DESCQ_OWNER_ID_WIDTH);
	__RANGECHCK(evq_id, TX_DESCQ_EVQ_ID_WIDTH);

	val1 = ((desc_type << TX_DESCQ_TYPE_LBN) |
		(index << TX_DESCQ_SIZE_LBN) |
		(tag << TX_DESCQ_LABEL_LBN) |
		(own_id << TX_DESCQ_OWNER_ID_LBN) |
		(__LOW(evq_id, TX_DESCQ_EVQ_ID_LBN, TX_DESCQ_EVQ_ID_WIDTH)));

	/* dword 2 */
	__DW2CHCK(TX_DESCQ_BUF_BASE_ID_LBN, TX_DESCQ_BUF_BASE_ID_WIDTH);
	__RANGECHCK(buf_idx, TX_DESCQ_BUF_BASE_ID_WIDTH);

	val2 = ((__HIGH(evq_id, TX_DESCQ_EVQ_ID_LBN, TX_DESCQ_EVQ_ID_WIDTH)) |
		(buf_idx << __DW2(TX_DESCQ_BUF_BASE_ID_LBN)));

	/* dword 3 */
	__DW3CHCK(TX_ISCSI_HDIG_EN_LBN, TX_ISCSI_HDIG_EN_WIDTH);
	__DW3CHCK(TX_ISCSI_DDIG_EN_LBN, TX_ISCSI_DDIG_EN_WIDTH);
	__RANGECHCK(iscsi_hdig_en, TX_ISCSI_HDIG_EN_WIDTH);
	__RANGECHCK(iscsi_ddig_en, TX_ISCSI_DDIG_EN_WIDTH);

	val3 = ((iscsi_hdig_en << __DW3(TX_ISCSI_HDIG_EN_LBN)) |
		(iscsi_ddig_en << __DW3(TX_ISCSI_DDIG_EN_LBN)) |
		(1 << __DW3(TX_DESCQ_EN_LBN)));	/* queue enable bit */

	switch (nic->devtype.variant) {
	case 'B':
		__DW3CHCK(TX_NON_IP_DROP_DIS_B0_LBN,
			  TX_NON_IP_DROP_DIS_B0_WIDTH);
		__DW3CHCK(TX_IP_CHKSM_DIS_B0_LBN, TX_IP_CHKSM_DIS_B0_WIDTH);
		__DW3CHCK(TX_TCP_CHKSM_DIS_B0_LBN, TX_TCP_CHKSM_DIS_B0_WIDTH);

		val3 |= ((non_ip_drop_dis << __DW3(TX_NON_IP_DROP_DIS_B0_LBN))|
			 (csum_ip_dis << __DW3(TX_IP_CHKSM_DIS_B0_LBN)) |
			 (csum_tcp_dis << __DW3(TX_TCP_CHKSM_DIS_B0_LBN)));
		break;
	case 'A':
		if (csum_ip_dis || csum_tcp_dis || !non_ip_drop_dis)
			EFHW_WARN
				("%s: bad settings for A1 csum_ip_dis=%d "
				 "csum_tcp_dis=%d non_ip_drop_dis=%d",
				 __func__, csum_ip_dis,
				 csum_tcp_dis, non_ip_drop_dis);
		break;
	default:
		EFHW_ASSERT(0);
		break;
	}

	EFHW_TRACE("%s: txq %x evq %u tag %x id %x buf %x "
		   "%x:%x:%x->%" PRIx64 ":%" PRIx64 ":%" PRIx64,
		   __func__,
		   dmaq, evq_id, tag, own_id, buf_idx, dmaq_size,
		   iscsi_hdig_en, iscsi_ddig_en, val1, val2, val3);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + offset, ((val2 << 32) | val1), val3);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
	return;
}

static inline ulong
falcon_dma_rx_q_offset(struct efhw_nic *nic, unsigned dmaq)
{
	EFHW_ASSERT(dmaq < nic->num_dmaqs);
	return RX_DESC_PTR_TBL_OFST + dmaq * FALCON_REGISTER128;
}

static void
falcon_dmaq_rx_q_init(struct efhw_nic *nic,
		      uint dmaq, uint evq_id, uint own_id,
		      uint tag, uint dmaq_size, uint buf_idx, uint flags)
{
	FALCON_LOCK_DECL;
	uint i, desc_type = 1;
	uint64_t val1, val2, val3;
	ulong offset;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	/* Q attributes */
#if BUG5762_WORKAROUND
	int jumbo = 1;		/* Queues must not have mixed types */
#else
	int jumbo = ((flags & EFHW_VI_JUMBO_EN) != 0);
#endif
	int iscsi_hdig_en = ((flags & EFHW_VI_ISCSI_RX_HDIG_EN) != 0);
	int iscsi_ddig_en = ((flags & EFHW_VI_ISCSI_RX_DDIG_EN) != 0);

	/* initialise the TX descriptor queue pointer table */
	offset = falcon_dma_rx_q_offset(nic, dmaq);

	/* size must be one of the various options, otherwise we assert */
	for (i = 0; i < N_DMAQ_SIZES; i++) {
		if (dmaq_size == dmaq_sizes[i])
			break;
	}
	EFHW_ASSERT(i < N_DMAQ_SIZES);

	/* allow VI flag to override this queue's descriptor type */
	desc_type = (flags & EFHW_VI_RX_PHYS_ADDR_EN) ? 0 : 1;

	/* bug9403: It is dangerous to allow buffer-addressed queues to have
	 * owner_id=0 */
	EFHW_ASSERT((own_id > 0) || desc_type == 0);

	/* dword 1 */
	__DWCHCK(RX_DESCQ_EN_LBN, RX_DESCQ_EN_WIDTH);
	__DWCHCK(RX_DESCQ_JUMBO_LBN, RX_DESCQ_JUMBO_WIDTH);
	__DWCHCK(RX_DESCQ_TYPE_LBN, RX_DESCQ_TYPE_WIDTH);
	__DWCHCK(RX_DESCQ_SIZE_LBN, RX_DESCQ_SIZE_WIDTH);
	__DWCHCK(RX_DESCQ_LABEL_LBN, RX_DESCQ_LABEL_WIDTH);
	__DWCHCK(RX_DESCQ_OWNER_ID_LBN, RX_DESCQ_OWNER_ID_WIDTH);

	__LWCHK(RX_DESCQ_EVQ_ID_LBN, RX_DESCQ_EVQ_ID_WIDTH);

	__RANGECHCK(1, RX_DESCQ_EN_WIDTH);
	__RANGECHCK(jumbo, RX_DESCQ_JUMBO_WIDTH);
	__RANGECHCK(desc_type, RX_DESCQ_TYPE_WIDTH);
	__RANGECHCK(i, RX_DESCQ_SIZE_WIDTH);
	__RANGECHCK(tag, RX_DESCQ_LABEL_WIDTH);
	__RANGECHCK(own_id, RX_DESCQ_OWNER_ID_WIDTH);
	__RANGECHCK(evq_id, RX_DESCQ_EVQ_ID_WIDTH);

	val1 = ((1 << RX_DESCQ_EN_LBN) |
		(jumbo << RX_DESCQ_JUMBO_LBN) |
		(desc_type << RX_DESCQ_TYPE_LBN) |
		(i << RX_DESCQ_SIZE_LBN) |
		(tag << RX_DESCQ_LABEL_LBN) |
		(own_id << RX_DESCQ_OWNER_ID_LBN) |
		(__LOW(evq_id, RX_DESCQ_EVQ_ID_LBN, RX_DESCQ_EVQ_ID_WIDTH)));

	/* dword 2 */
	__DW2CHCK(RX_DESCQ_BUF_BASE_ID_LBN, RX_DESCQ_BUF_BASE_ID_WIDTH);
	__RANGECHCK(buf_idx, RX_DESCQ_BUF_BASE_ID_WIDTH);

	val2 = ((__HIGH(evq_id, RX_DESCQ_EVQ_ID_LBN, RX_DESCQ_EVQ_ID_WIDTH)) |
		(buf_idx << __DW2(RX_DESCQ_BUF_BASE_ID_LBN)));

	/* dword 3 */
	__DW3CHCK(RX_ISCSI_HDIG_EN_LBN, RX_ISCSI_HDIG_EN_WIDTH);
	__DW3CHCK(RX_ISCSI_DDIG_EN_LBN, RX_ISCSI_DDIG_EN_WIDTH);
	__RANGECHCK(iscsi_hdig_en, RX_ISCSI_HDIG_EN_WIDTH);
	__RANGECHCK(iscsi_ddig_en, RX_ISCSI_DDIG_EN_WIDTH);

	val3 = (iscsi_hdig_en << __DW3(RX_ISCSI_HDIG_EN_LBN)) |
	    (iscsi_ddig_en << __DW3(RX_ISCSI_DDIG_EN_LBN));

	EFHW_TRACE("%s: rxq %x evq %u tag %x id %x buf %x %s "
		   "%x:%x:%x -> %" PRIx64 ":%" PRIx64 ":%" PRIx64,
		   __func__,
		   dmaq, evq_id, tag, own_id, buf_idx,
		   jumbo ? "jumbo" : "normal", dmaq_size,
		   iscsi_hdig_en, iscsi_ddig_en, val1, val2, val3);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + offset, ((val2 << 32) | val1), val3);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
	return;
}

static void falcon_dmaq_tx_q_disable(struct efhw_nic *nic, uint dmaq)
{
	FALCON_LOCK_DECL;
	uint64_t val1, val2, val3;
	ulong offset;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	/* initialise the TX descriptor queue pointer table */

	offset = falcon_dma_tx_q_offset(nic, dmaq);

	/* dword 1 */
	__DWCHCK(TX_DESCQ_TYPE_LBN, TX_DESCQ_TYPE_WIDTH);

	val1 = ((uint64_t) 1 << TX_DESCQ_TYPE_LBN);

	/* dword 2 */
	val2 = 0;

	/* dword 3 */
	val3 = (0 << __DW3(TX_DESCQ_EN_LBN));	/* queue enable bit */

	EFHW_TRACE("%s: %x->%" PRIx64 ":%" PRIx64 ":%" PRIx64,
		   __func__, dmaq, val1, val2, val3);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + offset, ((val2 << 32) | val1), val3);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
	return;
}

static void falcon_dmaq_rx_q_disable(struct efhw_nic *nic, uint dmaq)
{
	FALCON_LOCK_DECL;
	uint64_t val1, val2, val3;
	ulong offset;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	/* initialise the TX descriptor queue pointer table */
	offset = falcon_dma_rx_q_offset(nic, dmaq);

	/* dword 1 */
	__DWCHCK(RX_DESCQ_EN_LBN, RX_DESCQ_EN_WIDTH);
	__DWCHCK(RX_DESCQ_TYPE_LBN, RX_DESCQ_TYPE_WIDTH);

	val1 = ((0 << RX_DESCQ_EN_LBN) | (1 << RX_DESCQ_TYPE_LBN));

	/* dword 2 */
	val2 = 0;

	/* dword 3 */
	val3 = 0;

	EFHW_TRACE("falcon_dmaq_rx_q_disable: %x->%"
		   PRIx64 ":%" PRIx64 ":%" PRIx64,
		   dmaq, val1, val2, val3);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + offset, ((val2 << 32) | val1), val3);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
	return;
}


/*----------------------------------------------------------------------------
 *
 * Buffer Table low-level register interface
 *
 *---------------------------------------------------------------------------*/

/*! Convert a (potentially) 64-bit physical address to 32-bits.  Every use
** of this function is a place where we're not 64-bit clean.
*/
static inline uint32_t dma_addr_to_u32(dma_addr_t addr)
{
	/* Top bits had better be zero! */
	EFHW_ASSERT(addr == (addr & 0xffffffff));
	return (uint32_t) addr;
}

static inline uint32_t
falcon_nic_buffer_table_entry32_mk(dma_addr_t dma_addr, int own_id)
{
	uint32_t dma_addr32 = FALCON_BUFFER_4K_PAGE(dma_addr_to_u32(dma_addr));

	/* don't do this to me */
	EFHW_BUILD_ASSERT(BUF_ADR_HBUF_ODD_LBN == BUF_ADR_HBUF_EVEN_LBN + 32);
	EFHW_BUILD_ASSERT(BUF_OWNER_ID_HBUF_ODD_LBN ==
			  BUF_OWNER_ID_HBUF_EVEN_LBN + 32);

	EFHW_BUILD_ASSERT(BUF_OWNER_ID_HBUF_ODD_WIDTH ==
			  BUF_OWNER_ID_HBUF_EVEN_WIDTH);
	EFHW_BUILD_ASSERT(BUF_ADR_HBUF_ODD_WIDTH == BUF_ADR_HBUF_EVEN_WIDTH);

	__DWCHCK(BUF_ADR_HBUF_EVEN_LBN, BUF_ADR_HBUF_EVEN_WIDTH);
	__DWCHCK(BUF_OWNER_ID_HBUF_EVEN_LBN, BUF_OWNER_ID_HBUF_EVEN_WIDTH);

	__RANGECHCK(dma_addr32, BUF_ADR_HBUF_EVEN_WIDTH);
	__RANGECHCK(own_id, BUF_OWNER_ID_HBUF_EVEN_WIDTH);

	return (dma_addr32 << BUF_ADR_HBUF_EVEN_LBN) |
		(own_id << BUF_OWNER_ID_HBUF_EVEN_LBN);
}

static inline uint64_t
falcon_nic_buffer_table_entry64_mk(dma_addr_t dma_addr,
				   int bufsz,	/* bytes */
				   int region, int own_id)
{
	__DW2CHCK(IP_DAT_BUF_SIZE_LBN, IP_DAT_BUF_SIZE_WIDTH);
	__DW2CHCK(BUF_ADR_REGION_LBN, BUF_ADR_REGION_WIDTH);
	__LWCHK(BUF_ADR_FBUF_LBN, BUF_ADR_FBUF_WIDTH);
	__DWCHCK(BUF_OWNER_ID_FBUF_LBN, BUF_OWNER_ID_FBUF_WIDTH);

	EFHW_ASSERT((bufsz == EFHW_4K) || (bufsz == EFHW_8K));

	dma_addr = (dma_addr >> 12) & __FALCON_MASK64(BUF_ADR_FBUF_WIDTH);

	__RANGECHCK(dma_addr, BUF_ADR_FBUF_WIDTH);
	__RANGECHCK(1, IP_DAT_BUF_SIZE_WIDTH);
	__RANGECHCK(region, BUF_ADR_REGION_WIDTH);
	__RANGECHCK(own_id, BUF_OWNER_ID_FBUF_WIDTH);

	return ((uint64_t) (bufsz == EFHW_8K) << IP_DAT_BUF_SIZE_LBN) |
		((uint64_t) region << BUF_ADR_REGION_LBN) |
		((uint64_t) dma_addr << BUF_ADR_FBUF_LBN) |
		((uint64_t) own_id << BUF_OWNER_ID_FBUF_LBN);
}

static inline void
_falcon_nic_buffer_table_set32(struct efhw_nic *nic,
			       dma_addr_t dma_addr, uint bufsz,
			       uint region, /* not used */
			       int own_id, int buffer_id)
{
	/* programming the half table needs to be done in pairs. */
	uint64_t entry, val, shift;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	volatile char __iomem *offset;

	EFHW_BUILD_ASSERT(BUF_ADR_HBUF_ODD_LBN == BUF_ADR_HBUF_EVEN_LBN + 32);
	EFHW_BUILD_ASSERT(BUF_OWNER_ID_HBUF_ODD_LBN ==
			  BUF_OWNER_ID_HBUF_EVEN_LBN + 32);

	shift = (buffer_id & 1) ? 32 : 0;

	offset = (efhw_kva + BUF_HALF_TBL_OFST +
		  ((buffer_id & ~1) * FALCON_BUFFER_TBL_HALF_BYTES));

	entry = falcon_nic_buffer_table_entry32_mk(dma_addr_to_u32(dma_addr),
						   own_id);

#if FALCON_USE_SHADOW_BUFFER_TABLE
	val = _falcon_buffer_table[buffer_id & ~1];
#else
	/* This will not work unless we've completed
	 * the buffer table updates */
	falcon_read_q(offset, &val);
#endif
	val &= ~(((uint64_t) 0xffffffff) << shift);
	val |= (entry << shift);

	EFHW_TRACE("%s[%x]: %lx:%x:%" PRIx64 "->%x = %"
		   PRIx64, __func__, buffer_id, (unsigned long) dma_addr,
		   own_id, entry, (unsigned)(offset - efhw_kva), val);

	/* Falcon requires that access to this register is serialised */
	falcon_write_q(offset, val);

	/* NB. No mmiowb().  Caller should do that e.g by calling commit  */

#if FALCON_USE_SHADOW_BUFFER_TABLE
	_falcon_buffer_table[buffer_id & ~1] = val;
#endif

	/* Confirm the entry if the event queues haven't been set up. */
	if (!nic->irq_handler) {
		uint64_t new_val;
		int count = 0;
		while (1) {
			mmiowb();
			falcon_read_q(offset, &new_val);
			if (new_val == val)
				break;
			count++;
			if (count > 1000) {
				EFHW_WARN("%s: poll Timeout", __func__);
				break;
			}
			udelay(1);
		}
	}
}

static inline void
_falcon_nic_buffer_table_set64(struct efhw_nic *nic,
			       dma_addr_t dma_addr, uint bufsz,
			       uint region, int own_id, int buffer_id)
{
	volatile char __iomem *offset;
	uint64_t entry;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	EFHW_ASSERT(region < FALCON_REGION_NUM);

	EFHW_ASSERT((bufsz == EFHW_4K) ||
		    (bufsz == EFHW_8K && FALCON_BUFFER_TABLE_FULL_MODE));

	offset = (efhw_kva + BUF_FULL_TBL_OFST +
		  (buffer_id * FALCON_BUFFER_TBL_FULL_BYTES));

	entry = falcon_nic_buffer_table_entry64_mk(dma_addr, bufsz, region,
						   own_id);

	EFHW_TRACE("%s[%x]: %lx:bufsz=%x:region=%x:ownid=%x",
		   __func__, buffer_id, (unsigned long) dma_addr, bufsz,
		   region, own_id);

	EFHW_TRACE("%s: BUF[%x]:NIC[%x]->%" PRIx64,
		   __func__, buffer_id,
		   (unsigned int)(offset - efhw_kva), entry);

	/* Falcon requires that access to this register is serialised */
	falcon_write_q(offset, entry);

	/* NB. No mmiowb().  Caller should do that e.g by calling commit */

	/* Confirm the entry if the event queues haven't been set up. */
	if (!nic->irq_handler) {
		uint64_t new_entry;
		int count = 0;
		while (1) {
			mmiowb();
			falcon_read_q(offset, &new_entry);
			if (new_entry == entry)
				return;
			count++;
			if (count > 1000) {
				EFHW_WARN("%s: poll Timeout waiting for "
					  "value %"PRIx64
					  " (last was %"PRIx64")",
					  __func__, entry, new_entry);
				break;
			}
			udelay(1);
		}
	}
}

#if FALCON_BUFFER_TABLE_FULL_MODE
#define _falcon_nic_buffer_table_set _falcon_nic_buffer_table_set64
#else
#define _falcon_nic_buffer_table_set _falcon_nic_buffer_table_set32
#endif

static inline void _falcon_nic_buffer_table_commit(struct efhw_nic *nic)
{
	/* MUST be called holding the FALCON_LOCK */
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	uint64_t cmd;

	EFHW_BUILD_ASSERT(BUF_TBL_UPD_REG_KER_OFST == BUF_TBL_UPD_REG_OFST);

	__DW2CHCK(BUF_UPD_CMD_LBN, BUF_UPD_CMD_WIDTH);
	__RANGECHCK(1, BUF_UPD_CMD_WIDTH);

	cmd = ((uint64_t) 1 << BUF_UPD_CMD_LBN);

	/* Falcon requires 128 bit atomic access for this register */
	falcon_write_qq(efhw_kva + BUF_TBL_UPD_REG_OFST,
			cmd, FALCON_ATOMIC_UPD_REG);
	mmiowb();

	nic->buf_commit_outstanding++;
	EFHW_TRACE("COMMIT REQ out=%d", nic->buf_commit_outstanding);
}

static void falcon_nic_buffer_table_commit(struct efhw_nic *nic)
{
	/* nothing to do */
}

static inline void
_falcon_nic_buffer_table_clear(struct efhw_nic *nic, int buffer_id, int num)
{
	uint64_t cmd;
	uint64_t start_id = buffer_id;
	uint64_t end_id = buffer_id + num - 1;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	volatile char __iomem *offset = (efhw_kva + BUF_TBL_UPD_REG_OFST);

	EFHW_BUILD_ASSERT(BUF_TBL_UPD_REG_KER_OFST == BUF_TBL_UPD_REG_OFST);

#if !FALCON_BUFFER_TABLE_FULL_MODE
	/* buffer_ids in half buffer mode reference pairs of buffers */
	EFHW_ASSERT(buffer_id % 1 == 0);
	EFHW_ASSERT(num % 1 == 0);
	start_id = start_id >> 1;
	end_id = end_id >> 1;
#endif

	EFHW_ASSERT(num >= 1);

	__DWCHCK(BUF_CLR_START_ID_LBN, BUF_CLR_START_ID_WIDTH);
	__DW2CHCK(BUF_CLR_END_ID_LBN, BUF_CLR_END_ID_WIDTH);

	__DW2CHCK(BUF_CLR_CMD_LBN, BUF_CLR_CMD_WIDTH);
	__RANGECHCK(1, BUF_CLR_CMD_WIDTH);

	__RANGECHCK(start_id, BUF_CLR_START_ID_WIDTH);
	__RANGECHCK(end_id, BUF_CLR_END_ID_WIDTH);

	cmd = (((uint64_t) 1 << BUF_CLR_CMD_LBN) |
	       (start_id << BUF_CLR_START_ID_LBN) |
	       (end_id << BUF_CLR_END_ID_LBN));

	/* Falcon requires 128 bit atomic access for this register */
	falcon_write_qq(offset, cmd, FALCON_ATOMIC_UPD_REG);
	mmiowb();

	nic->buf_commit_outstanding++;
	EFHW_TRACE("COMMIT CLEAR out=%d", nic->buf_commit_outstanding);
}

/*----------------------------------------------------------------------------
 *
 * Events low-level register interface
 *
 *---------------------------------------------------------------------------*/

static unsigned eventq_sizes[] = {
	512,
	EFHW_1K,
	EFHW_2K,
	EFHW_4K,
	EFHW_8K,
	EFHW_16K,
	EFHW_32K
};

#define N_EVENTQ_SIZES  (sizeof(eventq_sizes) / sizeof(eventq_sizes[0]))

static inline void falcon_nic_srm_upd_evq(struct efhw_nic *nic, int evq)
{
	/* set up the eventq which will receive events from the SRAM module.
	 * i.e buffer table updates and clears, TX and RX aperture table
	 * updates */

	FALCON_LOCK_DECL;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	EFHW_BUILD_ASSERT(SRM_UPD_EVQ_REG_OFST == SRM_UPD_EVQ_REG_KER_OFST);

	__DWCHCK(SRM_UPD_EVQ_ID_LBN, SRM_UPD_EVQ_ID_WIDTH);
	__RANGECHCK(evq, SRM_UPD_EVQ_ID_WIDTH);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + SRM_UPD_EVQ_REG_OFST,
			((uint64_t) evq << SRM_UPD_EVQ_ID_LBN),
			FALCON_ATOMIC_SRPM_UDP_EVQ_REG);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
}

static void
falcon_nic_evq_ptr_tbl(struct efhw_nic *nic,
		       uint evq,	/* evq id */
		       uint enable,	/* 1 to enable, 0 to disable */
		       uint buf_base_id,/* Buffer table base for EVQ */
		       uint evq_size	/* Number of events */)
{
	FALCON_LOCK_DECL;
	uint i, val;
	ulong offset;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	/* size must be one of the various options, otherwise we assert */
	for (i = 0; i < N_EVENTQ_SIZES; i++) {
		if (evq_size <= eventq_sizes[i])
			break;
	}
	EFHW_ASSERT(i < N_EVENTQ_SIZES);

	__DWCHCK(EVQ_BUF_BASE_ID_LBN, EVQ_BUF_BASE_ID_WIDTH);
	__DWCHCK(EVQ_SIZE_LBN, EVQ_SIZE_WIDTH);
	__DWCHCK(EVQ_EN_LBN, EVQ_EN_WIDTH);

	__RANGECHCK(i, EVQ_SIZE_WIDTH);
	__RANGECHCK(buf_base_id, EVQ_BUF_BASE_ID_WIDTH);
	__RANGECHCK(1, EVQ_EN_WIDTH);

	/* if !enable then only evq needs to be correct, although valid
	 * values need to be passed in for other arguments to prevent
	 * assertions */

	val = ((i << EVQ_SIZE_LBN) | (buf_base_id << EVQ_BUF_BASE_ID_LBN) |
	       (enable ? (1 << EVQ_EN_LBN) : 0));

	EFHW_ASSERT(evq < nic->num_evqs);

	offset = EVQ_PTR_TBL_CHAR_OFST;
	offset += evq * FALCON_REGISTER128;

	EFHW_TRACE("%s: evq %u en=%x:buf=%x:size=%x->%x at %lx",
		   __func__, evq, enable, buf_base_id, evq_size, val,
		   offset);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + offset, val, FALCON_ATOMIC_PTR_TBL_REG);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);

	/* caller must wait for an update done event before writing any more
	   table entries */

	return;
}

void
falcon_nic_evq_ack(struct efhw_nic *nic,
		   uint evq,	/* evq id */
		   uint rptr,	/* new read pointer update */
		   bool wakeup	/* request a wakeup event if ptr's != */
    )
{
	uint val;
	ulong offset;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	EFHW_BUILD_ASSERT(FALCON_EVQ_CHAR == 4);

	__DWCHCK(EVQ_RPTR_LBN, EVQ_RPTR_WIDTH);
	__RANGECHCK(rptr, EVQ_RPTR_WIDTH);

	val = (rptr << EVQ_RPTR_LBN);

	EFHW_ASSERT(evq < nic->num_evqs);

	if (evq < FALCON_EVQ_CHAR) {
		offset = EVQ_RPTR_REG_KER_OFST;
		offset += evq * FALCON_REGISTER128;

		EFHW_ASSERT(!wakeup);	/* don't try this at home */
	} else {
		offset = EVQ_RPTR_REG_OFST + (FALCON_EVQ_CHAR *
					      FALCON_REGISTER128);
		offset += (evq - FALCON_EVQ_CHAR) * FALCON_REGISTER128;

		/* nothing to do for interruptless event queues which do
		 * not want a wakeup */
		if (evq != FALCON_EVQ_CHAR && !wakeup)
			return;
	}

	EFHW_TRACE("%s: %x %x %x->%x", __func__, evq, rptr, wakeup, val);

	writel(val, efhw_kva + offset);
	mmiowb();
}

/*---------------------------------------------------------------------------*/

static inline void
falcon_drv_ev(struct efhw_nic *nic, uint64_t data, uint qid)
{
	FALCON_LOCK_DECL;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	/* send an event from one driver to the other */
	EFHW_BUILD_ASSERT(DRV_EV_REG_KER_OFST == DRV_EV_REG_OFST);
	EFHW_BUILD_ASSERT(DRV_EV_DATA_LBN == 0);
	EFHW_BUILD_ASSERT(DRV_EV_DATA_WIDTH == 64);
	EFHW_BUILD_ASSERT(DRV_EV_QID_LBN == 64);
	EFHW_BUILD_ASSERT(DRV_EV_QID_WIDTH == 12);

	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + DRV_EV_REG_OFST, data, qid);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
}

_DEBUG_SYM_ void
falcon_ab_timer_tbl_set(struct efhw_nic *nic,
			uint evq,	/* timer id */
			uint mode,	/* mode bits */
			uint countdown /* counting value to set */)
{
	FALCON_LOCK_DECL;
	uint val;
	ulong offset;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	EFHW_BUILD_ASSERT(TIMER_VAL_LBN == 0);

	__DWCHCK(TIMER_MODE_LBN, TIMER_MODE_WIDTH);
	__DWCHCK(TIMER_VAL_LBN, TIMER_VAL_WIDTH);

	__RANGECHCK(mode, TIMER_MODE_WIDTH);
	__RANGECHCK(countdown, TIMER_VAL_WIDTH);

	val = ((mode << TIMER_MODE_LBN) | (countdown << TIMER_VAL_LBN));

	if (evq < FALCON_EVQ_CHAR) {
		offset = TIMER_CMD_REG_KER_OFST;
		offset += evq * EFHW_8K;	/* PAGE mapped register */
	} else {
		offset = TIMER_TBL_OFST;
		offset += evq * FALCON_REGISTER128;
	}
	EFHW_ASSERT(evq < nic->num_evqs);

	EFHW_TRACE("%s: evq %u mode %x (%s) time %x -> %08x",
		   __func__, evq, mode,
		   mode == 0 ? "DISABLE" :
		   mode == 1 ? "IMMED" :
		   mode == 2 ? (evq < 5 ? "HOLDOFF" : "RX_TRIG") :
		   "<BAD>", countdown, val);

	/* Falcon requires 128 bit atomic access for this register when
	 * accessed from the driver. User access to timers is paged mapped
	 */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + offset, val, FALCON_ATOMIC_TIMER_CMD_REG);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
	return;
}


/*--------------------------------------------------------------------
 *
 * Rate pacing - Low level interface
 *
 *--------------------------------------------------------------------*/
void falcon_nic_pace(struct efhw_nic *nic, uint dmaq, uint pace)
{
	/* Pace specified in 2^(units of microseconds). This is the minimum
	   additional delay imposed over and above the IPG.

	   Pacing only available on the virtual interfaces
	 */
	FALCON_LOCK_DECL;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	ulong offset;

	if (pace > 20)
		pace = 20;	/* maxm supported value */

	__DWCHCK(TX_PACE_LBN, TX_PACE_WIDTH);
	__RANGECHCK(pace, TX_PACE_WIDTH);

	switch (nic->devtype.variant) {
	case 'A':
		EFHW_ASSERT(dmaq >= TX_PACE_TBL_FIRST_QUEUE_A1);
		offset = TX_PACE_TBL_A1_OFST;
		offset += (dmaq - TX_PACE_TBL_FIRST_QUEUE_A1) * 16;
		break;
	case 'B':
		/* Would be nice to assert this, but as dmaq is unsigned and
		 * TX_PACE_TBL_FIRST_QUEUE_B0 is 0, it makes no sense
		 * EFHW_ASSERT(dmaq >= TX_PACE_TBL_FIRST_QUEUE_B0);
		 */
		offset = TX_PACE_TBL_B0_OFST;
		offset += (dmaq - TX_PACE_TBL_FIRST_QUEUE_B0) * 16;
		break;
	default:
		EFHW_ASSERT(0);
		offset = 0;
		break;
	}

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + offset, pace, FALCON_ATOMIC_PACE_REG);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);

	EFHW_TRACE("%s: txq %d offset=%lx pace=2^%x",
		   __func__, dmaq, offset, pace);
}

/*--------------------------------------------------------------------
 *
 * Interrupt - Low level interface
 *
 *--------------------------------------------------------------------*/

static void falcon_nic_handle_fatal_int(struct efhw_nic *nic)
{
	FALCON_LOCK_DECL;
	volatile char __iomem *offset;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	uint64_t val;

	offset = (efhw_kva + FATAL_INTR_REG_OFST);

	/* Falcon requires 32 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	val = readl(offset);
	FALCON_LOCK_UNLOCK(nic);

	/* ?? BUG3249 - need to disable illegal address interrupt */
	/* ?? BUG3114 - need to backport interrupt storm protection code */
	EFHW_ERR("fatal interrupt: %s%s%s%s%s%s%s%s%s%s%s%s[%" PRIx64 "]",
		 val & (1 << PCI_BUSERR_INT_CHAR_LBN) ? "PCI-bus-error " : "",
		 val & (1 << SRAM_OOB_INT_CHAR_LBN) ? "SRAM-oob " : "",
		 val & (1 << BUFID_OOB_INT_CHAR_LBN) ? "bufid-oob " : "",
		 val & (1 << MEM_PERR_INT_CHAR_LBN) ? "int-parity " : "",
		 val & (1 << RBUF_OWN_INT_CHAR_LBN) ? "rx-bufid-own " : "",
		 val & (1 << TBUF_OWN_INT_CHAR_LBN) ? "tx-bufid-own " : "",
		 val & (1 << RDESCQ_OWN_INT_CHAR_LBN) ? "rx-desc-own " : "",
		 val & (1 << TDESCQ_OWN_INT_CHAR_LBN) ? "tx-desc-own " : "",
		 val & (1 << EVQ_OWN_INT_CHAR_LBN) ? "evq-own " : "",
		 val & (1 << EVFF_OFLO_INT_CHAR_LBN) ? "evq-fifo " : "",
		 val & (1 << ILL_ADR_INT_CHAR_LBN) ? "ill-addr " : "",
		 val & (1 << SRM_PERR_INT_CHAR_LBN) ? "sram-parity " : "", val);
}

static void falcon_nic_interrupt_hw_enable(struct efhw_nic *nic)
{
	FALCON_LOCK_DECL;
	uint val;
	volatile char __iomem *offset;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	EFHW_BUILD_ASSERT(DRV_INT_EN_CHAR_WIDTH == 1);

	if (nic->flags & NIC_FLAG_NO_INTERRUPT)
		return;

	offset = (efhw_kva + INT_EN_REG_CHAR_OFST);
	val = 1 << DRV_INT_EN_CHAR_LBN;

	EFHW_NOTICE("%s: %x -> %x", __func__, (int)(offset - efhw_kva),
		    val);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(offset, val, FALCON_ATOMIC_INT_EN_REG);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
}

static void falcon_nic_interrupt_hw_disable(struct efhw_nic *nic)
{
	FALCON_LOCK_DECL;
	volatile char __iomem *offset;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	EFHW_BUILD_ASSERT(SRAM_PERR_INT_KER_WIDTH == 1);
	EFHW_BUILD_ASSERT(DRV_INT_EN_KER_LBN == 0);
	EFHW_BUILD_ASSERT(SRAM_PERR_INT_CHAR_WIDTH == 1);
	EFHW_BUILD_ASSERT(DRV_INT_EN_CHAR_LBN == 0);
	EFHW_BUILD_ASSERT(SRAM_PERR_INT_KER_LBN == SRAM_PERR_INT_CHAR_LBN);
	EFHW_BUILD_ASSERT(DRV_INT_EN_KER_LBN == DRV_INT_EN_CHAR_LBN);

	if (nic->flags & NIC_FLAG_NO_INTERRUPT)
		return;

	offset = (efhw_kva + INT_EN_REG_CHAR_OFST);

	EFHW_NOTICE("%s: %x -> 0", __func__, (int)(offset - efhw_kva));

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(offset, 0, FALCON_ATOMIC_INT_EN_REG);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
}

static void falcon_nic_irq_addr_set(struct efhw_nic *nic, dma_addr_t dma_addr)
{
	FALCON_LOCK_DECL;
	volatile char __iomem *offset;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);

	offset = (efhw_kva + INT_ADR_REG_CHAR_OFST);

	EFHW_NOTICE("%s: %x -> " DMA_ADDR_T_FMT, __func__,
		    (int)(offset - efhw_kva), dma_addr);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(offset, dma_addr, FALCON_ATOMIC_INT_ADR_REG);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
}


/*--------------------------------------------------------------------
 *
 * RXDP - low level interface
 *
 *--------------------------------------------------------------------*/

void
falcon_nic_set_rx_usr_buf_size(struct efhw_nic *nic, int usr_buf_bytes)
{
	FALCON_LOCK_DECL;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	uint64_t val, val2, usr_buf_size = usr_buf_bytes / 32;
	int rubs_lbn, rubs_width, roec_lbn;

	EFHW_BUILD_ASSERT(RX_CFG_REG_OFST == RX_CFG_REG_KER_OFST);

	switch (nic->devtype.variant) {
	default:
		EFHW_ASSERT(0);
		/* Fall-through to avoid compiler warnings. */
	case 'A':
		rubs_lbn = RX_USR_BUF_SIZE_A1_LBN;
		rubs_width = RX_USR_BUF_SIZE_A1_WIDTH;
		roec_lbn = RX_OWNERR_CTL_A1_LBN;
		break;
	case 'B':
		rubs_lbn = RX_USR_BUF_SIZE_B0_LBN;
		rubs_width = RX_USR_BUF_SIZE_B0_WIDTH;
		roec_lbn = RX_OWNERR_CTL_B0_LBN;
		break;
	}

	__DWCHCK(rubs_lbn, rubs_width);
	__QWCHCK(roec_lbn, 1);
	__RANGECHCK(usr_buf_size, rubs_width);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_read_qq(efhw_kva + RX_CFG_REG_OFST, &val, &val2);

	val &= ~((__FALCON_MASK64(rubs_width)) << rubs_lbn);
	val |= (usr_buf_size << rubs_lbn);

	/* shouldn't be needed for a production driver */
	val |= ((uint64_t) 1 << roec_lbn);

	falcon_write_qq(efhw_kva + RX_CFG_REG_OFST, val, val2);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
}
EXPORT_SYMBOL(falcon_nic_set_rx_usr_buf_size);


/*--------------------------------------------------------------------
 *
 * TXDP - low level interface
 *
 *--------------------------------------------------------------------*/

_DEBUG_SYM_ void falcon_nic_tx_cfg(struct efhw_nic *nic, int unlocked)
{
	FALCON_LOCK_DECL;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	uint64_t val1, val2;

	EFHW_BUILD_ASSERT(TX_CFG_REG_OFST == TX_CFG_REG_KER_OFST);
	__DWCHCK(TX_OWNERR_CTL_LBN, TX_OWNERR_CTL_WIDTH);
	__DWCHCK(TX_NON_IP_DROP_DIS_LBN, TX_NON_IP_DROP_DIS_WIDTH);

	FALCON_LOCK_LOCK(nic);
	falcon_read_qq(efhw_kva + TX_CFG_REG_OFST, &val1, &val2);

	/* Will flag fatal interrupts on owner id errors. This should not be
	   on for production code because there is otherwise a denial of
	   serivce attack possible */
	val1 |= (1 << TX_OWNERR_CTL_LBN);

	/* Setup user queue TCP/UDP only packet security */
	if (unlocked)
		val1 |= (1 << TX_NON_IP_DROP_DIS_LBN);
	else
		val1 &= ~(1 << TX_NON_IP_DROP_DIS_LBN);

	falcon_write_qq(efhw_kva + TX_CFG_REG_OFST, val1, val2);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
}

/*--------------------------------------------------------------------
 *
 * Random thresholds - Low level interface (Would like these to be op
 * defaults wherever possible)
 *
 *--------------------------------------------------------------------*/

void falcon_nic_pace_cfg(struct efhw_nic *nic, int fb_base, int bin_thresh)
{
	FALCON_LOCK_DECL;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	unsigned offset = 0;
	uint64_t val;

	__DWCHCK(TX_PACE_FB_BASE_LBN, TX_PACE_FB_BASE_WIDTH);
	__DWCHCK(TX_PACE_BIN_TH_LBN, TX_PACE_BIN_TH_WIDTH);

	switch (nic->devtype.variant) {
	case 'A':  offset = TX_PACE_REG_A1_OFST;  break;
	case 'B':  offset = TX_PACE_REG_B0_OFST;  break;
	default:   EFHW_ASSERT(0);                break;
	}

	val = (0x15 << TX_PACE_SB_NOTAF_LBN);
	val |= (0xb << TX_PACE_SB_AF_LBN);

	val |= ((fb_base & __FALCON_MASK64(TX_PACE_FB_BASE_WIDTH)) <<
		 TX_PACE_FB_BASE_LBN);
	val |= ((bin_thresh & __FALCON_MASK64(TX_PACE_BIN_TH_WIDTH)) <<
		 TX_PACE_BIN_TH_LBN);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + offset, val, 0);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
}


/**********************************************************************
 * Implementation of the HAL. ********************************************
 **********************************************************************/

/*----------------------------------------------------------------------------
 *
 * Initialisation and configuration discovery
 *
 *---------------------------------------------------------------------------*/

static int falcon_nic_init_irq_channel(struct efhw_nic *nic, int enable)
{
	/* create a buffer for the irq channel */
	int rc;

	if (enable) {
		rc = efhw_iopage_alloc(nic, &nic->irq_iobuff);
		if (rc < 0)
			return rc;

		falcon_nic_irq_addr_set(nic,
				efhw_iopage_dma_addr(&nic->irq_iobuff));
	} else {
		if (efhw_iopage_is_valid(&nic->irq_iobuff))
			efhw_iopage_free(nic, &nic->irq_iobuff);

		efhw_iopage_mark_invalid(&nic->irq_iobuff);
		falcon_nic_irq_addr_set(nic, 0);
	}

	EFHW_TRACE("%s: %lx %sable", __func__,
		   (unsigned long) efhw_iopage_dma_addr(&nic->irq_iobuff),
		   enable ? "en" : "dis");

	return 0;
}

static void falcon_nic_close_hardware(struct efhw_nic *nic)
{
	/* check we are in possession of some hardware */
	if (!efhw_nic_have_hw(nic))
		return;

	falcon_nic_init_irq_channel(nic, 0);
	falcon_nic_filter_dtor(nic);

	EFHW_NOTICE("%s:", __func__);
}

static int
falcon_nic_init_hardware(struct efhw_nic *nic,
			 struct efhw_ev_handler *ev_handlers,
			 const uint8_t *mac_addr, int non_irq_evq)
{
	int rc;

	/* header sanity checks */
	FALCON_ASSERT_VALID();

	/* Initialise supporting modules */
	rc = falcon_nic_filter_ctor(nic);
	if (rc < 0)
		return rc;

#if FALCON_USE_SHADOW_BUFFER_TABLE
	CI_ZERO_ARRAY(_falcon_buffer_table, FALCON_BUFFER_TBL_NUM);
#endif

	/* Initialise the top level hardware blocks */
	memcpy(nic->mac_addr, mac_addr, ETH_ALEN);

	EFHW_TRACE("%s:", __func__);

	/* nic.c:efhw_nic_init marks all the interrupt units as unused.

	   ?? TODO we should be able to request the non-interrupting event
	   queue and the net driver's (for a net driver that is using libefhw)
	   additional RSS queues here.

	   Result would be that that net driver could call
	   nic.c:efhw_nic_allocate_common_hardware_resources() and that the
	   IFDEF FALCON's can be removed from
	   nic.c:efhw_nic_allocate_common_hardware_resources()
	 */
	nic->irq_unit = INT_EN_REG_CHAR_OFST;

	/*****************************************************************
	 * The rest of this function deals with initialization of the NICs
	 * hardware (as opposed to the initialization of the
	 * struct efhw_nic data structure */

	/* char driver grabs SRM events onto the non interrupting
	 * event queue */
	falcon_nic_srm_upd_evq(nic, non_irq_evq);

	/* RXDP tweaks */

	/* ?? bug2396 rx_cfg should be ok so long as the net driver
	 * always pushes buffers big enough for the link MTU */

	/* set the RX buffer cutoff size to be the same as PAGE_SIZE.
	 * Use this value when we think that there will be a lot of
	 * jumbo frames.
	 *
	 * The default value 1600 is useful when packets are small,
	 * but would means that jumbo frame RX queues would need more
	 * descriptors pushing */
	falcon_nic_set_rx_usr_buf_size(nic, FALCON_RX_USR_BUF_SIZE);

	/* TXDP tweaks */
	/* ?? bug2396 looks ok */
	falcon_nic_tx_cfg(nic, /*unlocked(for non-UDP/TCP)= */ 0);
	falcon_nic_pace_cfg(nic, 4, 2);

	/* ?? bug2396
	 * netdriver must load first or else must RMW this register */
	falcon_nic_rx_filter_ctl_set(nic, RX_FILTER_CTL_SRCH_LIMIT_TCP_FULL,
				     RX_FILTER_CTL_SRCH_LIMIT_TCP_WILD,
				     RX_FILTER_CTL_SRCH_LIMIT_UDP_FULL,
				     RX_FILTER_CTL_SRCH_LIMIT_UDP_WILD);

	if (!(nic->flags & NIC_FLAG_NO_INTERRUPT)) {
		rc = efhw_keventq_ctor(nic, FALCON_EVQ_CHAR,
				       &nic->interrupting_evq, ev_handlers);
		if (rc < 0) {
			EFHW_ERR("%s: efhw_keventq_ctor() failed (%d) evq=%d",
				 __func__, rc, FALCON_EVQ_CHAR);
			return rc;
		}
	}
	rc = efhw_keventq_ctor(nic, non_irq_evq,
			       &nic->non_interrupting_evq, NULL);
	if (rc < 0) {
		EFHW_ERR("%s: efhw_keventq_ctor() failed (%d) evq=%d",
			 __func__, rc, non_irq_evq);
		return rc;
	}

	/* allocate IRQ channel */
	rc = falcon_nic_init_irq_channel(nic, 1);
	/* ignore failure at user-level for eftest */
	if ((rc < 0) && !(nic->options & NIC_OPT_EFTEST))
		return rc;

	return 0;
}

/*--------------------------------------------------------------------
 *
 * Interrupt
 *
 *--------------------------------------------------------------------*/

static void
falcon_nic_interrupt_enable(struct efhw_nic *nic)
{
	struct efhw_keventq *q;
	unsigned rdptr;

	if (nic->flags & NIC_FLAG_NO_INTERRUPT)
		return;

	/* Enable driver interrupts */
	EFHW_NOTICE("%s: enable master interrupt", __func__);
	falcon_nic_interrupt_hw_enable(nic);

	/* An interrupting eventq must start of day ack its read pointer */
	q = &nic->interrupting_evq;
	rdptr = EFHW_EVENT_OFFSET(q, q, 1) / sizeof(efhw_event_t);
	falcon_nic_evq_ack(nic, FALCON_EVQ_CHAR, rdptr, false);
	EFHW_NOTICE("%s: ACK evq[%d]:%x", __func__,
		    FALCON_EVQ_CHAR, rdptr);
}

static void falcon_nic_interrupt_disable(struct efhw_nic *nic)
{
	/* NB. No need to check for NIC_FLAG_NO_INTERRUPT, as
	 ** falcon_nic_interrupt_hw_disable() will do it. */
	falcon_nic_interrupt_hw_disable(nic);
}

static void
falcon_nic_set_interrupt_moderation(struct efhw_nic *nic, int evq,
				    uint32_t val)
{
	if (evq < 0)
		evq = FALCON_EVQ_CHAR;

	falcon_ab_timer_tbl_set(nic, evq, TIMER_MODE_INT_HLDOFF, val / 5);
}

static inline void legacy_irq_ack(struct efhw_nic *nic)
{
	EFHW_ASSERT(!(nic->flags & NIC_FLAG_NO_INTERRUPT));

	if (!(nic->flags & NIC_FLAG_MSI)) {
		writel(1, EFHW_KVA(nic) + INT_ACK_REG_CHAR_A1_OFST);
		mmiowb();
		/* ?? FIXME: We should be doing a read here to ensure IRQ is
		 * thoroughly acked before we return from ISR. */
	}
}

static int falcon_nic_interrupt(struct efhw_nic *nic)
{
	uint32_t *syserr_ptr =
	    (uint32_t *) efhw_iopage_ptr(&nic->irq_iobuff);
	int handled = 0;
	int done_ack = 0;

	EFHW_ASSERT(!(nic->flags & NIC_FLAG_NO_INTERRUPT));
	EFHW_ASSERT(syserr_ptr);

	/* FIFO fill level interrupt - just log it. */
	if (unlikely(*(syserr_ptr + (DW0_OFST / 4)))) {
		EFHW_WARN("%s: *** FIFO *** %x", __func__,
			  *(syserr_ptr + (DW0_OFST / 4)));
		*(syserr_ptr + (DW0_OFST / 4)) = 0;
		handled++;
	}

	/* Fatal interrupts. */
	if (unlikely(*(syserr_ptr + (DW2_OFST / 4)))) {
		*(syserr_ptr + (DW2_OFST / 4)) = 0;
		falcon_nic_handle_fatal_int(nic);
		handled++;
	}

	/* Event queue interrupt.  For legacy interrupts we have to check
	 * that the interrupt is for us, because it could be shared. */
	if (*(syserr_ptr + (DW1_OFST / 4))) {
		*(syserr_ptr + (DW1_OFST / 4)) = 0;
		/* ACK must come before callback to handler fn. */
		legacy_irq_ack(nic);
		done_ack = 1;
		handled++;
		if (nic->irq_handler)
			nic->irq_handler(nic, 0);
	}

	if (unlikely(!done_ack)) {
		if (!handled)
			/* Shared interrupt line (hopefully). */
			return 0;
		legacy_irq_ack(nic);
	}

	EFHW_TRACE("%s: handled %d", __func__, handled);
	return 1;
}

/*--------------------------------------------------------------------
 *
 * Event Management - and SW event posting
 *
 *--------------------------------------------------------------------*/

static void
falcon_nic_event_queue_enable(struct efhw_nic *nic, uint evq, uint evq_size,
			      dma_addr_t q_base_addr,	/* not used */
			      uint buf_base_id, int interrupting)
{
	EFHW_ASSERT(nic);

	/* Whether or not queue has an interrupt depends on
	 * instance number and h/w variant, so [interrupting] is
	 * ignored.
	 */
	falcon_ab_timer_tbl_set(nic, evq, 0/*disable*/, 0);

	falcon_nic_evq_ptr_tbl(nic, evq, 1, buf_base_id, evq_size);
	EFHW_TRACE("%s: enable evq %u size %u", __func__, evq, evq_size);
}

static void
falcon_nic_event_queue_disable(struct efhw_nic *nic, uint evq, int timer_only)
{
	EFHW_ASSERT(nic);

	falcon_ab_timer_tbl_set(nic, evq, 0 /* disable */ , 0);

	if (!timer_only)
		falcon_nic_evq_ptr_tbl(nic, evq, 0, 0, 0);
	EFHW_TRACE("%s: disenable evq %u", __func__, evq);
}

static void
falcon_nic_wakeup_request(struct efhw_nic *nic, dma_addr_t q_base_addr,
			  int next_i, int evq)
{
	EFHW_ASSERT(evq > FALCON_EVQ_CHAR);
	falcon_nic_evq_ack(nic, evq, next_i, true);
	EFHW_TRACE("%s: evq %d next_i %d", __func__, evq, next_i);
}

static void falcon_nic_sw_event(struct efhw_nic *nic, int data, int evq)
{
	uint64_t ev_data = data;

	ev_data &= ~FALCON_EVENT_CODE_MASK;
	ev_data |= FALCON_EVENT_CODE_SW;

	falcon_drv_ev(nic, ev_data, evq);
	EFHW_NOTICE("%s: evq[%d]->%x", __func__, evq, data);
}


/*--------------------------------------------------------------------
 *
 * Buffer table - helpers
 *
 *--------------------------------------------------------------------*/

#define FALCON_LAZY_COMMIT_HWM (FALCON_BUFFER_UPD_MAX - 16)

/* Note re.:
 *  falcon_nic_buffer_table_lazy_commit(struct efhw_nic *nic)
 *  falcon_nic_buffer_table_update_poll(struct efhw_nic *nic)
 *  falcon_nic_buffer_table_confirm(struct efhw_nic *nic)
 * -- these are no-ops in the user-level driver because it would need to
 * coordinate with the real driver on the number of outstanding commits.
 *
 * An exception is made for eftest apps, which manage the hardware without
 * using the char driver.
 */

static inline void falcon_nic_buffer_table_lazy_commit(struct efhw_nic *nic)
{
	/* Do nothing if operating in synchronous mode. */
	if (!nic->irq_handler)
		return;
}

static inline void falcon_nic_buffer_table_update_poll(struct efhw_nic *nic)
{
	FALCON_LOCK_DECL;
	int count = 0, rc = 0;

	/* We can be called here early days */
	if (!nic->irq_handler)
		return;

	/* If we need to gather buffer update events then poll the
	   non-interrupting event queue */

	/* For each _buffer_table_commit there will be an update done
	   event. We don't keep track of how many buffers each commit has
	   committed, just make sure that all the expected events have been
	   gathered */
	FALCON_LOCK_LOCK(nic);

	EFHW_TRACE("%s: %d", __func__, nic->buf_commit_outstanding);

	while (nic->buf_commit_outstanding > 0) {
		/* we're not expecting to handle any events that require
		 * upcalls into the core driver */
		struct efhw_ev_handler handler;
		memset(&handler, 0, sizeof(handler));
		nic->non_interrupting_evq.ev_handlers = &handler;
		rc = efhw_keventq_poll(nic, &nic->non_interrupting_evq);
		nic->non_interrupting_evq.ev_handlers = NULL;

		if (rc < 0) {
			EFHW_ERR("%s: poll ERROR (%d:%d) ***** ",
				 __func__, rc,
				 nic->buf_commit_outstanding);
			goto out;
		}

		FALCON_LOCK_UNLOCK(nic);

		if (count++)
			udelay(1);

		if (count > 1000) {
			EFHW_WARN("%s: poll Timeout ***** (%d)", __func__,
				  nic->buf_commit_outstanding);
			nic->buf_commit_outstanding = 0;
			return;
		}
		FALCON_LOCK_LOCK(nic);
	}

out:
	FALCON_LOCK_UNLOCK(nic);
	return;
}

void falcon_nic_buffer_table_confirm(struct efhw_nic *nic)
{
	/* confirm buffer table updates - should be used for items where
	   loss of data would be unacceptable. E.g for the buffers that back
	   an event or DMA queue */
	FALCON_LOCK_DECL;

	/* Do nothing if operating in synchronous mode. */
	if (!nic->irq_handler)
		return;

	FALCON_LOCK_LOCK(nic);

	_falcon_nic_buffer_table_commit(nic);

	FALCON_LOCK_UNLOCK(nic);

	falcon_nic_buffer_table_update_poll(nic);
}

/*--------------------------------------------------------------------
 *
 * Buffer table - API
 *
 *--------------------------------------------------------------------*/

static void
falcon_nic_buffer_table_clear(struct efhw_nic *nic, int buffer_id, int num)
{
	FALCON_LOCK_DECL;
	FALCON_LOCK_LOCK(nic);
	_falcon_nic_buffer_table_clear(nic, buffer_id, num);
	FALCON_LOCK_UNLOCK(nic);
}

static void
falcon_nic_buffer_table_set(struct efhw_nic *nic, dma_addr_t dma_addr,
			    uint bufsz, uint region,
			    int own_id, int buffer_id)
{
	FALCON_LOCK_DECL;

	EFHW_ASSERT(region < FALCON_REGION_NUM);

	EFHW_ASSERT((bufsz == EFHW_4K) ||
		    (bufsz == EFHW_8K && FALCON_BUFFER_TABLE_FULL_MODE));

	falcon_nic_buffer_table_update_poll(nic);

	FALCON_LOCK_LOCK(nic);

	_falcon_nic_buffer_table_set(nic, dma_addr, bufsz, region, own_id,
				     buffer_id);

	falcon_nic_buffer_table_lazy_commit(nic);

	FALCON_LOCK_UNLOCK(nic);
}

void
falcon_nic_buffer_table_set_n(struct efhw_nic *nic, int buffer_id,
			      dma_addr_t dma_addr, uint bufsz, uint region,
			      int n_pages, int own_id)
{
	/* used to set up a contiguous range of buffers */
	FALCON_LOCK_DECL;

	EFHW_ASSERT(region < FALCON_REGION_NUM);

	EFHW_ASSERT((bufsz == EFHW_4K) ||
		    (bufsz == EFHW_8K && FALCON_BUFFER_TABLE_FULL_MODE));

	while (n_pages--) {

		falcon_nic_buffer_table_update_poll(nic);

		FALCON_LOCK_LOCK(nic);

		_falcon_nic_buffer_table_set(nic, dma_addr, bufsz, region,
					     own_id, buffer_id++);

		falcon_nic_buffer_table_lazy_commit(nic);

		FALCON_LOCK_UNLOCK(nic);

		dma_addr += bufsz;
	}
}

/*--------------------------------------------------------------------
 *
 * DMA Queues - mid level API
 *
 *--------------------------------------------------------------------*/

#if BUG5302_WORKAROUND

/* Tx queues can get stuck if the software write pointer is set to an index
 * beyond the configured size of the queue, such that they will not flush.
 * This code can be run before attempting a flush; it will detect the bogus
 * value and reset it.  This fixes most instances of this problem, although
 * sometimes it does not work, or we may not detect it in the first place,
 * if the out-of-range value was replaced by an in-range value earlier.
 * (In those cases we have to apply a bigger hammer later, if we see that
 * the queue is still not flushing.)
 */
static void
falcon_check_for_bogus_tx_dma_wptr(struct efhw_nic *nic, uint dmaq)
{
	FALCON_LOCK_DECL;
	uint64_t val_low64, val_high64;
	uint64_t size, hwptr, swptr, val;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	ulong offset = falcon_dma_tx_q_offset(nic, dmaq);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_read_qq(efhw_kva + offset, &val_low64, &val_high64);
	FALCON_LOCK_UNLOCK(nic);

	size = (val_low64 >> TX_DESCQ_SIZE_LBN)
	    & __FALCON_MASK64(TX_DESCQ_SIZE_WIDTH);
	size = (1 << size) * 512;
	hwptr = (val_high64 >> __DW3(TX_DESCQ_HW_RPTR_LBN))
	    & __FALCON_MASK64(TX_DESCQ_HW_RPTR_WIDTH);
	swptr = (val_low64 >> TX_DESCQ_SW_WPTR_LBN)
	    & __FALCON_MASK64(__LW2(TX_DESCQ_SW_WPTR_LBN));
	val = (val_high64)
	    &
	    __FALCON_MASK64(__DW3
			    (TX_DESCQ_SW_WPTR_LBN + TX_DESCQ_SW_WPTR_WIDTH));
	val = val << __LW2(TX_DESCQ_SW_WPTR_LBN);
	swptr = swptr | val;

	if (swptr >= size) {
		EFHW_WARN("Resetting bad write pointer for TXQ[%d]", dmaq);
		writel((uint32_t) ((hwptr + 0) & (size - 1)),
		       efhw_kva + falcon_tx_dma_page_addr(dmaq) + 12);
		mmiowb();
	}
}

/* Here's that "bigger hammer": we reset all the pointers (hardware read,
 * hardware descriptor cache read, software write) to zero.
 */
void falcon_clobber_tx_dma_ptrs(struct efhw_nic *nic, uint dmaq)
{
	FALCON_LOCK_DECL;
	uint64_t val_low64, val_high64;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	ulong offset = falcon_dma_tx_q_offset(nic, dmaq);

	EFHW_WARN("Recovering stuck TXQ[%d]", dmaq);
	FALCON_LOCK_LOCK(nic);
	falcon_read_qq(efhw_kva + offset, &val_low64, &val_high64);
	val_high64 &= ~(__FALCON_MASK64(TX_DESCQ_HW_RPTR_WIDTH)
			<< __DW3(TX_DESCQ_HW_RPTR_LBN));
	val_high64 &= ~(__FALCON_MASK64(TX_DC_HW_RPTR_WIDTH)
			<< __DW3(TX_DC_HW_RPTR_LBN));
	falcon_write_qq(efhw_kva + offset, val_low64, val_high64);
	mmiowb();
	writel(0, efhw_kva + falcon_tx_dma_page_addr(dmaq) + 12);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
}

#endif

static inline int
__falcon_really_flush_tx_dma_channel(struct efhw_nic *nic, uint dmaq)
{
	FALCON_LOCK_DECL;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	uint val;

	EFHW_BUILD_ASSERT(TX_FLUSH_DESCQ_REG_KER_OFST ==
			  TX_FLUSH_DESCQ_REG_OFST);

	__DWCHCK(TX_FLUSH_DESCQ_CMD_LBN, TX_FLUSH_DESCQ_CMD_WIDTH);
	__DWCHCK(TX_FLUSH_DESCQ_LBN, TX_FLUSH_DESCQ_WIDTH);
	__RANGECHCK(dmaq, TX_FLUSH_DESCQ_WIDTH);

	val = ((1 << TX_FLUSH_DESCQ_CMD_LBN) | (dmaq << TX_FLUSH_DESCQ_LBN));

	EFHW_TRACE("TX DMA flush[%d]", dmaq);

#if BUG5302_WORKAROUND
	falcon_check_for_bogus_tx_dma_wptr(nic, dmaq);
#endif

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + TX_FLUSH_DESCQ_REG_OFST,
			val, FALCON_ATOMIC_TX_FLUSH_DESCQ);

	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
	return 0;
}

static inline int
__falcon_is_tx_dma_channel_flushed(struct efhw_nic *nic, uint dmaq)
{
	FALCON_LOCK_DECL;
	uint64_t val_low64, val_high64;
	uint64_t enable, flush_pending;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	ulong offset = falcon_dma_tx_q_offset(nic, dmaq);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_read_qq(efhw_kva + offset, &val_low64, &val_high64);
	FALCON_LOCK_UNLOCK(nic);

	/* should see one of three values for these 2 bits
	 *   1, queue enabled no flush pending
	 *	- i.e. first flush request
	 *   2, queue enabled, flush pending
	 *	- i.e. request to reflush before flush finished
	 *   3, queue disabled (no flush pending)
	 *	- flush complete
	 */
	__DWCHCK(TX_DESCQ_FLUSH_LBN, TX_DESCQ_FLUSH_WIDTH);
	__DW3CHCK(TX_DESCQ_EN_LBN, TX_DESCQ_EN_WIDTH);
	enable = val_high64 & (1 << __DW3(TX_DESCQ_EN_LBN));
	flush_pending = val_low64 & (1 << TX_DESCQ_FLUSH_LBN);

	if (enable && !flush_pending)
		return 0;

	EFHW_TRACE("%d, %s: %s, %sflush pending", dmaq, __func__,
		   enable ? "enabled" : "disabled",
		   flush_pending ? "" : "NO ");
	/* still in progress */
	if (enable && flush_pending)
		return -EALREADY;

	return -EAGAIN;
}

static int falcon_flush_tx_dma_channel(struct efhw_nic *nic, uint dmaq)
{
	int rc;
	rc = __falcon_is_tx_dma_channel_flushed(nic, dmaq);
	if (rc < 0) {
		EFHW_WARN("%s: failed %d", __func__, rc);
		return rc;
	}
	return __falcon_really_flush_tx_dma_channel(nic, dmaq);
}

static int
__falcon_really_flush_rx_dma_channel(struct efhw_nic *nic, uint dmaq)
{
	FALCON_LOCK_DECL;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	uint val;

	EFHW_BUILD_ASSERT(RX_FLUSH_DESCQ_REG_KER_OFST ==
			  RX_FLUSH_DESCQ_REG_OFST);

	__DWCHCK(RX_FLUSH_DESCQ_CMD_LBN, RX_FLUSH_DESCQ_CMD_WIDTH);
	__DWCHCK(RX_FLUSH_DESCQ_LBN, RX_FLUSH_DESCQ_WIDTH);
	__RANGECHCK(dmaq, RX_FLUSH_DESCQ_WIDTH);

	val = ((1 << RX_FLUSH_DESCQ_CMD_LBN) | (dmaq << RX_FLUSH_DESCQ_LBN));

	EFHW_TRACE("RX DMA flush[%d]", dmaq);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_write_qq(efhw_kva + RX_FLUSH_DESCQ_REG_OFST, val,
			FALCON_ATOMIC_RX_FLUSH_DESCQ);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
	return 0;
}

static inline int
__falcon_is_rx_dma_channel_flushed(struct efhw_nic *nic, uint dmaq)
{
	FALCON_LOCK_DECL;
	uint64_t val;
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	ulong offset = falcon_dma_rx_q_offset(nic, dmaq);

	/* Falcon requires 128 bit atomic access for this register */
	FALCON_LOCK_LOCK(nic);
	falcon_read_q(efhw_kva + offset, &val);
	FALCON_LOCK_UNLOCK(nic);

	__DWCHCK(RX_DESCQ_EN_LBN, RX_DESCQ_EN_WIDTH);

	/* is it enabled? */
	return (val & (1 << RX_DESCQ_EN_LBN))
	    ? 0 : -EAGAIN;
}

static int falcon_flush_rx_dma_channel(struct efhw_nic *nic, uint dmaq)
{
	int rc;
	rc = __falcon_is_rx_dma_channel_flushed(nic, dmaq);
	if (rc < 0) {
		EFHW_ERR("%s: failed %d", __func__, rc);
		return rc;
	}
	return __falcon_really_flush_rx_dma_channel(nic, dmaq);
}

/*--------------------------------------------------------------------
 *
 * Falcon specific event callbacks
 *
 *--------------------------------------------------------------------*/

int
falcon_handle_char_event(struct efhw_nic *nic, struct efhw_ev_handler *h,
			 efhw_event_t *ev)
{
	EFHW_TRACE("DRIVER EVENT: "FALCON_EVENT_FMT,
		   FALCON_EVENT_PRI_ARG(*ev));

	switch (FALCON_EVENT_DRIVER_SUBCODE(ev)) {

	case TX_DESCQ_FLS_DONE_EV_DECODE:
		EFHW_TRACE("TX[%d] flushed",
			   (int)FALCON_EVENT_TX_FLUSH_Q_ID(ev));
		efhw_handle_txdmaq_flushed(nic, h, ev);
		break;

	case RX_DESCQ_FLS_DONE_EV_DECODE:
		EFHW_TRACE("RX[%d] flushed",
			   (int)FALCON_EVENT_TX_FLUSH_Q_ID(ev));
		efhw_handle_rxdmaq_flushed(nic, h, ev);
		break;

	case SRM_UPD_DONE_EV_DECODE:
		nic->buf_commit_outstanding =
		    max(0, nic->buf_commit_outstanding - 1);
		EFHW_TRACE("COMMIT DONE %d", nic->buf_commit_outstanding);
		break;

	case EVQ_INIT_DONE_EV_DECODE:
		EFHW_TRACE("%sEVQ INIT", "");
		break;

	case WAKE_UP_EV_DECODE:
		EFHW_TRACE("%sWAKE UP", "");
		efhw_handle_wakeup_event(nic, h, ev);
		break;

	case TIMER_EV_DECODE:
		EFHW_TRACE("%sTIMER", "");
		efhw_handle_timeout_event(nic, h, ev);
		break;

	case RX_DESCQ_FLSFF_OVFL_EV_DECODE:
		/* This shouldn't happen. */
		EFHW_ERR("%s: RX flush fifo overflowed", __func__);
		return -EINVAL;

	default:
		EFHW_TRACE("UNKOWN DRIVER EVENT: " FALCON_EVENT_FMT,
			   FALCON_EVENT_PRI_ARG(*ev));
		break;
	}
	return 0;
}


/*--------------------------------------------------------------------
 *
 * Filter search depth control
 *
 *--------------------------------------------------------------------*/


#define Q0_READ(q0, name) \
	((unsigned)(((q0) >> name##_LBN) & (__FALCON_MASK64(name##_WIDTH))))
#define Q0_MASK(name) \
	((__FALCON_MASK64(name##_WIDTH)) << name##_LBN)
#define Q0_VALUE(name, value) \
	(((uint64_t)(value)) << name##_LBN)

#define Q1_READ(q1, name) \
	((unsigned)(((q1) >> (name##_LBN - 64)) & \
		    (__FALCON_MASK64(name##_WIDTH))))
#define Q1_MASK(name) \
	((__FALCON_MASK64(name##_WIDTH)) << (name##_LBN - 64))
#define Q1_VALUE(name, value) \
	(((uint64_t)(value)) << (name##_LBN - 64))


void
falcon_nic_get_rx_filter_search_limits(struct efhw_nic *nic,
				       struct efhw_filter_search_limits *lim,
				       int use_raw_values)
{
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	FALCON_LOCK_DECL;
	uint64_t q0, q1;
	unsigned ff = (use_raw_values ? 0 : RX_FILTER_CTL_SRCH_FUDGE_FULL);
	unsigned wf = (use_raw_values ? 0 : RX_FILTER_CTL_SRCH_FUDGE_WILD);

	FALCON_LOCK_LOCK(nic);
	falcon_read_qq(efhw_kva + RX_FILTER_CTL_REG_OFST, &q0, &q1);
	FALCON_LOCK_UNLOCK(nic);

	lim->tcp_full = Q0_READ(q0, TCP_FULL_SRCH_LIMIT) - ff;
	lim->tcp_wild = Q0_READ(q0, TCP_WILD_SRCH_LIMIT) - wf;
	lim->udp_full = Q0_READ(q0, UDP_FULL_SRCH_LIMIT) - ff;
	lim->udp_wild = Q0_READ(q0, UDP_WILD_SRCH_LIMIT) - wf;
}
EXPORT_SYMBOL(falcon_nic_get_rx_filter_search_limits);


void
falcon_nic_set_rx_filter_search_limits(struct efhw_nic *nic,
				       struct efhw_filter_search_limits *lim,
				       int use_raw_values)
{
	volatile char __iomem *efhw_kva = EFHW_KVA(nic);
	FALCON_LOCK_DECL;
	uint64_t q0, q1;
	unsigned ff = (use_raw_values ? 0 : RX_FILTER_CTL_SRCH_FUDGE_FULL);
	unsigned wf = (use_raw_values ? 0 : RX_FILTER_CTL_SRCH_FUDGE_WILD);

	FALCON_LOCK_LOCK(nic);
	falcon_read_qq(efhw_kva + RX_FILTER_CTL_REG_OFST, &q0, &q1);

	q0 &= ~Q0_MASK(TCP_FULL_SRCH_LIMIT);
	q0 &= ~Q0_MASK(TCP_WILD_SRCH_LIMIT);
	q0 &= ~Q0_MASK(UDP_FULL_SRCH_LIMIT);
	q0 &= ~Q0_MASK(UDP_WILD_SRCH_LIMIT);
	q0 |= Q0_VALUE(TCP_FULL_SRCH_LIMIT, lim->tcp_full + ff);
	q0 |= Q0_VALUE(TCP_WILD_SRCH_LIMIT, lim->tcp_wild + wf);
	q0 |= Q0_VALUE(UDP_FULL_SRCH_LIMIT, lim->udp_full + ff);
	q0 |= Q0_VALUE(UDP_WILD_SRCH_LIMIT, lim->udp_wild + wf);
	nic->tcp_full_srch.max = lim->tcp_full + ff
					- RX_FILTER_CTL_SRCH_FUDGE_FULL;
	nic->tcp_wild_srch.max = lim->tcp_wild + wf
					- RX_FILTER_CTL_SRCH_FUDGE_WILD;
	nic->udp_full_srch.max = lim->udp_full + ff
					- RX_FILTER_CTL_SRCH_FUDGE_FULL;
	nic->udp_wild_srch.max = lim->udp_wild + wf
					- RX_FILTER_CTL_SRCH_FUDGE_WILD;

	falcon_write_qq(efhw_kva + RX_FILTER_CTL_REG_OFST, q0, q1);
	mmiowb();
	FALCON_LOCK_UNLOCK(nic);
}
EXPORT_SYMBOL(falcon_nic_set_rx_filter_search_limits);


#undef READ_Q0
#undef Q0_MASK
#undef Q0_VALUE
#undef READ_Q1
#undef Q1_MASK
#undef Q1_VALUE


/*--------------------------------------------------------------------
 *
 * New unified filter API
 *
 *--------------------------------------------------------------------*/


#if FALCON_FULL_FILTER_CACHE
static inline struct efhw_filter_spec *
filter_spec_cache_entry(struct efhw_nic *nic, int filter_idx)
{
	EFHW_ASSERT(nic->filter_spec_cache);
	return &nic->filter_spec_cache[filter_idx];
}
#endif


static int filter_is_active(struct efhw_nic *nic, int filter_idx)
{
	return nic->filter_in_use[filter_idx];
}


static void set_filter_cache_entry(struct efhw_nic *nic,
				   struct efhw_filter_spec *spec,
				   int filter_idx)
{
	nic->filter_in_use[filter_idx] = 1;
#if FALCON_FULL_FILTER_CACHE
	memcpy(filter_spec_cache_entry(nic, filter_idx), spec,
	       sizeof(struct efhw_filter_spec));
#endif
}


static void clear_filter_cache_entry(struct efhw_nic *nic,
				     int filter_idx)
{
	nic->filter_in_use[filter_idx] = 0;
#if FALCON_FULL_FILTER_CACHE
	memset(filter_spec_cache_entry(nic, filter_idx), 0,
	       sizeof(struct efhw_filter_spec));
#endif
}


#if FALCON_FULL_FILTER_CACHE
static int filter_is_duplicate(struct efhw_nic *nic,
			       struct efhw_filter_spec *spec, int filter_idx)
{
	struct efhw_filter_spec *cmp;

	cmp = filter_spec_cache_entry(nic, filter_idx);

	EFHW_ASSERT(filter_is_active(nic, filter_idx));

	return (spec->saddr_le32 == cmp->saddr_le32) &&
	       (spec->daddr_le32 == cmp->daddr_le32) &&
	       (spec->sport_le16 == cmp->sport_le16) &&
	       (spec->dport_le16 == cmp->dport_le16) &&
	       (spec->tcp == cmp->tcp) &&
	       (spec->full == cmp->full);
}
#endif


static void common_build_ip_filter(struct efhw_nic *nic, int tcp, int full,
				   int rss, int scatter, uint dmaq_id,
				   unsigned saddr_le32, unsigned sport_le16,
				   unsigned daddr_le32, unsigned dport_le16,
				   uint64_t *q0, uint64_t *q1)
{
	uint64_t v1, v2, v3, v4;
	unsigned tmp_port_le16;

	if (!full) {
		saddr_le32 = 0;
		sport_le16 = 0;
		if (!tcp) {
			tmp_port_le16 = sport_le16;
			sport_le16 = dport_le16;
			dport_le16 = tmp_port_le16;
		}
	}

	v4 = (((!tcp) << __DW4(TCP_UDP_0_LBN)) |
	      (dmaq_id << __DW4(RXQ_ID_0_LBN)));

	switch (nic->devtype.variant) {
	case 'A':
		EFHW_ASSERT(!rss);
		break;
	case 'B':
		v4 |= scatter << __DW4(SCATTER_EN_0_B0_LBN);
		v4 |= rss << __DW4(RSS_EN_0_B0_LBN);
		break;
	default:
		EFHW_ASSERT(0);
		break;
	}

	v3 = daddr_le32;
	v2 = ((dport_le16 << __DW2(DEST_PORT_TCP_0_LBN)) |
	      (__HIGH(saddr_le32, SRC_IP_0_LBN, SRC_IP_0_WIDTH)));
	v1 = ((__LOW(saddr_le32, SRC_IP_0_LBN, SRC_IP_0_WIDTH)) |
	      (sport_le16 << SRC_TCP_DEST_UDP_0_LBN));

	*q0 = (v2 << 32) | v1;
	*q1 = (v4 << 32) | v3;
}


static void build_filter(struct efhw_nic *nic, struct efhw_filter_spec *spec,
			 unsigned *key, unsigned *tbl_size,
			 struct efhw_filter_depth **depth,
			 uint64_t *q0, uint64_t *q1)
{
	*key = falcon_hash_get_ip_key(spec->saddr_le32,
				      spec->sport_le16,
				      spec->daddr_le32,
				      spec->dport_le16,
				      spec->tcp,
				      spec->full);
	*tbl_size = nic->ip_filter_tbl_size;
	if (spec->tcp && spec->full)
		*depth = &nic->tcp_full_srch;
	else if (spec->tcp && !spec->full)
		*depth = &nic->tcp_wild_srch;
	else if (!spec->tcp && spec->full)
		*depth = &nic->udp_full_srch;
	else
		*depth = &nic->udp_wild_srch;
	common_build_ip_filter(nic, spec->tcp, spec->full,
			       spec->rss, spec->scatter,
			       spec->dmaq_id,
			       spec->saddr_le32,
			       spec->sport_le16,
			       spec->daddr_le32,
			       spec->dport_le16,
			       q0, q1);
}


#if FALCON_VERIFY_FILTERS
static void verify_filters(struct efhw_nic *nic)
{
	unsigned table_offset, table_stride;
	unsigned i, dummy_key, dummy_tbl_size;
	struct efhw_filter_depth *dummy_depth;
	unsigned filter_tbl_size;
	struct efhw_filter_spec *spec;
	uint64_t q0_expect, q1_expect, q0_got, q1_got;

	filter_tbl_size = nic->ip_filter_tbl_size;
	table_offset = RX_FILTER_TBL0_OFST;
	table_stride = 2 * FALCON_REGISTER128;

	for (i = 0; i < filter_tbl_size; i++) {
		if (!filter_is_active(nic, type, i))
			continue;

		spec = filter_spec_cache_entry(nic, type, i);

		build_filter(nic, spec, &dummy_key, &dummy_tbl_size,
			     &dummy_depth, &q0_expect, &q1_expect);

		falcon_read_qq(EFHW_KVA(nic) + table_offset + i * table_stride,
			       &q0_got, &q1_got);

		if ((q0_got != q0_expect) || (q1_got != q1_expect)) {
			falcon_write_qq(EFHW_KVA(nic) + 0x300,
					q0_got, q1_got);
			EFHW_ERR("ERROR: RX-filter[%d][%d] was "
				 "%"PRIx64":%" PRIx64" expected "
				 "%"PRIx64":%"PRIx64,
				 nic->index, i, q0_got, q1_got,
				 q0_expect, q1_expect);
		}
	}
}
#endif


static void write_filter_table_entry(struct efhw_nic *nic,
				     unsigned filter_idx,
				     uint64_t q0, uint64_t q1)
{
	unsigned table_offset, table_stride, offset;

	EFHW_ASSERT(filter_idx < nic->ip_filter_tbl_size);
	table_offset = RX_FILTER_TBL0_OFST;
	table_stride = 2 * FALCON_REGISTER128;

	offset = table_offset + filter_idx * table_stride;
	falcon_write_qq(EFHW_KVA(nic) + offset, q0, q1);
	mmiowb();

#if FALCON_VERIFY_FILTERS
	{
		uint64_t q0read, q1read;

		/* Read a different entry first - ensure BIU flushed shadow */
		falcon_read_qq(EFHW_KVA(nic) + offset + 0x10, &q0read, &q1read);
		falcon_read_qq(EFHW_KVA(nic) + offset, &q0read, &q1read);
		EFHW_ASSERT(q0read == q0);
		EFHW_ASSERT(q1read == q1);

		verify_filters(nic, type);
	}
#endif
}


static int falcon_nic_filter_set(struct efhw_nic *nic,
				 struct efhw_filter_spec *spec,
				 int *filter_idx_out)
{
	FALCON_LOCK_DECL;
	unsigned key = 0, tbl_size = 0, hash1, hash2, k;
	struct efhw_filter_depth *depth = NULL;
	int filter_idx = -1;
	int rc = 0;
	uint64_t q0, q1;

	build_filter(nic, spec, &key, &tbl_size, &depth, &q0, &q1);

	if (tbl_size == 0)
		return -EINVAL;

	EFHW_TRACE("%s: depth->max=%d", __func__, depth->max);

	hash1 = falcon_hash_function1(key, tbl_size);
	hash2 = falcon_hash_function2(key, tbl_size);

	FALCON_LOCK_LOCK(nic);

	for (k = 0; k < depth->max; k++) {
		filter_idx = falcon_hash_iterator(hash1, hash2, k, tbl_size);
		if (!filter_is_active(nic, filter_idx))
			break;
#if FALCON_FULL_FILTER_CACHE
		if (filter_is_duplicate(nic, spec, filter_idx)) {
			EFHW_WARN("%s: ERROR: duplicate filter (disabling "
				  "interrupts)", __func__);
			falcon_nic_interrupt_hw_disable(nic);
			rc = -EINVAL;
			goto fail1;
		}
#endif
	}
	if (k == depth->max) {
		rc = -EADDRINUSE;
		filter_idx = -1;
		goto fail1;
	} else if (depth->needed < (k + 1)) {
		depth->needed = k + 1;
	}

	EFHW_ASSERT(filter_idx < (int)tbl_size);

	set_filter_cache_entry(nic, spec, filter_idx);
	write_filter_table_entry(nic, filter_idx, q0, q1);

	++nic->ip_filter_tbl_used;

	*filter_idx_out = filter_idx;

	EFHW_TRACE("%s: filter index %d rxq %u set in %u",
		   __func__, filter_idx, spec->dmaq_id, k);

fail1:
	FALCON_LOCK_UNLOCK(nic);
	return rc;
}


static void falcon_nic_filter_clear(struct efhw_nic *nic,
				    int filter_idx)
{
	FALCON_LOCK_DECL;

	if (filter_idx < 0)
		return;

	FALCON_LOCK_LOCK(nic);
	if (filter_is_active(nic, filter_idx)) {
		if (--nic->ip_filter_tbl_used == 0) {
			nic->tcp_full_srch.needed = 0;
			nic->tcp_wild_srch.needed = 0;
			nic->udp_full_srch.needed = 0;
			nic->udp_wild_srch.needed = 0;
		}
	}
	clear_filter_cache_entry(nic, filter_idx);
	write_filter_table_entry(nic, filter_idx, 0, 0);
	FALCON_LOCK_UNLOCK(nic);
}


int
falcon_nic_filter_ctor(struct efhw_nic *nic)
{
	nic->ip_filter_tbl_size = 8 * 1024;
	nic->ip_filter_tbl_used = 0;

	nic->tcp_full_srch.needed = 0;
	nic->tcp_full_srch.max = RX_FILTER_CTL_SRCH_LIMIT_TCP_FULL
				   - RX_FILTER_CTL_SRCH_FUDGE_FULL;
	nic->tcp_wild_srch.needed = 0;
	nic->tcp_wild_srch.max = RX_FILTER_CTL_SRCH_LIMIT_TCP_WILD
				   - RX_FILTER_CTL_SRCH_FUDGE_WILD;
	nic->udp_full_srch.needed = 0;
	nic->udp_full_srch.max = RX_FILTER_CTL_SRCH_LIMIT_UDP_FULL
				   - RX_FILTER_CTL_SRCH_FUDGE_FULL;
	nic->udp_wild_srch.needed = 0;
	nic->udp_wild_srch.max = RX_FILTER_CTL_SRCH_LIMIT_UDP_WILD
				   - RX_FILTER_CTL_SRCH_FUDGE_WILD;

	nic->filter_in_use = vmalloc(FALCON_FILTER_TBL_NUM);
	if (nic->filter_in_use == NULL)
		return -ENOMEM;
	memset(nic->filter_in_use, 0, FALCON_FILTER_TBL_NUM);
#if FALCON_FULL_FILTER_CACHE
	nic->filter_spec_cache = vmalloc(FALCON_FILTER_TBL_NUM
					 * sizeof(struct efhw_filter_spec));
	if (nic->filter_spec_cache == NULL)
		return -ENOMEM;
	memset(nic->filter_spec_cache, 0, FALCON_FILTER_TBL_NUM
					  * sizeof(struct efhw_filter_spec));
#endif

	return 0;
}


void
falcon_nic_filter_dtor(struct efhw_nic *nic)
{
#if FALCON_FULL_FILTER_CACHE
	if (nic->filter_spec_cache)
		vfree(nic->filter_spec_cache);
#endif
	if (nic->filter_in_use)
		vfree(nic->filter_in_use);
}


/*--------------------------------------------------------------------
 *
 * Compatibility with old filter API
 *
 *--------------------------------------------------------------------*/

void
falcon_nic_rx_filter_ctl_get(struct efhw_nic *nic, uint32_t *tcp_full,
			     uint32_t *tcp_wild,
			     uint32_t *udp_full, uint32_t *udp_wild)
{
	struct efhw_filter_search_limits lim;

	falcon_nic_get_rx_filter_search_limits(nic, &lim, 0);
	*tcp_full = (uint32_t)lim.tcp_full;
	*tcp_wild = (uint32_t)lim.tcp_wild;
	*udp_full = (uint32_t)lim.udp_full;
	*udp_wild = (uint32_t)lim.udp_wild;
}
EXPORT_SYMBOL(falcon_nic_rx_filter_ctl_get);


void
falcon_nic_rx_filter_ctl_set(struct efhw_nic *nic, uint32_t tcp_full,
			     uint32_t tcp_wild,
			     uint32_t udp_full, uint32_t udp_wild)
{
	struct efhw_filter_search_limits lim;

	lim.tcp_full = (unsigned)tcp_full;
	lim.tcp_wild = (unsigned)tcp_wild;
	lim.udp_full = (unsigned)udp_full;
	lim.udp_wild = (unsigned)udp_wild;
	falcon_nic_set_rx_filter_search_limits(nic, &lim, 0);
}
EXPORT_SYMBOL(falcon_nic_rx_filter_ctl_set);


static int
falcon_nic_ipfilter_set(struct efhw_nic *nic, int type, int *_filter_idx,
			int dmaq,
			unsigned saddr_be32, unsigned sport_be16,
			unsigned daddr_be32, unsigned dport_be16)
{
	struct efhw_filter_spec spec;

	spec.dmaq_id = dmaq;
	spec.saddr_le32 = ntohl(saddr_be32);
	spec.daddr_le32 = ntohl(daddr_be32);
	spec.sport_le16 = ntohs((unsigned short) sport_be16);
	spec.dport_le16 = ntohs((unsigned short) dport_be16);
	spec.tcp = ((type & EFHW_IP_FILTER_TYPE_TCP_MASK) != 0);
	spec.full = ((type & EFHW_IP_FILTER_TYPE_FULL_MASK) != 0);
	spec.rss = ((type & EFHW_IP_FILTER_TYPE_RSS_B0_MASK) != 0);
	spec.scatter = ((type & EFHW_IP_FILTER_TYPE_NOSCAT_B0_MASK) == 0);
	return falcon_nic_filter_set(nic, &spec, _filter_idx);
}

static void falcon_nic_ipfilter_clear(struct efhw_nic *nic, int filter_idx)
{
	falcon_nic_filter_clear(nic, filter_idx);
}


/*--------------------------------------------------------------------
 *
 * Abstraction Layer Hooks
 *
 *--------------------------------------------------------------------*/

struct efhw_func_ops falcon_char_functional_units = {
	falcon_nic_close_hardware,
	falcon_nic_init_hardware,
	falcon_nic_interrupt,
	falcon_nic_interrupt_enable,
	falcon_nic_interrupt_disable,
	falcon_nic_set_interrupt_moderation,
	falcon_nic_event_queue_enable,
	falcon_nic_event_queue_disable,
	falcon_nic_wakeup_request,
	falcon_nic_sw_event,
	falcon_nic_ipfilter_set,
	falcon_nic_ipfilter_clear,
	falcon_dmaq_tx_q_init,
	falcon_dmaq_rx_q_init,
	falcon_dmaq_tx_q_disable,
	falcon_dmaq_rx_q_disable,
	falcon_flush_tx_dma_channel,
	falcon_flush_rx_dma_channel,
	falcon_nic_buffer_table_set,
	falcon_nic_buffer_table_set_n,
	falcon_nic_buffer_table_clear,
	falcon_nic_buffer_table_commit,
	falcon_nic_filter_set,
	falcon_nic_filter_clear,
};


