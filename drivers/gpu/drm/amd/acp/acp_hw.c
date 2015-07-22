/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * NOTE:
 * Certain pieces were reused from Synopsis I2S IP related code,
 * which otherwise can also be found at:
 * sound/soc/dwc/designware_i2s.c
 *
 * Copyright notice as appears in the above file:
 *
 * Copyright (C) 2010 ST Microelectronics
 * Rajeev Kumar <rajeevkumar.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>

#include "acp_gfx_if.h"
#include "acp_hw.h"

#include "acp_2_2_d.h"
#include "acp_2_2_sh_mask.h"

#define VISLANDS30_IV_SRCID_ACP 0x000000a2

/* Configure a given dma channel parameters - enable/disble,
 * number of descriptors, priority */
static void config_acp_dma_channel(struct amd_acp_device *acp_dev, u8 ch_num,
				   u16 dscr_strt_idx, u16 num_dscrs,
				   enum acp_dma_priority_level priority_level)
{
	u32 dma_ctrl;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	/* disable the channel run field */
	dma_ctrl = cgs_read_register(acp_prv->cgs_device,
				     mmACP_DMA_CNTL_0 + ch_num);
	dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChRun_MASK;
	cgs_write_register(acp_prv->cgs_device, (mmACP_DMA_CNTL_0 + ch_num),
			   dma_ctrl);

	/* program a DMA channel with first descriptor to be processed. */
	cgs_write_register(acp_prv->cgs_device,
			   (mmACP_DMA_DSCR_STRT_IDX_0 + ch_num),
			   (ACP_DMA_DSCR_STRT_IDX_0__DMAChDscrStrtIdx_MASK &
			    dscr_strt_idx));

	/* program a DMA channel with the number of descriptors to be
	 * processed in the transfer */
	cgs_write_register(acp_prv->cgs_device,
			   (mmACP_DMA_DSCR_CNT_0 + ch_num),
			   (ACP_DMA_DSCR_CNT_0__DMAChDscrCnt_MASK & num_dscrs));

	/* set DMA channel priority */
	cgs_write_register(acp_prv->cgs_device,	(mmACP_DMA_PRIO_0 + ch_num),
			   priority_level);
}

/* Initialize the dma descriptors location in SRAM and page size */
static void acp_dma_descr_init(struct amd_acp_private *acp_prv)
{
	u32 sram_pte_offset = 0;

	/* SRAM starts at 0x04000000. From that offset one page (4KB) left for
	 * filling DMA descriptors.sram_pte_offset = 0x04001000 , used for
	 * filling system RAM's physical pages.
	 * This becomes the ALSA's Ring buffer start address
	 */
	sram_pte_offset = ACP_DAGB_GRP_SRAM_BASE_ADDRESS;

	/* snoopable */
	sram_pte_offset |= ACP_DAGB_BASE_ADDR_GRP_1__AXI2DAGBSnoopSel_MASK;
	/* Memmory is system mmemory */
	sram_pte_offset |= ACP_DAGB_BASE_ADDR_GRP_1__AXI2DAGBTargetMemSel_MASK;
	/* Page Enabled */
	sram_pte_offset |= ACP_DAGB_BASE_ADDR_GRP_1__AXI2DAGBGrpEnable_MASK;

	cgs_write_register(acp_prv->cgs_device,	mmACP_DAGB_BASE_ADDR_GRP_1,
			   sram_pte_offset);
	cgs_write_register(acp_prv->cgs_device,	mmACP_DAGB_PAGE_SIZE_GRP_1,
			   PAGE_SIZE_4K_ENABLE);
}

/* Initialize a dma descriptor in SRAM based on descritor information passed */
static void config_dma_descriptor_in_sram(struct amd_acp_device *acp_dev,
					  u16 descr_idx,
					  acp_dma_dscr_transfer_t *descr_info)
{
	u32 sram_offset;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	sram_offset = (descr_idx * sizeof(acp_dma_dscr_transfer_t));

	/* program the source base address. */
	cgs_write_register(acp_prv->cgs_device,	mmACP_SRBM_Targ_Idx_Addr,
			   sram_offset);
	cgs_write_register(acp_prv->cgs_device,	mmACP_SRBM_Targ_Idx_Data,
			   descr_info->src);
	/* program the destination base address. */
	cgs_write_register(acp_prv->cgs_device,	mmACP_SRBM_Targ_Idx_Addr,
			   (sram_offset + 4));
	cgs_write_register(acp_prv->cgs_device,	mmACP_SRBM_Targ_Idx_Data,
						descr_info->dest);

	/* program the number of bytes to be transferred for this descriptor. */
	cgs_write_register(acp_prv->cgs_device,	mmACP_SRBM_Targ_Idx_Addr,
			   (sram_offset + 8));
	cgs_write_register(acp_prv->cgs_device,	mmACP_SRBM_Targ_Idx_Data,
			   descr_info->size_xfer_dir.val);
}

/* Initialize the DMA descriptor information for transfer between
 * system memory <-> ACP SRAM
 */
static void set_acp_sysmem_dma_descriptors(struct amd_acp_device *acp_dev,
					   u32 size, int direction,
					   u32 pte_offset)
{
	u16 num_descr;
	u16 dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH12;
	acp_dma_dscr_transfer_t dmadscr[2];

	num_descr = 2;

	dmadscr[0].size_xfer_dir.val = (u32) 0x0;
	if (direction == STREAM_PLAYBACK) {
		dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH12;
		dmadscr[0].dest = ACP_SHARED_RAM_BANK_1_ADDRESS + (size / 2);
		dmadscr[0].src = ACP_INTERNAL_APERTURE_WINDOW_0_ADDRESS +
			(pte_offset * PAGE_SIZE_4K);
		dmadscr[0].size_xfer_dir.s.trans_direction =
		    ACP_DMA_ATTRIBUTES_DAGB_ONION_TO_SHAREDMEM;
		dmadscr[0].size_xfer_dir.s.size = (size / 2);
		dmadscr[0].size_xfer_dir.s.ioc = (u32) 0x0;
	} else {
		dma_dscr_idx = CAPTURE_START_DMA_DESCR_CH14;
		dmadscr[0].src = ACP_SHARED_RAM_BANK_5_ADDRESS;
		dmadscr[0].dest = ACP_INTERNAL_APERTURE_WINDOW_0_ADDRESS +
			(pte_offset * PAGE_SIZE_4K);
		dmadscr[0].size_xfer_dir.s.trans_direction =
		    ACP_DMA_ATTRIBUTES_SHAREDMEM_TO_DAGB_ONION;
		dmadscr[0].size_xfer_dir.s.size = size / 2;
		dmadscr[0].size_xfer_dir.s.ioc = (u32) 0x1;
	}

	config_dma_descriptor_in_sram(acp_dev, dma_dscr_idx, &dmadscr[0]);

	dmadscr[1].size_xfer_dir.val = (u32) 0x0;
	if (direction == STREAM_PLAYBACK) {
		dma_dscr_idx = PLAYBACK_END_DMA_DESCR_CH12;
		dmadscr[1].dest = ACP_SHARED_RAM_BANK_1_ADDRESS;
		dmadscr[1].src = ACP_INTERNAL_APERTURE_WINDOW_0_ADDRESS +
			(pte_offset * PAGE_SIZE_4K) + (size / 2);
		dmadscr[1].size_xfer_dir.s.trans_direction =
		    ACP_DMA_ATTRIBUTES_DAGB_ONION_TO_SHAREDMEM;
		dmadscr[1].size_xfer_dir.s.size = (size / 2);
		dmadscr[1].size_xfer_dir.s.ioc = (u32) 0x0;
	} else {
		dma_dscr_idx = CAPTURE_END_DMA_DESCR_CH14;
		dmadscr[1].size_xfer_dir.s.trans_direction =
		    ACP_DMA_ATTRIBUTES_SHAREDMEM_TO_DAGB_ONION;
		dmadscr[1].size_xfer_dir.val = (u32) 0x0;
		dmadscr[1].dest = dmadscr[0].dest + (size / 2);
		dmadscr[1].src = dmadscr[0].src + (size / 2);
		dmadscr[1].size_xfer_dir.s.size = (size / 2);
		dmadscr[1].size_xfer_dir.s.ioc = (u32) 0x1;
	}

	config_dma_descriptor_in_sram(acp_dev, dma_dscr_idx, &dmadscr[1]);

	if (direction == STREAM_PLAYBACK) {
		/* starting descriptor for this channel */
		dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH12;
		config_acp_dma_channel(acp_dev, SYSRAM_TO_ACP_CH_NUM,
					dma_dscr_idx, num_descr,
					ACP_DMA_PRIORITY_LEVEL_NORMAL);
	} else {
		/* starting descriptor for this channel */
		dma_dscr_idx = CAPTURE_START_DMA_DESCR_CH14;
		config_acp_dma_channel(acp_dev, ACP_TO_SYSRAM_CH_NUM,
					dma_dscr_idx, num_descr,
					ACP_DMA_PRIORITY_LEVEL_NORMAL);
	}
}

/* Initialize the DMA descriptor information for transfer between
 * ACP SRAM <-> I2S
 */
static void set_acp_to_i2s_dma_descriptors(struct amd_acp_device *acp_dev,
					   u32 size, int direction)
{

	u16 num_descr;
	u16 dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH13;
	acp_dma_dscr_transfer_t dmadscr[2];

	num_descr = 2;

	dmadscr[0].size_xfer_dir.val = (u32) 0x0;
	if (direction == STREAM_PLAYBACK) {
		dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH13;
		dmadscr[0].src = ACP_SHARED_RAM_BANK_1_ADDRESS;
		dmadscr[0].size_xfer_dir.s.trans_direction = TO_ACP_I2S_1;
		dmadscr[0].size_xfer_dir.s.size = (size / 2);
		dmadscr[0].size_xfer_dir.s.ioc = (u32) 0x1;
	} else {
		dma_dscr_idx = CAPTURE_START_DMA_DESCR_CH15;
		dmadscr[0].dest = ACP_SHARED_RAM_BANK_5_ADDRESS;
		dmadscr[0].size_xfer_dir.s.trans_direction = FROM_ACP_I2S_1;
		dmadscr[0].size_xfer_dir.s.size = (size / 2);
		dmadscr[0].size_xfer_dir.s.ioc = (u32) 0x1;
	}

	config_dma_descriptor_in_sram(acp_dev, dma_dscr_idx, &dmadscr[0]);

	dmadscr[1].size_xfer_dir.val = (u32) 0x0;
	if (direction == STREAM_PLAYBACK) {
		dma_dscr_idx = PLAYBACK_END_DMA_DESCR_CH13;
		dmadscr[1].src = dmadscr[0].src + (size / 2);
		dmadscr[1].size_xfer_dir.s.trans_direction = TO_ACP_I2S_1;
		dmadscr[1].size_xfer_dir.s.size = (size / 2);
		dmadscr[1].size_xfer_dir.s.ioc = (u32) 0x1;

	} else {
		dma_dscr_idx = CAPTURE_END_DMA_DESCR_CH15;
		dmadscr[1].dest = dmadscr[0].dest + (size / 2);
		dmadscr[1].size_xfer_dir.s.trans_direction = FROM_ACP_I2S_1;
		dmadscr[1].size_xfer_dir.s.size = (size / 2);
		dmadscr[1].size_xfer_dir.s.ioc = (u32) 0x1;
	}

	config_dma_descriptor_in_sram(acp_dev, dma_dscr_idx, &dmadscr[1]);

	/* Configure the DMA channel with the above descriptore */
	if (direction == STREAM_PLAYBACK) {
		/* starting descriptor for this channel */
		dma_dscr_idx = PLAYBACK_START_DMA_DESCR_CH13;
		config_acp_dma_channel(acp_dev, ACP_TO_I2S_DMA_CH_NUM,
					dma_dscr_idx, num_descr,
					ACP_DMA_PRIORITY_LEVEL_NORMAL);
	} else {
		/* starting descriptor for this channel */
		dma_dscr_idx = CAPTURE_START_DMA_DESCR_CH15;
		config_acp_dma_channel(acp_dev, I2S_TO_ACP_DMA_CH_NUM,
					dma_dscr_idx, num_descr,
					ACP_DMA_PRIORITY_LEVEL_NORMAL);
	}

}

static u16 get_dscr_idx(struct amd_acp_device *acp_dev, int direction)
{
	u16 dscr_idx;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	if (direction == STREAM_PLAYBACK) {
		dscr_idx = cgs_read_register(acp_prv->cgs_device,
							mmACP_DMA_CUR_DSCR_13);
		dscr_idx = (dscr_idx == PLAYBACK_START_DMA_DESCR_CH13) ?
				PLAYBACK_START_DMA_DESCR_CH12 :
				PLAYBACK_END_DMA_DESCR_CH12;
	} else {
		dscr_idx = cgs_read_register(acp_prv->cgs_device,
							mmACP_DMA_CUR_DSCR_15);
		dscr_idx = (dscr_idx == CAPTURE_START_DMA_DESCR_CH15) ?
				CAPTURE_END_DMA_DESCR_CH14 :
				CAPTURE_START_DMA_DESCR_CH14;
	}

	return dscr_idx;

}

/* Create page table entries in ACP SRAM for the allocated memory */
static void acp_pte_config(struct amd_acp_device *acp_dev, struct page *pg,
			   u16 num_of_pages, u32 pte_offset)
{
	u16 page_idx;
	u64 addr;
	u32 low;
	u32 high;
	u32 offset;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	offset	= ACP_DAGB_GRP_SRBM_SRAM_BASE_OFFSET + (pte_offset * 8);
	for (page_idx = 0; page_idx < (num_of_pages); page_idx++) {
		/* Load the low address of page int ACP SRAM through SRBM */
		cgs_write_register(acp_prv->cgs_device,
				   mmACP_SRBM_Targ_Idx_Addr,
				   (offset + (page_idx * 8)));
		addr = page_to_phys(pg);

		low = lower_32_bits(addr);
		high = upper_32_bits(addr);

		cgs_write_register(acp_prv->cgs_device,
				   mmACP_SRBM_Targ_Idx_Data, low);

		/* Load the High address of page int ACP SRAM through SRBM */
		cgs_write_register(acp_prv->cgs_device,
				   mmACP_SRBM_Targ_Idx_Addr,
				   (offset + (page_idx * 8) + 4));

		/* page enable in ACP */
		high |= BIT(31);
		cgs_write_register(acp_prv->cgs_device,
				   mmACP_SRBM_Targ_Idx_Data, high);

		/* Move to next physically contiguos page */
		pg++;
	}
}

/* enables/disables ACP's external interrupt */
static void acp_enable_external_interrupts(struct amd_acp_device *acp_dev,
					   int enable)
{
	u32 acp_ext_intr_enb;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	acp_ext_intr_enb = enable ?
		ACP_EXTERNAL_INTR_ENB__ACPExtIntrEnb_MASK :
		0;

	/* Write the Software External Interrupt Enable register */
	cgs_write_register(acp_prv->cgs_device,
			   mmACP_EXTERNAL_INTR_ENB, acp_ext_intr_enb);
}

/* Clear (acknowledge) DMA 'Interrupt on Complete' (IOC) in ACP
 * external interrupt status register
 */
static void acp_ext_stat_clear_dmaioc(struct amd_acp_device *acp_dev, u8 ch_num)
{
	u32 ext_intr_stat;
	u32 chmask = BIT(ch_num);
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	ext_intr_stat = cgs_read_register(acp_prv->cgs_device,
					  mmACP_EXTERNAL_INTR_STAT);
	if (ext_intr_stat & (chmask <<
			     ACP_EXTERNAL_INTR_STAT__DMAIOCStat__SHIFT)) {

		ext_intr_stat &= (chmask <<
				  ACP_EXTERNAL_INTR_STAT__DMAIOCAck__SHIFT);
		cgs_write_register(acp_prv->cgs_device,
				   mmACP_EXTERNAL_INTR_STAT, ext_intr_stat);
	}
}

/* Check whether ACP DMA interrupt (IOC) is generated or not */
static u16 acp_get_intr_flag(struct amd_acp_device *acp_dev)
{
	u32 ext_intr_status;
	u32 intr_gen;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	ext_intr_status = cgs_read_register(acp_prv->cgs_device,
					    mmACP_EXTERNAL_INTR_STAT);
	intr_gen = (((ext_intr_status &
		      ACP_EXTERNAL_INTR_STAT__DMAIOCStat_MASK) >>
		     ACP_EXTERNAL_INTR_STAT__DMAIOCStat__SHIFT));

	return intr_gen;
}

static int irq_set_source(void *private_data, unsigned src_id, unsigned type,
								int enabled)
{
	struct amd_acp_device *acp_dev =
		((struct acp_irq_prv *)private_data)->acp_dev;

	if (src_id == VISLANDS30_IV_SRCID_ACP) {
		acp_enable_external_interrupts(acp_dev, enabled);
		return 0;
	} else {
		return -1;
	}
}

static inline void i2s_clear_irqs(struct amd_acp_device *acp_dev,
				  int direction)
{
	u32 i = 0;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	if (direction == STREAM_PLAYBACK) {
		for (i = 0; i < 4; i++)
			cgs_write_register(acp_prv->cgs_device,
					(mmACP_I2SSP_TOR0 + (0x10 * i)), 0);
	} else {
		for (i = 0; i < 4; i++)
			cgs_write_register(acp_prv->cgs_device,
					(mmACP_I2SMICSP_ROR0 +(0x10 * i)), 0);
	}
}

static void i2s_disable_channels(struct amd_acp_device *acp_dev,
					u32 stream)
{
	u32 i = 0;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	if (stream == STREAM_PLAYBACK) {
		for (i = 0; i < 4; i++)
			cgs_write_register(acp_prv->cgs_device,
					(mmACP_I2SSP_TER0 + (0x10 * i)), 0);
	} else {
		for (i = 0; i < 4; i++)
			cgs_write_register(acp_prv->cgs_device,
					(mmACP_I2SMICSP_RER0 + (0x10 * i)), 0);
	}
}

static void configure_i2s_stream(struct amd_acp_device *acp_dev,
					struct acp_i2s_config *i2s_config)
{
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	if (i2s_config->direction == STREAM_PLAYBACK) {
		/* Transmit configuration register for data width */
		cgs_write_register(acp_prv->cgs_device,
				   (mmACP_I2SSP_TCR0 + (0x10 *
							i2s_config->ch_reg)),
				   i2s_config->xfer_resolution);
		cgs_write_register(acp_prv->cgs_device,
				   (mmACP_I2SSP_TFCR0 + (0x10 *
							 i2s_config->ch_reg)),
				   0x02);

		/* Read interrupt mask register */
		i2s_config->irq =
			cgs_read_register(acp_prv->cgs_device,
					  (mmACP_I2SSP_IMR0 +
					   (0x10 * i2s_config->ch_reg)));
		/* TX FIFO Overrun,Empty interrupts */
		cgs_write_register(acp_prv->cgs_device,
				   (mmACP_I2SSP_IMR0 + (0x10 *
							i2s_config->ch_reg)),
				   (i2s_config->irq & ~0x30));
		/*Enable Transmit */
		cgs_write_register(acp_prv->cgs_device,
				   (mmACP_I2SSP_TER0 + (0x10 *
						i2s_config->ch_reg)), 1);
	} else {
		/* Receive configuration register for data width */
		cgs_write_register(acp_prv->cgs_device,
				   (mmACP_I2SMICSP_RCR0 + (0x10 *
							   i2s_config->ch_reg)),
				   i2s_config->xfer_resolution);
		cgs_write_register(acp_prv->cgs_device,
				   (mmACP_I2SMICSP_RFCR0 + (0x10 *
						    i2s_config->ch_reg)), 0x07);
		/*Read interrupt mask register */
		i2s_config->irq = cgs_read_register(acp_prv->cgs_device,
						    (mmACP_I2SMICSP_IMR0 +
					     (0x10 * i2s_config->ch_reg)));

		/* TX FIFO Overrun,Empty interrupts */
		cgs_write_register(acp_prv->cgs_device,
				   (mmACP_I2SMICSP_IMR0 + (0x10 *
							   i2s_config->ch_reg)),
				   i2s_config->irq & ~0x03);
		/*Enable Receive */
		cgs_write_register(acp_prv->cgs_device,
				   (mmACP_I2SMICSP_RER0 + (0x10 *
						   i2s_config->ch_reg)), 1);
	}
}

static void config_acp_dma(struct amd_acp_device *acp_dev,
			   struct acp_dma_config *dma_config)
{
	u32 pte_offset;

	if (dma_config->direction == STREAM_PLAYBACK)
		pte_offset = PLAYBACK_PTE_OFFSET;
	else
		pte_offset = CAPTURE_PTE_OFFSET;

	acp_pte_config(acp_dev, dma_config->pg,	dma_config->num_of_pages,
		       pte_offset);

	/* Configure System memory <-> ACP SRAM DMA descriptors */
	set_acp_sysmem_dma_descriptors(acp_dev, dma_config->size,
				       dma_config->direction,
				       pte_offset);

	/* Configure ACP SRAM <-> I2S DMA descriptors */
	set_acp_to_i2s_dma_descriptors(acp_dev, dma_config->size,
				       dma_config->direction);
}

/* Start a given DMA channel transfer */
static int acp_dma_start(struct amd_acp_device *acp_dev,
			 u16 ch_num, bool is_circular)
{
	int status;
	u32 dma_ctrl;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	status = STATUS_UNSUCCESSFUL;

	/* read the dma control register and disable the channel run field */
	dma_ctrl = cgs_read_register(acp_prv->cgs_device,
				     mmACP_DMA_CNTL_0 + ch_num);

	/*Invalidating the DAGB cache */
	cgs_write_register(acp_prv->cgs_device,	mmACP_DAGB_ATU_CTRL, ENABLE);

	/* configure the DMA channel and start the DMA transfer
	 * set dmachrun bit to start the transfer and enable the
	 * interrupt on completion of the dma transfer
	 */
	dma_ctrl |= ACP_DMA_CNTL_0__DMAChRun_MASK;

	if ((ch_num == ACP_TO_I2S_DMA_CH_NUM) ||
	    (ch_num == ACP_TO_SYSRAM_CH_NUM) ||
	    (ch_num == I2S_TO_ACP_DMA_CH_NUM)) {
		dma_ctrl |= ACP_DMA_CNTL_0__DMAChIOCEn_MASK;
	} else {
		dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChIOCEn_MASK;
	}

	/* enable  for ACP SRAM to/from I2S DMA channel */
	if (is_circular == true) {
		dma_ctrl |= ACP_DMA_CNTL_0__Circular_DMA_En_MASK;
		cgs_irq_get(acp_prv->cgs_device, VISLANDS30_IV_SRCID_ACP, 0);
	}
	else
		dma_ctrl &= ~ACP_DMA_CNTL_0__Circular_DMA_En_MASK;

	cgs_write_register(acp_prv->cgs_device,	(mmACP_DMA_CNTL_0 + ch_num),
			   dma_ctrl);

	status = STATUS_SUCCESS;
	return status;
}

/* Stop a given DMA channel transfer */
static int acp_dma_stop(struct amd_acp_device *acp_dev, u8 ch_num)
{
	int status = STATUS_UNSUCCESSFUL;
	u32 dma_ctrl;
	u32 dma_ch_sts;
	u32 delay_time = ACP_DMA_RESET_TIME;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	if (acp_dev == NULL)
		return status;

	dma_ctrl = cgs_read_register(acp_prv->cgs_device,
				     mmACP_DMA_CNTL_0 + ch_num);

	/* clear the dma control register fields before writing zero
	 * in reset bit
	 */
	dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChRun_MASK;
	dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChIOCEn_MASK;

	cgs_write_register(acp_prv->cgs_device,
			   (mmACP_DMA_CNTL_0 + ch_num), dma_ctrl);
	dma_ch_sts = cgs_read_register(acp_prv->cgs_device, mmACP_DMA_CH_STS);

	if (dma_ch_sts & BIT(ch_num)) {
		/* set the reset bit for this channel
		 * to stop the dma transfer */
		dma_ctrl |= ACP_DMA_CNTL_0__DMAChRst_MASK;
		cgs_write_register(acp_prv->cgs_device,
				   (mmACP_DMA_CNTL_0 + ch_num), dma_ctrl);
	}

	/* if channel transfer is not stopped with in time delay
	 * return this status */
	status = -EBUSY;

	/* check the channel status bit for some time and return the status */
	while (0 < delay_time) {
		dma_ch_sts = cgs_read_register(acp_prv->cgs_device,
					       mmACP_DMA_CH_STS);
		if (!(dma_ch_sts & BIT(ch_num))) {
			/* clear the reset flag after successfully stopping
			   the dma transfer and break from the loop */
			dma_ctrl &= ~ACP_DMA_CNTL_0__DMAChRst_MASK;

			cgs_write_register(acp_prv->cgs_device,
					   (mmACP_DMA_CNTL_0 + ch_num),
					   dma_ctrl);
			status = STATUS_SUCCESS;
			break;
		}
		delay_time--;
	}

	if ((ch_num == ACP_TO_I2S_DMA_CH_NUM) ||
	    (ch_num == I2S_TO_ACP_DMA_CH_NUM)) {
		cgs_irq_put(acp_prv->cgs_device, VISLANDS30_IV_SRCID_ACP, 0);
	}

	return status;
}

/* ACP DMA irq handler routine for playback, capture usecases */
static int dma_irq_handler(void *prv_data)
{
	u16 play_acp_i2s_intr, cap_i2s_acp_intr, cap_acp_sysram_intr;
	u16 dscr_idx, intr_flag;
	u32 ext_intr_status;
	int priority_level = 0x0;
	int dma_transfer_status = STATUS_UNSUCCESSFUL;
	struct acp_irq_prv *idata = prv_data;
	struct amd_acp_device *acp_dev = idata->acp_dev;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	intr_flag = acp_get_intr_flag(acp_dev);

	play_acp_i2s_intr = (intr_flag & BIT(ACP_TO_I2S_DMA_CH_NUM));
	cap_i2s_acp_intr = (intr_flag & BIT(I2S_TO_ACP_DMA_CH_NUM));
	cap_acp_sysram_intr = (intr_flag & BIT(ACP_TO_SYSRAM_CH_NUM));

	if (!play_acp_i2s_intr && !cap_i2s_acp_intr && !cap_acp_sysram_intr) {
		/* We registered for DMA Interrupt-On-Complete interrupts only.
		 * If we hit here, log it and return. */
		ext_intr_status = cgs_read_register(acp_prv->cgs_device,
					    mmACP_EXTERNAL_INTR_STAT);
		pr_info("ACP: Not a DMA IOC irq: %x\n", ext_intr_status);
		return 0;
	}

	if (play_acp_i2s_intr) {
		dscr_idx = get_dscr_idx(acp_dev, STREAM_PLAYBACK);
		config_acp_dma_channel(acp_dev, SYSRAM_TO_ACP_CH_NUM, dscr_idx,
				       1, priority_level);
		dma_transfer_status = acp_dma_start(acp_dev,
						    SYSRAM_TO_ACP_CH_NUM,
						    false);
		idata->set_elapsed(idata->dev, 1, 0);

		acp_ext_stat_clear_dmaioc(acp_dev, ACP_TO_I2S_DMA_CH_NUM);
	}

	if (cap_i2s_acp_intr) {
		dscr_idx = get_dscr_idx(acp_dev, STREAM_CAPTURE);
		config_acp_dma_channel(acp_dev, ACP_TO_SYSRAM_CH_NUM, dscr_idx,
				       1, priority_level);
		dma_transfer_status = acp_dma_start(acp_dev,
						    ACP_TO_SYSRAM_CH_NUM,
						    false);

		acp_ext_stat_clear_dmaioc(acp_dev, I2S_TO_ACP_DMA_CH_NUM);
	}

	if (cap_acp_sysram_intr) {
		idata->set_elapsed(idata->dev, 0, 1);
		acp_ext_stat_clear_dmaioc(acp_dev, ACP_TO_SYSRAM_CH_NUM);
	}

	return 0;
}

static int irq_handler(void *private_data, unsigned src_id,
		       const uint32_t *iv_entry)
{
	if (src_id == VISLANDS30_IV_SRCID_ACP)
		return dma_irq_handler(private_data);
	else
		return -1;
}

/* power off a tile/block within ACP */
static void acp_suspend_tile(struct amd_acp_private *acp_prv, int tile)
{
	u32 val = 0;
	u32 timeout = 0;

	if ((tile  < ACP_TILE_P1) || (tile > ACP_TILE_DSP2)) {
		pr_err(" %s : Invalid ACP power tile index\n", __func__);
		return;
	}

	val = cgs_read_register(acp_prv->cgs_device,
					mmACP_PGFSM_READ_REG_0 + tile);
	val &= ACP_TILE_ON_MASK;

	if (val == 0x0) {
		val = cgs_read_register(acp_prv->cgs_device,
					mmACP_PGFSM_RETAIN_REG);
		val = val | (1 << tile);
		cgs_write_register(acp_prv->cgs_device,	mmACP_PGFSM_RETAIN_REG,
							val);
		cgs_write_register(acp_prv->cgs_device,	mmACP_PGFSM_CONFIG_REG,
							0x500 + tile);

		timeout = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
		while (timeout--) {
			val = cgs_read_register(acp_prv->cgs_device,
					mmACP_PGFSM_READ_REG_0 + tile);
			val = val & ACP_TILE_ON_MASK;
			if (val == ACP_TILE_OFF_MASK)
				break;
		}

		val = cgs_read_register(acp_prv->cgs_device,
					mmACP_PGFSM_RETAIN_REG);

		val |= ACP_TILE_OFF_RETAIN_REG_MASK;
		cgs_write_register(acp_prv->cgs_device,	mmACP_PGFSM_RETAIN_REG,
							val);
	}

}

/* power on a tile/block within ACP */
static void acp_resume_tile(struct amd_acp_private *acp_prv, int tile)
{
	u32 val = 0;
	u32 timeout = 0;

	if ((tile  < ACP_TILE_P1) || (tile > ACP_TILE_DSP2)) {
		pr_err(" %s : Invalid ACP power tile index\n", __func__);
		return;
	}

	val = cgs_read_register(acp_prv->cgs_device,
					mmACP_PGFSM_READ_REG_0 + tile);
	val = val & ACP_TILE_ON_MASK;

	if (val != 0x0) {
		cgs_write_register(acp_prv->cgs_device,	mmACP_PGFSM_CONFIG_REG,
							0x600 + tile);
		timeout = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
		while (timeout--) {
			val = cgs_read_register(acp_prv->cgs_device,
					mmACP_PGFSM_READ_REG_0 + tile);
			val = val & ACP_TILE_ON_MASK;
			if (val == 0x0)
				break;
		}
		val = cgs_read_register(acp_prv->cgs_device,
					mmACP_PGFSM_RETAIN_REG);
		if (tile == ACP_TILE_P1)
			val = val & (ACP_TILE_P1_MASK);
		else if (tile == ACP_TILE_P2)
			val = val & (ACP_TILE_P2_MASK);

		cgs_write_register(acp_prv->cgs_device,	mmACP_PGFSM_RETAIN_REG,
							val);
	}
}

/* Initialize and bring ACP hardware to default state. */
static void acp_init(struct amd_acp_private *acp_prv)
{
	u32 val;
	u32 timeout_value;

	/* Assert Soft reset of ACP */
	val = cgs_read_register(acp_prv->cgs_device, mmACP_SOFT_RESET);

	val |= ACP_SOFT_RESET__SoftResetAud_MASK;
	cgs_write_register(acp_prv->cgs_device,
			   mmACP_SOFT_RESET, val);

	timeout_value = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
	while (timeout_value--) {
		val = cgs_read_register(acp_prv->cgs_device, mmACP_SOFT_RESET);
		if (ACP_SOFT_RESET__SoftResetAudDone_MASK ==
		    (val & ACP_SOFT_RESET__SoftResetAudDone_MASK))
			break;
	}

	/* Enable clock to ACP and wait until the clock is enabled */
	val = cgs_read_register(acp_prv->cgs_device, mmACP_CONTROL);
	val = val | ACP_CONTROL__ClkEn_MASK;
	cgs_write_register(acp_prv->cgs_device,	mmACP_CONTROL, val);

	timeout_value = ACP_CLOCK_EN_TIME_OUT_VALUE;

	while (timeout_value--) {
		val = cgs_read_register(acp_prv->cgs_device, mmACP_STATUS);
		if (val & (u32) 0x1)
			break;
		udelay(100);
	}

	/* Deassert the SOFT RESET flags */
	val = cgs_read_register(acp_prv->cgs_device, mmACP_SOFT_RESET);
	val &= ~ACP_SOFT_RESET__SoftResetAud_MASK;
	cgs_write_register(acp_prv->cgs_device,	mmACP_SOFT_RESET, val);

	/* initiailizing Garlic Control DAGB register */
	cgs_write_register(acp_prv->cgs_device,	mmACP_AXI2DAGB_ONION_CNTL,
			   ONION_CNTL_DEFAULT);

	/* initiailizing Onion Control DAGB registers */
	cgs_write_register(acp_prv->cgs_device,	mmACP_AXI2DAGB_GARLIC_CNTL,
			   GARLIC_CNTL_DEFAULT);

	acp_dma_descr_init(acp_prv);

	cgs_write_register(acp_prv->cgs_device,	mmACP_DMA_DESC_BASE_ADDR,
			   ACP_SRAM_BASE_ADDRESS);

	/* Num of descriptiors in SRAM 0x4, means 256 descriptors;(64 * 4) */
	cgs_write_register(acp_prv->cgs_device, mmACP_DMA_DESC_MAX_NUM_DSCR,
			   0x4);

	cgs_write_register(acp_prv->cgs_device,	mmACP_EXTERNAL_INTR_CNTL,
			   ACP_EXTERNAL_INTR_CNTL__DMAIOCMask_MASK);

	pr_info("ACP: Initialized.\n");

}

static int acp_hw_init(struct amd_acp_device *acp_dev, void *iprv)
{
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	acp_init(acp_prv);

	cgs_add_irq_source(acp_prv->cgs_device, VISLANDS30_IV_SRCID_ACP, 1,
			   irq_set_source, irq_handler, iprv);

	/* Disable DSPs which are not used */
	acp_suspend_tile(acp_prv, ACP_TILE_DSP0);
	acp_suspend_tile(acp_prv, ACP_TILE_DSP1);
	acp_suspend_tile(acp_prv, ACP_TILE_DSP2);

	return STATUS_SUCCESS;
}

/* Deintialize ACP */
static void acp_deinit(struct amd_acp_private *acp_prv)
{
	u32 val;
	u32 timeout_value;

	/* Assert Soft reset of ACP */
	val = cgs_read_register(acp_prv->cgs_device, mmACP_SOFT_RESET);

	val |= ACP_SOFT_RESET__SoftResetAud_MASK;
	cgs_write_register(acp_prv->cgs_device,	mmACP_SOFT_RESET, val);

	timeout_value = ACP_SOFT_RESET_DONE_TIME_OUT_VALUE;
	while (timeout_value--) {
		val = cgs_read_register(acp_prv->cgs_device, mmACP_SOFT_RESET);
		if (ACP_SOFT_RESET__SoftResetAudDone_MASK ==
		    (val & ACP_SOFT_RESET__SoftResetAudDone_MASK)) {
			break;
	    }
	}
	/** Disable ACP clock */
	val = cgs_read_register(acp_prv->cgs_device, mmACP_CONTROL);
	val &= ~ACP_CONTROL__ClkEn_MASK;
	cgs_write_register(acp_prv->cgs_device, mmACP_CONTROL, val);

	timeout_value = ACP_CLOCK_EN_TIME_OUT_VALUE;

	while (timeout_value--) {
		val = cgs_read_register(acp_prv->cgs_device, mmACP_STATUS);
		if (!(val & (u32) 0x1))
			break;
		udelay(100);
	}

	pr_info("ACP: De-Initialized.\n");
}

static void acp_hw_deinit(struct amd_acp_device *acp_dev)
{
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	acp_deinit(acp_prv);
}

/* Update DMA postion in audio ring buffer at period level granularity.
 * This will be used by ALSA PCM driver
 */
static u32 acp_update_dma_pointer(struct amd_acp_device *acp_dev, int direction,
				  u32 period_size)
{
	u32 pos;
	u16 dscr;
	u32 mul;
	u32 dma_config;
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	pos = 0;

	if (direction == STREAM_PLAYBACK) {
		dscr = cgs_read_register(acp_prv->cgs_device,
					 mmACP_DMA_CUR_DSCR_13);

		mul = (dscr == PLAYBACK_START_DMA_DESCR_CH13) ? 0 : 1;
		pos =  (mul * period_size);

	} else {
		dma_config = cgs_read_register(acp_prv->cgs_device,
					       mmACP_DMA_CNTL_14);
		if (dma_config != 0) {
			dscr = cgs_read_register(acp_prv->cgs_device,
					 mmACP_DMA_CUR_DSCR_14);
			mul = (dscr == CAPTURE_START_DMA_DESCR_CH14) ? 1 : 2;
			pos = (mul * period_size);
		}

		if (pos >= (2 * period_size))
			pos = 0;

	}
	return pos;
}

/* Wait for initial buffering to complete in HOST to SRAM DMA channel
 * for plaback usecase
 */
static void wait_for_prebuffer_finish(struct amd_acp_device *acp_dev)
{
	u32 dma_ch_sts;
	u32 channel_mask = BIT(SYSRAM_TO_ACP_CH_NUM);
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	do {
		/* Read the channel status to poll dma transfer completion
		 * (System RAM to SRAM)
		 * In this case, it will be runtime->start_threshold
		 * (2 ALSA periods) of transfer. Rendering starts after this
		 * threshold is met.
		 */
		dma_ch_sts = cgs_read_register(acp_prv->cgs_device,
					       mmACP_DMA_CH_STS);
		udelay(20);
	} while (dma_ch_sts & channel_mask);
}

static void i2s_reset(struct amd_acp_device *acp_dev, int direction)
{
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	if (direction == STREAM_PLAYBACK)
		cgs_write_register(acp_prv->cgs_device,	mmACP_I2SSP_TXFFR, 1);
	else
		cgs_write_register(acp_prv->cgs_device,
				   mmACP_I2SMICSP_RXFFR, 1);

}

static void i2s_start(struct amd_acp_device *acp_dev, int direction)
{
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	if (direction == STREAM_PLAYBACK) {
		cgs_write_register(acp_prv->cgs_device,	mmACP_I2SSP_IER, 1);
		cgs_write_register(acp_prv->cgs_device,	mmACP_I2SSP_ITER, 1);

	} else {
		cgs_write_register(acp_prv->cgs_device, mmACP_I2SMICSP_IER, 1);
		cgs_write_register(acp_prv->cgs_device, mmACP_I2SMICSP_IRER, 1);
	}

	cgs_write_register(acp_prv->cgs_device,	mmACP_I2SSP_CER, 1);
}

static void i2s_stop(struct amd_acp_device *acp_dev, int direction)
{
	struct amd_acp_private *acp_prv = (struct amd_acp_private *)acp_dev;

	i2s_clear_irqs(acp_dev, direction);

	if (direction == STREAM_PLAYBACK)
		cgs_write_register(acp_prv->cgs_device, mmACP_I2SSP_ITER, 0);
	else
		cgs_write_register(acp_prv->cgs_device,	mmACP_I2SMICSP_IRER, 0);

	if (direction == STREAM_PLAYBACK) {
		cgs_write_register(acp_prv->cgs_device,	mmACP_I2SSP_CER, 0);
		cgs_write_register(acp_prv->cgs_device,	mmACP_I2SSP_IER, 0);
	} else {
		cgs_write_register(acp_prv->cgs_device,	mmACP_I2SMICSP_CER, 0);
		cgs_write_register(acp_prv->cgs_device,	mmACP_I2SMICSP_IER, 0);
	}
}

static void configure_i2s(struct amd_acp_device *acp_dev,
			  struct acp_i2s_config *i2s_config)
{
	i2s_disable_channels(acp_dev, i2s_config->direction);
	configure_i2s_stream(acp_dev, i2s_config);
}

void amd_acp_pcm_suspend(struct amd_acp_device *acp_dev)
{
	struct amd_acp_private *acp_prv;

	acp_prv = (struct amd_acp_private *)acp_dev;
	amd_acp_suspend(acp_prv);
}

void amd_acp_pcm_resume(struct amd_acp_device *acp_dev)
{
	struct amd_acp_private *acp_prv;

	acp_prv = (struct amd_acp_private *)acp_dev;
	amd_acp_resume(acp_prv);
}

int amd_acp_hw_init(void *cgs_device,
		    unsigned acp_version_major, unsigned acp_version_minor,
		    struct amd_acp_private **acp_private)
{
	unsigned int acp_mode = ACP_MODE_I2S;

	if ((acp_version_major == 2) && (acp_version_minor == 2))
		acp_mode = cgs_read_register(cgs_device,
					mmACP_AZALIA_I2S_SELECT);

	if (acp_mode != ACP_MODE_I2S)
		return -ENODEV;

	*acp_private = kzalloc(sizeof(struct amd_acp_private), GFP_KERNEL);
	if (*acp_private == NULL)
		return -ENOMEM;

	(*acp_private)->cgs_device = cgs_device;
	(*acp_private)->acp_version_major = acp_version_major;
	(*acp_private)->acp_version_minor = acp_version_minor;

	(*acp_private)->public.init = acp_hw_init;
	(*acp_private)->public.fini = acp_hw_deinit;
	(*acp_private)->public.config_dma = config_acp_dma;
	(*acp_private)->public.config_dma_channel = config_acp_dma_channel;
	(*acp_private)->public.dma_start = acp_dma_start;
	(*acp_private)->public.dma_stop = acp_dma_stop;
	(*acp_private)->public.update_dma_pointer = acp_update_dma_pointer;
	(*acp_private)->public.prebuffer_audio = wait_for_prebuffer_finish;

	(*acp_private)->public.i2s_reset = i2s_reset;
	(*acp_private)->public.config_i2s = configure_i2s;
	(*acp_private)->public.i2s_start = i2s_start;
	(*acp_private)->public.i2s_stop = i2s_stop;

	(*acp_private)->public.acp_suspend = amd_acp_pcm_suspend;
	(*acp_private)->public.acp_resume = amd_acp_pcm_resume;

	return 0;
}

int amd_acp_hw_fini(struct amd_acp_private *acp_private)
{
	kfree(acp_private);
	return 0;
}

void amd_acp_suspend(struct amd_acp_private *acp_private)
{
	acp_suspend_tile(acp_private, ACP_TILE_P2);
	acp_suspend_tile(acp_private, ACP_TILE_P1);
}

void amd_acp_resume(struct amd_acp_private *acp_private)
{
	acp_resume_tile(acp_private, ACP_TILE_P1);
	acp_resume_tile(acp_private, ACP_TILE_P2);

	acp_init(acp_private);

	/* Disable DSPs which are not going to be used */
	acp_suspend_tile(acp_private, ACP_TILE_DSP0);
	acp_suspend_tile(acp_private, ACP_TILE_DSP1);
	acp_suspend_tile(acp_private, ACP_TILE_DSP2);
}
