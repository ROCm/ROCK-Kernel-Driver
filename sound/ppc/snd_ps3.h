/*
 * Audio support for PS3
 * Copyright (C) 2006 Sony Computer Entertainment Inc.
 * All rights reserved.
 * Copyright 2006, 2007 Sony Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the Licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#if !defined(_SND_PS3_H_)
#define _SND_PS3_H_

#include <linux/irqreturn.h>

#define SND_PS3_DRIVER_NAME "snd_ps3"

enum snd_ps3_out_channel {
	SND_PS3_OUT_SPDIF_0,
	SND_PS3_OUT_SPDIF_1,
	SND_PS3_OUT_SERIAL_0,
	SND_PS3_OUT_DEVS
};

enum snd_ps3_dma_filltype {
	SND_PS3_DMA_FILLTYPE_FIRSTFILL,
	SND_PS3_DMA_FILLTYPE_RUNNING,
	SND_PS3_DMA_FILLTYPE_SILENT_FIRSTFILL,
	SND_PS3_DMA_FILLTYPE_SILENT_RUNNING
};

enum snd_ps3_ch {
	SND_PS3_CH_L = 0,
	SND_PS3_CH_R = 1,
	SND_PS3_CH_MAX = 2
};

struct snd_ps3_avsetting_info {
	uint32_t avs_audio_ch;     /* fixed */
	uint32_t avs_audio_rate;
	uint32_t avs_audio_width;
	uint32_t avs_audio_format; /* fixed */
	uint32_t avs_audio_source; /* fixed */
};
/*
 * PS3 audio 'card' instance
 * there should be only ONE hardware.
 */
struct snd_ps3_card_info {
	struct ps3_system_bus_device * ps3_dev;
	//struct platform_device * platform_device;
	struct snd_card * card;

	struct snd_pcm * pcm;
	struct snd_pcm_substream * substream;

	/* hvc info */
	u64 audio_lpar_addr;
	u64 audio_lpar_size;

	/* registers */
	void __iomem * mapped_mmio_vaddr;

	/* irq */
	u64 audio_irq_outlet;
	unsigned int irq_no;

	/* remember avsetting */
	struct snd_ps3_avsetting_info avs;

	/* dma buffer management */
	rwlock_t dma_lock;
		/* dma_lock start */
		void * dma_start_vaddr[2]; /* 0 for L, 1 for R */
		dma_addr_t dma_start_bus_addr[2];
		size_t dma_buffer_size;
		void * dma_last_transfer_vaddr[2];
		void * dma_next_transfer_vaddr[2];
		int    silent;
		/* dma_lock end */

	int running;

	/* null buffer */
	void * null_buffer_start_vaddr;
	dma_addr_t null_buffer_start_dma_addr;

	/* start delay */
	rwlock_t start_delay_lock;
		/* start_delay_lock start */
		unsigned int start_delay;
		/* start_delay_lock end */

	struct snd_kcontrol * vol_control;
	int attenuater[2]; /* store by attenuation, not volume*/
};


/* module  entries */
static int __init snd_ps3_init(void);
static void __exit snd_ps3_exit(void);

/* ALSA snd driver ops */
static int snd_ps3_pcm_open(struct snd_pcm_substream * substream);
static int snd_ps3_pcm_close(struct snd_pcm_substream * substream);
static int snd_ps3_pcm_prepare(struct snd_pcm_substream * substream);
static int snd_ps3_pcm_trigger(struct snd_pcm_substream * substream,
				 int cmd);
static snd_pcm_uframes_t snd_ps3_pcm_pointer(struct snd_pcm_substream *
					     substream);
static int snd_ps3_pcm_hw_params(struct snd_pcm_substream * substream,
				 struct snd_pcm_hw_params * hw_params);
static int snd_ps3_pcm_hw_free(struct snd_pcm_substream * substream);


/* ps3_system_bus_driver entries */
static int __init snd_ps3_driver_probe(struct ps3_system_bus_device * dev);
//static void snd_ps3_driver_shutdown(struct ps3_system_bus_device * dev);
static int snd_ps3_driver_remove(struct ps3_system_bus_device * dev);

/* address setup */
static int snd_ps3_map_mmio(void);
static void snd_ps3_unmap_mmio(void);
static int snd_ps3_allocate_irq(void);
static void snd_ps3_free_irq(void);
static void snd_ps3_audio_set_base_addr(uint64_t ioaddr_start);

/* interrupt handler */
static irqreturn_t snd_ps3_interrupt(int irq, void * dev_id);


/* set sampling rate/format */
static int snd_ps3_set_avsetting(struct snd_pcm_substream * substream);
/* take effect parameter change */
static int snd_ps3_change_avsetting(struct snd_ps3_card_info * card);
/* initialize avsetting and take it effect */
static int snd_ps3_init_avsetting(struct snd_ps3_card_info * card);
/* setup dma */
static int snd_ps3_program_dma(struct snd_ps3_card_info * card,
			       enum snd_ps3_dma_filltype filltype);
//static int snd_ps3_kick_dma(struct snd_ps3_card_info * card);
static void snd_ps3_wait_for_dma_stop(struct snd_ps3_card_info * card);

static dma_addr_t v_to_bus(struct snd_ps3_card_info *, void  * vaddr, int ch);

#ifdef _SND_PS3_DEV_ATTR
static ssize_t snd_ps3_get_start_delay(struct device *dev,
				       struct device_attribute *attr,
				       char *buf);
static ssize_t snd_ps3_set_start_delay(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count);
#endif

static int snd_ps3_info_vol_control(struct snd_kcontrol * kcontrol,
				    struct snd_ctl_elem_info * uinfo);
static int snd_ps3_get_vol_control(struct snd_kcontrol * kcontrol,
				   struct snd_ctl_elem_value * ucontrol);
static int snd_ps3_put_vol_control(struct snd_kcontrol * kcontrol,
				   struct snd_ctl_elem_value * ucontrol);

static int snd_ps3_soft_attenuate(struct snd_ps3_card_info * card,
				  void * start_l, void * star_r, int bytes);

/* PS3 audio DMAC block size in bytes */
#define PS3_AUDIO_DMAC_BLOCK_SIZE (128)
/* one stage (stereo)  of audio FIFO in bytes */
#define PS3_AUDIO_FIFO_STAGE_SIZE (256)
/* how many stages the fifo have */
#define PS3_AUDIO_FIFO_STAGE_COUNT (8)
/* fifo size 128 bytes * 8 stages * stereo (2ch) */
#define PS3_AUDIO_FIFO_SIZE \
	(PS3_AUDIO_FIFO_STAGE_SIZE * PS3_AUDIO_FIFO_STAGE_COUNT)

/* PS3 audio DMAC max block count in one dma shot = 128 (0x80) blocks*/
#define PS3_AUDIO_DMAC_MAX_BLOCKS  (PS3_AUDIO_DMASIZE_BLOCKS_MASK + 1)

#define PS3_AUDIO_NORMAL_DMA_START_CH (0)
#define PS3_AUDIO_NORMAL_DMA_COUNT    (8)
#define PS3_AUDIO_NULL_DMA_START_CH \
	(PS3_AUDIO_NORMAL_DMA_START_CH + PS3_AUDIO_NORMAL_DMA_COUNT)
#define PS3_AUDIO_NULL_DMA_COUNT      (2)

#define SND_PS3_MAX_VOL (0x0F)
#define SND_PS3_MIN_VOL (0x00)
#define SND_PS3_MIN_ATT SND_PS3_MIN_VOL
#define SND_PS3_MAX_ATT SND_PS3_MAX_VOL

#define SND_PS3_PCM_PREALLOC_SIZE \
	(PS3_AUDIO_DMAC_BLOCK_SIZE * PS3_AUDIO_DMAC_MAX_BLOCKS * 4)

#define SND_PS3_DMA_REGION_SIZE \
	(SND_PS3_PCM_PREALLOC_SIZE + PAGE_SIZE)

#define PS3_AUDIO_IOID       (1UL)

#endif /* _SND_PS3_H_ */
