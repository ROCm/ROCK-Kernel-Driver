/*
 *   ALSA driver for VIA VT82xx (South Bridge)
 *
 *   VT82C686A/B/C, VT8233A/C, VT8235
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
 *	                   Tjeerd.Mulder <Tjeerd.Mulder@fujitsu-siemens.com>
 *                    2002 Takashi Iwai <tiwai@suse.de>
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

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("VIA VT82xx audio");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{VIA,VT82C686A/B/C,pci},{VIA,VT8233A/B/C}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static long snd_mpu_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1};
static int snd_ac97_clock[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 48000};

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for VIA 82xx bridge.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for VIA 82xx bridge.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable audio part of VIA 82xx bridge.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_mpu_port, "MPU-401 port.");
MODULE_PARM_SYNTAX(snd_mpu_port, SNDRV_PORT_DESC);
MODULE_PARM(snd_ac97_clock, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_ac97_clock, "AC'97 codec clock (default 48000Hz).");
MODULE_PARM_SYNTAX(snd_ac97_clock, SNDRV_ENABLED ",default:48000");


/* pci ids */
#ifndef PCI_DEVICE_ID_VIA_82C686_5
#define PCI_DEVICE_ID_VIA_82C686_5	0x3058
#endif
#ifndef PCI_DEVICE_ID_VIA_8233_5
#define PCI_DEVICE_ID_VIA_8233_5	0x3059
#endif

/* revision numbers for via8233 */
#define VIA_REV_PRE_8233	0x10	/* not in market */
#define VIA_REV_8233C		0x20	/* 2 rec, 4 pb, 1 multi-pb */
#define VIA_REV_8233		0x30	/* 2 rec, 4 pb, 1 multi-pb, spdif */
#define VIA_REV_8233A		0x40	/* 1 rec, 1 multi-pb, spdf */
#define VIA_REV_8235		0x50	/* 2 rec, 4 pb, 1 multi-pb, spdif */

/*
 *  Direct registers
 */

#define VIAREG(via, x) ((via)->port + VIA_REG_##x)

/* common offsets */
#define VIA_REG_OFFSET_STATUS		0x00	/* byte - channel status */
#define   VIA_REG_STAT_ACTIVE		0x80	/* RO */
#define   VIA_REG_STAT_PAUSED		0x40	/* RO */
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
#define   VIA_REG_CTRL_RESET		0x01	/* RW - probably reset? undocumented */
#define   VIA_REG_CTRL_INT (VIA_REG_CTRL_INT_FLAG | VIA_REG_CTRL_INT_EOL | VIA_REG_CTRL_AUTOSTART)
#define VIA_REG_OFFSET_TYPE		0x02	/* byte - channel type (686 only) */
#define   VIA_REG_TYPE_AUTOSTART	0x80	/* RW - autostart at EOL */
#define   VIA_REG_TYPE_16BIT		0x20	/* RW */
#define   VIA_REG_TYPE_STEREO		0x10	/* RW */
#define   VIA_REG_TYPE_INT_LLINE	0x00
#define   VIA_REG_TYPE_INT_LSAMPLE	0x04
#define   VIA_REG_TYPE_INT_LESSONE	0x08
#define   VIA_REG_TYPE_INT_MASK		0x0c
#define   VIA_REG_TYPE_INT_EOL		0x02
#define   VIA_REG_TYPE_INT_FLAG		0x01
#define VIA_REG_OFFSET_TABLE_PTR	0x04	/* dword - channel table pointer */
#define VIA_REG_OFFSET_CURR_PTR		0x04	/* dword - channel current pointer */
#define VIA_REG_OFFSET_STOP_IDX		0x08	/* dword - stop index, channel type, sample rate */
#define   VIA8233_REG_TYPE_16BIT	0x00200000	/* RW */
#define   VIA8233_REG_TYPE_STEREO	0x00100000	/* RW */
#define VIA_REG_OFFSET_CURR_COUNT	0x0c	/* dword - channel current count (24 bit) */
#define VIA_REG_OFFSET_CURR_INDEX	0x0f	/* byte - channel current index (for via8233 only) */

#define DEFINE_VIA_REGSET(name,val) \
enum {\
	VIA_REG_##name##_STATUS		= (val),\
	VIA_REG_##name##_CONTROL	= (val) + 0x01,\
	VIA_REG_##name##_TYPE		= (val) + 0x02,\
	VIA_REG_##name##_TABLE_PTR	= (val) + 0x04,\
	VIA_REG_##name##_CURR_PTR	= (val) + 0x04,\
	VIA_REG_##name##_STOP_IDX	= (val) + 0x08,\
	VIA_REG_##name##_CURR_COUNT	= (val) + 0x0c,\
}

/* playback block */
DEFINE_VIA_REGSET(PLAYBACK, 0x00);
DEFINE_VIA_REGSET(CAPTURE, 0x10);
DEFINE_VIA_REGSET(FM, 0x20);

/* AC'97 */
#define VIA_REG_AC97			0x80	/* dword */
#define   VIA_REG_AC97_CODEC_ID_MASK	(3<<30)
#define   VIA_REG_AC97_CODEC_ID_SHIFT	30
#define   VIA_REG_AC97_CODEC_ID_PRIMARY	0x00
#define   VIA_REG_AC97_CODEC_ID_SECONDARY 0x01
#define   VIA_REG_AC97_SECONDARY_VALID	(1<<27)
#define   VIA_REG_AC97_PRIMARY_VALID	(1<<25)
#define   VIA_REG_AC97_BUSY		(1<<24)
#define   VIA_REG_AC97_READ		(1<<23)
#define   VIA_REG_AC97_CMD_SHIFT	16
#define   VIA_REG_AC97_CMD_MASK		0x7e
#define   VIA_REG_AC97_DATA_SHIFT	0
#define   VIA_REG_AC97_DATA_MASK	0xffff
#define VIA_REG_SGD_SHADOW		0x84	/* dword */

/* multi-channel and capture registers for via8233 */
DEFINE_VIA_REGSET(MULTPLAY, 0x40);
DEFINE_VIA_REGSET(CAPTURE_8233, 0x60);

/* via8233-specific registers */
#define VIA_REG_PLAYBACK_VOLUME_L	0x02	/* byte */
#define VIA_REG_PLAYBACK_VOLUME_R	0x03	/* byte */
#define VIA_REG_MULTPLAY_FORMAT		0x42	/* byte - format and channels */
#define   VIA_REG_MULTPLAY_FMT_8BIT	0x00
#define   VIA_REG_MULTPLAY_FMT_16BIT	0x80
#define   VIA_REG_MULTPLAY_FMT_CH_MASK	0x70	/* # channels << 4 (valid = 1,2,4,6) */
#define VIA_REG_CAPTURE_FIFO		0x62	/* byte - bit 6 = fifo  enable */
#define   VIA_REG_CAPTURE_FIFO_ENABLE	0x40
#define VIA_REG_CAPTURE_CHANNEL		0x63	/* byte - input select */
#define   VIA_REG_CAPTURE_CHANNEL_MIC	0x4
#define   VIA_REG_CAPTURE_CHANNEL_LINE	0
#define   VIA_REG_CAPTURE_SELECT_CODEC	0x03	/* recording source codec (0 = primary) */

#define VIA_TBL_BIT_FLAG	0x40000000
#define VIA_TBL_BIT_EOL		0x80000000

/*
 * pcm stream
 */

struct snd_via_sg_table {
	unsigned int offset;
	unsigned int size;
} ;

#define VIA_TABLE_SIZE	255

typedef struct {
	unsigned long reg_offset;
        snd_pcm_substream_t *substream;
	int running;
	unsigned int tbl_entries; /* # descriptors */
	u32 *table; /* physical address + flag */
	dma_addr_t table_addr;
	struct snd_via_sg_table *idx_table;
} viadev_t;


static int build_via_table(viadev_t *dev, snd_pcm_substream_t *substream,
			   struct pci_dev *pci)
{
	int i, idx, ofs, rest, fragsize;
	snd_pcm_runtime_t *runtime = substream->runtime;
	struct snd_sg_buf *sgbuf = snd_magic_cast(snd_pcm_sgbuf_t, substream->dma_private, return -EINVAL);

	if (! dev->table) {
		/* the start of each lists must be aligned to 8 bytes,
		 * but the kernel pages are much bigger, so we don't care
		 */
		dev->table = (u32*)snd_malloc_pci_pages(pci, PAGE_ALIGN(VIA_TABLE_SIZE * 2 * 8), &dev->table_addr);
		if (! dev->table)
			return -ENOMEM;
	}
	if (! dev->idx_table) {
		dev->idx_table = kmalloc(sizeof(unsigned int) * VIA_TABLE_SIZE, GFP_KERNEL);
		if (! dev->idx_table)
			return -ENOMEM;
	}

	/* fill the entries */
	fragsize = snd_pcm_lib_period_bytes(substream);
	idx = 0;
	ofs = 0;
	for (i = 0; i < runtime->periods; i++) {
		rest = fragsize;
		/* fill descriptors for a period.
		 * a period can be split to several descriptors if it's
		 * over page boundary.
		 */
		do {
			int r;
			unsigned int flag;
			dev->table[idx << 1] = cpu_to_le32((u32)snd_pcm_sgbuf_get_addr(sgbuf, ofs));
			r = PAGE_SIZE - (ofs % PAGE_SIZE);
			if (rest < r)
				r = rest;
			rest -= r;
			if (! rest) {
				if (i == runtime->periods - 1)
					flag = VIA_TBL_BIT_EOL; /* buffer boundary */
				else
					flag = VIA_TBL_BIT_FLAG; /* period boundary */
			} else
				flag = 0; /* period continues to the next */
			// printk("via: tbl %d: at %d  size %d (rest %d)\n", idx, ofs, r, rest);
			dev->table[(idx<<1) + 1] = cpu_to_le32(r | flag);
			dev->idx_table[idx].offset = ofs;
			dev->idx_table[idx].size = r;
			ofs += r;
			idx++;
			if (idx >= VIA_TABLE_SIZE) {
				snd_printk(KERN_ERR "via82xx: too much table size!\n");
				return -EINVAL;
			}
		} while (rest > 0);
	}
	dev->tbl_entries = idx;
	return 0;
}


static void clean_via_table(viadev_t *dev, snd_pcm_substream_t *substream,
			    struct pci_dev *pci)
{
	if (dev->table) {
		snd_free_pci_pages(pci, PAGE_ALIGN(dev->tbl_entries * 8), dev->table, dev->table_addr);
		dev->table = NULL;
	}
	if (dev->idx_table) {
		kfree(dev->idx_table);
		dev->idx_table = NULL;
	}
}


/*
 */

enum { TYPE_VIA686 = 1, TYPE_VIA8233 };

typedef struct _snd_via82xx via82xx_t;
#define chip_t via82xx_t

struct _snd_via82xx {
	int irq;

	unsigned long port;
	struct resource *res_port;
	int chip_type;
	unsigned char revision;

	unsigned char old_legacy;
	unsigned char old_legacy_cfg;

	struct pci_dev *pci;
	snd_card_t *card;

	snd_pcm_t *pcm;
	viadev_t playback;
	viadev_t capture;

	snd_rawmidi_t *rmidi;

	ac97_t *ac97;
	unsigned int ac97_clock;
	unsigned int ac97_secondary;	/* secondary AC'97 codec is present */

	spinlock_t reg_lock;
	spinlock_t ac97_lock;
	snd_info_entry_t *proc_entry;
};

static struct pci_device_id snd_via82xx_ids[] __devinitdata = {
	{ 0x1106, 0x3058, PCI_ANY_ID, PCI_ANY_ID, 0, 0, TYPE_VIA686, },	/* 686A */
	{ 0x1106, 0x3059, PCI_ANY_ID, PCI_ANY_ID, 0, 0, TYPE_VIA8233, },	/* VT8233 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_via82xx_ids);

/*
 *  Basic I/O
 */

static inline unsigned int snd_via82xx_codec_xread(via82xx_t *chip)
{
	return inl(VIAREG(chip, AC97));
}
 
static inline void snd_via82xx_codec_xwrite(via82xx_t *chip, unsigned int val)
{
	outl(val, VIAREG(chip, AC97));
}
 
static int snd_via82xx_codec_ready(via82xx_t *chip, int secondary)
{
	unsigned int timeout = 1000;	/* 1ms */
	unsigned int val;
	
	while (timeout-- > 0) {
		udelay(1);
		if (!((val = snd_via82xx_codec_xread(chip)) & VIA_REG_AC97_BUSY))
			return val & 0xffff;
	}
	snd_printk("codec_ready: codec %i is not ready [0x%x]\n", secondary, snd_via82xx_codec_xread(chip));
	return -EIO;
}
 
static int snd_via82xx_codec_valid(via82xx_t *chip, int secondary)
{
	unsigned int timeout = 1000;	/* 1ms */
	unsigned int val;
	unsigned int stat = !secondary ? VIA_REG_AC97_PRIMARY_VALID :
					 VIA_REG_AC97_SECONDARY_VALID;
	
	while (timeout-- > 0) {
		udelay(1);
		if ((val = snd_via82xx_codec_xread(chip)) & stat)
			return val & 0xffff;
	}
	snd_printk("codec_valid: codec %i is not valid [0x%x]\n", secondary, snd_via82xx_codec_xread(chip));
	return -EIO;
}
 
static void snd_via82xx_codec_wait(ac97_t *ac97)
{
	via82xx_t *chip = snd_magic_cast(via82xx_t, ac97->private_data, return);
	int err;
	err = snd_via82xx_codec_ready(chip, ac97->num);
	/* here we need to wait fairly for long time.. */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ/2);
}

static void snd_via82xx_codec_write(ac97_t *ac97,
				    unsigned short reg,
				    unsigned short val)
{
	via82xx_t *chip = snd_magic_cast(via82xx_t, ac97->private_data, return);
	unsigned int xval;
	
	xval = !ac97->num ? VIA_REG_AC97_CODEC_ID_PRIMARY : VIA_REG_AC97_CODEC_ID_SECONDARY;
	xval <<= VIA_REG_AC97_CODEC_ID_SHIFT;
	xval |= reg << VIA_REG_AC97_CMD_SHIFT;
	xval |= val << VIA_REG_AC97_DATA_SHIFT;
	spin_lock(&chip->ac97_lock);
	snd_via82xx_codec_xwrite(chip, xval);
	snd_via82xx_codec_ready(chip, ac97->num);
	spin_unlock(&chip->ac97_lock);
}

static unsigned short snd_via82xx_codec_read(ac97_t *ac97, unsigned short reg)
{
	via82xx_t *chip = snd_magic_cast(via82xx_t, ac97->private_data, return ~0);
	unsigned int xval, val = 0xffff;
	int again = 0;

	xval = ac97->num << VIA_REG_AC97_CODEC_ID_SHIFT;
	xval |= ac97->num ? VIA_REG_AC97_SECONDARY_VALID : VIA_REG_AC97_PRIMARY_VALID;
	xval |= VIA_REG_AC97_READ;
	xval |= (reg & 0x7f) << VIA_REG_AC97_CMD_SHIFT;
	spin_lock(&chip->ac97_lock);
      	while (1) {
      		if (again++ > 3) {
		        spin_unlock(&chip->ac97_lock);
		      	return 0xffff;
		}
		snd_via82xx_codec_xwrite(chip, xval);
		if (snd_via82xx_codec_ready(chip, ac97->num) < 0)
			continue;
		if (snd_via82xx_codec_valid(chip, ac97->num) >= 0) {
			udelay(25);
			val = snd_via82xx_codec_xread(chip);
			break;
		}
	}
	spin_unlock(&chip->ac97_lock);
	return val & 0xffff;
}

static void snd_via82xx_channel_reset(via82xx_t *chip, viadev_t *viadev)
{
	unsigned long port = chip->port + viadev->reg_offset;

	outb(VIA_REG_CTRL_PAUSE | VIA_REG_CTRL_TERMINATE | VIA_REG_CTRL_RESET, port + VIA_REG_OFFSET_CONTROL);
	udelay(50);
	/* disable interrupts */
	outb(0x00, port + VIA_REG_OFFSET_CONTROL);
	/* clear interrupts */
	outb(0x03, port + VIA_REG_OFFSET_STATUS);
	outb(0x00, port + VIA_REG_OFFSET_TYPE); /* for via686 */
	outl(0, port + VIA_REG_OFFSET_CURR_PTR);
}

static int snd_via82xx_trigger(via82xx_t *chip, viadev_t *viadev, int cmd)
{
	unsigned char val;
	unsigned long port = chip->port + viadev->reg_offset;
	
	if (chip->chip_type == TYPE_VIA8233)
		val = VIA_REG_CTRL_INT;
	else
		val = 0;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		val |= VIA_REG_CTRL_START;
		viadev->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		val = VIA_REG_CTRL_TERMINATE;
		viadev->running = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val |= VIA_REG_CTRL_PAUSE;
		viadev->running = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		viadev->running = 1;
		break;
	default:
		return -EINVAL;
	}
	outb(val, port + VIA_REG_OFFSET_CONTROL);
	if (cmd == SNDRV_PCM_TRIGGER_STOP)
		snd_via82xx_channel_reset(chip, viadev);
	return 0;
}


static int snd_via82xx_setup_periods(via82xx_t *chip, viadev_t *viadev,
				     snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long port = chip->port + viadev->reg_offset;
	int err;

	snd_via82xx_channel_reset(chip, viadev);

	err = build_via_table(viadev, substream, chip->pci);
	if (err < 0)
		return err;

	outl((u32)viadev->table_addr, port + VIA_REG_OFFSET_TABLE_PTR);
	switch (chip->chip_type) {
	case TYPE_VIA686:
		outb(VIA_REG_TYPE_AUTOSTART |
		     (runtime->format == SNDRV_PCM_FORMAT_S16_LE ? VIA_REG_TYPE_16BIT : 0) |
		     (runtime->channels > 1 ? VIA_REG_TYPE_STEREO : 0) |
		     ((viadev->reg_offset & 0x10) == 0 ? VIA_REG_TYPE_INT_LSAMPLE : 0) |
		     VIA_REG_TYPE_INT_EOL |
		     VIA_REG_TYPE_INT_FLAG, port + VIA_REG_OFFSET_TYPE);
		break;
	case TYPE_VIA8233:
		if (viadev->reg_offset == VIA_REG_MULTPLAY_STATUS) {
			unsigned int slots;
			int fmt = (runtime->format == SNDRV_PCM_FORMAT_S16_LE) ? VIA_REG_MULTPLAY_FMT_16BIT : VIA_REG_MULTPLAY_FMT_8BIT;
			fmt |= runtime->channels << 4;
			outb(fmt, port + VIA_REG_OFFSET_TYPE);
			/* set sample number to slot 3, 4, 7, 8, 6, 9 */
			switch (runtime->channels) {
			case 1: slots = (1<<0) | (1<<4); break;
			case 2: slots = (1<<0) | (2<<4); break;
			case 4: slots = (1<<0) | (2<<4) | (3<<8) | (4<<12); break;
			case 6: slots = (1<<0) | (2<<4) | (5<<8) | (6<<12) | (3<<16) | (4<<20); break;
			default: slots = 0; break;
			}
			/* STOP index is never reached */
			outl(0xff000000 | slots, port + VIA_REG_OFFSET_STOP_IDX);
		} else {
			outl((runtime->format == SNDRV_PCM_FORMAT_S16_LE ? VIA8233_REG_TYPE_16BIT : 0) |
			     (runtime->channels > 1 ? VIA8233_REG_TYPE_STEREO : 0) |
			     0xff000000,    /* STOP index is never reached */
			     port + VIA_REG_OFFSET_STOP_IDX);
		}
		break;
	}
	return 0;
}

/*
 *  Interrupt handler
 */

static inline void snd_via82xx_update(via82xx_t *chip, viadev_t *viadev)
{
	outb(VIA_REG_STAT_FLAG | VIA_REG_STAT_EOL, VIAREG(chip, OFFSET_STATUS) + viadev->reg_offset);
	if (viadev->substream && viadev->running) {
		spin_unlock(&chip->reg_lock);
		snd_pcm_period_elapsed(viadev->substream);
		spin_lock(&chip->reg_lock);
	}
}

static void snd_via82xx_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	via82xx_t *chip = snd_magic_cast(via82xx_t, dev_id, return);
	unsigned int status;

	spin_lock(&chip->reg_lock);
	if (chip->chip_type == TYPE_VIA686) {
		/* check mpu401 interrupt */
		status = inl(VIAREG(chip, SGD_SHADOW));
		if ((status & 0x00000077) == 0) {
			spin_unlock(&chip->reg_lock);
			if (chip->rmidi != NULL)
				snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data, regs);
			return;
		}
	}
	/* check status for each stream */
	if (inb(chip->port + chip->playback.reg_offset) & (VIA_REG_STAT_EOL|VIA_REG_STAT_FLAG))
		snd_via82xx_update(chip, &chip->playback);
	if (inb(chip->port + chip->capture.reg_offset) & (VIA_REG_STAT_EOL|VIA_REG_STAT_FLAG))
		snd_via82xx_update(chip, &chip->capture);
	spin_unlock(&chip->reg_lock);
}

/*
 *  PCM part
 */

static int snd_via82xx_playback_trigger(snd_pcm_substream_t * substream,
					int cmd)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);

	return snd_via82xx_trigger(chip, &chip->playback, cmd);
}

static int snd_via82xx_capture_trigger(snd_pcm_substream_t * substream,
				       int cmd)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);

	return snd_via82xx_trigger(chip, &chip->capture, cmd);
}

static int snd_via82xx_hw_params(snd_pcm_substream_t * substream,
				 snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_sgbuf_alloc(substream, params_buffer_bytes(hw_params));
}

static int snd_via82xx_hw_free(snd_pcm_substream_t * substream)
{
	return 0;
}

static int snd_via82xx_playback_prepare(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_ac97_set_rate(chip->ac97, AC97_PCM_FRONT_DAC_RATE, runtime->rate);
	if (chip->chip_type == TYPE_VIA8233 &&
	    chip->playback.reg_offset != VIA_REG_MULTPLAY_STATUS) {
		unsigned int tmp;
		/* I don't understand this stuff but its from the documentation and this way it works */
		outb(0 , VIAREG(chip, PLAYBACK_VOLUME_L));
		outb(0 , VIAREG(chip, PLAYBACK_VOLUME_R));
		tmp = inl(VIAREG(chip, PLAYBACK_STOP_IDX)) & ~0xfffff;
		outl(tmp | (0xffff * runtime->rate)/(48000/16), VIAREG(chip, PLAYBACK_STOP_IDX));
	}
	return snd_via82xx_setup_periods(chip, &chip->playback, substream);
}

static int snd_via82xx_capture_prepare(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_ac97_set_rate(chip->ac97, AC97_PCM_LR_ADC_RATE, runtime->rate);
	if (chip->chip_type == TYPE_VIA8233)
		outb(VIA_REG_CAPTURE_FIFO_ENABLE, VIAREG(chip, CAPTURE_FIFO));
	return snd_via82xx_setup_periods(chip, &chip->capture, substream);
}

static inline unsigned int snd_via82xx_cur_ptr(via82xx_t *chip, viadev_t *viadev)
{
	unsigned int val, ptr, count;
	
	snd_assert(viadev->tbl_entries, return 0);
	if (!(inb(VIAREG(chip, OFFSET_STATUS) + viadev->reg_offset) & VIA_REG_STAT_ACTIVE))
		return 0;

	count = inl(VIAREG(chip, OFFSET_CURR_COUNT) + viadev->reg_offset) & 0xffffff;
	switch (chip->chip_type) {
	case TYPE_VIA686:
		/* The via686a does not have the current index register,
		 * so we need to calculate the index from CURR_PTR.
		 */
		ptr = inl(VIAREG(chip, OFFSET_CURR_PTR) + viadev->reg_offset);
		if (ptr <= (unsigned int)viadev->table_addr)
			val = 0;
		else /* CURR_PTR holds the address + 8 */
			val = ((ptr - (unsigned int)viadev->table_addr) / 8 - 1) % viadev->tbl_entries;
		break;

	case TYPE_VIA8233:
	default:
		/* ah, this register makes life easier for us here. */
		val = inb(VIAREG(chip, OFFSET_CURR_INDEX) + viadev->reg_offset) % viadev->tbl_entries;
		break;
	}

	/* convert to the linear position */
	return viadev->idx_table[val].offset +
		viadev->idx_table[val].size - count;
}

static snd_pcm_uframes_t snd_via82xx_playback_pointer(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	return bytes_to_frames(substream->runtime, snd_via82xx_cur_ptr(chip, &chip->playback));
}

static snd_pcm_uframes_t snd_via82xx_capture_pointer(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	return bytes_to_frames(substream->runtime, snd_via82xx_cur_ptr(chip, &chip->capture));
}

static snd_pcm_hardware_t snd_via82xx_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		0,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	128 * 1024,
	.period_bytes_min =	32,
	.period_bytes_max =	128 * 1024,
	.periods_min =		2,
	.periods_max =		VIA_TABLE_SIZE / 2,
	.fifo_size =		0,
};

static snd_pcm_hardware_t snd_via82xx_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		0,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	128 * 1024,
	.period_bytes_min =	32,
	.period_bytes_max =	128 * 1024,
	.periods_min =		2,
	.periods_max =		VIA_TABLE_SIZE / 2,
	.fifo_size =		0,
};

static unsigned int channels[] = {
	1, 2, 4, 6
};

#define CHANNELS sizeof(channels) / sizeof(channels[0])

static snd_pcm_hw_constraint_list_t hw_constraints_channels = {
	.count = CHANNELS,
	.list = channels,
	.mask = 0,
};

static int snd_via82xx_playback_open(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	chip->playback.substream = substream;
	runtime->hw = snd_via82xx_playback;
	runtime->hw.rates = chip->ac97->rates[AC97_RATES_FRONT_DAC];
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	if ((err = snd_pcm_sgbuf_init(substream, chip->pci, 32)) < 0)
		return err;
	/* we may remove following constaint when we modify table entries
	   in interrupt */
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	if (chip->chip_type == TYPE_VIA8233) {
		runtime->hw.channels_max = 6;
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, &hw_constraints_channels);
	}
	return 0;
}

static int snd_via82xx_capture_open(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	chip->capture.substream = substream;
	runtime->hw = snd_via82xx_capture;
	runtime->hw.rates = chip->ac97->rates[AC97_RATES_ADC];
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	if ((err = snd_pcm_sgbuf_init(substream, chip->pci, 32)) < 0)
		return err;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	return 0;
}

static int snd_via82xx_playback_close(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);

	clean_via_table(&chip->playback, substream, chip->pci);
	snd_pcm_sgbuf_delete(substream);
	chip->playback.substream = NULL;
	return 0;
}

static int snd_via82xx_capture_close(snd_pcm_substream_t * substream)
{
	via82xx_t *chip = snd_pcm_substream_chip(substream);

	clean_via_table(&chip->capture, substream, chip->pci);
	snd_pcm_sgbuf_delete(substream);
	chip->capture.substream = NULL;
	return 0;
}

static snd_pcm_ops_t snd_via82xx_playback_ops = {
	.open =		snd_via82xx_playback_open,
	.close =	snd_via82xx_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_via82xx_hw_params,
	.hw_free =	snd_via82xx_hw_free,
	.prepare =	snd_via82xx_playback_prepare,
	.trigger =	snd_via82xx_playback_trigger,
	.pointer =	snd_via82xx_playback_pointer,
	.copy =		snd_pcm_sgbuf_ops_copy_playback,
	.silence =	snd_pcm_sgbuf_ops_silence,
	.page =		snd_pcm_sgbuf_ops_page,
};

static snd_pcm_ops_t snd_via82xx_capture_ops = {
	.open =		snd_via82xx_capture_open,
	.close =	snd_via82xx_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_via82xx_hw_params,
	.hw_free =	snd_via82xx_hw_free,
	.prepare =	snd_via82xx_capture_prepare,
	.trigger =	snd_via82xx_capture_trigger,
	.pointer =	snd_via82xx_capture_pointer,
	.copy =		snd_pcm_sgbuf_ops_copy_capture,
	.silence =	snd_pcm_sgbuf_ops_silence,
	.page =		snd_pcm_sgbuf_ops_page,
};

static void snd_via82xx_pcm_free(snd_pcm_t *pcm)
{
	via82xx_t *chip = snd_magic_cast(via82xx_t, pcm->private_data, return);
	chip->pcm = NULL;
}

static int __devinit snd_via82xx_pcm(via82xx_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, chip->card->shortname, device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_via82xx_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_via82xx_capture_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_via82xx_pcm_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcm = pcm;

	if (rpcm)
		*rpcm = NULL;
	return 0;
}


/*
 *  Mixer part
 */

static int snd_via8233_capture_source_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[2] = {
		"Line", "Mic"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= 2)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_via8233_capture_source_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = inb(VIAREG(chip, CAPTURE_CHANNEL)) & VIA_REG_CAPTURE_CHANNEL_MIC ? 1 : 0;
	return 0;
}

static int snd_via8233_capture_source_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	u8 val, oval;

	spin_lock_irqsave(&chip->reg_lock, flags);
	oval = inb(VIAREG(chip, CAPTURE_CHANNEL));
	val = oval & ~VIA_REG_CAPTURE_CHANNEL_MIC;
	if (ucontrol->value.enumerated.item[0])
		val |= VIA_REG_CAPTURE_CHANNEL_MIC;
	if (val != oval)
		outb(val, VIAREG(chip, CAPTURE_CHANNEL));
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return val != oval;
}

static snd_kcontrol_new_t snd_via8233_capture_source __devinitdata = {
	.name = "Input Source Select",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = snd_via8233_capture_source_info,
	.get = snd_via8233_capture_source_get,
	.put = snd_via8233_capture_source_put,
};

static void snd_via82xx_mixer_free_ac97(ac97_t *ac97)
{
	via82xx_t *chip = snd_magic_cast(via82xx_t, ac97->private_data, return);
	chip->ac97 = NULL;
}

static int __devinit snd_via82xx_mixer(via82xx_t *chip)
{
	ac97_t ac97;
	int err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.write = snd_via82xx_codec_write;
	ac97.read = snd_via82xx_codec_read;
	ac97.wait = snd_via82xx_codec_wait;
	ac97.private_data = chip;
	ac97.private_free = snd_via82xx_mixer_free_ac97;
	ac97.clock = chip->ac97_clock;
	if ((err = snd_ac97_mixer(chip->card, &ac97, &chip->ac97)) < 0)
		return err;
	return 0;
}

/*
 * joystick
 */

static int snd_via82xx_joystick_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_via82xx_joystick_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	u16 val;

	pci_read_config_word(chip->pci, 0x42, &val);
	ucontrol->value.integer.value[0] = (val & 0x08) ? 1 : 0;
	return 0;
}

static int snd_via82xx_joystick_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via82xx_t *chip = snd_kcontrol_chip(kcontrol);
	u16 val, oval;

	pci_read_config_word(chip->pci, 0x42, &oval);
	val = oval & ~0x08;
	if (ucontrol->value.integer.value[0])
		val |= 0x08;
	if (val != oval) {
		pci_write_config_word(chip->pci, 0x42, val);
		return 1;
	}
	return 0;
}

static snd_kcontrol_new_t snd_via82xx_joystick_control __devinitdata = {
	.name = "Joystick",
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.info = snd_via82xx_joystick_info,
	.get = snd_via82xx_joystick_get,
	.put = snd_via82xx_joystick_put,
};

/*
 *
 */

static int __devinit snd_via82xx_chip_init(via82xx_t *chip)
{
	ac97_t ac97;
	unsigned int val;
	int max_count;
	unsigned char pval;

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;

#if 0 /* broken on K7M? */
	if (chip->chip_type == TYPE_VIA686)
		/* disable all legacy ports */
		pci_write_config_byte(chip->pci, 0x42, 0);
#endif
	pci_read_config_byte(chip->pci, 0x40, &pval);
	if (! (pval & 0x01)) { /* codec not ready? */
		/* deassert ACLink reset, force SYNC */
		pci_write_config_byte(chip->pci, 0x41, 0xe0);
		udelay(100);
		/* deassert ACLink reset, force SYNC (warm AC'97 reset) */
		pci_write_config_byte(chip->pci, 0x41, 0x60);
		udelay(2);
		/* pci_write_config_byte(chip->pci, 0x41, 0x00);
		   udelay(100);
		*/
		/* ACLink on, deassert ACLink reset, VSR, SGD data out */
		/* note - FM data out has trouble with non VRA codecs !! */
		pci_write_config_byte(chip->pci, 0x41, 0xcc);
		udelay(100);
	}
	
	/* Make sure VRA is enabled, in case we didn't do a
	 * complete codec reset, above */
	pci_read_config_byte(chip->pci, 0x41, &pval);
	if ((pval & 0xcc) != 0xcc) {
		/* ACLink on, deassert ACLink reset, VSR, SGD data out */
		/* note - FM data out has trouble with non VRA codecs !! */
		pci_write_config_byte(chip->pci, 0x41, 0xcc);
		udelay(100);
	}

	/* wait until codec ready */
	max_count = ((3 * HZ) / 4) + 1;
	do {
		pci_read_config_byte(chip->pci, 0x40, &pval);
		if (pval & 0x01) /* primary codec ready */
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (--max_count > 0);

	if ((val = snd_via82xx_codec_xread(chip)) & VIA_REG_AC97_BUSY)
		snd_printk("AC'97 codec is not ready [0x%x]\n", val);

	/* and then reset codec.. */
	snd_via82xx_codec_ready(chip, 0);
	snd_via82xx_codec_write(&ac97, AC97_RESET, 0x0000);
	snd_via82xx_codec_read(&ac97, 0);

#if 0 /* FIXME: we don't support the second codec yet so skip the detection now.. */
	snd_via82xx_codec_xwrite(chip, VIA_REG_AC97_READ |
				 VIA_REG_AC97_SECONDARY_VALID |
				 (VIA_REG_AC97_CODEC_ID_SECONDARY << VIA_REG_AC97_CODEC_ID_SHIFT));
	max_count = ((3 * HZ) / 4) + 1;
	snd_via82xx_codec_xwrite(chip, VIA_REG_AC97_READ |
				 VIA_REG_AC97_SECONDARY_VALID |
				 (VIA_REG_AC97_CODEC_ID_SECONDARY << VIA_REG_AC97_CODEC_ID_SHIFT));
	do {
		if ((val = snd_via82xx_codec_xread(chip)) & VIA_REG_AC97_SECONDARY_VALID) {
			chip->ac97_secondary = 1;
			goto __ac97_ok2;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
	} while (--max_count > 0);
	/* This is ok, the most of motherboards have only one codec */

      __ac97_ok2:
#endif

	if (chip->chip_type == TYPE_VIA686) {
		/* route FM trap to IRQ, disable FM trap */
		pci_write_config_byte(chip->pci, 0x48, 0);
		/* disable all GPI interrupts */
		outl(0, chip->port + 0x8c);
	}

	/* disable interrupts */
	snd_via82xx_channel_reset(chip, &chip->playback);
	snd_via82xx_channel_reset(chip, &chip->capture);
	return 0;
}

static int snd_via82xx_free(via82xx_t *chip)
{
	if (chip->irq < 0)
		goto __end_hw;
	/* disable interrupts */
	snd_via82xx_channel_reset(chip, &chip->playback);
	snd_via82xx_channel_reset(chip, &chip->capture);
	/* --- */
	synchronize_irq(chip->irq);
      __end_hw:
	if (chip->res_port) {
		release_resource(chip->res_port);
		kfree_nocheck(chip->res_port);
	}
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	if (chip->chip_type == TYPE_VIA686) {
		pci_write_config_byte(chip->pci, 0x42, chip->old_legacy);
		pci_write_config_byte(chip->pci, 0x43, chip->old_legacy_cfg);
	}
	snd_magic_kfree(chip);
	return 0;
}

static int snd_via82xx_dev_free(snd_device_t *device)
{
	via82xx_t *chip = snd_magic_cast(via82xx_t, device->device_data, return -ENXIO);
	return snd_via82xx_free(chip);
}

static int __devinit snd_via82xx_create(snd_card_t * card,
					struct pci_dev *pci,
					int chip_type,
					unsigned int ac97_clock,
					via82xx_t ** r_via)
{
	via82xx_t *chip;
	int err;
        static snd_device_ops_t ops = {
		.dev_free =	snd_via82xx_dev_free,
        };

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	if ((chip = snd_magic_kcalloc(via82xx_t, 0, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	chip->chip_type = chip_type;

	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->ac97_lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	pci_read_config_byte(pci, 0x42, &chip->old_legacy);
	pci_read_config_byte(pci, 0x43, &chip->old_legacy_cfg);

	chip->port = pci_resource_start(pci, 0);
	if ((chip->res_port = request_region(chip->port, 256, card->driver)) == NULL) {
		snd_via82xx_free(chip);
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", chip->port, chip->port + 256 - 1);
		return -EBUSY;
	}
	if (request_irq(pci->irq, snd_via82xx_interrupt, SA_INTERRUPT|SA_SHIRQ,
			card->driver, (void *)chip)) {
		snd_via82xx_free(chip);
		snd_printk("unable to grab IRQ %d\n", chip->irq);
		return -EBUSY;
	}
	chip->irq = pci->irq;
	if (ac97_clock >= 8000 && ac97_clock <= 48000)
		chip->ac97_clock = ac97_clock;
	pci_read_config_byte(pci, PCI_REVISION_ID, &chip->revision);
	synchronize_irq(chip->irq);

	/* initialize offsets */
	switch (chip->chip_type) {
	case TYPE_VIA686:
		chip->playback.reg_offset = VIA_REG_PLAYBACK_STATUS;
		chip->capture.reg_offset = VIA_REG_CAPTURE_STATUS;
		break;
	case TYPE_VIA8233:
		/* we use multi-channel playback mode, since this mode is supported
		 * by all VIA8233 models (and obviously suitable for our purpose).
		 */
		chip->playback.reg_offset = VIA_REG_MULTPLAY_STATUS;
		chip->capture.reg_offset = VIA_REG_CAPTURE_8233_STATUS;
		break;
	}

	if ((err = snd_via82xx_chip_init(chip)) < 0) {
		snd_via82xx_free(chip);
		return err;
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_via82xx_free(chip);
		return err;
	}

	/* The 8233 ac97 controller does not implement the master bit
	 * in the pci command register. IMHO this is a violation of the PCI spec.
	 * We call pci_set_master here because it does not hurt. */
	pci_set_master(pci);

	*r_via = chip;
	return 0;
}

static int __devinit snd_via82xx_probe(struct pci_dev *pci,
				       const struct pci_device_id *id)
{
	static int dev;
	snd_card_t *card;
	via82xx_t *chip;
	int pcm_dev = 0;
	int chip_type;
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

	chip_type = id->driver_data;
	switch (chip_type) {
	case TYPE_VIA686:
		strcpy(card->driver, "VIA686A");
		strcpy(card->shortname, "VIA 82C686A/B");
		break;
	case TYPE_VIA8233:
		strcpy(card->driver, "VIA8233");
		strcpy(card->shortname, "VIA 8233A/C");
		break;
	default:
		snd_printk(KERN_ERR "invalid chip type %d\n", chip_type);
		snd_card_free(card);
		return -EINVAL;
	}
		
	if ((err = snd_via82xx_create(card, pci, chip_type, snd_ac97_clock[dev], &chip)) < 0) {
		snd_card_free(card);
		return err;
	}


	if (snd_via82xx_mixer(chip) < 0) {
		snd_card_free(card);
		return err;
	}
	if (snd_via82xx_pcm(chip, pcm_dev++, NULL) < 0) {
		snd_card_free(card);
		return err;
	}
#if 0
	if (snd_via82xx_pcm_fm(chip, pcm_dev++, NULL) < 0) {
		snd_card_free(card);
		return err;
	}
#endif

	if (chip->chip_type == TYPE_VIA686) {
		unsigned char legacy, legacy_cfg;
		int rev_h = 0;
		legacy = chip->old_legacy;
		legacy_cfg = chip->old_legacy_cfg;
		legacy |= 0x40;		/* disable MIDI */
		legacy &= ~0x08;	/* disable joystick */
		if (chip->revision >= 0x20) {
			if (check_region(pci_resource_start(pci, 2), 4)) {
				rev_h = 0;
				legacy &= ~0x80;	/* disable PCI I/O 2 */
			} else {
				rev_h = 1;
				legacy |= 0x80;		/* enable PCI I/O 2 */
			}
		}
		pci_write_config_byte(pci, 0x42, legacy);
		pci_write_config_byte(pci, 0x43, legacy_cfg);
		if (rev_h && snd_mpu_port[dev] >= 0x200) {	/* force MIDI */
			legacy |= 0x02;	/* enable MPU */
			pci_write_config_dword(pci, 0x18, (snd_mpu_port[dev] & 0xfffc) | 0x01);
		} else {
			if (rev_h && (legacy & 0x02)) {
				snd_mpu_port[dev] = pci_resource_start(pci, 2);
				if (snd_mpu_port[dev] < 0x200)	/* bad value */
					legacy &= ~0x02;	/* disable MIDI */
			} else {
				switch (snd_mpu_port[dev]) {	/* force MIDI */
				case 0x300:
				case 0x310:
				case 0x320:
				case 0x330:
					legacy_cfg &= ~(3 << 2);
					legacy_cfg |= (snd_mpu_port[dev] & 0x0030) >> 2;
					legacy |= 0x02;
					break;
				default:			/* no, use BIOS settings */
					if (legacy & 0x02)
						snd_mpu_port[dev] = 0x300 + ((legacy_cfg & 0x000c) << 2);
				}
			}
		}
		pci_write_config_byte(pci, 0x42, legacy);
		pci_write_config_byte(pci, 0x43, legacy_cfg);
		if (legacy & 0x02) {
			if (check_region(snd_mpu_port[dev], 2)) {
				printk(KERN_WARNING "unable to get MPU-401 port at 0x%lx, skipping\n", snd_mpu_port[dev]);
				legacy &= ~0x02;
				pci_write_config_byte(pci, 0x42, legacy);
				goto __skip_mpu;
			}
			if (snd_mpu401_uart_new(card, 0, MPU401_HW_VIA686A,
						snd_mpu_port[dev], 0,
						pci->irq, 0,
						&chip->rmidi) < 0) {
				printk(KERN_WARNING "unable to initialize MPU-401 at 0x%lx, skipping\n", snd_mpu_port[dev]);
				legacy &= ~0x02;
				pci_write_config_byte(pci, 0x42, legacy);
				goto __skip_mpu;
			}
			legacy &= ~0x40;	/* enable MIDI interrupt */
			pci_write_config_byte(pci, 0x42, legacy);
		__skip_mpu:
			;
		}
	
		/* card switches */
		err = snd_ctl_add(card, snd_ctl_new1(&snd_via82xx_joystick_control, chip));
		if (err < 0) {
			snd_card_free(card);
			return err;
		}

	} else {
		/* VIA8233 */
		err = snd_ctl_add(card, snd_ctl_new1(&snd_via8233_capture_source, chip));
		if (err < 0) {
			snd_card_free(card);
			return err;
		}
	}

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

static void __devexit snd_via82xx_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "VIA 82xx Audio",
	.id_table = snd_via82xx_ids,
	.probe = snd_via82xx_probe,
	.remove = __devexit_p(snd_via82xx_remove),
};

static int __init alsa_card_via82xx_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		printk(KERN_ERR "VIA 82xx soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_via82xx_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_via82xx_init)
module_exit(alsa_card_via82xx_exit)

#ifndef MODULE

/* format is: snd-via82xx=snd_enable,snd_index,snd_id,
			  snd_mpu_port,snd_ac97_clock */

static int __init alsa_card_via82xx_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_mpu_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_ac97_clock[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-via82xx=", alsa_card_via82xx_setup);

#endif /* ifndef MODULE */
 
