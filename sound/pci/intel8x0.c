/*
 *   ALSA driver for Intel ICH (i8x0) chipsets
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This code also contains alpha support for SiS 735 chipsets provided
 *   by Mike Pieper <mptei@users.sourceforge.net>. We have no datasheet
 *   for SiS735, so the code is not fully functional.
 *
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
#include <sound/ac97_codec.h>
#include <sound/info.h>
#include <sound/mpu401.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Intel 82801AA,82901AB,i810,i820,i830,i840,MX440; SiS 7012");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Intel,82801AA-ICH},"
		"{Intel,82901AB-ICH0},"
		"{Intel,82801BA-ICH2},"
		"{Intel,82801CA-ICH3},"
		"{Intel,82801DB-ICH4},"
		"{Intel,MX440},"
		"{SiS,SI7012},"
		"{NVidia,NForce Audio},"
		"{AMD,AMD768},"
		"{AMD,AMD8111}}");

#define SUPPORT_JOYSTICK 1
#define SUPPORT_MIDI 1

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int snd_ac97_clock[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
#ifdef SUPPORT_JOYSTICK
static int snd_joystick_port[SNDRV_CARDS] =
#ifdef CONFIG_ISA
	{0x200};	/* enable as default */
#else
	{0};	/* disabled */
#endif
#endif
#ifdef SUPPORT_MIDI
static int snd_mpu_port[SNDRV_CARDS]; /* disabled */
#endif

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for Intel i8x0 soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for Intel i8x0 soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable Intel i8x0 soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_ac97_clock, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_ac97_clock, "AC'97 codec clock (0 = auto-detect).");
MODULE_PARM_SYNTAX(snd_ac97_clock, SNDRV_ENABLED ",default:0");
#ifdef SUPPORT_JOYSTICK
MODULE_PARM(snd_joystick_port, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_joystick_port, "Joystick port address for Intel i8x0 soundcard. (0 = disabled)");
MODULE_PARM_SYNTAX(snd_joystick_port, SNDRV_ENABLED ",allows:{{0},{0x200}},dialog:list");
#endif
#ifdef SUPPORT_MIDI
MODULE_PARM(snd_mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_mpu_port, "MPU401 port # for Intel i8x0 driver.");
MODULE_PARM_SYNTAX(snd_mpu_port, SNDRV_ENABLED ",allows:{{0},{0x330},{0x300}},dialog:list");
#endif

/*
 *  Direct registers
 */

#ifndef PCI_DEVICE_ID_INTEL_82801
#define PCI_DEVICE_ID_INTEL_82801       0x2415
#endif
#ifndef PCI_DEVICE_ID_INTEL_82901
#define PCI_DEVICE_ID_INTEL_82901       0x2425
#endif
#ifndef PCI_DEVICE_ID_INTEL_82801BA
#define PCI_DEVICE_ID_INTEL_82801BA     0x2445
#endif
#ifndef PCI_DEVICE_ID_INTEL_440MX
#define PCI_DEVICE_ID_INTEL_440MX       0x7195
#endif
#ifndef PCI_DEVICE_ID_INTEL_ICH3
#define PCI_DEVICE_ID_INTEL_ICH3	0x2485
#endif
#ifndef PCI_DEVICE_ID_INTEL_ICH4
#define PCI_DEVICE_ID_INTEL_ICH4	0x24c5
#endif
#ifndef PCI_DEVICE_ID_SI_7012
#define PCI_DEVICE_ID_SI_7012		0x7012
#endif
#ifndef PCI_DEVICE_ID_NVIDIA_MCP_AUDIO
#define PCI_DEVICE_ID_NVIDIA_MCP_AUDIO	0x01b1
#endif

#define DEVICE_INTEL	0
#define DEVICE_SIS	1

#define ICHREG(ice, x) ((ice)->bmport + ICH_REG_##x)
#define ICHREG2(ice, x) ((ice)->bmport + x)

/* capture block */
#define ICH_REG_PI_BDBAR		0x00	/* dword - buffer descriptor list base address */
#define ICH_REG_PI_CIV			0x04	/* byte - current index value */
#define ICH_REG_PI_LVI			0x05	/* byte - last valid index */
#define   ICH_REG_LVI_MASK		0x1f
#define ICH_REG_PI_SR			0x06	/* byte - status register */
#define   ICH_FIFOE			0x10	/* FIFO error */
#define   ICH_BCIS			0x08	/* buffer completion interrupt status */
#define   ICH_LVBCI			0x04	/* last valid buffer completion interrupt */
#define   ICH_CELV			0x02	/* current equals last valid */
#define   ICH_DCH			0x01	/* DMA controller halted */
#define ICH_REG_PI_PICB			0x08	/* word - position in current buffer */
#define ICH_REG_PI_PIV			0x0a	/* byte - prefetched index value */
#define   ICH_REG_PIV_MASK		0x1f	/* mask */
#define ICH_REG_PI_CR			0x0b	/* byte - control register */
#define   ICH_IOCE			0x10	/* interrupt on completion enable */
#define   ICH_FEIE			0x08	/* fifo error interrupt enable */
#define   ICH_LVBIE			0x04	/* last valid buffer interrupt enable */
#define   ICH_RESETREGS			0x02	/* reset busmaster registers */
#define   ICH_STARTBM			0x01	/* start busmaster operation */
/* playback block */
#define ICH_REG_PO_BDBAR		0x10	/* dword - buffer descriptor list base address */
#define ICH_REG_PO_CIV			0x14	/* byte - current index value */
#define ICH_REG_PO_LVI			0x15	/* byte - last valid command */
#define ICH_REG_PO_SR			0x16	/* byte - status register */
#define ICH_REG_PO_PICB			0x18	/* word - position in current buffer */
#define ICH_REG_PO_PIV			0x1a	/* byte - prefetched index value */
#define ICH_REG_PO_CR			0x1b	/* byte - control register */
/* mic capture block */
#define ICH_REG_MC_BDBAR		0x20	/* dword - buffer descriptor list base address */
#define ICH_REG_MC_CIV			0x24	/* byte - current index value */
#define ICH_REG_MC_LVI			0x25	/* byte - last valid command */
#define ICH_REG_MC_SR			0x26	/* byte - status register */
#define ICH_REG_MC_PICB			0x28	/* word - position in current buffer */
#define ICH_REG_MC_PIV			0x2a	/* byte - prefetched index value */
#define ICH_REG_MC_CR			0x2b	/* byte - control register */
/* global block */
#define ICH_REG_GLOB_CNT		0x2c	/* dword - global control */
#define   ICH_PCM_246_MASK	0x00300000	/* 6 channels (not all chips) */
#define   ICH_PCM_6		0x00200000	/* 6 channels (not all chips) */
#define   ICH_PCM_4		0x00100000	/* 4 channels (not all chips) */
#define   ICH_PCM_2		0x00000000	/* 2 channels (stereo) */
#define   ICH_SRIE		0x00000020	/* secondary resume interrupt enable */
#define   ICH_PRIE		0x00000010	/* primary resume interrupt enable */
#define   ICH_ACLINK		0x00000008	/* AClink shut off */
#define   ICH_AC97WARM		0x00000004	/* AC'97 warm reset */
#define   ICH_AC97COLD		0x00000002	/* AC'97 cold reset */
#define   ICH_GIE		0x00000001	/* GPI interrupt enable */
#define ICH_REG_GLOB_STA		0x30	/* dword - global status */
#define   ICH_MD3		0x00020000	/* modem power down semaphore */
#define   ICH_AD3		0x00010000	/* audio power down semaphore */
#define   ICH_RCS		0x00008000	/* read completion status */
#define   ICH_BIT3		0x00004000	/* bit 3 slot 12 */
#define   ICH_BIT2		0x00002000	/* bit 2 slot 12 */
#define   ICH_BIT1		0x00001000	/* bit 1 slot 12 */
#define   ICH_SRI		0x00000800	/* secondary resume interrupt */
#define   ICH_PRI		0x00000400	/* primary resume interrupt */
#define   ICH_SCR		0x00000200	/* secondary codec ready */
#define   ICH_PCR		0x00000100	/* primary codec ready */
#define   ICH_MCINT		0x00000080	/* MIC capture interrupt */
#define   ICH_POINT		0x00000040	/* playback interrupt */
#define   ICH_PIINT		0x00000020	/* capture interrupt */
#define   ICH_MOINT		0x00000004	/* modem playback interrupt */
#define   ICH_MIINT		0x00000002	/* modem capture interrupt */
#define   ICH_GSCI		0x00000001	/* GPI status change interrupt */
#define ICH_REG_ACC_SEMA		0x34	/* byte - codec write semaphore */
#define   ICH_CAS			0x01	/* codec access semaphore */

#define ICH_MAX_FRAGS		32		/* max hw frags */

/*
 *  
 */

typedef struct {
	unsigned long reg_offset;
	u32 *bdbar;				/* CPU address (32bit) */
	unsigned int bdbar_addr;		/* PCI bus address (32bit) */
	snd_pcm_substream_t *substream;
	unsigned int physbuf;			/* physical address (32bit) */
        unsigned int size;
        unsigned int fragsize;
        unsigned int fragsize1;
        unsigned int position;
        int frags;
        int lvi;
        int lvi_frag;
	int ack;
	int ack_reload;
#ifdef CONFIG_PM
	unsigned char civ_saved;
	unsigned char piv_saved;
	unsigned short picb_saved;
#endif
} ichdev_t;

typedef struct _snd_intel8x0 intel8x0_t;
#define chip_t intel8x0_t

struct _snd_intel8x0 {
	unsigned int device_type;
	char ac97_name[32];
	char ctrl_name[32];

	unsigned long dma_playback_size;
	unsigned long dma_capture_size;
	unsigned long dma_mic_size;

	int irq;

	unsigned long port;
	struct resource *res_port;
	unsigned long bmport;
	struct resource *res_bmport;

	struct pci_dev *pci;
	snd_card_t *card;

	snd_pcm_t *pcm;
	snd_pcm_t *pcm_mic;
	ichdev_t playback;
	ichdev_t capture;
	ichdev_t capture_mic;

	int multi4: 1,
	    multi6: 1;
	int in_ac97_init: 1;

	ac97_t *ac97;
	ac97_t *ac97sec;

	snd_rawmidi_t *rmidi;

	spinlock_t reg_lock;
	spinlock_t ac97_lock;
	snd_info_entry_t *proc_entry;
	
	u32 *bdbars;
	dma_addr_t bdbars_addr;
	
	unsigned int reg_pi_sr;
	unsigned int reg_pi_picb;
	unsigned int reg_po_sr;
	unsigned int reg_po_picb;
	unsigned int reg_mc_sr;
	unsigned int reg_mc_picb;	

#ifdef CONFIG_PM
	int in_suspend;
#endif
};

static struct pci_device_id snd_intel8x0_ids[] __devinitdata = {
	{ 0x8086, 0x2415, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 82801AA */
	{ 0x8086, 0x2425, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 82901AB */
	{ 0x8086, 0x2445, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 82801BA */
	{ 0x8086, 0x2485, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* ICH3 */
	{ 0x8086, 0x24c5, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* ICH4 */
	{ 0x8086, 0x7195, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 440MX */
	{ 0x1039, 0x7012, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_SIS },	/* SI7012 */
	{ 0x10de, 0x01b1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* NFORCE */
	{ 0x1022, 0x764d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* AMD8111 */
	{ 0x1022, 0x7445, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* AMD768 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_intel8x0_ids);

/*
 *  Basic I/O
 */
static int snd_intel8x0_codec_semaphore(intel8x0_t *chip, unsigned int codec)
{
	int time;

	/* codec ready ? */
	if ((inl(ICHREG(chip, GLOB_STA)) & (codec ? ICH_SCR : ICH_PCR)) == 0)
		return -EIO;

	/* Anyone holding a semaphore for 1 msec should be shot... */
	time = 100;
      	do {
      		if (!(inb(ICHREG(chip, ACC_SEMA)) & ICH_CAS))
      			return 0;
		udelay(10);
	} while (time--);

	/* access to some forbidden (non existant) ac97 registers will not
	 * reset the semaphore. So even if you don't get the semaphore, still
	 * continue the access. We don't need the semaphore anyway. */
	snd_printk("codec_semaphore: semaphore is not ready [0x%x][0x%x]\n",
			inb(ICHREG(chip, ACC_SEMA)), inl(ICHREG(chip, GLOB_STA)));
	inw(chip->port);	/* clear semaphore flag */
	/* I don't care about the semaphore */
	return -EBUSY;
}
 
static void snd_intel8x0_codec_write(ac97_t *ac97,
				     unsigned short reg,
				     unsigned short val)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, ac97->private_data, return);
	
	spin_lock(&chip->ac97_lock);
	if (snd_intel8x0_codec_semaphore(chip, ac97->num) < 0) {
		if (! chip->in_ac97_init)
			snd_printk("codec_write %d: semaphore is not ready for register 0x%x\n", ac97->num, reg);
	}
	outw(val, chip->port + reg + ac97->num * 0x80);
	spin_unlock(&chip->ac97_lock);
}

static unsigned short snd_intel8x0_codec_read(ac97_t *ac97,
					      unsigned short reg)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, ac97->private_data, return ~0);
	unsigned short res;
	unsigned int tmp;

	spin_lock(&chip->ac97_lock);
	if (snd_intel8x0_codec_semaphore(chip, ac97->num) < 0) {
		if (! chip->in_ac97_init)
			snd_printk("codec_read %d: semaphore is not ready for register 0x%x\n", ac97->num, reg);
		res = 0xffff;
	} else {
		res = inw(chip->port + reg + ac97->num * 0x80);
		if ((tmp = inl(ICHREG(chip, GLOB_STA))) & ICH_RCS) {
			/* reset RCS and preserve other R/WC bits */
			outl(tmp & ~(ICH_SRI|ICH_PRI|ICH_GSCI), ICHREG(chip, GLOB_STA));
			if (! chip->in_ac97_init)
				snd_printk("codec_read %d: read timeout for register 0x%x\n", ac97->num, reg);
			res = 0xffff;
		}
	}
	spin_unlock(&chip->ac97_lock);
	return res;
}

static int snd_intel8x0_trigger(intel8x0_t *chip, ichdev_t *ichdev, int cmd)
{
	unsigned char val = 0;
	unsigned long port = chip->bmport + ichdev->reg_offset;
	
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		val = ICH_IOCE | ICH_STARTBM;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		val = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val = ICH_IOCE;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		val = ICH_IOCE | ICH_STARTBM;
		break;
	default:
		return -EINVAL;
	}
	outb(val, port + ICH_REG_PI_CR);
	if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		/* reset whole DMA things */
		while (!(inb(port + chip->reg_pi_sr) & ICH_DCH)) ;
		outb(ICH_RESETREGS, port + ICH_REG_PI_CR);
	}
	return 0;
}

static void snd_intel8x0_setup_periods(intel8x0_t *chip, ichdev_t *ichdev) 
{
	int idx;
	u32 *bdbar = ichdev->bdbar;
	unsigned long port = chip->bmport + ichdev->reg_offset;
	int shiftlen = (chip->device_type == DEVICE_SIS) ? 0 : 1;

	outl(ichdev->bdbar_addr, port + ICH_REG_PI_BDBAR);
	if (ichdev->size == ichdev->fragsize) {
		ichdev->ack_reload = ichdev->ack = 2;
		ichdev->fragsize1 = ichdev->fragsize >> 1;
		for (idx = 0; idx < (ICH_REG_LVI_MASK + 1) * 2; idx += 4) {
			bdbar[idx + 0] = cpu_to_le32(ichdev->physbuf);
			bdbar[idx + 1] = cpu_to_le32(0x80000000 | /* interrupt on completion */
						     ichdev->fragsize1 >> shiftlen);
			bdbar[idx + 2] = cpu_to_le32(ichdev->physbuf + (ichdev->size >> 1));
			bdbar[idx + 3] = cpu_to_le32(0x80000000 | /* interrupt on completion */
						     ichdev->fragsize1 >> shiftlen);
		}
		ichdev->frags = 2;
	} else {
		ichdev->ack_reload = ichdev->ack = 1;
		ichdev->fragsize1 = ichdev->fragsize;
		for (idx = 0; idx < (ICH_REG_LVI_MASK + 1) * 2; idx += 2) {
			bdbar[idx + 0] = cpu_to_le32(ichdev->physbuf + (((idx >> 1) * ichdev->fragsize) % ichdev->size));
			bdbar[idx + 1] = cpu_to_le32(0x80000000 | /* interrupt on completion */
						     ichdev->fragsize >> shiftlen);
			// printk("bdbar[%i] = 0x%x [0x%x]\n", idx + 0, bdbar[idx + 0], bdbar[idx + 1]);
		}
		ichdev->frags = ichdev->size / ichdev->fragsize;
	}
	outb(ichdev->lvi = ICH_REG_LVI_MASK, port + ICH_REG_PI_LVI);
	ichdev->lvi_frag = ICH_REG_LVI_MASK % ichdev->frags;
	ichdev->position = 0;
#if 0
	printk("lvi_frag = %i, frags = %i, period_size = 0x%x, period_size1 = 0x%x\n",
			ichdev->lvi_frag, ichdev->frags, ichdev->fragsize, ichdev->fragsize1);
#endif
	/* clear interrupts */
	outb(ICH_FIFOE | ICH_BCIS | ICH_LVBCI, port + chip->reg_pi_sr);
}

/*
 *  Interrupt handler
 */

static inline void snd_intel8x0_update(intel8x0_t *chip, ichdev_t *ichdev)
{
	unsigned long port = chip->bmport + ichdev->reg_offset;
	int ack = 0;

	spin_lock(&chip->reg_lock);
	ichdev->position += ichdev->fragsize1;
	ichdev->position %= ichdev->size;
	ichdev->lvi++;
	ichdev->lvi &= ICH_REG_LVI_MASK;
	outb(ichdev->lvi, port + ICH_REG_PI_LVI);
	ichdev->lvi_frag++;
	ichdev->lvi_frag %= ichdev->frags;
	ichdev->bdbar[ichdev->lvi * 2] = ichdev->physbuf + ichdev->lvi_frag * ichdev->fragsize1;
	// printk("new: bdbar[%i] = 0x%x [0x%x], prefetch = %i, all = 0x%x, 0x%x\n", ichdev->lvi * 2, ichdev->bdbar[ichdev->lvi * 2], ichdev->bdbar[ichdev->lvi * 2 + 1], inb(ICH_REG_PI_PIV + port), inl(port + 4), inb(port + ICH_REG_PI_CR));
	if ((ack = (--ichdev->ack == 0)) != 0)
		ichdev->ack = ichdev->ack_reload;
	spin_unlock(&chip->reg_lock);
	if (ack && ichdev->substream)
		snd_pcm_period_elapsed(ichdev->substream);
	outb(ICH_FIFOE | ICH_BCIS | ICH_LVBCI, port + chip->reg_pi_sr);
}

static void snd_intel8x0_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, dev_id, return);
	unsigned int status;

	spin_lock(&chip->reg_lock);
	status = inl(ICHREG(chip, GLOB_STA));
	if ((status & (ICH_MCINT | ICH_POINT | ICH_PIINT)) == 0) {
		spin_unlock(&chip->reg_lock);
		return;
	}
	/* ack first */
	outl(status & (ICH_MCINT | ICH_POINT | ICH_PIINT), ICHREG(chip, GLOB_STA));
	spin_unlock(&chip->reg_lock);

	if (status & ICH_POINT)
		snd_intel8x0_update(chip, &chip->playback);
	if (status & ICH_PIINT)
		snd_intel8x0_update(chip, &chip->capture);
	if (status & ICH_MCINT)
		snd_intel8x0_update(chip, &chip->capture_mic);
}

/*
 *  PCM part
 */

static int snd_intel8x0_playback_ioctl(snd_pcm_substream_t * substream,
				       unsigned int cmd,
				       void *arg)
{
	int result;
	result = snd_pcm_lib_ioctl(substream, cmd, arg);
	if (result < 0)
		return result;
	return 0;
}

static int snd_intel8x0_capture_ioctl(snd_pcm_substream_t * substream,
				      unsigned int cmd,
				      void *arg)
{
	int result;
	result = snd_pcm_lib_ioctl(substream, cmd, arg);
	if (result < 0)
		return result;
	return 0;
}

static int snd_intel8x0_playback_trigger(snd_pcm_substream_t *substream, int cmd)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_trigger(chip, &chip->playback, cmd);
}

static int snd_intel8x0_capture_trigger(snd_pcm_substream_t *substream, int cmd)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_trigger(chip, &chip->capture, cmd);
}

static int snd_intel8x0_hw_params(snd_pcm_substream_t * substream,
				  snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_intel8x0_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static void snd_intel8x0_setup_multi_channels(intel8x0_t *chip, int channels)
{
	unsigned int cnt = inl(ICHREG(chip, GLOB_CNT)) & ~ICH_PCM_246_MASK;
	if (chip->multi4 && channels == 4)
		cnt |= ICH_PCM_4;
	else if (chip->multi6 && channels == 6)
		cnt |= ICH_PCM_6;
	outl(cnt, ICHREG(chip, GLOB_CNT));
}

static int snd_intel8x0_playback_prepare(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	chip->playback.physbuf = runtime->dma_addr;
	chip->playback.size = snd_pcm_lib_buffer_bytes(substream);
	chip->playback.fragsize = snd_pcm_lib_period_bytes(substream);
	spin_lock(&chip->reg_lock);
	snd_intel8x0_setup_multi_channels(chip, runtime->channels);
	spin_unlock(&chip->reg_lock);
	snd_ac97_set_rate(chip->ac97, AC97_PCM_FRONT_DAC_RATE, runtime->rate);
	snd_intel8x0_setup_periods(chip, &chip->playback);
	return 0;
}

static int snd_intel8x0_capture_prepare(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	chip->capture.physbuf = runtime->dma_addr;
	chip->capture.size = snd_pcm_lib_buffer_bytes(substream);
	chip->capture.fragsize = snd_pcm_lib_period_bytes(substream);
	snd_ac97_set_rate(chip->ac97, AC97_PCM_LR_ADC_RATE, runtime->rate);
	snd_intel8x0_setup_periods(chip, &chip->capture);
	return 0;
}

static snd_pcm_uframes_t snd_intel8x0_playback_pointer(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;

	ptr = chip->playback.fragsize1;
	if (chip->device_type == DEVICE_SIS)
		ptr -= inw(ICHREG2(chip,chip->reg_po_picb));
	else
		ptr -= inw(ICHREG2(chip,chip->reg_po_picb)) << 1;
	ptr += chip->playback.position;
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_intel8x0_capture_pointer(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;

	ptr = chip->capture.fragsize1;
	if (chip->device_type == DEVICE_SIS)
		ptr -= inw(ICHREG2(chip,chip->reg_pi_picb));
	else
		ptr -= inw(ICHREG2(chip,chip->reg_pi_picb)) << 1;
	ptr += chip->capture.position;
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_hardware_t snd_intel8x0_playback =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_RESUME),
	formats:		SNDRV_PCM_FMTBIT_S16_LE,
	rates:			0,
	rate_min:		8000,
	rate_max:		48000,
	channels_min:		2,
	channels_max:		2,
	buffer_bytes_max:	128 * 1024,
	period_bytes_min:	32,
	period_bytes_max:	128 * 1024,
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

static snd_pcm_hardware_t snd_intel8x0_capture =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_RESUME),
	formats:		SNDRV_PCM_FMTBIT_S16_LE,
	rates:			0,
	rate_min:		8000,
	rate_max:		48000,
	channels_min:		2,
	channels_max:		2,
	buffer_bytes_max:	128 * 1024,
	period_bytes_min:	32,
	period_bytes_max:	128 * 1024,
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

static unsigned int channels4[] = {
	2, 4,
};

#define CHANNELS4 sizeof(channels4) / sizeof(channels4[0])

static snd_pcm_hw_constraint_list_t hw_constraints_channels4 = {
	count: CHANNELS4,
	list: channels4,
	mask: 0,
};

static unsigned int channels6[] = {
	2, 4, 6,
};

#define CHANNELS6 sizeof(channels6) / sizeof(channels6[0])

static snd_pcm_hw_constraint_list_t hw_constraints_channels6 = {
	count: CHANNELS6,
	list: channels6,
	mask: 0,
};

static int snd_intel8x0_playback_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	chip->playback.substream = substream;
	runtime->hw = snd_intel8x0_playback;
	runtime->hw.rates = chip->ac97->rates_front_dac;
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	if (chip->device_type == DEVICE_SIS) {
		runtime->hw.buffer_bytes_max = 64*1024;
		runtime->hw.period_bytes_max = 64*1024;
	}
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	if (chip->multi6) {
		runtime->hw.channels_max = 6;
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, &hw_constraints_channels6);
	} else if (chip->multi4) {
		runtime->hw.channels_max = 4;
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, &hw_constraints_channels4);
	}
	return 0;
}

static int snd_intel8x0_capture_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	chip->capture.substream = substream;
	runtime->hw = snd_intel8x0_capture;
	runtime->hw.rates = chip->ac97->rates_adc;
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	if (chip->device_type == DEVICE_SIS) {
		runtime->hw.buffer_bytes_max = 64*1024;
		runtime->hw.period_bytes_max = 64*1024;
	}
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	return 0;
}

static int snd_intel8x0_playback_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->playback.substream = NULL;
	/* disable DAC power */
	snd_ac97_update_bits(chip->ac97, AC97_POWERDOWN, 0x0200, 0x0200);
	return 0;
}

static int snd_intel8x0_capture_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->capture.substream = NULL;
	/* disable ADC power */
	snd_ac97_update_bits(chip->ac97, AC97_POWERDOWN, 0x0100, 0x0100);
	return 0;
}

static snd_pcm_ops_t snd_intel8x0_playback_ops = {
	open:		snd_intel8x0_playback_open,
	close:		snd_intel8x0_playback_close,
	ioctl:		snd_intel8x0_playback_ioctl,
	hw_params:	snd_intel8x0_hw_params,
	hw_free:	snd_intel8x0_hw_free,
	prepare:	snd_intel8x0_playback_prepare,
	trigger:	snd_intel8x0_playback_trigger,
	pointer:	snd_intel8x0_playback_pointer,
};

static snd_pcm_ops_t snd_intel8x0_capture_ops = {
	open:		snd_intel8x0_capture_open,
	close:		snd_intel8x0_capture_close,
	ioctl:		snd_intel8x0_capture_ioctl,
	hw_params:	snd_intel8x0_hw_params,
	hw_free:	snd_intel8x0_hw_free,
	prepare:	snd_intel8x0_capture_prepare,
	trigger:	snd_intel8x0_capture_trigger,
	pointer:	snd_intel8x0_capture_pointer,
};

static void snd_intel8x0_pcm_free(snd_pcm_t *pcm)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pcm->private_data, return);
	chip->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __devinit snd_intel8x0_pcm(intel8x0_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, "Intel ICH", device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_intel8x0_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_intel8x0_capture_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_intel8x0_pcm_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, chip->card->shortname);
	chip->pcm = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 64*1024, 128*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  PCM code - MIC
 */

static int snd_intel8x0_capture_mic_ioctl(snd_pcm_substream_t * substream,
					  unsigned int cmd,
					  void *arg)
{
	int result;
	result = snd_pcm_lib_ioctl(substream, cmd, arg);
	if (result < 0)
		return result;
	return 0;
}

static int snd_intel8x0_capture_mic_trigger(snd_pcm_substream_t * substream,
					    int cmd)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_trigger(chip, &chip->capture_mic, cmd);
}

static int snd_intel8x0_capture_mic_prepare(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	chip->capture_mic.physbuf = runtime->dma_addr;
	chip->capture_mic.size = snd_pcm_lib_buffer_bytes(substream);
	chip->capture_mic.fragsize = snd_pcm_lib_period_bytes(substream);
	snd_ac97_set_rate(chip->ac97, AC97_PCM_MIC_ADC_RATE, runtime->rate);
	snd_intel8x0_setup_periods(chip, &chip->capture_mic);
	return 0;
}

static snd_pcm_uframes_t snd_intel8x0_capture_mic_pointer(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	size_t ptr;

	ptr = chip->capture_mic.fragsize1;
	if (chip->device_type == DEVICE_SIS)
		ptr -= inw(ICHREG2(chip,chip->reg_mc_picb));
	else
		ptr -= inw(ICHREG2(chip,chip->reg_mc_picb)) << 1;
	ptr += chip->capture_mic.position;
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_hardware_t snd_intel8x0_capture_mic =
{
	info:			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE),
	formats:		SNDRV_PCM_FMTBIT_S16_LE,
	rates:			0,
	rate_min:		8000,
	rate_max:		48000,
	channels_min:		1,
	channels_max:		1,
	buffer_bytes_max:	128 * 1024,
	period_bytes_min:	32,
	period_bytes_max:	128 * 1024,
	periods_min:		1,
	periods_max:		1024,
	fifo_size:		0,
};

static int snd_intel8x0_capture_mic_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	chip->capture_mic.substream = substream;
	runtime->hw = snd_intel8x0_capture_mic;
	runtime->hw.rates = chip->ac97->rates_mic_adc;
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	if (chip->device_type == DEVICE_SIS) {
		runtime->hw.buffer_bytes_max = 64*1024;
		runtime->hw.period_bytes_max = 64*1024;
	}
	return 0;
}

static int snd_intel8x0_capture_mic_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->capture_mic.substream = NULL;
	/* disable ADC power */
	snd_ac97_update_bits(chip->ac97, AC97_EXTENDED_STATUS, 0x4000, 0x4000);
	return 0;
}

static snd_pcm_ops_t snd_intel8x0_capture_mic_ops = {
	open:		snd_intel8x0_capture_mic_open,
	close:		snd_intel8x0_capture_mic_close,
	ioctl:		snd_intel8x0_capture_mic_ioctl,
	hw_params:	snd_intel8x0_hw_params,
	hw_free:	snd_intel8x0_hw_free,
	prepare:	snd_intel8x0_capture_mic_prepare,
	trigger:	snd_intel8x0_capture_mic_trigger,
	pointer:	snd_intel8x0_capture_mic_pointer,
};

static void snd_intel8x0_pcm_mic_free(snd_pcm_t *pcm)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pcm->private_data, return);
	chip->pcm_mic = NULL;
}

static int __devinit snd_intel8x0_pcm_mic(intel8x0_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, "Intel ICH - MIC ADC", device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_intel8x0_capture_mic_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_intel8x0_pcm_mic_free;
	pcm->info_flags = 0;
	sprintf(pcm->name, "%s - MIC ADC", chip->card->shortname);

	chip->pcm_mic = pcm;	
	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  Mixer part
 */

static void snd_intel8x0_codec_init(ac97_t *ac97)
{
	// intel8x0_t *chip = snd_magic_cast(intel8x0_t, ac97->private_data, return);

	/* disable DAC & ADC power */
	snd_ac97_update_bits(ac97, AC97_POWERDOWN, 0x0300, 0x0300);
	/* disable center DAC/surround DAC/LFE DAC/MIC ADC */
	snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, 0xe800, 0xe800);
}

static void snd_intel8x0_mixer_free_ac97(ac97_t *ac97)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, ac97->private_data, return);
	if (ac97->num == 0) {
		chip->ac97 = NULL;
	} else {
		chip->ac97sec = NULL;
	}
}

static int __devinit snd_intel8x0_mixer(intel8x0_t *chip, int ac97_clock)
{
	ac97_t ac97;
	int err;

	chip->in_ac97_init = 1;
	memset(&ac97, 0, sizeof(ac97));
	ac97.write = snd_intel8x0_codec_write;
	ac97.read = snd_intel8x0_codec_read;
	ac97.init = snd_intel8x0_codec_init;
	ac97.private_data = chip;
	ac97.private_free = snd_intel8x0_mixer_free_ac97;
	if (ac97_clock >= 8000 && ac97_clock <= 48000)
		ac97.clock = ac97_clock;
	else
		ac97.clock = 48000;
	if ((err = snd_ac97_mixer(chip->card, &ac97, &chip->ac97)) < 0)
		return err;
#if 0 /* it seems that SDIN signals are mixed together (at least for AD CNR boards) */
	if (inl(ICHREG(chip, GLOB_STA)) & ICH_SCR) {
		ac97.num = 1;
		ac97.addr = 1;
		snd_ac97_mixer(chip->card, &ac97, &chip->ac97sec);
	}
#endif
	if ((inl(ICHREG(chip, GLOB_STA)) & (ICH_PCM_4|ICH_PCM_6)) != (ICH_PCM_4|ICH_PCM_6))
		return 0;
	if ((chip->ac97->scaps & AC97_SCAP_SURROUND_DAC) ||
	    (chip->ac97sec && (chip->ac97sec->scaps & AC97_SCAP_SURROUND_DAC))) {
		chip->multi4 = 1;
		if ((chip->ac97->scaps & AC97_SCAP_CENTER_LFE_DAC) ||
		    (chip->ac97sec && (chip->ac97sec->scaps & AC97_SCAP_CENTER_LFE_DAC)))
			chip->multi6 = 1;
	}
	chip->in_ac97_init = 0;
	return 0;
}


/*
 *
 */

static int snd_intel8x0_chip_init(intel8x0_t *chip)
{
	signed long end_time;
	unsigned int cnt;
	
	/* put logic to right state */
	/* first clear status bits */
	cnt = inl(ICHREG(chip, GLOB_STA));
	outl(cnt & (ICH_RCS | ICH_MCINT | ICH_POINT | ICH_PIINT), ICHREG(chip, GLOB_STA));

	/* ACLink on, 2 channels */
	cnt = inl(ICHREG(chip, GLOB_CNT));
	cnt &= ~(ICH_ACLINK | ICH_PCM_246_MASK);
	/* finish cold or do warm reset */
	cnt |= (cnt & ICH_AC97COLD) == 0 ? ICH_AC97COLD : ICH_AC97WARM;
	outl(cnt, ICHREG(chip, GLOB_CNT));
	end_time = (jiffies + (HZ / 4)) + 1;
	do {
		if ((inl(ICHREG(chip, GLOB_CNT)) & ICH_AC97WARM) == 0)
			goto __ok;
#ifdef CONFIG_PM
		if (chip->in_suspend) {
			mdelay(10);
			continue;
		}
#endif
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (end_time - (signed long)jiffies >= 0);
	snd_printk("AC'97 warm reset still in progress? [0x%x]\n", inl(ICHREG(chip, GLOB_CNT)));
	return -EIO;

      __ok:
	/* wait for primary codec ready status.
	 * Once it becomes ready it should remain ready
	 * as long as we do not disable the ac97 link.
 	 */
	end_time = jiffies + HZ;
	do {
		if (inl(ICHREG(chip, GLOB_STA)) & ICH_PCR)
			goto __ok1;
#ifdef CONFIG_PM
		if (chip->in_suspend) {
			mdelay(10);
			continue;
		}
#endif
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (end_time - (signed long)jiffies >= 0);
	snd_printk("codec_ready: primary codec is not ready [0x%x]\n", inl(ICHREG(chip, GLOB_STA)));
	return -EIO;

      __ok1:
	/* wait for secondary codec ready status. No secondary codec? , ok */
	/* the end_time variable is not initialized again */
	do {
		if (inl(ICHREG(chip, GLOB_STA)) & ICH_SCR)
			break;
#ifdef CONFIG_PM
		if (chip->in_suspend) {
			mdelay(10);
			continue;
		}
#endif
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
	} while (end_time - (signed long)jiffies >= 0);

	inw(chip->port);	/* clear semaphore flag */

	/* disable interrupts */
	outb(0x00, ICHREG(chip, PI_CR));
	outb(0x00, ICHREG(chip, PO_CR));
	outb(0x00, ICHREG(chip, MC_CR));
	/* reset channels */
	outb(ICH_RESETREGS, ICHREG(chip, PI_CR));
	outb(ICH_RESETREGS, ICHREG(chip, PO_CR));
	outb(ICH_RESETREGS, ICHREG(chip, MC_CR));
	/* initialize Buffer Descriptor Lists */
	outl(chip->playback.bdbar_addr, ICHREG(chip, PO_BDBAR));
	outl(chip->capture.bdbar_addr, ICHREG(chip, PI_BDBAR));
	outl(chip->capture_mic.bdbar_addr, ICHREG(chip, MC_BDBAR));
	return 0;
}

static int snd_intel8x0_free(intel8x0_t *chip)
{
	if (chip->irq < 0)
		goto __hw_end;
	/* disable interrupts */
	outb(0x00, ICHREG(chip, PI_CR));
	outb(0x00, ICHREG(chip, PO_CR));
	outb(0x00, ICHREG(chip, MC_CR));
	/* reset channels */
	outb(ICH_RESETREGS, ICHREG(chip, PI_CR));
	outb(ICH_RESETREGS, ICHREG(chip, PO_CR));
	outb(ICH_RESETREGS, ICHREG(chip, MC_CR));
	/* --- */
	if(chip->irq >= 0)
		synchronize_irq(chip->irq);
      __hw_end:
	if (chip->bdbars)
		snd_free_pci_pages(chip->pci, 3 * sizeof(u32) * ICH_MAX_FRAGS * 2, chip->bdbars, chip->bdbars_addr);
	if (chip->res_port) {
		release_resource(chip->res_port);
		kfree_nocheck(chip->res_port);
	}
	if (chip->res_bmport) {
		release_resource(chip->res_bmport);
		kfree_nocheck(chip->res_bmport);
	}
	if (chip->irq >= 0)
		free_irq(chip->irq, (void *)chip);
	snd_magic_kfree(chip);
	return 0;
}

#ifdef CONFIG_PM
/*
 * power management
 */
static void intel8x0_suspend(intel8x0_t *chip)
{
	snd_card_t *card = chip->card;

	chip->in_suspend = 1;
	snd_power_lock(card);
	if (card->power_state == SNDRV_CTL_POWER_D3hot)
		goto __skip;

	snd_pcm_suspend_all(chip->pcm);
	if (chip->pcm_mic)
		snd_pcm_suspend_all(chip->pcm_mic);
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
      __skip:
      	snd_power_unlock(card);
}

static void intel8x0_resume(intel8x0_t *chip)
{
	snd_card_t *card = chip->card;

	snd_power_lock(card);
	if (card->power_state == SNDRV_CTL_POWER_D0)
		goto __skip;

	pci_enable_device(chip->pci);
	snd_intel8x0_chip_init(chip);
	snd_ac97_resume(chip->ac97);

	chip->in_suspend = 0;
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
      __skip:
      	snd_power_unlock(card);
}

#ifndef PCI_OLD_SUSPEND
static int snd_intel8x0_suspend(struct pci_dev *dev, u32 state)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pci_get_drvdata(dev), return -ENXIO);
	intel8x0_suspend(chip);
	return 0;
}
static int snd_intel8x0_resume(struct pci_dev *dev)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pci_get_drvdata(dev), return -ENXIO);
	intel8x0_resume(chip);
	return 0;
}
#else
static void snd_intel8x0_suspend(struct pci_dev *dev)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pci_get_drvdata(dev), return);
	intel8x0_suspend(chip);
}
static void snd_intel8x0_resume(struct pci_dev *dev)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pci_get_drvdata(dev), return);
	intel8x0_resume(chip);
}
#endif

/* callback */
static int snd_intel8x0_set_power_state(snd_card_t *card, unsigned int power_state)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, card->power_state_private_data, return -ENXIO);
	switch (power_state) {
	case SNDRV_CTL_POWER_D0:
	case SNDRV_CTL_POWER_D1:
	case SNDRV_CTL_POWER_D2:
		intel8x0_resume(chip);
		break;
	case SNDRV_CTL_POWER_D3hot:
	case SNDRV_CTL_POWER_D3cold:
		intel8x0_suspend(chip);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#endif /* CONFIG_PM */

#define INTEL8X0_TESTBUF_SIZE	32768	/* enough large for one shot */

static void __devinit intel8x0_measure_ac97_clock(intel8x0_t *chip)
{
	snd_pcm_substream_t *subs;
	unsigned long port;
	unsigned long pos, t;
	unsigned long flags;
	struct timeval start_time, stop_time;

	if (chip->ac97->clock != 48000)
		return; /* specified in module option */

	subs = chip->pcm->streams[0].substream;
	if (! subs || subs->dma_bytes < INTEL8X0_TESTBUF_SIZE) {
		snd_printk("no playback buffer allocated - aborting measure ac97 clock\n");
		return;
	}
	chip->playback.physbuf = subs->dma_addr;
	chip->playback.size = chip->playback.fragsize = INTEL8X0_TESTBUF_SIZE;
	chip->playback.substream = NULL; /* don't process interrupts */

	/* set rate */
	snd_ac97_set_rate(chip->ac97, AC97_PCM_FRONT_DAC_RATE, 48000);
	snd_intel8x0_setup_periods(chip, &chip->playback);
	port = chip->bmport + chip->playback.reg_offset;
	spin_lock_irqsave(&chip->reg_lock, flags);
	outb(ICH_IOCE | ICH_STARTBM, port + ICH_REG_PI_CR); /* trigger */
	do_gettimeofday(&start_time);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
#if 0
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ / 20);
#else
	/* FIXME: schedule() can take too long time and overlap the boundary.. */
	mdelay(50);
#endif
	spin_lock_irqsave(&chip->reg_lock, flags);
	/* check the position */
	pos = chip->playback.fragsize1;
	if (chip->device_type == DEVICE_SIS)
		pos -= inw(ICHREG2(chip,chip->reg_po_picb));
	else
		pos -= inw(ICHREG2(chip,chip->reg_po_picb)) << 1;
	pos += chip->playback.position;
	do_gettimeofday(&stop_time);
	outb(0, port + ICH_REG_PI_CR); /* stop */
	/* reset whole DMA things */
	while (!(inb(port + chip->reg_pi_sr) & ICH_DCH))
		;
	outb(ICH_RESETREGS, port + ICH_REG_PI_CR);
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	t = stop_time.tv_sec - start_time.tv_sec;
	t *= 1000000;
	if (stop_time.tv_usec < start_time.tv_usec)
		t -= start_time.tv_usec - stop_time.tv_usec;
	else
		t += stop_time.tv_usec - start_time.tv_usec;
	if (t == 0) {
		snd_printk(KERN_ERR "?? calculation error..\n");
		return;
	}
	pos = (pos / 4) * 1000;
	pos = (pos / t) * 1000 + ((pos % t) * 1000) / t;
	if (pos < 40000 || pos >= 60000) 
		/* abnormal value. hw problem? */
		printk(KERN_INFO "intel8x0: measured clock %ld rejected\n", pos);
	else if (pos < 47500 || pos > 48500)
		/* not 48000Hz, tuning the clock.. */
		chip->ac97->clock = (chip->ac97->clock * 48000) / pos;
	printk(KERN_INFO "intel8x0: clocking to %d\n", chip->ac97->clock);
}

static int snd_intel8x0_dev_free(snd_device_t *device)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, device->device_data, return -ENXIO);
	return snd_intel8x0_free(chip);
}

static int __devinit snd_intel8x0_create(snd_card_t * card,
					 struct pci_dev *pci,
					 unsigned long device_type,
					 intel8x0_t ** r_intel8x0)
{
	intel8x0_t *chip;
	int err;
	static snd_device_ops_t ops = {
		dev_free:	snd_intel8x0_dev_free,
	};

	*r_intel8x0 = NULL;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = snd_magic_kcalloc(intel8x0_t, 0, GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->ac97_lock);
	chip->device_type = device_type;
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->port = pci_resource_start(pci, 0);
	sprintf(chip->ac97_name, "%s - AC'97", card->shortname);
	if ((chip->res_port = request_region(chip->port, 256, chip->ac97_name)) == NULL) {
		snd_intel8x0_free(chip);
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", chip->port, chip->port + 256 - 1);
		return -EBUSY;
	}
	sprintf(chip->ctrl_name, "%s - Controller", card->shortname);
	chip->bmport = pci_resource_start(pci, 1);
	if ((chip->res_bmport = request_region(chip->bmport, 64, chip->ctrl_name)) == NULL) {
		snd_intel8x0_free(chip);
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", chip->bmport, chip->bmport + 64 - 1);
		return -EBUSY;
	}
	if (request_irq(pci->irq, snd_intel8x0_interrupt, SA_INTERRUPT|SA_SHIRQ, card->shortname, (void *)chip)) {
		snd_intel8x0_free(chip);
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		return -EBUSY;
	}
	chip->irq = pci->irq;
	pci_set_master(pci);
	synchronize_irq(chip->irq);

	/* initialize offsets */
	chip->reg_pi_sr = ICH_REG_PI_SR;
	chip->reg_pi_picb = ICH_REG_PI_PICB;
	chip->reg_po_sr = ICH_REG_PO_SR;
	chip->reg_po_picb = ICH_REG_PO_PICB;
	chip->reg_mc_sr = ICH_REG_MC_SR;
	chip->reg_mc_picb = ICH_REG_MC_PICB;
	if (device_type == DEVICE_SIS) {
		chip->reg_pi_sr = ICH_REG_PI_PICB;
		chip->reg_pi_picb = ICH_REG_PI_SR;
		chip->reg_po_sr = ICH_REG_PO_PICB;
		chip->reg_po_picb = ICH_REG_PO_SR;
		chip->reg_mc_sr = ICH_REG_MC_PICB;
		chip->reg_mc_picb = ICH_REG_MC_SR;
	}
	chip->playback.reg_offset = 0x10;
	chip->capture.reg_offset = 0;
	chip->capture_mic.reg_offset = 0x20;

	/* allocate buffer descriptor lists */
	/* the start of each lists must be aligned to 8 bytes */
	chip->bdbars = (u32 *)snd_malloc_pci_pages(pci, 3 * sizeof(unsigned int) * ICH_MAX_FRAGS * 2, &chip->bdbars_addr);
	if (chip->bdbars == NULL) {
		snd_intel8x0_free(chip);
		return -ENOMEM;
	}
	/* tables must be aligned to 8 bytes here, but the kernel pages
	   are much bigger, so we don't care (on i386) */
#ifndef __i386__
	/* .. not sure on other architectures, so we check now. */
	if (chip->bdbars_addr & ~((dma_addr_t)0xffffffff | 0x07)) {
		snd_printk("invalid i/o port address %lx\n", (unsigned long)chip->bdbars_addr);
		snd_intel8x0_free(chip);
		return -ENOMEM;
	}
#endif
	chip->playback.bdbar = chip->bdbars; /* crop to 32bit */
	chip->playback.bdbar_addr = (unsigned int)chip->bdbars_addr;
	chip->capture.bdbar = chip->playback.bdbar + ICH_MAX_FRAGS * 2;
	chip->capture.bdbar_addr = chip->playback.bdbar_addr + sizeof(u32) * ICH_MAX_FRAGS * 2;
	chip->capture_mic.bdbar = chip->capture.bdbar + ICH_MAX_FRAGS * 2;
	chip->capture_mic.bdbar_addr = chip->capture.bdbar_addr + sizeof(u32) * ICH_MAX_FRAGS * 2;

	if ((err = snd_intel8x0_chip_init(chip)) < 0) {
		snd_intel8x0_free(chip);
		return err;
	}

#ifdef CONFIG_PM
	card->set_power_state = snd_intel8x0_set_power_state;
	card->power_state_private_data = chip;
#endif

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_intel8x0_free(chip);
		return err;
	}

	*r_intel8x0 = chip;
	return 0;
}

static struct shortname_table {
	unsigned int id;
	const char *s;
} shortnames[] __devinitdata = {
	{ PCI_DEVICE_ID_INTEL_82801, "Intel 82801AA-ICH" },
	{ PCI_DEVICE_ID_INTEL_82901, "Intel 82901AB-ICH0" },
	{ PCI_DEVICE_ID_INTEL_82801BA, "Intel 82801BA-ICH2" },
	{ PCI_DEVICE_ID_INTEL_440MX, "Intel 440MX" },
	{ PCI_DEVICE_ID_INTEL_ICH3, "Intel 82801CA-ICH3" },
	{ PCI_DEVICE_ID_INTEL_ICH4, "Intel 82801DB-ICH4" },
	{ PCI_DEVICE_ID_SI_7012, "SiS SI7012" },
	{ PCI_DEVICE_ID_NVIDIA_MCP_AUDIO, "NVidia NForce" },
	{ 0x764d, "AMD AMD8111" },
	{ 0x7445, "AMD AMD768" },
	{ 0, 0 },
};

static int __devinit snd_intel8x0_probe(struct pci_dev *pci,
					const struct pci_device_id *id)
{
	static int dev;
	snd_card_t *card;
	intel8x0_t *chip;
	int pcm_dev = 0, err;
	struct shortname_table *name;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!snd_enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	strcpy(card->driver, "ICH");
	strcpy(card->shortname, "Intel ICH");
	for (name = shortnames; name->id; name++) {
		if (pci->device == name->id) {
			strcpy(card->shortname, name->s);
			break;
		}
	}

	if ((err = snd_intel8x0_create(card, pci, id->driver_data, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_intel8x0_mixer(chip, snd_ac97_clock[dev])) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_intel8x0_pcm(chip, pcm_dev++, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if (chip->ac97->ext_id & 0x0008) {	/* MIC VRM */
		if ((err = snd_intel8x0_pcm_mic(chip, pcm_dev++, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	
	if (snd_mpu_port[dev] == 0x300 || snd_mpu_port[dev] == 0x330) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_CMIPCI,
					       snd_mpu_port[dev], 0,
					       -1, 0, &chip->rmidi)) < 0) {
			printk(KERN_ERR "intel8x0: no UART401 device at 0x%x, skipping.\n", snd_mpu_port[dev]);
			snd_mpu_port[dev] = 0;
		}
	} else
		snd_mpu_port[dev] = 0;

	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, chip->port, chip->irq);

	if (! snd_ac97_clock[dev])
		intel8x0_measure_ac97_clock(chip);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, chip);
	dev++;
	return 0;
}

static void __devexit snd_intel8x0_remove(struct pci_dev *pci)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pci_get_drvdata(pci), return);
	if (chip)
		snd_card_free(chip->card);
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	name: "Intel ICH",
	id_table: snd_intel8x0_ids,
	probe: snd_intel8x0_probe,
	remove: __devexit_p(snd_intel8x0_remove),
#ifdef CONFIG_PM
	suspend: snd_intel8x0_suspend,
	resume: snd_intel8x0_resume,
#endif
};


#if defined(SUPPORT_JOYSTICK) || defined(SUPPORT_MIDI)
/*
 * initialize joystick/midi addresses
 */

static int __devinit snd_intel8x0_joystick_probe(struct pci_dev *pci,
						 const struct pci_device_id *id)
{
	static int dev;
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!snd_enable[dev]) {
		dev++;
		return -ENOENT;
	}

	if (snd_joystick_port[dev] > 0 || snd_mpu_port[dev] > 0) {
		u16 val;
		pci_read_config_word(pci, 0xe6, &val);
		if (snd_joystick_port[dev] > 0)
			val |= 0x100;
		if (snd_mpu_port[dev] == 0x300 || snd_mpu_port[dev] == 0x330)
			val |= 0x20;
		pci_write_config_word(pci, 0xe6, val | 0x100);

		if (snd_mpu_port[dev] == 0x300 || snd_mpu_port[dev] == 0x330) {
			u8 b;
			pci_read_config_byte(pci, 0xe2, &b);
			if (snd_mpu_port[dev] == 0x300)
				b |= 0x08;
			else
				b &= ~0x08;
			pci_write_config_byte(pci, 0xe2, b);
		}
	}
	return 0;
}

static struct pci_device_id snd_intel8x0_joystick_ids[] __devinitdata = {
	{ 0x8086, 0x2410, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* 82801AA */
	{ 0x8086, 0x2420, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* 82901AB */
	{ 0x8086, 0x2440, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, /* ICH2 */
	{ 0x8086, 0x244c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, /* ICH2M */
	{ 0x8086, 0x248c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* ICH3 */
	// { 0x8086, 0x7195, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* 440MX */
	// { 0x1039, 0x7012, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* SI7012 */
	{ 0x10de, 0x01b2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* NFORCE */
	{ 0, }
};

static struct pci_driver joystick_driver = {
	name: "Intel ICH Joystick",
	id_table: snd_intel8x0_joystick_ids,
	probe: snd_intel8x0_joystick_probe,
};
#endif

static int __init alsa_card_intel8x0_init(void)
{
	int err;

        if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		printk(KERN_ERR "Intel ICH soundcard not found or device busy\n");
#endif
                return err;
        }
#if defined(SUPPORT_JOYSTICK) || defined(SUPPORT_MIDI)
	pci_module_init(&joystick_driver);
#endif
        return 0;

}

static void __exit alsa_card_intel8x0_exit(void)
{
	pci_unregister_driver(&driver);
#if defined(SUPPORT_JOYSTICK) || defined(SUPPORT_MIDI)
	pci_unregister_driver(&joystick_driver);
#endif
}

module_init(alsa_card_intel8x0_init)
module_exit(alsa_card_intel8x0_exit)

#ifndef MODULE

/* format is: snd-intel8x0=snd_enable,snd_index,snd_id,snd_ac97_clock */

static int __init alsa_card_intel8x0_setup(char *str)
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

__setup("snd-intel8x0=", alsa_card_intel8x0_setup);

#endif /* ifndef MODULE */
