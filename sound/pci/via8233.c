/*
 *   ALSA driver for VIA VT8233 (South Bridge)
 *
 *	Copyright (c) 2000 Tjeerd.Mulder@fujitsu-siemens.com
 *	This driver is based on VIA686 code by Jaroslav Kysela <perex@suse.cz>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */      

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_sgbuf.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/ac97_codec.h>
#include <sound/mpu401.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

MODULE_AUTHOR("Tjeerd.Mulder@fujitsu-siemens.com");
MODULE_DESCRIPTION("VIA VT8233");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{VIA,VT8233,pci}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int snd_ac97_clock[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 48000};

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for VIA 8233 bridge.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for VIA 8233 bridge.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable audio part of VIA 8233 bridge.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_ac97_clock, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_ac97_clock, "AC'97 codec clock (default 48000Hz).");
MODULE_PARM_SYNTAX(snd_ac97_clock, SNDRV_ENABLED ",default:48000");


/*
 *  Direct registers
 */

#ifndef PCI_DEVICE_ID_VIA_8233_5
#define PCI_DEVICE_ID_VIA_8233_5	0x3059
#endif

/* revision numbers */
#define VIA_REV_PRE_8233	0x10	/* not in market */
#define VIA_REV_8233C		0x20	/* 2 rec, 4 pb, 1 multi-pb */
#define VIA_REV_8233		0x30	/* 2 rec, 4 pb, 1 multi-pb, spdif */
#define VIA_REV_8233A		0x40	/* 1 rec, 1 multi-pb, spdf */
#define VIA_REV_8235		0x50	/* 2 rec, 4 pb, 1 multi-pb, spdif */

#define VIAREG(via, x) ((via)->port + VIA_REG_##x)

/* offsets */
#define VIA_REG_OFFSET_STATUS		0x00	/* byte - channel status */
#define   VIA_REG_STAT_ACTIVE		0x80	/* RO */
#define   VIA_REG_STAT_TRIGGER_QUEUED	0x08	/* RO */
#define   VIA_REG_STAT_STOPPED		0x04	/* RWC */
#define   VIA_REG_STAT_EOL		0x02	/* RWC */
#define   VIA_REG_STAT_FLAG		0x01	/* RWC */
#define VIA_REG_OFFSET_CONTROL		0x01	/* byte - channel control */
#define   VIA_REG_CTRL_START		0x80	/* WO */
#define   VIA_REG_CTRL_TERMINATE	0x40	/* WO */
#define   VIA_REG_CTRL_AUTOSTART	0x20
#define   VIA_REG_CTRL_PAUSE		0x08	/* RW */
#define   VIA_REG_CTRL_INT_STOP		0x04		
#define   VIA_REG_CTRL_INT_EOL		0x02
#define   VIA_REG_CTRL_INT_FLAG		0x01
#define   VIA_REG_CTRL_INT (VIA_REG_CTRL_INT_FLAG | VIA_REG_CTRL_INT_EOL | VIA_REG_CTRL_AUTOSTART)
#define VIA_REG_OFFSET_TABLE_PTR	0x04	/* dword - channel table pointer */
#define VIA_REG_OFFSET_CURR_PTR		0x04	/* dword - channel current pointer */
#define VIA_REG_OFFSET_STOP_IDX		0x08	/* dword - stop index, channel type, sample rate */
#define   VIA_REG_TYPE_16BIT		0x00200000	/* RW */
#define   VIA_REG_TYPE_STEREO		0x00100000	/* RW */
#define VIA_REG_OFFSET_CURR_COUNT	0x0c	/* dword - channel current count (24 bit) */
#define VIA_REG_OFFSET_CURR_INDEX	0x0f	/* byte - channel current index */

#define VIA_NUM_OF_DMA_CHANNELS	2

/* playback block (VT8233/8233C) - channels 0-3 (0-0x3f) */
#define VIA_REG_PLAYBACK_STATUS		0x00	/* byte - channel status */
#define VIA_REG_PLAYBACK_CONTROL	0x01	/* byte - channel control */
#define VIA_REG_PLAYBACK_VOLUME_L	0x02	/* byte */
#define VIA_REG_PLAYBACK_VOLUME_R	0x03	/* byte */
#define VIA_REG_PLAYBACK_TABLE_PTR	0x04	/* dword - channel table pointer */
#define VIA_REG_PLAYBACK_CURR_PTR	0x04	/* dword - channel current pointer */
#define VIA_REG_PLAYBACK_STOP_IDX	0x08    /* dword - stop index, channel type, sample rate */
#define VIA_REG_PLAYBACK_CURR_COUNT	0x0c	/* dword - channel current count (24 bit) */
#define VIA_REG_PLAYBACK_CURR_INDEX	0x0f	/* byte - channel current index */

/* multi-channel playback */
#define VIA_REG_MULTPLAY_STATUS		0x40	/* byte - channel status */
#define VIA_REG_MULTPLAY_CONTROL	0x41	/* byte - channel control */
#define VIA_REG_MULTPLAY_FORMAT		0x42	/* byte - format and channels */
#define   VIA_REG_MULTPLAY_FMT_8BIT	0x00
#define   VIA_REG_MULTPLAY_FMT_16BIT	0x80
#define   VIA_REG_MULTPLAY_FMT_CH_MASK	0x70	/* # channels << 4 (valid = 1,2,4,6) */
#define VIA_REG_MULTPLAY_SCRATCH	0x43	/* byte - nop */
#define VIA_REG_MULTPLAY_TABLE_PTR	0x44	/* dword - channel table pointer */
#define VIA_REG_MULTPLAY_CURR_PTR	0x44	/* dword - channel current pointer */
#define VIA_REG_MULTPLAY_STOP_IDX	0x48    /* dword - stop index, slots */
#define VIA_REG_MULTPLAY_CURR_COUNT	0x4c	/* dword - channel current count (24 bit) */
#define VIA_REG_MULTIPLAY_CURR_INDEX	0x4f	/* byte - channel current index */

/* capture block */
#define VIA_REG_CAPTURE_STATUS		0x60	/* byte - channel status */
#define VIA_REG_CAPTURE_CONTROL		0x61	/* byte - channel control */
#define VIA_REG_CAPTURE_FIFO		0x62	/* byte - bit 6 = fifo  enable */
#define   VIA_REG_CAPTURE_FIFO_ENABLE	0x40
#define VIA_REG_CAPTURE_CHANNEL		0x63	/* byte - input select */
#define   VIA_REG_CAPTURE_CHANNEL_MIC	0x4
#define   VIA_REG_CAPTURE_CHANNEL_LINE	0
#define   VIA_REG_CAPTURE_SELECT_CODEC	0x03	/* recording source codec (0 = primary) */
#define VIA_REG_CAPTURE_TABLE_PTR	0x64	/* dword - channel table pointer */
#define VIA_REG_CAPTURE_CURR_PTR	0x64	/* dword - channel current pointer */
#define VIA_REG_CAPTURE_STOP_IDX	0x68	/* dword - stop index */
#define VIA_REG_CAPTURE_CURR_COUNT	0x6c	/* dword - channel current count (24 bit) */
#define VIA_REG_CAPTURE_CURR_INDEX	0x6f	/* byte - channel current index */
/* AC'97 */
#define VIA_REG_AC97			0x80	/* dword */
#define   VIA_REG_AC97_CODEC_ID_MASK	(3<<30)
#define   VIA_REG_AC97_CODEC_ID_SHIFT	30
#define   VIA_REG_AC97_SECONDARY_VALID	(1<<27)
#define   VIA_REG_AC97_PRIMARY_VALID	(1<<25)
#define   VIA_REG_AC97_ANY_VALID	(VIA_REG_AC97_PRIMARY_VALID | VIA_REG_AC97_SECONDARY_VALID | (1<<28)| (1<<29))
#define   VIA_REG_AC97_BUSY		(1<<24)
#define   VIA_REG_AC97_READ		(1<<23)
#define   VIA_REG_AC97_CMD_SHIFT	16
#define   VIA_REG_AC97_CMD_MASK		0x7e
#define   VIA_REG_AC97_DATA_SHIFT	0
#define   VIA_REG_AC97_DATA_MASK	0xffff
#define VIA_REG_SGD_SHADOW		0x84	/* dword */

#define VIA_TBL_BIT_FLAG	0x40000000
#define VIA_TBL_BIT_EOL		0x80000000

/*
 *  
 */

typedef struct {
	unsigned long reg_offset;
        snd_pcm_substream_t *substream;
	int running;
        unsigned int size;
        unsigned int fragsize;
	unsigned int frags;
	unsigned int page_per_frag;
	unsigned int curidx;
	unsigned int tbl_entries;	/* number of descriptor table entries */
	unsigned int tbl_size;		/* size of a table entry */
	u32 *table; /* physical address + flag */
	dma_addr_t table_addr;
} viadev_t;

static int build_via_table(viadev_t *dev, snd_pcm_substream_t *substream,
			   struct pci_dev *pci)
{
	int i, size;
	struct snd_sg_buf *sgbuf = snd_magic_cast(snd_pcm_sgbuf_t, substream->dma_private, return -EINVAL);

	if (dev->table) {
		snd_free_pci_pages(pci, PAGE_ALIGN(dev->tbl_entries * 8), dev->table, dev->table_addr);
		dev->table = NULL;
	}

	/* allocate buffer descriptor lists */
	if (dev->fragsize < PAGE_SIZE) {
		dev->tbl_size = dev->fragsize;
		dev->tbl_entries = dev->frags;
		dev->page_per_frag = 1;
	} else {
		dev->tbl_size = PAGE_SIZE;
		dev->tbl_entries = sgbuf->pages;
		dev->page_per_frag = dev->fragsize >> PAGE_SHIFT;
	}
	/* the start of each lists must be aligned to 8 bytes,
	 * but the kernel pages are much bigger, so we don't care
	 */
	dev->table = (u32*)snd_malloc_pci_pages(pci, PAGE_ALIGN(dev->tbl_entries * 8), &dev->table_addr);
	if (! dev->table)
		return -ENOMEM;

	if (dev->tbl_size < PAGE_SIZE) {
		for (i = 0; i < dev->tbl_entries; i++)
			dev->table[i << 1] = cpu_to_le32((u32)sgbuf->table[0].addr + dev->fragsize * i);
	} else {
		for (i = 0; i < dev->tbl_entries; i++)
			dev->table[i << 1] = cpu_to_le32((u32)sgbuf->table[i].addr);
	}
	size = dev->size;
	for (i = 0; i < dev->tbl_entries - 1; i++) {
		dev->table[(i << 1) + 1] = cpu_to_le32(VIA_TBL_BIT_FLAG | dev->tbl_size);
		size -= dev->tbl_size;
	}
	dev->table[(dev->tbl_entries << 1) - 1] = cpu_to_le32(VIA_TBL_BIT_EOL | size);

	return 0;
}


static void clean_via_table(viadev_t *dev, snd_pcm_substream_t *substream,
			    struct pci_dev *pci)
{
	if (dev->table) {
		snd_free_pci_pages(pci, PAGE_ALIGN(dev->tbl_entries * 8), dev->table, dev->table_addr);
		dev->table = NULL;
	}
}


/*
 */

typedef struct _snd_via8233 via8233_t;
#define chip_t via8233_t

struct _snd_via8233 {
	int irq;

	unsigned long port;
	struct resource *res_port;
	unsigned char revision;

	struct pci_dev *pci;
	snd_card_t *card;

	snd_pcm_t *pcm;
	viadev_t playback;
	viadev_t capture;

	ac97_t *ac97;
	unsigned int ac97_clock;

	spinlock_t reg_lock;
	snd_info_entry_t *proc_entry;
};

static struct pci_device_id snd_via8233_ids[] __devinitdata = {
	{ 0x1106, 0x3059, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* VT8233 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_via8233_ids);

/*
 *  Basic I/O
 */

static inline unsigned int snd_via8233_codec_xread(via8233_t *chip)
{
	/* this acces should be atomic */
	return inl(VIAREG(chip, AC97));
}
 
static inline void snd_via8233_codec_xwrite(via8233_t *chip, unsigned int val)
{
	/* this acces should be atomic */
	outl(val, VIAREG(chip, AC97));
}
 
static int snd_via8233_codec_ready(via8233_t *chip, int secondary)
{
	int time;

	time = 1000;
	do {
		udelay(1);
		if ((snd_via8233_codec_xread(chip) & VIA_REG_AC97_BUSY) == 0)
			return 0;
	} while (time--);
	snd_printk("codec_ready: codec %i is not ready [0x%x]\n", secondary, snd_via8233_codec_xread(chip));
	return -EIO;
}
 
static void snd_via8233_codec_write(ac97_t *ac97,
				    unsigned short reg,
				    unsigned short val)
{
	via8233_t *chip = snd_magic_cast(via8233_t, ac97->private_data, return);
	unsigned int xval;
	
	xval = (ac97->num) << VIA_REG_AC97_CODEC_ID_SHIFT;
	xval |= ac97->num ? VIA_REG_AC97_SECONDARY_VALID : VIA_REG_AC97_PRIMARY_VALID;
	xval |= (reg & 0x7f) << VIA_REG_AC97_CMD_SHIFT;
	xval |= val << VIA_REG_AC97_DATA_SHIFT;
	spin_lock(&chip->reg_lock);
	snd_via8233_codec_ready(chip, ac97->num);
	snd_via8233_codec_xwrite(chip, xval);
	snd_via8233_codec_ready(chip, ac97->num);
	spin_unlock(&chip->reg_lock);
}

static unsigned short snd_via8233_codec_read(ac97_t *ac97, unsigned short reg)
{
	via8233_t *chip = snd_magic_cast(via8233_t, ac97->private_data, return ~0);
	unsigned int val;
	int valid = ac97->num ? VIA_REG_AC97_SECONDARY_VALID : VIA_REG_AC97_PRIMARY_VALID;
	int i;
	
	val = (ac97->num) << VIA_REG_AC97_CODEC_ID_SHIFT;
	val |= valid;
	val |= VIA_REG_AC97_READ;
	val |= (reg & 0x7f) << VIA_REG_AC97_CMD_SHIFT;
	spin_lock(&chip->reg_lock);
	snd_via8233_codec_ready(chip, ac97->num);
	snd_via8233_codec_xwrite(chip, val);
	snd_via8233_codec_ready(chip, ac97->num);
	for (i=1000; i--;) {
		val = snd_via8233_codec_xread(chip);
		if (val & valid) {
			spin_unlock(&chip->reg_lock);
			return (unsigned short)val;
		}
	}
	spin_unlock(&chip->reg_lock);
	snd_printk("codec_read: codec %i is not valid [0x%x]\n", ac97->num, val);
	/* have to return some value, this is better then 0 */
	return ~0;
}

#if 0
static void snd_via8233_channel_print(via8233_t *chip, viadev_t *viadev)
{
	unsigned long port = chip->port + viadev->reg_offset;

	printk("[0x%x] status = 0x%x, control = 0x%x, type = 0x%x, ptr = 0x%x, count = 0x%x\n",
			port,
			inb(port + VIA_REG_OFFSET_STATUS),
			inb(port + VIA_REG_OFFSET_CONTROL),
			inl(port + VIA_REG_OFFSET_STOP_IDX),
			inl(port + VIA_REG_OFFSET_CURR_PTR),
			inl(port + VIA_REG_OFFSET_CURR_COUNT));
}
#endif

static void snd_via8233_channel_reset(via8233_t *chip, viadev_t *viadev)
{
	unsigned long port = chip->port + viadev->reg_offset;

	outb(VIA_REG_CTRL_TERMINATE, port + VIA_REG_OFFSET_CONTROL);
	udelay(50);
	/* disable interrupts */
	outb(0, port + VIA_REG_OFFSET_CONTROL);
	/* clear interrupts */
	outb(0x3, port + VIA_REG_OFFSET_STATUS);
}

static int snd_via8233_trigger(via8233_t *chip, viadev_t *viadev, int cmd)
{
	unsigned char val;
	unsigned long port = chip->port + viadev->reg_offset;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		val = VIA_REG_CTRL_INT | VIA_REG_CTRL_START;
		viadev->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		val = VIA_REG_CTRL_TERMINATE;
		viadev->running = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val = VIA_REG_CTRL_INT | VIA_REG_CTRL_PAUSE;
		viadev->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		val = VIA_REG_CTRL_INT;
		viadev->running = 0;
		break;
	default:
		return -EINVAL;
	}
	outb(val, port + VIA_REG_OFFSET_CONTROL);
	if (cmd == SNDRV_PCM_TRIGGER_STOP)
		snd_via8233_channel_reset(chip, viadev);
	return 0;
}

static int snd_via8233_setup_periods(via8233_t *chip, viadev_t *viadev,
				     snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long port = chip->port + viadev->reg_offset;
	int v, err;

	viadev->size = snd_pcm_lib_buffer_bytes(substream);
	viadev->fragsize = snd_pcm_lib_period_bytes(substream);
	viadev->frags = runtime->periods;
	viadev->curidx = 0;

	/* the period size must be in power of 2 */
	v = ld2(viadev->fragsize);
	if (viadev->fragsize != (1 << v)) {
		snd_printd(KERN_ERR "invalid fragment size %d\n", viadev->fragsize);
		return -EINVAL;
	}

	snd_via8233_channel_reset(chip, viadev);

	err = build_via_table(viadev, substream, chip->pci);
	if (err < 0)
		return err;

	runtime->dma_bytes = viadev->size;
	outl((u32)viadev->table_addr, port + VIA_REG_OFFSET_TABLE_PTR);
	if (viadev->reg_offset == VIA_REG_MULTPLAY_STATUS) {
		unsigned int slots;
		int fmt = (runtime->format == SNDRV_PCM_FORMAT_S16_LE) ? VIA_REG_MULTPLAY_FMT_16BIT : VIA_REG_MULTPLAY_FMT_8BIT;
		fmt |= runtime->channels << 4;
		outb(fmt, chip->port + VIA_REG_MULTPLAY_FORMAT);
		/* set sample number to slot 3, 4, 7, 8, 6, 9 */
		switch (runtime->channels) {
		case 1: slots = (1<<0) | (1<<4); break;
		case 2: slots = (1<<0) | (2<<4); break;
		case 4: slots = (1<<0) | (2<<4) | (3<<8) | (4<<12); break;
		case 6: slots = (1<<0) | (2<<4) | (5<<8) | (6<<12) | (3<<16) | (4<<20); break;
		default: slots = 0; break;
		}
		/* STOP index is never reached */
		outl(0xff000000 | slots, chip->port + VIA_REG_MULTPLAY_STOP_IDX);
	} else {
		outl((runtime->format == SNDRV_PCM_FORMAT_S16_LE ? VIA_REG_TYPE_16BIT : 0) |
		     (runtime->channels > 1 ? VIA_REG_TYPE_STEREO : 0) |
		     0xff000000,    /* STOP index is never reached */
		     port + VIA_REG_OFFSET_STOP_IDX);
	}
	return 0;
}

/*
 *  Interrupt handler
 */

static inline void snd_via8233_update(via8233_t *chip, viadev_t *viadev)
{
	outb(VIA_REG_STAT_FLAG | VIA_REG_STAT_EOL, VIAREG(chip, OFFSET_STATUS) + viadev->reg_offset);
	if (viadev->substream && viadev->running) {
		viadev->curidx++;
		if (viadev->curidx >= viadev->page_per_frag) {
			viadev->curidx = 0;
			spin_unlock(&chip->reg_lock);
			snd_pcm_period_elapsed(viadev->substream);
			spin_lock(&chip->reg_lock);
		}
	}
}

static void snd_via8233_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	via8233_t *chip = snd_magic_cast(via8233_t, dev_id, return);

	spin_lock(&chip->reg_lock);
	if (inb(chip->port + chip->playback.reg_offset) & (VIA_REG_STAT_EOL|VIA_REG_STAT_FLAG))
		snd_via8233_update(chip, &chip->playback);
	if (inb(chip->port + chip->capture.reg_offset) & (VIA_REG_STAT_EOL|VIA_REG_STAT_FLAG))
		snd_via8233_update(chip, &chip->capture);
	spin_unlock(&chip->reg_lock);
}

/*
 *  PCM part
 */

static int snd_via8233_playback_trigger(snd_pcm_substream_t * substream,
					int cmd)
{
	via8233_t *chip = snd_pcm_substream_chip(substream);

	return snd_via8233_trigger(chip, &chip->playback, cmd);
}

static int snd_via8233_capture_trigger(snd_pcm_substream_t * substream,
				       int cmd)
{
	via8233_t *chip = snd_pcm_substream_chip(substream);
	return snd_via8233_trigger(chip, &chip->capture, cmd);
}

static int snd_via8233_hw_params(snd_pcm_substream_t * substream,
					  snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_sgbuf_alloc(substream, params_buffer_bytes(hw_params));
}

static int snd_via8233_hw_free(snd_pcm_substream_t * substream)
{
	return 0;
}

static int snd_via8233_playback_prepare(snd_pcm_substream_t * substream)
{
	via8233_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_ac97_set_rate(chip->ac97, AC97_PCM_FRONT_DAC_RATE, runtime->rate);
	if (chip->playback.reg_offset != VIA_REG_MULTPLAY_STATUS) {
		unsigned int tmp;
		/* I don't understand this stuff but its from the documentation and this way it works */
		outb(0 , VIAREG(chip, PLAYBACK_VOLUME_L));
		outb(0 , VIAREG(chip, PLAYBACK_VOLUME_R));
		tmp = inl(VIAREG(chip, PLAYBACK_STOP_IDX)) & ~0xfffff;
		outl(tmp | (0xffff * runtime->rate)/(48000/16), VIAREG(chip, PLAYBACK_STOP_IDX));
	}
	return snd_via8233_setup_periods(chip, &chip->playback, substream);
}

static int snd_via8233_capture_prepare(snd_pcm_substream_t * substream)
{
	via8233_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_ac97_set_rate(chip->ac97, AC97_PCM_LR_ADC_RATE, runtime->rate);
	outb(VIA_REG_CAPTURE_CHANNEL_LINE, VIAREG(chip, CAPTURE_CHANNEL));
	outb(VIA_REG_CAPTURE_FIFO_ENABLE, VIAREG(chip, CAPTURE_FIFO));
	return snd_via8233_setup_periods(chip, &chip->capture, substream);
}

static inline unsigned int snd_via8233_cur_ptr(via8233_t *chip, viadev_t *viadev)
{
	unsigned int val, count;

	count = inl(VIAREG(chip, OFFSET_CURR_COUNT) + viadev->reg_offset) & 0xffffff;
	/* The via686a does not have this current index register,
	 * this register makes life easier for us here. */
	val = inb(VIAREG(chip, OFFSET_CURR_INDEX) + viadev->reg_offset) % viadev->tbl_entries;
	if (val < viadev->tbl_entries - 1) {
		val *= viadev->tbl_size;
		val += viadev->tbl_size - count;
	} else {
		val *= viadev->tbl_size;
		val += (viadev->size % viadev->tbl_size) + 1 - count;
	}
	// printk("pointer: ptr = 0x%x, count = 0x%x, val = 0x%x\n", ptr, count, val);
	return val;
}

static snd_pcm_uframes_t snd_via8233_playback_pointer(snd_pcm_substream_t * substream)
{
	via8233_t *chip = snd_pcm_substream_chip(substream);
	return bytes_to_frames(substream->runtime, snd_via8233_cur_ptr(chip, &chip->playback));
}

static snd_pcm_uframes_t snd_via8233_capture_pointer(snd_pcm_substream_t * substream)
{
	via8233_t *chip = snd_pcm_substream_chip(substream);
	return bytes_to_frames(substream->runtime, snd_via8233_cur_ptr(chip, &chip->capture));
}

static snd_pcm_hardware_t snd_via8233_playback =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			0,
	rate_min:		8000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		6,
	buffer_bytes_max:	128 * 1024,
	period_bytes_min:	32,
	period_bytes_max:	128 * 1024,
	periods_min:		2,
	periods_max:		1024,
	fifo_size:		0,
};

static snd_pcm_hardware_t snd_via8233_capture =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	formats:		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	rates:			0,
	rate_min:		8000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		2,
	buffer_bytes_max:	128 * 1024,
	period_bytes_min:	32,
	period_bytes_max:	128 * 1024,
	periods_min:		2,
	periods_max:		1024,
	fifo_size:		0,
};

static unsigned int channels[] = {
	1, 2, 4, 6
};

#define CHANNELS sizeof(channels) / sizeof(channels[0])

static snd_pcm_hw_constraint_list_t hw_constraints_channels = {
	count: CHANNELS,
	list: channels,
	mask: 0,
};

static int snd_via8233_playback_open(snd_pcm_substream_t * substream)
{
	via8233_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	chip->playback.substream = substream;
	runtime->hw = snd_via8233_playback;
	runtime->hw.rates = chip->ac97->rates_front_dac;
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	if ((err = snd_pcm_sgbuf_init(substream, chip->pci, 32)) < 0)
		return err;
	if ((err = snd_pcm_hw_constraint_pow2(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES)) < 0)
		return err;
#if 0
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
#endif
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, &hw_constraints_channels);
	return 0;
}

static int snd_via8233_capture_open(snd_pcm_substream_t * substream)
{
	via8233_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	chip->capture.substream = substream;
	runtime->hw = snd_via8233_capture;
	runtime->hw.rates = chip->ac97->rates_adc;
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	if ((err = snd_pcm_sgbuf_init(substream, chip->pci, 32)) < 0)
		return err;
	if ((err = snd_pcm_hw_constraint_pow2(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES)) < 0)
		return err;
#if 0
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
#endif
	return 0;
}

static int snd_via8233_playback_close(snd_pcm_substream_t * substream)
{
	via8233_t *chip = snd_pcm_substream_chip(substream);

	snd_via8233_channel_reset(chip, &chip->playback);
	clean_via_table(&chip->playback, substream, chip->pci);
	snd_pcm_sgbuf_delete(substream);
	chip->playback.substream = NULL;
	/* disable DAC power */
	snd_ac97_update_bits(chip->ac97, AC97_POWERDOWN, 0x0200, 0x0200);
	return 0;
}

static int snd_via8233_capture_close(snd_pcm_substream_t * substream)
{
	via8233_t *chip = snd_pcm_substream_chip(substream);

	snd_via8233_channel_reset(chip, &chip->capture);
	clean_via_table(&chip->capture, substream, chip->pci);
	snd_pcm_sgbuf_delete(substream);
	chip->capture.substream = NULL;
	/* disable ADC power */
	snd_ac97_update_bits(chip->ac97, AC97_POWERDOWN, 0x0100, 0x0100);
	return 0;
}

static snd_pcm_ops_t snd_via8233_playback_ops = {
	open:		snd_via8233_playback_open,
	close:		snd_via8233_playback_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_via8233_hw_params,
	hw_free:	snd_via8233_hw_free,
	prepare:	snd_via8233_playback_prepare,
	trigger:	snd_via8233_playback_trigger,
	pointer:	snd_via8233_playback_pointer,
	copy:           snd_pcm_sgbuf_ops_copy_playback,
	silence:        snd_pcm_sgbuf_ops_silence,
	page:           snd_pcm_sgbuf_ops_page,
};

static snd_pcm_ops_t snd_via8233_capture_ops = {
	open:		snd_via8233_capture_open,
	close:		snd_via8233_capture_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_via8233_hw_params,
	hw_free:	snd_via8233_hw_free,
	prepare:	snd_via8233_capture_prepare,
	trigger:	snd_via8233_capture_trigger,
	pointer:	snd_via8233_capture_pointer,
	copy:           snd_pcm_sgbuf_ops_copy_capture,
	silence:        snd_pcm_sgbuf_ops_silence,
	page:           snd_pcm_sgbuf_ops_page,
};

static void snd_via8233_pcm_free(snd_pcm_t *pcm)
{
	via8233_t *chip = snd_magic_cast(via8233_t, pcm->private_data, return);
	chip->pcm = NULL;
}

static int __devinit snd_via8233_pcm(via8233_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, "VIA 8233", device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_via8233_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_via8233_capture_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_via8233_pcm_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, "VIA 8233");
	chip->pcm = pcm;

	if (rpcm)
		*rpcm = NULL;
	return 0;
}

/*
 *  Mixer part
 */

static void snd_via8233_codec_init(ac97_t *ac97)
{
	// via8233_t *chip = snd_magic_cast(via8233_t, ac97->private_data, return);

	/* disable DAC & ADC power */
	snd_ac97_update_bits(ac97, AC97_POWERDOWN, 0x0300, 0x0300);
	/* disable center DAC/surround DAC/LFE DAC/MIC ADC */
	snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, 0xe800, 0xe800);
}

static void snd_via8233_mixer_free_ac97(ac97_t *ac97)
{
	via8233_t *chip = snd_magic_cast(via8233_t, ac97->private_data, return);
	chip->ac97 = NULL;
}

static int __devinit snd_via8233_mixer(via8233_t *chip)
{
	ac97_t ac97;
	int err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.write = snd_via8233_codec_write;
	ac97.read = snd_via8233_codec_read;
	ac97.init = snd_via8233_codec_init;
	ac97.private_data = chip;
	ac97.private_free = snd_via8233_mixer_free_ac97;
	ac97.clock = chip->ac97_clock;
	if ((err = snd_ac97_mixer(chip->card, &ac97, &chip->ac97)) < 0)
		return err;
	return 0;
}

/*
 *
 */

static int __devinit snd_via8233_chip_init(via8233_t *chip)
{
	ac97_t ac97;
	unsigned char stat;
	int i;
	
	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;

	/* deassert ACLink reset */
	pci_write_config_byte(chip->pci, 0x41, 0x40);
	udelay(100);
	/* deassert ACLink reset, force SYNC (warm AC'97 reset) */
	pci_write_config_byte(chip->pci, 0x41, 0x60);
	udelay(2);
	/* ACLink on, deassert ACLink reset, VSR, SGD data out */
	pci_write_config_byte(chip->pci, 0x41, 0xcc);

	/* Wait for codec ready to be accessed. */
	for (i=HZ; i--; ) {
		pci_read_config_byte(chip->pci, 0x40, &stat);
		if (stat & 1)
			break;
		if (!i) {
			snd_printk("chip_init: failed to access primary codec.\n");
			return ~0;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	}
	snd_via8233_codec_ready(chip, 0);
	snd_via8233_codec_write(&ac97, AC97_RESET, 0x0000);
	snd_via8233_codec_read(&ac97, 0);

	/* disable interrupts */
	snd_via8233_channel_reset(chip, &chip->playback);
	snd_via8233_channel_reset(chip, &chip->capture);
	return 0;
}

static int snd_via8233_free(via8233_t *chip)
{
	if (chip->irq < 0)
		goto __end_hw;
	/* disable interrupts */
	snd_via8233_channel_reset(chip, &chip->playback);
	snd_via8233_channel_reset(chip, &chip->capture);
	/* --- */
	synchronize_irq(chip->irq);
      __end_hw:
	if (chip->res_port) {
		release_resource(chip->res_port);
		kfree_nocheck(chip->res_port);
	}
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	snd_magic_kfree(chip);
	return 0;
}

static int snd_via8233_dev_free(snd_device_t *device)
{
	via8233_t *chip = snd_magic_cast(via8233_t, device->device_data, return -ENXIO);
	return snd_via8233_free(chip);
}

static int __devinit snd_via8233_create(snd_card_t * card,
				     struct pci_dev *pci,
				     unsigned int ac97_clock,
				     via8233_t ** r_via)
{
	via8233_t *chip;
	int err;
        static snd_device_ops_t ops = {
		dev_free:	snd_via8233_dev_free,
        };

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	if ((chip = snd_magic_kcalloc(via8233_t, 0, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	spin_lock_init(&chip->reg_lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->port = pci_resource_start(pci, 0);
	if ((chip->res_port = request_region(chip->port, 256, "VIA8233")) == NULL) {
		snd_via8233_free(chip);
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", chip->port, chip->port + 256 - 1);
		return -EBUSY;
	}
	if (request_irq(pci->irq, snd_via8233_interrupt, SA_INTERRUPT|SA_SHIRQ, "VIA8233", (void *)chip)) {
		snd_via8233_free(chip);
		snd_printk("unable to grab IRQ %d\n", chip->irq);
		return -EBUSY;
	}
	chip->irq = pci->irq;
	if (ac97_clock >= 8000 && ac97_clock <= 48000)
		chip->ac97_clock = ac97_clock;
	pci_read_config_byte(pci, PCI_REVISION_ID, &chip->revision);
	synchronize_irq(chip->irq);

	/* initialize offsets */
#if 0
	chip->playback.reg_offset = VIA_REG_PLAYBACK_STATUS; /* this doesn't work on VIA8233A */
#endif
	/* we use multi-channel playback mode, since this mode is supported
	 * by all VIA8233 models (and obviously suitable for our purpose).
	 */
	chip->playback.reg_offset = VIA_REG_MULTPLAY_STATUS;

	chip->capture.reg_offset = VIA_REG_CAPTURE_STATUS;

	if ((err = snd_via8233_chip_init(chip)) < 0) {
		snd_via8233_free(chip);
		return err;
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_via8233_free(chip);
		return err;
	}

	/* The 8233 ac97 controller does not implement the master bit
	 * in the pci command register. IMHO this is a violation of the PCI spec.
	 * We call pci_set_master here because it does not hurt. */
	pci_set_master(pci);

	*r_via = chip;
	return 0;
}

static int __devinit snd_via8233_probe(struct pci_dev *pci,
				       const struct pci_device_id *id)
{
	static int dev;
	snd_card_t *card;
	via8233_t *chip;
	int pcm_dev = 0;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!snd_enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	if ((err = snd_via8233_create(card,
				      pci,
				      snd_ac97_clock[dev],
				      &chip)) < 0) {
		snd_card_free(card);
		return err;
	}


	if (snd_via8233_mixer(chip) < 0) {
		snd_card_free(card);
		return err;
	}
	if (snd_via8233_pcm(chip, pcm_dev++, NULL) < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->driver, "VIA8233");
	strcpy(card->shortname, "VIA 8233");
	
	sprintf(card->longname, "%s at 0x%lx, irq %d",
		card->shortname, chip->port, chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_via8233_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	name: "VIA 8233",
	id_table: snd_via8233_ids,
	probe: snd_via8233_probe,
	remove: __devexit_p(snd_via8233_remove),
};

static int __init alsa_card_via8233_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		printk(KERN_ERR "VIA 8233 soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_via8233_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_via8233_init)
module_exit(alsa_card_via8233_exit)

#ifndef MODULE

/* format is: snd-via8233=snd_enable,snd_index,snd_id,snd_ac97_clock */

static int __init alsa_card_via8233_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,&snd_ac97_clock[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-via8233=", alsa_card_via8233_setup);

#endif /* ifndef MODULE */
