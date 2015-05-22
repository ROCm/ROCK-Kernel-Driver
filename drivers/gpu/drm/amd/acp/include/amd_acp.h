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
*/

#ifndef _AMD_ACP_H
#define _AMD_ACP_H

#include <linux/types.h>

/* Playback DMA channels */
#define SYSRAM_TO_ACP_CH_NUM 12
#define ACP_TO_I2S_DMA_CH_NUM 13

/* Capture DMA channels */
#define ACP_TO_SYSRAM_CH_NUM 14
#define I2S_TO_ACP_DMA_CH_NUM 15

#define PLAYBACK_START_DMA_DESCR_CH12 0
#define PLAYBACK_END_DMA_DESCR_CH12 1

#define PLAYBACK_START_DMA_DESCR_CH13 2
#define PLAYBACK_END_DMA_DESCR_CH13 3


#define CAPTURE_START_DMA_DESCR_CH14 4
#define CAPTURE_END_DMA_DESCR_CH14 5

#define CAPTURE_START_DMA_DESCR_CH15 6
#define CAPTURE_END_DMA_DESCR_CH15 7

#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL -1

enum acp_dma_priority_level {
	/* 0x0 Specifies the DMA channel is given normal priority */
	ACP_DMA_PRIORITY_LEVEL_NORMAL = 0x0,
	/* 0x1 Specifies the DMA channel is given high priority */
	ACP_DMA_PRIORITY_LEVEL_HIGH = 0x1,
	ACP_DMA_PRIORITY_LEVEL_FORCESIZE = 0xFF
};

struct acp_dma_config {
	struct page *pg;
	u16 num_of_pages;
	u16 direction;
	uint64_t size;
};

struct acp_i2s_config {
	u16 direction;
	u32 xfer_resolution;
	u32 irq;
	u32 ch_reg;
};

struct acp_irq_prv {
	struct device *dev;
	struct amd_acp_device *acp_dev;
	void (*set_elapsed)(struct device *pdev, u16 play_intr,
						u16 capture_intr);
};

/* Public interface of ACP device exposed on AMD GNB bus */
struct amd_acp_device {
	/* Handshake when ALSA driver connects, disconnects
	 * TBD: is this really needed? */
	int (*init)(struct amd_acp_device *acp_dev, void *iprv);
	void (*fini)(struct amd_acp_device *acp_dev);

	/**
	 * config_dma() - Configure ACP internal DMA controller
	 * @acp_dev:	    acp device
	 * @acp_dma_config: DMA configuration parameters
	 *
	 * This will configure the DMA controller with the given
	 * configuration parameters.
	 */
	void (*config_dma)(struct amd_acp_device *acp_dev,
			   struct acp_dma_config *dma_config);

	/**
	 * config_dma_channel() - Configure ACP DMA channel
	 * @acp_dev:	    acp device
	 * @ch_num:	    channel number to be configured
	 * @dscr_strt_idx:  DMA descriptor starting index
	 * @priority_level: priority level of channel
	 *
	 * This will configure the DMA channel with the given
	 * configuration parameters.
	 */
	void (*config_dma_channel)(struct amd_acp_device *acp_dev,
				   u8 ch_num, u16 dscr_strt_idx, u16 num_dscrs,
				   enum acp_dma_priority_level priority_level);

	/**
	 * dma_start() - Start ACP DMA engine
	 * @acp_dev:	 acp device
	 * @ch_num:	 DMA channel number
	 * @is_circular: configure circular DMA
	 *
	 * Start DMA channel as configured.
	 */
	int (*dma_start)(struct amd_acp_device *acp_dev, u16 ch_num,
			  bool is_circular);

	/**
	 * dma_stop() - Stop ACP DMA engine
	 * @acp_dev:	acp device
	 * @ch_num:	DMA channel number
	 *
	 * Stop DMA channel as configured.
	 */
	int (*dma_stop)(struct amd_acp_device *acp_dev, u8 ch_num);

	/**
	 * update_dma_pointer() - Query the buffer postion
	 * @acp_dev:	 acp device
	 * @direction:   Dma transfer direction
	 * @period_size: size of buffer in-terms of ALSA terminology
	 *
	 * This will query the buffer position from ACP IP, based on data
	 * produced/consumed
	 */
	u32 (*update_dma_pointer)(struct amd_acp_device *acp_dev,
				  int direction, u32 period_size);

	/**
	 * prebuffer_audio() - Wait for buffering to complete
	 * @acp_dev:	acp device
	 *
	 * Wait for buffering to complete in HOST to SRAM DMA channel.
	 */
	void (*prebuffer_audio)(struct amd_acp_device *acp_dev);

	/**
	 * i2s_reset() -  Reset i2s FIFOs
	 * @acp_dev:	  acp device
	 * @direction:    direction of stream – playback/record
	 *
	 * Resets I2S FIFOs
	 */
	void (*i2s_reset)(struct amd_acp_device *acp_dev, int direction);

	/**
	 * config_i2s() - Configure the i2s controller
	 * @acp_dev:    acp device
	 * @i2s_config: configuration of i2s controller
	 *
	 * This will configure the i2s controller instance used on the
	 * board, with the given configuration parameters.
	 */
	void (*config_i2s)(struct amd_acp_device *acp_dev,
			   struct acp_i2s_config *i2s_config);

	/**
	 * i2s_start() - Start i2s controller
	 * @acp_dev:	  acp device
	 * @direction:    direction of stream – playback/record
	 *
	 * Starts I2S data transmission
	 */
	void (*i2s_start)(struct amd_acp_device *acp_dev, int direction);

	/**
	 * i2s_stop() - Stop i2s controller
	 * @acp_dev:	acp device
	 * @stream:	Type of stream – playback/record
	 *
	 * Stops I2S data transmission
	 */
	void (*i2s_stop)(struct amd_acp_device *acp_dev, int direction);

	/* TODO: Need callback registration interface for asynchronous
	 * notifications */
};

#endif /* _AMD_ACP_H */
