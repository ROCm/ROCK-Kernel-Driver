/*
 *   ALSA driver for VIA VT82C686A (South Bridge)
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
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
#include <sound/info.h>
#include <sound/ac97_codec.h>
#include <sound/mpu401.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("VIA VT82C686A");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{VIA,VT82C686A,pci},{VIA,VT82C686B}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static long snd_mpu_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1};
static int snd_ac97_clock[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 48000};

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for VIA 82C686A bridge.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for VIA 82C686A bridge.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable audio part of VIA 82C686A bridge.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_mpu_port, "MPU-401 port.");
MODULE_PARM_SYNTAX(snd_mpu_port, SNDRV_PORT_DESC);
MODULE_PARM(snd_ac97_clock, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_ac97_clock, "AC'97 codec clock (default 48000Hz).");
MODULE_PARM_SYNTAX(snd_ac97_clock, SNDRV_ENABLED ",default:48000");

/*
 *  Direct registers
 */

#ifndef PCI_DEVICE_ID_VIA_82C686_5
#define PCI_DEVICE_ID_VIA_82C686_5	0x3058
#endif

#define VIAREG(via, x) ((via)->port + VIA_REG_##x)

/* offsets */
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
#define   VIA_REG_CTRL_PAUSE		0x08	/* RW */
#define   VIA_REG_CTRL_RESET		0x01	/* RW - probably reset? undocumented */
#define VIA_REG_OFFSET_TYPE		0x02	/* byte - channel type */
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
#define VIA_REG_OFFSET_CURR_COUNT	0x0c	/* dword - channel current count */
/* playback block */
#define VIA_REG_PLAYBACK_STATUS		0x00	/* byte - channel status */
#define VIA_REG_PLAYBACK_CONTROL	0x01	/* byte - channel control */
#define VIA_REG_PLAYBACK_TYPE		0x02	/* byte - channel type */
#define VIA_REG_PLAYBACK_TABLE_PTR	0x04	/* dword - channel table pointer */
#define VIA_REG_PLAYBACK_CURR_PTR	0x04	/* dword - channel current pointer */
#define VIA_REG_PLAYBACK_CURR_COUNT	0x0c	/* dword - channel current count */
/* capture block */
#define VIA_REG_CAPTURE_STATUS		0x10	/* byte - channel status */
#define VIA_REG_CAPTURE_CONTROL		0x11	/* byte - channel control */
#define VIA_REG_CAPTURE_TYPE		0x12	/* byte - channel type */
#define VIA_REG_CAPTURE_TABLE_PTR	0x14	/* dword - channel table pointer */
#define VIA_REG_CAPTURE_CURR_PTR	0x14	/* dword - channel current pointer */
#define VIA_REG_CAPTURE_CURR_COUNT	0x1c	/* dword - channel current count */
/* FM block */
#define VIA_REG_FM_STATUS		0x20	/* byte - channel status */
#define VIA_REG_FM_CONTROL		0x21	/* byte - channel control */
#define VIA_REG_FM_TYPE			0x22	/* byte - channel type */
#define VIA_REG_FM_TABLE_PTR		0x24	/* dword - channel table pointer */
#define VIA_REG_FM_CURR_PTR		0x24	/* dword - channel current pointer */
#define VIA_REG_FM_CURR_COUNT		0x2c	/* dword - channel current count */
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

/*
 *
 */

#define VIA_MAX_FRAGS			32

/*
 *  
 */

typedef struct {
	unsigned long reg_offset;
	unsigned int *table;
	dma_addr_t table_addr;
        snd_pcm_substream_t *substream;
	dma_addr_t physbuf;
        unsigned int size;
        unsigned int fragsize;
	unsigned int frags;
	unsigned int lastptr;
	unsigned int lastcount;
} viadev_t;

typedef struct _snd_via686a via686a_t;
#define chip_t via686a_t

struct _snd_via686a {
	int irq;

	unsigned long port;
	struct resource *res_port;
	unsigned char revision;

	unsigned char old_legacy;
	unsigned char old_legacy_cfg;

	struct pci_dev *pci;
	snd_card_t *card;

	snd_pcm_t *pcm;
	snd_pcm_t *pcm_fm;
	viadev_t playback;
	viadev_t capture;
	viadev_t playback_fm;

	snd_rawmidi_t *rmidi;

	ac97_t *ac97;
	unsigned int ac97_clock;
	unsigned int ac97_secondary;	/* secondary AC'97 codec is present */

	spinlock_t reg_lock;
	snd_info_entry_t *proc_entry;

	void *tables;
	dma_addr_t tables_addr;
};

static struct pci_device_id snd_via686a_ids[] __devinitdata = {
	{ 0x1106, 0x3058, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* 686A */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_via686a_ids);

/*
 *  Basic I/O
 */

static inline unsigned int snd_via686a_codec_xread(via686a_t *chip)
{
	return inl(VIAREG(chip, AC97));
}
 
static inline void snd_via686a_codec_xwrite(via686a_t *chip, unsigned int val)
{
	outl(val, VIAREG(chip, AC97));
}
 
static int snd_via686a_codec_ready(via686a_t *chip, int secondary)
{
	unsigned int timeout = 1000;	/* 1ms */
	unsigned int val;
	
	while (timeout-- > 0) {
		udelay(1);
		if (!((val = snd_via686a_codec_xread(chip)) & VIA_REG_AC97_BUSY))
			return val & 0xffff;
	}
	snd_printk("codec_ready: codec %i is not ready [0x%x]\n", secondary, snd_via686a_codec_xread(chip));
	return -EIO;
}
 
static int snd_via686a_codec_valid(via686a_t *chip, int secondary)
{
	unsigned int timeout = 1000;	/* 1ms */
	unsigned int val;
	unsigned int stat = !secondary ? VIA_REG_AC97_PRIMARY_VALID :
					 VIA_REG_AC97_SECONDARY_VALID;
	
	while (timeout-- > 0) {
		udelay(1);
		if ((val = snd_via686a_codec_xread(chip)) & stat)
			return val & 0xffff;
	}
	snd_printk("codec_valid: codec %i is not valid [0x%x]\n", secondary, snd_via686a_codec_xread(chip));
	return -EIO;
}
 
static void snd_via686a_codec_wait(ac97_t *ac97)
{
	via686a_t *chip = snd_magic_cast(via686a_t, ac97->private_data, return);
	int err;
	err = snd_via686a_codec_ready(chip, ac97->num);
	/* here we need to wait fairly for long time.. */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ/2);
}

static void snd_via686a_codec_write(ac97_t *ac97,
				    unsigned short reg,
				    unsigned short val)
{
	via686a_t *chip = snd_magic_cast(via686a_t, ac97->private_data, return);
	unsigned int xval;
	
	xval = !ac97->num ? VIA_REG_AC97_CODEC_ID_PRIMARY : VIA_REG_AC97_CODEC_ID_SECONDARY;
	xval <<= VIA_REG_AC97_CODEC_ID_SHIFT;
	xval |= reg << VIA_REG_AC97_CMD_SHIFT;
	xval |= val << VIA_REG_AC97_DATA_SHIFT;
	spin_lock(&chip->reg_lock);
	snd_via686a_codec_xwrite(chip, xval);
	snd_via686a_codec_ready(chip, ac97->num);
	spin_unlock(&chip->reg_lock);
}

static unsigned short snd_via686a_codec_read(ac97_t *ac97, unsigned short reg)
{
	via686a_t *chip = snd_magic_cast(via686a_t, ac97->private_data, return ~0);
	unsigned int xval, val = 0xffff;
	int again = 0;

	xval = !ac97->num ? VIA_REG_AC97_CODEC_ID_PRIMARY : VIA_REG_AC97_CODEC_ID_SECONDARY;
	xval <<= VIA_REG_AC97_CODEC_ID_SHIFT;
	xval = (!ac97->num ? VIA_REG_AC97_PRIMARY_VALID : VIA_REG_AC97_SECONDARY_VALID);
	xval |= VIA_REG_AC97_READ;
	xval |= reg << VIA_REG_AC97_CMD_SHIFT;
	spin_lock(&chip->reg_lock);
      	while (1) {
      		if (again++ > 3) {
		        spin_unlock(&chip->reg_lock);
		      	return 0xffff;
		}
		snd_via686a_codec_xwrite(chip, xval);
		if (snd_via686a_codec_ready(chip, ac97->num) < 0)
			continue;
		if (snd_via686a_codec_valid(chip, ac97->num) >= 0) {
			udelay(25);
			val = snd_via686a_codec_xread(chip);
			break;
		}
	}
	spin_unlock(&chip->reg_lock);
	return val & 0xffff;
}

#if 0
static void snd_via686a_channel_print(via686a_t *chip, viadev_t *viadev)
{
	unsigned long port = chip->port + viadev->reg_offset;

	printk("[0x%x] status = 0x%x, control = 0x%x, type = 0x%x, ptr = 0x%x, count = 0x%x\n",
			port,
			inb(port + VIA_REG_OFFSET_STATUS),
			inb(port + VIA_REG_OFFSET_CONTROL),
			inb(port + VIA_REG_OFFSET_TYPE),
			inl(port + VIA_REG_OFFSET_CURR_PTR),
			inl(port + VIA_REG_OFFSET_CURR_COUNT));
}
#endif

static void snd_via686a_channel_reset(via686a_t *chip, viadev_t *viadev)
{
	unsigned long port = chip->port + viadev->reg_offset;

	outb(VIA_REG_CTRL_PAUSE | VIA_REG_CTRL_TERMINATE | VIA_REG_CTRL_RESET, port + VIA_REG_OFFSET_CONTROL);
	udelay(50);
	outb(0x00, port + VIA_REG_OFFSET_CONTROL);
	outb(0xff, port + VIA_REG_OFFSET_STATUS);
	outb(0x00, port + VIA_REG_OFFSET_TYPE);
	outl(0, port + VIA_REG_OFFSET_CURR_PTR);
}

static int snd_via686a_trigger(via686a_t *chip, viadev_t *viadev, int cmd)
{
	unsigned char val = 0;
	unsigned long port = chip->port + viadev->reg_offset;
	
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		val = VIA_REG_CTRL_START;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		val = VIA_REG_CTRL_TERMINATE | VIA_REG_CTRL_RESET;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val = VIA_REG_CTRL_PAUSE;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		val = 0;
		break;
	default:
		return -EINVAL;
	}
	outb(val, port + VIA_REG_OFFSET_CONTROL);
	if (cmd == SNDRV_PCM_TRIGGER_STOP)
		snd_via686a_channel_reset(chip, viadev);
	return 0;
}

static void snd_via686a_setup_periods(via686a_t *chip, viadev_t *viadev,
				      snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int idx, frags;
	unsigned int *table = viadev->table;
	unsigned long port = chip->port + viadev->reg_offset;

	viadev->physbuf = runtime->dma_addr;
	viadev->size = snd_pcm_lib_buffer_bytes(substream);
	viadev->fragsize = snd_pcm_lib_period_bytes(substream);
	viadev->frags = runtime->periods;
	viadev->lastptr = ~0;
	viadev->lastcount = ~0;

	snd_via686a_channel_reset(chip, viadev);
	outl(viadev->table_addr, port + VIA_REG_OFFSET_TABLE_PTR);
	outb(VIA_REG_TYPE_AUTOSTART |
	     (runtime->format == SNDRV_PCM_FORMAT_S16_LE ? VIA_REG_TYPE_16BIT : 0) |
	     (runtime->channels > 1 ? VIA_REG_TYPE_STEREO : 0) |
	     ((viadev->reg_offset & 0x10) == 0 ? VIA_REG_TYPE_INT_LSAMPLE : 0) |
	     VIA_REG_TYPE_INT_EOL |
	     VIA_REG_TYPE_INT_FLAG, port + VIA_REG_OFFSET_TYPE);
	if (viadev->size == viadev->fragsize) {
		table[0] = cpu_to_le32(viadev->physbuf);
		table[1] = cpu_to_le32(0xc0000000 | /* EOL + flag */
				       viadev->fragsize);
	} else {
		frags = viadev->size / viadev->fragsize;
		for (idx = 0; idx < frags - 1; idx++) {
			table[(idx << 1) + 0] = cpu_to_le32(viadev->physbuf + (idx * viadev->fragsize));
			table[(idx << 1) + 1] = cpu_to_le32(0x40000000 |	/* flag */
							    viadev->fragsize);
		}
		table[((frags-1) << 1) + 0] = cpu_to_le32(viadev->physbuf + ((frags-1) * viadev->fragsize));
		table[((frags-1) << 1) + 1] = cpu_to_le32(0x80000000 |	/* EOL */
							  viadev->fragsize);
	}
}

/*
 *  Interrupt handler
 */

static inline void snd_via686a_update(via686a_t *chip, viadev_t *viadev)
{
	snd_pcm_period_elapsed(viadev->substream);
	outb(VIA_REG_STAT_FLAG | VIA_REG_STAT_EOL, VIAREG(chip, OFFSET_STATUS) + viadev->reg_offset);
}

static void snd_via686a_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	via686a_t *chip = snd_magic_cast(via686a_t, dev_id, return);
	unsigned int status;

	status = inl(VIAREG(chip, SGD_SHADOW));
	if ((status & 0x00000077) == 0) {
		if (chip->rmidi != NULL) {
			snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data, regs);
		}
		return;
	}
	if (inb(VIAREG(chip, PLAYBACK_STATUS)) & (VIA_REG_STAT_EOL|VIA_REG_STAT_FLAG))
		snd_via686a_update(chip, &chip->playback);
	if (inb(VIAREG(chip, FM_STATUS)) & (VIA_REG_STAT_EOL|VIA_REG_STAT_FLAG))
		snd_via686a_update(chip, &chip->playback_fm);
	if (inb(VIAREG(chip, CAPTURE_STATUS)) & (VIA_REG_STAT_EOL|VIA_REG_STAT_FLAG))
		snd_via686a_update(chip, &chip->capture);
}

/*
 *  PCM part
 */

static int snd_via686a_playback_trigger(snd_pcm_substream_t * substream,
					int cmd)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);

	return snd_via686a_trigger(chip, &chip->playback, cmd);
}

static int snd_via686a_capture_trigger(snd_pcm_substream_t * substream,
				       int cmd)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);

	return snd_via686a_trigger(chip, &chip->capture, cmd);
}

static int snd_via686a_hw_params(snd_pcm_substream_t * substream,
				 snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_via686a_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_via686a_playback_prepare(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_ac97_set_rate(chip->ac97, AC97_PCM_FRONT_DAC_RATE, runtime->rate);
	snd_via686a_setup_periods(chip, &chip->playback, substream);
	return 0;
}

static int snd_via686a_capture_prepare(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_ac97_set_rate(chip->ac97, AC97_PCM_LR_ADC_RATE, runtime->rate);
	snd_via686a_setup_periods(chip, &chip->capture, substream);
	return 0;
}

static inline unsigned int snd_via686a_cur_ptr(via686a_t *chip, viadev_t *viadev)
{
	unsigned int val, ptr, count;
	
	ptr = inl(VIAREG(chip, OFFSET_CURR_PTR) + viadev->reg_offset);
	count = inl(VIAREG(chip, OFFSET_CURR_COUNT) + viadev->reg_offset);
	if (ptr == viadev->lastptr && count > viadev->lastcount)
		ptr += 8;
	if (!(inb(VIAREG(chip, OFFSET_STATUS) + viadev->reg_offset) & VIA_REG_STAT_ACTIVE))
		return 0;
	val = (((unsigned int)(ptr - viadev->table_addr) / 8) - 1) % viadev->frags;
	val *= viadev->fragsize;
	val += viadev->fragsize - count;
	viadev->lastptr = ptr;
	viadev->lastcount = count;
	// printk("pointer: ptr = 0x%x (%i), count = 0x%x, val = 0x%x\n", ptr, count, val);
	return val;
}

static snd_pcm_uframes_t snd_via686a_playback_pointer(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);
	return bytes_to_frames(substream->runtime, snd_via686a_cur_ptr(chip, &chip->playback));
}

static snd_pcm_uframes_t snd_via686a_capture_pointer(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);
	return bytes_to_frames(substream->runtime, snd_via686a_cur_ptr(chip, &chip->capture));
}

static snd_pcm_hardware_t snd_via686a_playback =
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
	channels_max:		2,
	buffer_bytes_max:	128 * 1024,
	period_bytes_min:	32,
	period_bytes_max:	128 * 1024,
	periods_min:		1,
	periods_max:		VIA_MAX_FRAGS,
	fifo_size:		0,
};

static snd_pcm_hardware_t snd_via686a_capture =
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
	periods_min:		1,
	periods_max:		VIA_MAX_FRAGS,
	fifo_size:		0,
};

static int snd_via686a_playback_open(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	chip->playback.substream = substream;
	runtime->hw = snd_via686a_playback;
	runtime->hw.rates = chip->ac97->rates_front_dac;
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	return 0;
}

static int snd_via686a_capture_open(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	chip->capture.substream = substream;
	runtime->hw = snd_via686a_capture;
	runtime->hw.rates = chip->ac97->rates_adc;
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	return 0;
}

static int snd_via686a_playback_close(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);

	chip->playback.substream = NULL;
	/* disable DAC power */
	snd_ac97_update_bits(chip->ac97, AC97_POWERDOWN, 0x0200, 0x0200);
	return 0;
}

static int snd_via686a_capture_close(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);

	chip->capture.substream = NULL;
	/* disable ADC power */
	snd_ac97_update_bits(chip->ac97, AC97_POWERDOWN, 0x0100, 0x0100);
	return 0;
}

static snd_pcm_ops_t snd_via686a_playback_ops = {
	open:		snd_via686a_playback_open,
	close:		snd_via686a_playback_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_via686a_hw_params,
	hw_free:	snd_via686a_hw_free,
	prepare:	snd_via686a_playback_prepare,
	trigger:	snd_via686a_playback_trigger,
	pointer:	snd_via686a_playback_pointer,
};

static snd_pcm_ops_t snd_via686a_capture_ops = {
	open:		snd_via686a_capture_open,
	close:		snd_via686a_capture_close,
	ioctl:		snd_pcm_lib_ioctl,
	hw_params:	snd_via686a_hw_params,
	hw_free:	snd_via686a_hw_free,
	prepare:	snd_via686a_capture_prepare,
	trigger:	snd_via686a_capture_trigger,
	pointer:	snd_via686a_capture_pointer,
};

static void snd_via686a_pcm_free(snd_pcm_t *pcm)
{
	via686a_t *chip = snd_magic_cast(via686a_t, pcm->private_data, return);
	chip->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __devinit snd_via686a_pcm(via686a_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, "VIA 82C686A", device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_via686a_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_via686a_capture_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_via686a_pcm_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, "VIA 82C686A");
	chip->pcm = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 64*1024, 128*1024);

	if (rpcm)
		*rpcm = NULL;
	return 0;
}

#if 0

/*
 *  PCM code - FM channel
 */

static int snd_via686a_playback_fm_ioctl(snd_pcm_substream_t * substream,
					 unsigned int cmd,
					 void *arg)
{
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static int snd_via686a_playback_fm_trigger(snd_pcm_substream_t * substream,
					   int cmd)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);

	return snd_via686a_trigger(chip, &chip->playback_fm, cmd);
}

static int snd_via686a_playback_fm_prepare(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_ac97_set_rate(chip->ac97, AC97_PCM_FRONT_DAC_RATE, runtime->rate);
	snd_via686a_setup_periods(chip, &chip->playback_fm, substream);
	return 0;
}

static snd_pcm_uframes_t snd_via686a_playback_fm_pointer(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);
	return bytes_to_frames(substream->runtime, snd_via686a_cur_ptr(chip, &chip->playback_fm));
}

static snd_pcm_hardware_t snd_via686a_playback_fm =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE),
	formats:		SNDRV_PCM_FMTBIT_S16_LE,
	rates:			SNDRV_PCM_RATE_KNOT,
	rate_min:		24000,
	rate_max:		24000,
	channels_min:		2,
	channels_max:		2,
	buffer_bytes_max:	128 * 1024,
	period_bytes_min:	32,
	period_bytes_max:	128 * 1024,
	periods_min:		1,
	periods_max:		VIA_MAX_FRAGS,
	fifo_size:		0,
};

static int snd_via686a_playback_fm_open(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	if ((runtime->dma_area = snd_malloc_pci_pages_fallback(chip->pci, chip->dma_fm_size, &runtime->dma_addr, &runtime->dma_bytes)) == NULL)
		return -ENOMEM;
	chip->playback_fm.substream = substream;
	runtime->hw = snd_via686a_playback_fm;
#if 0
	runtime->hw.rates = chip->ac97->rates_front_dac;
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.min_rate = 48000;
#endif
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	return 0;
}

static int snd_via686a_playback_fm_close(snd_pcm_substream_t * substream)
{
	via686a_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	chip->playback_fm.substream = NULL;
	snd_free_pci_pages(chip->pci, runtime->dma_bytes, runtime->dma_area, runtime->dma_addr);
	/* disable DAC power */
	snd_ac97_update_bits(chip->ac97, AC97_POWERDOWN, 0x0200, 0x0200);
	return 0;
}

static snd_pcm_ops_t snd_via686a_playback_fm_ops = {
	open:		snd_via686a_playback_fm_open,
	close:		snd_via686a_playback_fm_close,
	ioctl:		snd_pcm_lib_ioctl,
	prepare:	snd_via686a_playback_fm_prepare,
	trigger:	snd_via686a_playback_fm_trigger,
	pointer:	snd_via686a_playback_fm_pointer,
};

static void snd_via686a_pcm_fm_free(void *private_data)
{
	via686a_t *chip = snd_magic_cast(via686a_t, private_data, return);
	chip->pcm_fm = NULL;
	snd_pcm_lib_preallocate_pci_free_for_all(ensoniq->pci, pcm);
}

static int __devinit snd_via686a_pcm_fm(via686a_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, "VIA 82C686A - FM DAC", device, 1, 0, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_via686a_playback_fm_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_via686a_pcm_fm_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, "VIA 82C686A - FM DAC");

	snd_pcm_add_buffer_bytes_controls(pcm);
	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 64*1024);

	chip->pcm_fm = pcm;
	if (rpcm)
		*rpcm = NULL;
	return 0;
}

#endif

/*
 *  Mixer part
 */

static void snd_via686a_codec_init(ac97_t *ac97)
{
	// via686a_t *chip = snd_magic_cast(via686a_t, ac97->private_data, return);

	/* disable DAC & ADC power */
	snd_ac97_update_bits(ac97, AC97_POWERDOWN, 0x0300, 0x0300);
	/* disable center DAC/surround DAC/LFE DAC/MIC ADC */
	snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, 0xe800, 0xe800);
}

static void snd_via686a_mixer_free_ac97(ac97_t *ac97)
{
	via686a_t *chip = snd_magic_cast(via686a_t, ac97->private_data, return);
	chip->ac97 = NULL;
}

static int __devinit snd_via686a_mixer(via686a_t *chip)
{
	ac97_t ac97;
	int err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.write = snd_via686a_codec_write;
	ac97.read = snd_via686a_codec_read;
	ac97.init = snd_via686a_codec_init;
	ac97.wait = snd_via686a_codec_wait;
	ac97.private_data = chip;
	ac97.private_free = snd_via686a_mixer_free_ac97;
	ac97.clock = chip->ac97_clock;
	if ((err = snd_ac97_mixer(chip->card, &ac97, &chip->ac97)) < 0)
		return err;
	return 0;
}

/*
 * joystick
 */

static int snd_via686a_joystick_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_via686a_joystick_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via686a_t *chip = snd_kcontrol_chip(kcontrol);
	u16 val;

	pci_read_config_word(chip->pci, 0x42, &val);
	ucontrol->value.integer.value[0] = (val & 0x08) ? 1 : 0;
	return 0;
}

static int snd_via686a_joystick_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	via686a_t *chip = snd_kcontrol_chip(kcontrol);
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

static snd_kcontrol_new_t snd_via686a_joystick_control __devinitdata = {
	name: "Joystick",
	iface: SNDRV_CTL_ELEM_IFACE_CARD,
	info: snd_via686a_joystick_info,
	get: snd_via686a_joystick_get,
	put: snd_via686a_joystick_put,
};

/*
 *
 */

static int __devinit snd_via686a_chip_init(via686a_t *chip)
{
	ac97_t ac97;
	unsigned int val;
	int max_count;
	unsigned char pval;

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;

#if 0 /* broken on K7M? */
	/* disable all legacy ports */
	pci_write_config_byte(chip->pci, 0x42, 0);
#endif

	/* deassert ACLink reset, force SYNC */
	pci_write_config_byte(chip->pci, 0x41, 0xe0);
	udelay(100);
	pci_write_config_byte(chip->pci, 0x41, 0x00);
	udelay(100);
	/* ACLink on, deassert ACLink reset, VSR, SGD data out */
	/* note - FM data out has trouble with non VRA codecs !! */
	pci_write_config_byte(chip->pci, 0x41, 0xcc);
	udelay(100);

	/* wait until codec ready */
	max_count = ((3 * HZ) / 4) + 1;
	do {
		pci_read_config_byte(chip->pci, 0x40, &pval);
		if (pval & 0x01) /* primary codec ready */
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (--max_count > 0);

	if ((val = snd_via686a_codec_xread(chip)) & VIA_REG_AC97_BUSY)
		snd_printk("AC'97 codec is not ready [0x%x]\n", val);

	/* and then reset codec.. */
	snd_via686a_codec_write(&ac97, AC97_RESET, 0x0000);

	/* check the primary codec */
	snd_via686a_codec_xwrite(chip, VIA_REG_AC97_READ |
				 VIA_REG_AC97_PRIMARY_VALID |
				 (VIA_REG_AC97_CODEC_ID_PRIMARY << VIA_REG_AC97_CODEC_ID_SHIFT));
	max_count = ((3 * HZ) / 4) + 1;
	do {
		if ((val = snd_via686a_codec_xread(chip)) & VIA_REG_AC97_PRIMARY_VALID)
			goto __ac97_ok1;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (--max_count > 0);
	snd_printk("Primary AC'97 codec is not valid [0x%x]\n", val);

      __ac97_ok1:
#if 0 /* FIXME: we don't support the second codec yet so skip the detection now.. */
	snd_via686a_codec_xwrite(chip, VIA_REG_AC97_READ |
				 VIA_REG_AC97_SECONDARY_VALID |
				 (VIA_REG_AC97_CODEC_ID_SECONDARY << VIA_REG_AC97_CODEC_ID_SHIFT));
	max_count = ((3 * HZ) / 4) + 1;
	snd_via686a_codec_xwrite(chip, VIA_REG_AC97_READ |
				 VIA_REG_AC97_SECONDARY_VALID |
				 (VIA_REG_AC97_CODEC_ID_SECONDARY << VIA_REG_AC97_CODEC_ID_SHIFT));
	do {
		if ((val = snd_via686a_codec_xread(chip)) & VIA_REG_AC97_SECONDARY_VALID) {
			chip->ac97_secondary = 1;
			goto __ac97_ok2;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
	} while (--max_count > 0);
	/* This is ok, the most of motherboards have only one codec */

      __ac97_ok2:
#endif

#if 0
	{
	unsigned char cmdb;

	pci_read_config_byte(chip->pci, 0x40, &cmdb);
	printk("PCI[0x40] = 0x%x\n", cmdb);
	pci_read_config_byte(chip->pci, 0x42, &cmdb);
	printk("PCI[0x42] = 0x%x\n", cmdb);
	pci_read_config_byte(chip->pci, 0x43, &cmdb);
	printk("PCI[0x43] = 0x%x\n", cmdb);
	pci_read_config_byte(chip->pci, 0x44, &cmdb);
	printk("PCI[0x44] = 0x%x\n", cmdb);
	pci_read_config_byte(chip->pci, 0x48, &cmdb);
	printk("PCI[0x48] = 0x%x\n", cmdb);
	}
#endif

	/* route FM trap to IRQ, disable FM trap */
	pci_write_config_byte(chip->pci, 0x48, 0);
	
	/* disable all GPI interrupts */
	outl(0, chip->port + 0x8c);

	/* disable interrupts */
	snd_via686a_channel_reset(chip, &chip->playback);
	snd_via686a_channel_reset(chip, &chip->capture);
	snd_via686a_channel_reset(chip, &chip->playback_fm);
	return 0;
}

static int snd_via686a_free(via686a_t *chip)
{
	if (chip->irq < 0)
		goto __end_hw;
	/* disable interrupts */
	snd_via686a_channel_reset(chip, &chip->playback);
	snd_via686a_channel_reset(chip, &chip->capture);
	snd_via686a_channel_reset(chip, &chip->playback_fm);
	/* --- */
      __end_hw:
	if(chip->irq >= 0)
		synchronize_irq(chip->irq);
	if (chip->tables)
		snd_free_pci_pages(chip->pci, 3 * sizeof(unsigned int) * VIA_MAX_FRAGS * 2, chip->tables, chip->tables_addr);
	if (chip->res_port) {
		release_resource(chip->res_port);
		kfree_nocheck(chip->res_port);
	}
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	pci_write_config_byte(chip->pci, 0x42, chip->old_legacy);
	pci_write_config_byte(chip->pci, 0x43, chip->old_legacy_cfg);
	snd_magic_kfree(chip);
	return 0;
}

static int snd_via686a_dev_free(snd_device_t *device)
{
	via686a_t *chip = snd_magic_cast(via686a_t, device->device_data, return -ENXIO);
	return snd_via686a_free(chip);
}

static int __devinit snd_via686a_create(snd_card_t * card,
				     struct pci_dev *pci,
				     unsigned int ac97_clock,
				     unsigned char old_legacy,
				     unsigned char old_legacy_cfg,
				     via686a_t ** r_via)
{
	via686a_t *chip;
	int err;
        static snd_device_ops_t ops = {
		dev_free:	snd_via686a_dev_free,
        };

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	if ((chip = snd_magic_kcalloc(via686a_t, 0, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	chip->old_legacy = old_legacy;
	chip->old_legacy_cfg = old_legacy_cfg;

	spin_lock_init(&chip->reg_lock);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->port = pci_resource_start(pci, 0);
	if ((chip->res_port = request_region(chip->port, 256, "VIA686A")) == NULL) {
		snd_via686a_free(chip);
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", chip->port, chip->port + 256 - 1);
		return -EBUSY;
	}
	if (request_irq(pci->irq, snd_via686a_interrupt, SA_INTERRUPT|SA_SHIRQ, "VIA686A", (void *)chip)) {
		snd_via686a_free(chip);
		snd_printk("unable to grab IRQ %d\n", chip->irq);
		return -EBUSY;
	}
	chip->irq = pci->irq;
	if (ac97_clock >= 8000 && ac97_clock <= 48000)
		chip->ac97_clock = ac97_clock;
	pci_read_config_byte(pci, PCI_REVISION_ID, &chip->revision);
	synchronize_irq(pci->irq);

	/* initialize offsets */
	chip->playback.reg_offset = VIA_REG_PLAYBACK_STATUS;
	chip->capture.reg_offset = VIA_REG_CAPTURE_STATUS;
	chip->playback_fm.reg_offset = VIA_REG_FM_STATUS;

	/* allocate buffer descriptor lists */
	/* the start of each lists must be aligned to 8 bytes */
	chip->tables = (unsigned int *)snd_malloc_pci_pages(pci, 3 * sizeof(unsigned int) * VIA_MAX_FRAGS * 2, &chip->tables_addr);
	if (chip->tables == NULL) {
		snd_via686a_free(chip);
		return -ENOMEM;
	}
	/* tables must be aligned to 8 bytes, but the kernel pages
	   are much bigger, so we don't care */
	chip->playback.table = chip->tables;
	chip->playback.table_addr = chip->tables_addr;
	chip->capture.table = chip->playback.table + VIA_MAX_FRAGS * 2;
	chip->capture.table_addr = chip->playback.table_addr + sizeof(unsigned int) * VIA_MAX_FRAGS * 2;
	chip->playback_fm.table = chip->capture.table + VIA_MAX_FRAGS * 2;
	chip->playback_fm.table_addr = chip->capture.table_addr + sizeof(unsigned int) * VIA_MAX_FRAGS * 2;

	if ((err = snd_via686a_chip_init(chip)) < 0) {
		snd_via686a_free(chip);
		return err;
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_via686a_free(chip);
		return err;
	}

	*r_via = chip;
	return 0;
}

static int __devinit snd_via686a_probe(struct pci_dev *pci,
				       const struct pci_device_id *id)
{
	static int dev;
	snd_card_t *card;
	via686a_t *chip;
	int pcm_dev = 0;
	unsigned char legacy;
	unsigned char legacy_cfg;
	int rev_h = 0, err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!snd_enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	pci_read_config_byte(pci, 0x42, &legacy);
	pci_read_config_byte(pci, 0x43, &legacy_cfg);

	if ((err = snd_via686a_create(card,
				      pci,
				      snd_ac97_clock[dev],
				      legacy,
				      legacy_cfg,
				      &chip)) < 0) {
		snd_card_free(card);
		return err;
	}


	if (snd_via686a_mixer(chip) < 0) {
		snd_card_free(card);
		return err;
	}
	if (snd_via686a_pcm(chip, pcm_dev++, NULL) < 0) {
		snd_card_free(card);
		return err;
	}
#if 0
	if (snd_via686a_pcm_fm(chip, pcm_dev++, NULL) < 0) {
		snd_card_free(card);
		return err;
	}
#endif

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
	err = snd_ctl_add(card, snd_ctl_new1(&snd_via686a_joystick_control, chip));
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->driver, "VIA686A");
	strcpy(card->shortname, "VIA 82C686A/B");
	
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

static void __devexit snd_via686a_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	name: "VIA 82C686A/B",
	id_table: snd_via686a_ids,
	probe: snd_via686a_probe,
	remove: __devexit_p(snd_via686a_remove),
};

static int __init alsa_card_via686a_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		printk(KERN_ERR "VIA 82C686A soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_via686a_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_via686a_init)
module_exit(alsa_card_via686a_exit)

#ifndef MODULE

/* format is: snd-via686a=snd_enable,snd_index,snd_id,
			  snd_mpu_port,snd_ac97_clock */

static int __init alsa_card_via686a_setup(char *str)
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

__setup("snd-via686a=", alsa_card_via686a_setup);

#endif /* ifndef MODULE */
