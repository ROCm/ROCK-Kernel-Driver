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
MODULE_DESCRIPTION("Intel 82801AA,82901AB,i810,i820,i830,i840,i845,MX440; SiS 7012; Ali 5455");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Intel,82801AA-ICH},"
		"{Intel,82901AB-ICH0},"
		"{Intel,82801BA-ICH2},"
		"{Intel,82801CA-ICH3},"
		"{Intel,82801DB-ICH4},"
		"{Intel,ICH5},"
		"{Intel,MX440},"
		"{SiS,SI7012},"
		"{NVidia,NForce Audio},"
		"{NVidia,NForce2 Audio},"
		"{AMD,AMD768},"
		"{AMD,AMD8111},"
	        "{ALI,M5455}}");

#define SUPPORT_JOYSTICK 1
#define SUPPORT_MIDI 1

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int ac97_clock[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
#ifdef SUPPORT_JOYSTICK
static int joystick_port[SNDRV_CARDS] =
#ifdef CONFIG_ISA
	{0x200};	/* enable as default */
#else
	{0};	/* disabled */
#endif
#endif
#ifdef SUPPORT_MIDI
static int mpu_port[SNDRV_CARDS]; /* disabled */
#endif

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for Intel i8x0 soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for Intel i8x0 soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable Intel i8x0 soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(ac97_clock, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(ac97_clock, "AC'97 codec clock (0 = auto-detect).");
MODULE_PARM_SYNTAX(ac97_clock, SNDRV_ENABLED ",default:0");
#ifdef SUPPORT_JOYSTICK
MODULE_PARM(joystick_port, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(joystick_port, "Joystick port address for Intel i8x0 soundcard. (0 = disabled)");
MODULE_PARM_SYNTAX(joystick_port, SNDRV_ENABLED ",allows:{{0},{0x200}},dialog:list");
#endif
#ifdef SUPPORT_MIDI
MODULE_PARM(mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(mpu_port, "MPU401 port # for Intel i8x0 driver.");
MODULE_PARM_SYNTAX(mpu_port, SNDRV_ENABLED ",allows:{{0},{0x330},{0x300}},dialog:list");
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
#ifndef PCI_DEVICE_ID_INTEL_ICH5
#define PCI_DEVICE_ID_INTEL_ICH5	0x24d5
#endif
#ifndef PCI_DEVICE_ID_SI_7012
#define PCI_DEVICE_ID_SI_7012		0x7012
#endif
#ifndef PCI_DEVICE_ID_NVIDIA_MCP_AUDIO
#define PCI_DEVICE_ID_NVIDIA_MCP_AUDIO	0x01b1
#endif
#ifndef PCI_DEVICE_ID_NVIDIA_MCP2_AUDIO
#define PCI_DEVICE_ID_NVIDIA_MCP2_AUDIO	0x006a
#endif
#ifndef PCI_DEVICE_ID_NVIDIA_MCP3_AUDIO
#define PCI_DEVICE_ID_NVIDIA_MCP3_AUDIO	0x00da
#endif

enum { DEVICE_INTEL, DEVICE_INTEL_ICH4, DEVICE_SIS, DEVICE_ALI };

#define ICHREG(x) ICH_REG_##x

#define DEFINE_REGSET(name,base) \
enum { \
	ICH_REG_##name##_BDBAR	= base + 0x0,	/* dword - buffer descriptor list base address */ \
	ICH_REG_##name##_CIV	= base + 0x04,	/* byte - current index value */ \
	ICH_REG_##name##_LVI	= base + 0x05,	/* byte - last valid index */ \
	ICH_REG_##name##_SR	= base + 0x06,	/* byte - status register */ \
	ICH_REG_##name##_PICB	= base + 0x08,	/* word - position in current buffer */ \
	ICH_REG_##name##_PIV	= base + 0x0a,	/* byte - prefetched index value */ \
	ICH_REG_##name##_CR	= base + 0x0b,	/* byte - control register */ \
};

/* busmaster blocks */
DEFINE_REGSET(OFF, 0);		/* offset */
DEFINE_REGSET(PI, 0x00);	/* PCM in */
DEFINE_REGSET(PO, 0x10);	/* PCM out */
DEFINE_REGSET(MC, 0x20);	/* Mic in */

/* ICH4 busmaster blocks */
DEFINE_REGSET(MC2, 0x40);	/* Mic in 2 */
DEFINE_REGSET(PI2, 0x50);	/* PCM in 2 */
DEFINE_REGSET(SP, 0x60);	/* SPDIF out */

/* values for each busmaster block */

/* LVI */
#define ICH_REG_LVI_MASK		0x1f

/* SR */
#define ICH_FIFOE			0x10	/* FIFO error */
#define ICH_BCIS			0x08	/* buffer completion interrupt status */
#define ICH_LVBCI			0x04	/* last valid buffer completion interrupt */
#define ICH_CELV			0x02	/* current equals last valid */
#define ICH_DCH				0x01	/* DMA controller halted */

/* PIV */
#define ICH_REG_PIV_MASK		0x1f	/* mask */

/* CR */
#define ICH_IOCE			0x10	/* interrupt on completion enable */
#define ICH_FEIE			0x08	/* fifo error interrupt enable */
#define ICH_LVBIE			0x04	/* last valid buffer interrupt enable */
#define ICH_RESETREGS			0x02	/* reset busmaster registers */
#define ICH_STARTBM			0x01	/* start busmaster operation */


/* global block */
#define ICH_REG_GLOB_CNT		0x2c	/* dword - global control */
#define   ICH_PCM_20BIT		0x00400000	/* 20-bit samples (ICH4) */
#define   ICH_PCM_246_MASK	0x00300000	/* 6 channels (not all chips) */
#define   ICH_PCM_6		0x00200000	/* 6 channels (not all chips) */
#define   ICH_PCM_4		0x00100000	/* 4 channels (not all chips) */
#define   ICH_PCM_2		0x00000000	/* 2 channels (stereo) */
#define   ICH_SIS_PCM_246_MASK	0x000000c0	/* 6 channels (SIS7012) */
#define   ICH_SIS_PCM_6		0x00000080	/* 6 channels (SIS7012) */
#define   ICH_SIS_PCM_4		0x00000040	/* 4 channels (SIS7012) */
#define   ICH_SIS_PCM_2		0x00000000	/* 2 channels (SIS7012) */
#define   ICH_TRIE		0x00000040	/* tertiary resume interrupt enable */
#define   ICH_SRIE		0x00000020	/* secondary resume interrupt enable */
#define   ICH_PRIE		0x00000010	/* primary resume interrupt enable */
#define   ICH_ACLINK		0x00000008	/* AClink shut off */
#define   ICH_AC97WARM		0x00000004	/* AC'97 warm reset */
#define   ICH_AC97COLD		0x00000002	/* AC'97 cold reset */
#define   ICH_GIE		0x00000001	/* GPI interrupt enable */
#define ICH_REG_GLOB_STA		0x30	/* dword - global status */
#define   ICH_TRI		0x20000000	/* ICH4: tertiary (AC_SDIN2) resume interrupt */
#define   ICH_TCR		0x10000000	/* ICH4: tertiary (AC_SDIN2) codec ready */
#define   ICH_BCS		0x08000000	/* ICH4: bit clock stopped */
#define   ICH_SPINT		0x04000000	/* ICH4: S/PDIF interrupt */
#define   ICH_P2INT		0x02000000	/* ICH4: PCM2-In interrupt */
#define   ICH_M2INT		0x01000000	/* ICH4: Mic2-In interrupt */
#define   ICH_SAMPLE_CAP	0x00c00000	/* ICH4: sample capability bits (RO) */
#define   ICH_MULTICHAN_CAP	0x00300000	/* ICH4: multi-channel capability bits (RO) */
#define   ICH_MD3		0x00020000	/* modem power down semaphore */
#define   ICH_AD3		0x00010000	/* audio power down semaphore */
#define   ICH_RCS		0x00008000	/* read completion status */
#define   ICH_BIT3		0x00004000	/* bit 3 slot 12 */
#define   ICH_BIT2		0x00002000	/* bit 2 slot 12 */
#define   ICH_BIT1		0x00001000	/* bit 1 slot 12 */
#define   ICH_SRI		0x00000800	/* secondary (AC_SDIN1) resume interrupt */
#define   ICH_PRI		0x00000400	/* primary (AC_SDIN0) resume interrupt */
#define   ICH_SCR		0x00000200	/* secondary (AC_SDIN1) codec ready */
#define   ICH_PCR		0x00000100	/* primary (AC_SDIN0) codec ready */
#define   ICH_MCINT		0x00000080	/* MIC capture interrupt */
#define   ICH_POINT		0x00000040	/* playback interrupt */
#define   ICH_PIINT		0x00000020	/* capture interrupt */
#define   ICH_MOINT		0x00000004	/* modem playback interrupt */
#define   ICH_MIINT		0x00000002	/* modem capture interrupt */
#define   ICH_GSCI		0x00000001	/* GPI status change interrupt */
#define ICH_REG_ACC_SEMA		0x34	/* byte - codec write semaphore */
#define   ICH_CAS		0x01		/* codec access semaphore */
#define ICH_REG_SDM		0x80
#define   ICH_DI2L_MASK		0x000000c0	/* PCM In 2, Mic In 2 data in line */
#define   ICH_DI2L_SHIFT	6
#define   ICH_DI1L_MASK		0x00000030	/* PCM In 1, Mic In 1 data in line */
#define   ICH_DI1L_SHIFT	4
#define   ICH_SE		0x00000008	/* steer enable */
#define   ICH_LDI_MASK		0x00000003	/* last codec read data input */

#define ICH_MAX_FRAGS		32		/* max hw frags */


/*
 * registers for Ali5455
 */

/* ALi 5455 busmaster blocks */
DEFINE_REGSET(AL_PI, 0x40);	/* ALi PCM in */
DEFINE_REGSET(AL_PO, 0x50);	/* Ali PCM out */
DEFINE_REGSET(AL_MC, 0x60);	/* Ali Mic in */
DEFINE_REGSET(AL_CDC_SPO, 0x70);	/* Ali Codec SPDIF out */
DEFINE_REGSET(AL_CLR_SPI, 0xa0);	/* Ali Controller SPDIF in */
DEFINE_REGSET(AL_CLR_SPO, 0xb0);	/* Ali Controller SPDIF out */

enum {
	ICH_REG_ALI_SCR = 0x00,		/* System Control Register */
	ICH_REG_ALI_SSR = 0x04,		/* System Status Register  */
	ICH_REG_ALI_DMACR = 0x08,	/* DMA Control Register    */
	ICH_REG_ALI_FIFOCR1 = 0x0c,	/* FIFO Control Register 1  */
	ICH_REG_ALI_INTERFACECR = 0x10,	/* Interface Control Register */
	ICH_REG_ALI_INTERRUPTCR = 0x14,	/* Interrupt control Register */
	ICH_REG_ALI_INTERRUPTSR = 0x18,	/* Interrupt  Status Register */
	ICH_REG_ALI_FIFOCR2 = 0x1c,	/* FIFO Control Register 2   */
	ICH_REG_ALI_CPR = 0x20,		/* Command Port Register     */
	ICH_REG_ALI_SPR = 0x24,		/* Status Port Register      */
	ICH_REG_ALI_FIFOCR3 = 0x2c,	/* FIFO Control Register 3  */
	ICH_REG_ALI_TTSR = 0x30,	/* Transmit Tag Slot Register */
	ICH_REG_ALI_RTSR = 0x34,	/* Receive Tag Slot  Register */
	ICH_REG_ALI_CSPSR = 0x38,	/* Command/Status Port Status Register */
	ICH_REG_ALI_CAS = 0x3c,		/* Codec Write Semaphore Register */
	ICH_REG_ALI_SPDIFCSR = 0xf8,	/* spdif channel status register  */
	ICH_REG_ALI_SPDIFICS = 0xfc	/* spdif interface control/status  */
};

/* interrupts for the whole chip by interrupt status register finish */
 
#define ALI_INT_SPDIFOUT	(1<<23)	/* controller spdif out INTERRUPT */
#define ALI_INT_SPDIFIN		(1<<22)
#define ALI_INT_CODECSPDIFOUT	(1<<19)
#define ALI_INT_MICIN		(1<<18)
#define ALI_INT_PCMOUT		(1<<17)
#define ALI_INT_PCMIN		(1<<16)
#define ALI_INT_CPRAIS		(1<<7)
#define ALI_INT_SPRAIS		(1<<5)
#define ALI_INT_GPIO		(1<<1)
#define ALI_INT_MASK		(ALI_INT_SPDIFOUT|ALI_INT_CODECSPDIFOUT|ALI_INT_MICIN|ALI_INT_PCMOUT|ALI_INT_PCMIN)

#define ALI_PCM_CH4		0x100
#define ALI_PCM_CH6		0x200
#define ALI_PCM_MASK		(ALI_PCM_CH4 | ALI_PCM_CH6)

/*
 *  
 */

enum { ICHD_PCMIN, ICHD_PCMOUT, ICHD_MIC, ICHD_MIC2, ICHD_PCM2IN, ICHD_SPBAR, ICHD_LAST = ICHD_SPBAR };
enum { ALID_PCMIN, ALID_PCMOUT, ALID_MIC, ALID_AC97SPDIFOUT, ALID_SPDIFIN, ALID_SPDIFOUT, ALID_LAST = ALID_SPDIFOUT };

#define get_ichdev(substream) (ichdev_t *)(substream->runtime->private_data)

typedef struct {
	unsigned int ichd;			/* ich device number */
	unsigned long reg_offset;		/* offset to bmaddr */
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
	unsigned int ack_bit;
	unsigned int roff_sr;
	unsigned int roff_picb;
	unsigned int int_sta_mask;		/* interrupt status mask */
	unsigned int ali_slot;			/* ALI DMA slot */
	ac97_t *ac97;
	unsigned short ac97_rate_regs[3];
	int ac97_rates_idx;
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

	int irq;

	unsigned int mmio;
	unsigned long addr;
	unsigned long remap_addr;
	struct resource *res;
	unsigned int bm_mmio;
	unsigned long bmaddr;
	unsigned long remap_bmaddr;
	struct resource *res_bm;

	struct pci_dev *pci;
	snd_card_t *card;

	snd_pcm_t *pcm;
	snd_pcm_t *pcm_mic;
	snd_pcm_t *pcm_mic2;
	snd_pcm_t *pcm2;
	snd_pcm_t *pcm_spdif;
	snd_pcm_t *pcm_ac97spdif;
	ichdev_t ichd[6];

	int multi4: 1,
	    multi6: 1,
	    smp20bit: 1;
	int in_ac97_init: 1,
	    in_sdin_init: 1;

	ac97_t *ac97[3];
	unsigned int ac97_sdin[3];

	snd_rawmidi_t *rmidi;

	spinlock_t reg_lock;
	spinlock_t ac97_lock;
	
	u32 bdbars_count;
	u32 *bdbars;
	dma_addr_t bdbars_addr;
	u32 int_sta_reg;		/* interrupt status register */
	u32 int_sta_mask;		/* interrupt status mask */
	
#ifdef CONFIG_PM
	int in_suspend;
#endif
};

static struct pci_device_id snd_intel8x0_ids[] __devinitdata = {
	{ 0x8086, 0x2415, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 82801AA */
	{ 0x8086, 0x2425, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 82901AB */
	{ 0x8086, 0x2445, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 82801BA */
	{ 0x8086, 0x2485, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* ICH3 */
	{ 0x8086, 0x24c5, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL_ICH4 }, /* ICH4 */
	{ 0x8086, 0x24d5, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL_ICH4 }, /* ICH5 */
	{ 0x8086, 0x7195, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* 440MX */
	{ 0x1039, 0x7012, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_SIS },	/* SI7012 */
	{ 0x10de, 0x01b1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* NFORCE */
	{ 0x10de, 0x006a, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* NFORCE2 */
	{ 0x10de, 0x00da, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* NFORCE3 */
	{ 0x1022, 0x746d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* AMD8111 */
	{ 0x1022, 0x7445, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_INTEL },	/* AMD768 */
	{ 0x10b9, 0x5455, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DEVICE_ALI },   /* Ali5455 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_intel8x0_ids);

/*
 *  Lowlevel I/O - busmaster
 */

static u8 igetbyte(intel8x0_t *chip, u32 offset)
{
	if (chip->bm_mmio)
		return readb(chip->remap_bmaddr + offset);
	else
		return inb(chip->bmaddr + offset);
}

static u16 igetword(intel8x0_t *chip, u32 offset)
{
	if (chip->bm_mmio)
		return readw(chip->remap_bmaddr + offset);
	else
		return inw(chip->bmaddr + offset);
}

static u32 igetdword(intel8x0_t *chip, u32 offset)
{
	if (chip->bm_mmio)
		return readl(chip->remap_bmaddr + offset);
	else
		return inl(chip->bmaddr + offset);
}

static void iputbyte(intel8x0_t *chip, u32 offset, u8 val)
{
	if (chip->bm_mmio)
		writeb(val, chip->remap_bmaddr + offset);
	else
		outb(val, chip->bmaddr + offset);
}

static void iputword(intel8x0_t *chip, u32 offset, u16 val)
{
	if (chip->bm_mmio)
		writew(val, chip->remap_bmaddr + offset);
	else
		outw(val, chip->bmaddr + offset);
}

static void iputdword(intel8x0_t *chip, u32 offset, u32 val)
{
	if (chip->bm_mmio)
		writel(val, chip->remap_bmaddr + offset);
	else
		outl(val, chip->bmaddr + offset);
}

/*
 *  Lowlevel I/O - AC'97 registers
 */

static u16 iagetword(intel8x0_t *chip, u32 offset)
{
	if (chip->mmio)
		return readw(chip->remap_addr + offset);
	else
		return inw(chip->addr + offset);
}

static void iaputword(intel8x0_t *chip, u32 offset, u16 val)
{
	if (chip->mmio)
		writew(val, chip->remap_addr + offset);
	else
		outw(val, chip->addr + offset);
}

/*
 *  Basic I/O
 */

/*
 * access to AC97 codec via normal i/o (for ICH and SIS7012)
 */
static int snd_intel8x0_codec_semaphore(intel8x0_t *chip, unsigned int codec)
{
	int time;
	
	if (codec > 2)
		return -EIO;
	if (chip->in_sdin_init) {
		/* we don't know the ready bit assignment at the moment */
		/* so we check any */
		codec = ICH_PCR | ICH_SCR | ICH_TCR;
	} else {
		if (chip->device_type == DEVICE_INTEL_ICH4) {
			switch (chip->ac97_sdin[codec]) {
			case 0: codec = ICH_PCR; break;
			case 1: codec = ICH_SCR; break;
			case 2: codec = ICH_TCR; break;
			}
		} else {
			switch (codec) {
			case 0: codec = ICH_PCR; break;
			case 1: codec = ICH_SCR; break;
			case 2: codec = ICH_TCR; break;
			}
		}
	}

	/* codec ready ? */
	if ((igetdword(chip, ICHREG(GLOB_STA)) & codec) == 0)
		return -EIO;

	/* Anyone holding a semaphore for 1 msec should be shot... */
	time = 100;
      	do {
      		if (!(igetbyte(chip, ICHREG(ACC_SEMA)) & ICH_CAS))
      			return 0;
		udelay(10);
	} while (time--);

	/* access to some forbidden (non existant) ac97 registers will not
	 * reset the semaphore. So even if you don't get the semaphore, still
	 * continue the access. We don't need the semaphore anyway. */
	snd_printk("codec_semaphore: semaphore is not ready [0x%x][0x%x]\n",
			igetbyte(chip, ICHREG(ACC_SEMA)), igetdword(chip, ICHREG(GLOB_STA)));
	iagetword(chip, 0);	/* clear semaphore flag */
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
	iaputword(chip, reg + ac97->num * 0x80, val);
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
		res = iagetword(chip, reg + ac97->num * 0x80);
		if ((tmp = igetdword(chip, ICHREG(GLOB_STA))) & ICH_RCS) {
			/* reset RCS and preserve other R/WC bits */
			iputdword(chip, ICHREG(GLOB_STA), tmp & ~(ICH_SRI|ICH_PRI|ICH_TRI|ICH_GSCI));
			if (! chip->in_ac97_init)
				snd_printk("codec_read %d: read timeout for register 0x%x\n", ac97->num, reg);
			res = 0xffff;
		}
	}
	spin_unlock(&chip->ac97_lock);
	return res;
}

/*
 * access to AC97 for Ali5455
 */
static int snd_intel8x0_ali_codec_ready(intel8x0_t *chip, int mask)
{
	int count = 0;
	for (count = 0; count < 0x7f; count++) {
		int val = igetbyte(chip, ICHREG(ALI_CSPSR));
		if (val & mask)
			return 0;
	}
	snd_printd(KERN_WARNING "intel8x0: AC97 codec ready timeout.\n");
	return -EBUSY;
}

static int snd_intel8x0_ali_codec_semaphore(intel8x0_t *chip)
{
	int time = 100;
	do {
		if (igetdword(chip, ICHREG(ALI_CAS)) & 0x80000000)
			return snd_intel8x0_ali_codec_ready(chip, 0x08);
		udelay(1);
	} while (time--);
	snd_printk(KERN_WARNING "intel8x0: AC97 codec semaphore timeout.\n");
	return -EBUSY;
}

static unsigned short snd_intel8x0_ali_codec_read(ac97_t *ac97, unsigned short reg)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, ac97->private_data, return ~0);
	unsigned short data, reg2;

	spin_lock(&chip->ac97_lock);
	if (snd_intel8x0_ali_codec_semaphore(chip))
		goto __err;
	reg |= 0x0080;
	iputword(chip, ICHREG(ALI_CPR) + 2, reg | 0x0080);
	if (snd_intel8x0_ali_codec_ready(chip, 0x02))
		goto __err;
	data = igetword(chip, ICHREG(ALI_SPR));
	reg2 = igetword(chip, ICHREG(ALI_SPR) + 2);
	if (reg != reg2) {
		snd_printd(KERN_WARNING "intel8x0: AC97 read not completed?\n");
		goto __err;
	}
	spin_unlock(&chip->ac97_lock);
	return data;
 __err:
	spin_unlock(&chip->ac97_lock);
	return 0xffff;
}

static void snd_intel8x0_ali_codec_write(ac97_t *ac97, unsigned short reg, unsigned short val)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, ac97->private_data, return);

	spin_lock(&chip->ac97_lock);
	if (snd_intel8x0_ali_codec_semaphore(chip)) {
		spin_unlock(&chip->ac97_lock);
		return;
	}
	iputword(chip, ICHREG(ALI_CPR), val);
	iputbyte(chip, ICHREG(ALI_CPR) + 2, reg);
	snd_intel8x0_ali_codec_ready(chip, 0x01);
	spin_unlock(&chip->ac97_lock);
}


/*
 * DMA I/O
 */
static void snd_intel8x0_setup_periods(intel8x0_t *chip, ichdev_t *ichdev) 
{
	int idx;
	u32 *bdbar = ichdev->bdbar;
	unsigned long port = ichdev->reg_offset;
	int shiftlen = (chip->device_type == DEVICE_SIS) ? 0 : 1;

	iputdword(chip, port + ICH_REG_OFF_BDBAR, ichdev->bdbar_addr);
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
	iputbyte(chip, port + ICH_REG_OFF_LVI, ichdev->lvi = ICH_REG_LVI_MASK);
	ichdev->lvi_frag = ICH_REG_LVI_MASK % ichdev->frags;
	ichdev->position = 0;
#if 0
	printk("lvi_frag = %i, frags = %i, period_size = 0x%x, period_size1 = 0x%x\n",
			ichdev->lvi_frag, ichdev->frags, ichdev->fragsize, ichdev->fragsize1);
#endif
	/* clear interrupts */
	iputbyte(chip, port + ichdev->roff_sr, ICH_FIFOE | ICH_BCIS | ICH_LVBCI);
}

/*
 *  Interrupt handler
 */

static inline void snd_intel8x0_update(intel8x0_t *chip, ichdev_t *ichdev)
{
	unsigned long port = ichdev->reg_offset;
	int ack = 0;

	spin_lock(&chip->reg_lock);
	ichdev->position += ichdev->fragsize1;
	ichdev->position %= ichdev->size;
	ichdev->lvi++;
	ichdev->lvi &= ICH_REG_LVI_MASK;
	iputbyte(chip, port + ICH_REG_OFF_LVI, ichdev->lvi);
	ichdev->lvi_frag++;
	ichdev->lvi_frag %= ichdev->frags;
	ichdev->bdbar[ichdev->lvi * 2] = cpu_to_le32(ichdev->physbuf + ichdev->lvi_frag * ichdev->fragsize1);
	// printk("new: bdbar[%i] = 0x%x [0x%x], prefetch = %i, all = 0x%x, 0x%x\n", ichdev->lvi * 2, ichdev->bdbar[ichdev->lvi * 2], ichdev->bdbar[ichdev->lvi * 2 + 1], inb(ICH_REG_OFF_PIV + port), inl(port + 4), inb(port + ICH_REG_OFF_CR));
	if ((ack = (--ichdev->ack == 0)) != 0)
		ichdev->ack = ichdev->ack_reload;
	spin_unlock(&chip->reg_lock);
	if (ack && ichdev->substream)
		snd_pcm_period_elapsed(ichdev->substream);
	iputbyte(chip, port + ichdev->roff_sr, ICH_FIFOE | ICH_BCIS | ICH_LVBCI);
}

static irqreturn_t snd_intel8x0_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, dev_id, return);
	ichdev_t *ichdev;
	unsigned int status;
	unsigned int i;

	spin_lock(&chip->reg_lock);
	status = igetdword(chip, chip->int_sta_reg);
	if ((status & chip->int_sta_mask) == 0) {
		spin_unlock(&chip->reg_lock);
		return IRQ_NONE;
	}
	/* ack first */
	iputdword(chip, chip->int_sta_reg, status & ~chip->int_sta_mask);
	spin_unlock(&chip->reg_lock);

	for (i = 0; i < chip->bdbars_count; i++) {
		ichdev = &chip->ichd[i];
		if (status & ichdev->int_sta_mask)
			snd_intel8x0_update(chip, ichdev);
	}
	return IRQ_HANDLED;
}

/*
 *  PCM part
 */

static int snd_intel8x0_pcm_trigger(snd_pcm_substream_t *substream, int cmd)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	ichdev_t *ichdev = get_ichdev(substream);
	unsigned char val = 0;
	unsigned long port = ichdev->reg_offset;

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
	iputbyte(chip, port + ICH_REG_OFF_CR, val);
	if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		/* reset whole DMA things */
		while (!(igetbyte(chip, port + ichdev->roff_sr) & ICH_DCH)) ;
		iputbyte(chip, port + ICH_REG_OFF_CR, ICH_RESETREGS);
	}
	return 0;
}

static int snd_intel8x0_ali_trigger(snd_pcm_substream_t *substream, int cmd)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	ichdev_t *ichdev = get_ichdev(substream);
	unsigned long port = ichdev->reg_offset;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		iputbyte(chip, port + ICH_REG_OFF_CR, ICH_IOCE);
		iputbyte(chip, ICHREG(ALI_DMACR), 1 << ichdev->ali_slot);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		iputbyte(chip, ICHREG(ALI_DMACR), 1 << (ichdev->ali_slot + 8));
		iputbyte(chip, port + ICH_REG_OFF_CR, 0);
		/* reset whole DMA things */
		while (!(igetbyte(chip, port + ICH_REG_OFF_CR)))
			;
		iputbyte(chip, port + ICH_REG_OFF_CR, ICH_RESETREGS);
		/* clear interrupts */
		iputbyte(chip, port + ICH_REG_OFF_SR, igetbyte(chip, port + ICH_REG_OFF_SR) | 0x1e);
		iputdword(chip, ICHREG(ALI_INTERRUPTSR),
			  igetdword(chip, ICHREG(ALI_INTERRUPTSR)) & (1 << (ichdev->ali_slot + 8)));
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		iputbyte(chip, port + ICH_REG_OFF_CR, 0);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		iputbyte(chip, ICHREG(ALI_DMACR), 1 << ichdev->ali_slot);
		break;
	default:
		return -EINVAL;
	}
	return 0;
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
	unsigned int cnt = igetdword(chip, ICHREG(GLOB_CNT));
	if (chip->device_type == DEVICE_SIS) {
		cnt &= ~ICH_SIS_PCM_246_MASK;
		if (chip->multi4 && channels == 4)
			cnt |= ICH_SIS_PCM_4;
		else if (chip->multi6 && channels == 6)
			cnt |= ICH_SIS_PCM_6;
	} else {
		cnt &= ~ICH_PCM_246_MASK;
		if (chip->multi4 && channels == 4)
			cnt |= ICH_PCM_4;
		else if (chip->multi6 && channels == 6)
			cnt |= ICH_PCM_6;
	}
	iputdword(chip, ICHREG(GLOB_CNT), cnt);
}

static int snd_intel8x0_pcm_prepare(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ichdev_t *ichdev = get_ichdev(substream);
	int i;

	ichdev->physbuf = runtime->dma_addr;
	ichdev->size = snd_pcm_lib_buffer_bytes(substream);
	ichdev->fragsize = snd_pcm_lib_period_bytes(substream);
	if (ichdev->ichd == ICHD_PCMOUT && chip->device_type != DEVICE_ALI) {
		spin_lock(&chip->reg_lock);
		snd_intel8x0_setup_multi_channels(chip, runtime->channels);
		spin_unlock(&chip->reg_lock);
	}
	for (i = 0; i < 3; i++)
		if (ichdev->ac97_rate_regs[i])
			snd_ac97_set_rate(ichdev->ac97, ichdev->ac97_rate_regs[i], runtime->rate);
	snd_intel8x0_setup_periods(chip, ichdev);
	return 0;
}

static snd_pcm_uframes_t snd_intel8x0_pcm_pointer(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	ichdev_t *ichdev = get_ichdev(substream);
	size_t ptr;

	ptr = ichdev->fragsize1;
	if (chip->device_type == DEVICE_SIS)
		ptr -= igetword(chip, ichdev->reg_offset + ichdev->roff_picb);
	else
		ptr -= igetword(chip, ichdev->reg_offset + ichdev->roff_picb) << 1;
	ptr += ichdev->position;
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_hardware_t snd_intel8x0_stream =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_RESUME),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	128 * 1024,
	.period_bytes_min =	32,
	.period_bytes_max =	128 * 1024,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static unsigned int channels4[] = {
	2, 4,
};

#define CHANNELS4 sizeof(channels4) / sizeof(channels4[0])

static snd_pcm_hw_constraint_list_t hw_constraints_channels4 = {
	.count = CHANNELS4,
	.list = channels4,
	.mask = 0,
};

static unsigned int channels6[] = {
	2, 4, 6,
};

#define CHANNELS6 sizeof(channels6) / sizeof(channels6[0])

static snd_pcm_hw_constraint_list_t hw_constraints_channels6 = {
	.count = CHANNELS6,
	.list = channels6,
	.mask = 0,
};

static int snd_intel8x0_pcm_open(snd_pcm_substream_t * substream, ichdev_t *ichdev)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	ichdev->substream = substream;
	runtime->hw = snd_intel8x0_stream;
	if (ichdev->ac97_rates_idx >= 0)
		runtime->hw.rates = ichdev->ac97->rates[ichdev->ac97_rates_idx];
	if (!(runtime->hw.rates & SNDRV_PCM_RATE_8000))
		runtime->hw.rate_min = 48000;
	if (chip->device_type == DEVICE_SIS) {
		runtime->hw.buffer_bytes_max = 64*1024;
		runtime->hw.period_bytes_max = 64*1024;
	}
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	runtime->private_data = ichdev;
	return 0;
}

static int snd_intel8x0_playback_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	err = snd_intel8x0_pcm_open(substream, &chip->ichd[ICHD_PCMOUT]);
	if (chip->multi6) {
		runtime->hw.channels_max = 6;
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, &hw_constraints_channels6);
	} else if (chip->multi4) {
		runtime->hw.channels_max = 4;
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, &hw_constraints_channels4);
	}
	return 0;
}

static int snd_intel8x0_playback_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_PCMOUT].substream = NULL;
	return 0;
}

static int snd_intel8x0_capture_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ICHD_PCMIN]);
}

static int snd_intel8x0_capture_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_PCMIN].substream = NULL;
	return 0;
}

static int snd_intel8x0_mic_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ICHD_MIC]);
}

static int snd_intel8x0_mic_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_MIC].substream = NULL;
	return 0;
}

static int snd_intel8x0_mic2_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ICHD_MIC2]);
}

static int snd_intel8x0_mic2_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_MIC2].substream = NULL;
	return 0;
}

static int snd_intel8x0_capture2_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ICHD_PCM2IN]);
}

static int snd_intel8x0_capture2_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_PCM2IN].substream = NULL;
	return 0;
}

static int snd_intel8x0_spdif_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ICHD_SPBAR]);
}

static int snd_intel8x0_spdif_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_SPBAR].substream = NULL;
	return 0;
}

static int snd_intel8x0_ali_ac97spdifout_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ALID_AC97SPDIFOUT]);
}

static int snd_intel8x0_ali_ac97spdifout_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ALID_AC97SPDIFOUT].substream = NULL;
	return 0;
}

static int snd_intel8x0_ali_spdifin_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ALID_SPDIFIN]);
}

static int snd_intel8x0_ali_spdifin_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ALID_SPDIFIN].substream = NULL;
	return 0;
}

static int snd_intel8x0_ali_spdifout_open(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ALID_SPDIFOUT]);
}

static int snd_intel8x0_ali_spdifout_close(snd_pcm_substream_t * substream)
{
	intel8x0_t *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ALID_SPDIFOUT].substream = NULL;
	return 0;
}

static snd_pcm_ops_t snd_intel8x0_playback_ops = {
	.open =		snd_intel8x0_playback_open,
	.close =	snd_intel8x0_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static snd_pcm_ops_t snd_intel8x0_capture_ops = {
	.open =		snd_intel8x0_capture_open,
	.close =	snd_intel8x0_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static snd_pcm_ops_t snd_intel8x0_capture_mic_ops = {
	.open =		snd_intel8x0_mic_open,
	.close =	snd_intel8x0_mic_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static snd_pcm_ops_t snd_intel8x0_capture_mic2_ops = {
	.open =		snd_intel8x0_mic2_open,
	.close =	snd_intel8x0_mic2_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static snd_pcm_ops_t snd_intel8x0_capture2_ops = {
	.open =		snd_intel8x0_capture2_open,
	.close =	snd_intel8x0_capture2_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static snd_pcm_ops_t snd_intel8x0_spdif_ops = {
	.open =		snd_intel8x0_spdif_open,
	.close =	snd_intel8x0_spdif_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static snd_pcm_ops_t snd_intel8x0_ali_playback_ops = {
	.open =		snd_intel8x0_playback_open,
	.close =	snd_intel8x0_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_ali_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static snd_pcm_ops_t snd_intel8x0_ali_capture_ops = {
	.open =		snd_intel8x0_capture_open,
	.close =	snd_intel8x0_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_ali_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static snd_pcm_ops_t snd_intel8x0_ali_capture_mic_ops = {
	.open =		snd_intel8x0_mic_open,
	.close =	snd_intel8x0_mic_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_ali_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static snd_pcm_ops_t snd_intel8x0_ali_ac97spdifout_ops = {
	.open =		snd_intel8x0_ali_ac97spdifout_open,
	.close =	snd_intel8x0_ali_ac97spdifout_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_ali_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static snd_pcm_ops_t snd_intel8x0_ali_spdifin_ops = {
	.open =		snd_intel8x0_ali_spdifin_open,
	.close =	snd_intel8x0_ali_spdifin_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static snd_pcm_ops_t snd_intel8x0_ali_spdifout_ops = {
	.open =		snd_intel8x0_ali_spdifout_open,
	.close =	snd_intel8x0_ali_spdifout_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
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

	if (chip->device_type == DEVICE_ALI) {
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_intel8x0_ali_playback_ops);
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_intel8x0_ali_capture_ops);
	} else {
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_intel8x0_playback_ops);
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_intel8x0_capture_ops);
	}

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
	err = snd_pcm_new(chip->card, "Intel ICH - MIC ADC", device, 0, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			chip->device_type == DEVICE_ALI ?
				&snd_intel8x0_ali_capture_mic_ops :
				&snd_intel8x0_capture_mic_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_intel8x0_pcm_mic_free;
	pcm->info_flags = 0;
	sprintf(pcm->name, "%s - MIC ADC", chip->card->shortname);

	chip->pcm_mic = pcm;	

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 0, 128*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  PCM code - MIC2
 */

static void snd_intel8x0_pcm_mic2_free(snd_pcm_t *pcm)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pcm->private_data, return);
	chip->pcm_mic2 = NULL;
}

static int __devinit snd_intel8x0_pcm_mic2(intel8x0_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, "Intel ICH - MIC2 ADC", device, 0, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_intel8x0_capture_mic2_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_intel8x0_pcm_mic2_free;
	pcm->info_flags = 0;
	sprintf(pcm->name, "%s - MIC2 ADC", chip->card->shortname);

	chip->pcm_mic2 = pcm;	

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 0, 128*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  PCM code - capture2
 */

static void snd_intel8x0_pcm_capture2_free(snd_pcm_t *pcm)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pcm->private_data, return);
	chip->pcm2 = NULL;
}

static int __devinit snd_intel8x0_pcm_capture2(intel8x0_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, "Intel ICH - ADC2", device, 0, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_intel8x0_capture2_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_intel8x0_pcm_capture2_free;
	pcm->info_flags = 0;
	sprintf(pcm->name, "%s - ADC2", chip->card->shortname);

	chip->pcm2 = pcm;	

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 0, 128*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  PCM code - S/PDIF
 */

static void snd_intel8x0_pcm_spdif_free(snd_pcm_t *pcm)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pcm->private_data, return);
	chip->pcm_spdif = NULL;
}

static int __devinit snd_intel8x0_pcm_spdif(intel8x0_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, "Intel ICH - IEC958", device, 1, 0, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_intel8x0_spdif_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_intel8x0_pcm_spdif_free;
	pcm->info_flags = 0;
	sprintf(pcm->name, "%s - IEC958", chip->card->shortname);

	chip->pcm_spdif = pcm;	

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 64*1024, 128*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  PCM code - ALI S/PDIF
 */

static void snd_intel8x0_ali_spdif_free(snd_pcm_t *pcm)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pcm->private_data, return);
	chip->pcm_spdif = NULL;
}

static int __devinit snd_intel8x0_ali_spdif(intel8x0_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, "Intel ICH - IEC958", device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_intel8x0_ali_spdifout_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_intel8x0_ali_spdifin_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_intel8x0_ali_spdif_free;
	pcm->info_flags = 0;
	sprintf(pcm->name, "%s - IEC958", chip->card->shortname);

	chip->pcm_spdif = pcm;	

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 64*1024, 128*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  PCM code - ALI AC'97 S/PDIF
 */

static void snd_intel8x0_ali_ac97spdif_free(snd_pcm_t *pcm)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, pcm->private_data, return);
	chip->pcm_ac97spdif = NULL;
}

static int __devinit snd_intel8x0_ali_ac97spdif(intel8x0_t *chip, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	err = snd_pcm_new(chip->card, "ALI - AC97 IEC958", device, 1, 0, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_intel8x0_ali_ac97spdifout_ops);

	pcm->private_data = chip;
	pcm->private_free = snd_intel8x0_ali_ac97spdif_free;
	pcm->info_flags = 0;
	sprintf(pcm->name, "%s - AC97 IEC958", chip->card->shortname);

	chip->pcm_ac97spdif = pcm;	

	snd_pcm_lib_preallocate_pci_pages_for_all(chip->pci, pcm, 64*1024, 128*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

/*
 *  Mixer part
 */

static void snd_intel8x0_mixer_free_ac97(ac97_t *ac97)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, ac97->private_data, return);
	chip->ac97[ac97->num] = NULL;
}

static struct _ac97_rate_regs {
	unsigned int ichd;
	unsigned short regs[3];
	short rates_idx;
} ac97_rate_regs[] = {
	{ ICHD_PCMOUT, { AC97_PCM_FRONT_DAC_RATE, AC97_PCM_SURR_DAC_RATE, AC97_PCM_LFE_DAC_RATE }, AC97_RATES_FRONT_DAC },
	{ ICHD_PCMIN, { AC97_PCM_LR_ADC_RATE, 0, 0 }, AC97_RATES_ADC },
	{ ICHD_MIC, { AC97_PCM_MIC_ADC_RATE, 0, 0 }, AC97_RATES_MIC_ADC },
	{ ICHD_MIC2, { AC97_PCM_MIC_ADC_RATE, 0, 0 }, AC97_RATES_MIC_ADC },
	{ ICHD_PCM2IN, { AC97_PCM_LR_ADC_RATE, 0, 0 }, AC97_RATES_ADC },
	{ ICHD_SPBAR, { AC97_SPDIF, 0, 0 }, AC97_RATES_SPDIF },
};

static struct _ac97_ali_rate_regs {
	unsigned int ichd;
	unsigned short regs[3];
	short rates_idx;
} ac97_ali_rate_regs[] = {
	{ ALID_PCMOUT, { AC97_PCM_FRONT_DAC_RATE, AC97_PCM_SURR_DAC_RATE, AC97_PCM_LFE_DAC_RATE }, AC97_RATES_FRONT_DAC },
	{ ALID_PCMIN, { AC97_PCM_LR_ADC_RATE, 0, 0 }, AC97_RATES_ADC },
	{ ALID_MIC, { AC97_PCM_MIC_ADC_RATE, 0, 0 }, AC97_RATES_MIC_ADC },
	{ ALID_AC97SPDIFOUT, { AC97_SPDIF, 0, 0 }, AC97_RATES_SPDIF },
	{ ALID_SPDIFOUT, { 0, 0, 0 }, -1 },
	{ ALID_SPDIFIN, { 0, 0, 0 }, -1 },
};

static struct ac97_quirk ac97_quirks[] = {
	{ 0x1028, 0x0126, "Dell Optiplex GX260", AC97_TUNE_HP_ONLY },
	{ 0x1734, 0x0088, "Fujisu-Siemens D1522", AC97_TUNE_HP_ONLY },
	{ } /* terminator */
};

static int __devinit snd_intel8x0_mixer(intel8x0_t *chip, int ac97_clock)
{
	ac97_t ac97, *x97;
	ichdev_t *ichdev;
	int err, i, num, channels = 2, codecs, _codecs;
	unsigned int glob_sta = 0;

	for (i = 0; i <= ICHD_LAST; i++) {
		if (chip->device_type != DEVICE_ALI) {
			struct _ac97_rate_regs *aregs;
			aregs = &ac97_rate_regs[i];
			ichdev = &chip->ichd[aregs->ichd];
			ichdev->ac97_rate_regs[0] = aregs->regs[0];
			ichdev->ac97_rate_regs[1] = aregs->regs[1];
			ichdev->ac97_rate_regs[2] = aregs->regs[2];
			ichdev->ac97_rates_idx = aregs->rates_idx;
		} else {
			struct _ac97_ali_rate_regs *aregs;
			aregs = &ac97_ali_rate_regs[i];
			ichdev = &chip->ichd[aregs->ichd];
			ichdev->ac97_rate_regs[0] = aregs->regs[0];
			ichdev->ac97_rate_regs[1] = aregs->regs[1];
			ichdev->ac97_rate_regs[2] = aregs->regs[2];
			ichdev->ac97_rates_idx = aregs->rates_idx;
		}
	}
	chip->in_ac97_init = 1;
	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	ac97.private_free = snd_intel8x0_mixer_free_ac97;
	if (ac97_clock >= 8000 && ac97_clock <= 48000)
		ac97.clock = ac97_clock;
	else
		ac97.clock = 48000;
	if (chip->device_type != DEVICE_ALI) {
		glob_sta = igetdword(chip, ICHREG(GLOB_STA));
		ac97.write = snd_intel8x0_codec_write;
		ac97.read = snd_intel8x0_codec_read;
		if (glob_sta & ICH_PCM_6)
			channels = 6;
		else if (glob_sta & ICH_PCM_4)
			channels = 4;
		if (chip->device_type == DEVICE_INTEL_ICH4) {
			codecs = 0;
			if (glob_sta & ICH_PCR)
				codecs++;
			if (glob_sta & ICH_SCR)
				codecs++;
			if (glob_sta & ICH_TCR)
				codecs++;
			chip->in_sdin_init = 1;
			for (i = 0; i < codecs; i++) {
				ac97.num = i;
				snd_intel8x0_codec_read(&ac97, 0);
				chip->ac97_sdin[i] = igetbyte(chip, ICHREG(SDM)) & ICH_LDI_MASK;
			}
			ac97.num = 0;
			chip->in_sdin_init = 0;
		} else {
			codecs = glob_sta & ICH_SCR ? 2 : 1;
		}
	} else {
		ac97.write = snd_intel8x0_ali_codec_write;
		ac97.read = snd_intel8x0_ali_codec_read;
		channels = 6;
		codecs = 1;
		/* detect the secondary codec */
		for (i = 0; i < 100; i++) {
			unsigned int reg = igetdword(chip, ICHREG(ALI_RTSR));
			if (reg & 0x40) {
				codecs = 2;
				break;
			}
			iputdword(chip, ICHREG(ALI_RTSR), reg | 0x40);
			udelay(1);
		}
	}
	ac97.pci = chip->pci;
	if ((err = snd_ac97_mixer(chip->card, &ac97, &x97)) < 0)
		return err;
	chip->ac97[0] = x97;
	snd_ac97_tune_hardware(chip->ac97[0], ac97_quirks);
	chip->ichd[ICHD_PCMOUT].ac97 = x97;
	chip->ichd[ICHD_PCMIN].ac97 = x97;
	if (x97->ext_id & AC97_EI_VRM)
		chip->ichd[ICHD_MIC].ac97 = x97;
	if (x97->ext_id & AC97_EI_SPDIF) {
		if (chip->device_type != DEVICE_ALI)
			chip->ichd[ICHD_SPBAR].ac97 = x97;
		else
			chip->ichd[ALID_AC97SPDIFOUT].ac97 = x97;
	}
	/* make sure, that we have DACs at right slot for rev2.2 */
	if (ac97_is_rev22(x97))
		snd_ac97_update_bits(x97, AC97_EXTENDED_ID, AC97_EI_DACS_SLOT_MASK, 0);
	/* AnalogDevices CNR boards uses special codec chaining */
	/* skip standard test method for secondary codecs in this case */
	if (x97->flags & AC97_AD_MULTI)
		codecs = 1;
	if (codecs < 2)
		goto __skip_secondary;
	for (i = 1, num = 1, _codecs = codecs; num < _codecs; num++) {
		ac97.num = num;
		if ((err = snd_ac97_mixer(chip->card, &ac97, &x97)) < 0) {
			snd_printk("Unable to initialize codec #%i [device = %i, GLOB_STA = 0x%x]\n", i, chip->device_type, glob_sta);
			codecs--;
			continue;
		}
		chip->ac97[i++] = x97;
		if (!ac97_is_audio(x97))
			continue;
		switch (chip->device_type) {
		case DEVICE_INTEL_ICH4:
			if (chip->ichd[ICHD_PCM2IN].ac97 == NULL)
				chip->ichd[ICHD_PCM2IN].ac97 = x97;
			if (x97->ext_id & AC97_EI_VRM) {
				if (chip->ichd[ICHD_MIC].ac97 == NULL)
					chip->ichd[ICHD_MIC].ac97 = x97;
				else if (chip->ichd[ICHD_MIC2].ac97 == NULL &&
					 chip->ichd[ICHD_PCM2IN].ac97 == x97)
					chip->ichd[ICHD_MIC2].ac97 = x97;
			}
			if (x97->ext_id & AC97_EI_SPDIF) {
				if (chip->ichd[ICHD_SPBAR].ac97 == NULL)
					chip->ichd[ICHD_SPBAR].ac97 = x97;
			}
			break;
		default:
			if (x97->ext_id & AC97_EI_VRM) {
				if (chip->ichd[ICHD_MIC].ac97 == NULL)
					chip->ichd[ICHD_MIC].ac97 = x97;
			}
			break;
		}
	}
	
      __skip_secondary:
	if (chip->device_type == DEVICE_INTEL_ICH4) {
		u8 tmp = igetbyte(chip, ICHREG(SDM));
		tmp &= ~(ICH_DI2L_MASK|ICH_DI1L_MASK);
		if (chip->ichd[ICHD_PCM2IN].ac97) {
			tmp |= ICH_SE;	/* steer enable for multiple SDINs */
			tmp |= chip->ac97_sdin[0] << ICH_DI1L_SHIFT;
			tmp |= chip->ac97_sdin[chip->ichd[ICHD_PCM2IN].ac97->num] << ICH_DI2L_SHIFT;
		} else {
			tmp &= ~ICH_SE;
		}
		iputbyte(chip, ICHREG(SDM), tmp);
	}
      	for (i = 0; i < codecs; i++) {
		x97 = chip->ac97[i];
		if (!ac97_is_audio(x97))
			continue;
		if (x97->scaps & AC97_SCAP_SURROUND_DAC)
			chip->multi4 = 1;
	}
      	for (i = 0; i < codecs && chip->multi4; i++) {
		x97 = chip->ac97[i];
		if (!ac97_is_audio(x97))
			continue;
		if (x97->scaps & AC97_SCAP_CENTER_LFE_DAC)
			chip->multi6 = 1;
	}
	if (codecs > 1) {
		/* assign right slots for rev2.2 codecs */
		i = 1;
		if (chip->multi4)
			goto __6ch;
		for ( ; i < codecs; i++) {
			x97 = chip->ac97[i];
			if (!ac97_is_audio(x97))
				continue;
			if (ac97_is_rev22(x97)) {
				snd_ac97_update_bits(x97, AC97_EXTENDED_ID, AC97_EI_DACS_SLOT_MASK, 1);
				chip->multi4 = 1;
				break;
			}
		}
	      __6ch:
		for ( ; i < codecs && chip->multi4; i++) {
			x97 = chip->ac97[i];
			if (!ac97_is_audio(x97))
				continue;
			if (ac97_is_rev22(x97)) {
				snd_ac97_update_bits(x97, AC97_EXTENDED_ID, AC97_EI_DACS_SLOT_MASK, 2);
				chip->multi6 = 1;
				break;
			}
		}
		/* ok, some older codecs might support only AMAP */
		if (!chip->multi4) {
			for (i = 1; i < codecs; i++) {
				x97 = chip->ac97[i];
				if (!ac97_is_audio(x97))
					continue;
				if (ac97_can_amap(x97)) {
					if (x97->addr == 1) {
						chip->multi4 = 1;
						break;
					}
				}
			}
			for ( ; i < codecs && chip->multi4; i++) {
				if (!ac97_is_audio(x97))
					continue;
				if (ac97_can_amap(x97)) {
					if (x97->addr == 2) {
						chip->multi6 = 1;
						break;
					}
				}
			}
		}
	}
	chip->in_ac97_init = 0;
	return 0;
}


/*
 *
 */

static void do_ali_reset(intel8x0_t *chip)
{
	iputdword(chip, ICHREG(ALI_SCR), 0x8000000);
	iputdword(chip, ICHREG(ALI_FIFOCR1), 0x83838383);
	iputdword(chip, ICHREG(ALI_FIFOCR2), 0x83838383);
	iputdword(chip, ICHREG(ALI_INTERFACECR), 0x04080002); /* no spdif? */
	iputdword(chip, ICHREG(ALI_INTERRUPTCR), 0x00000000);
	iputdword(chip, ICHREG(ALI_INTERRUPTSR), 0x00000000);
}

static void do_delay(intel8x0_t *chip)
{
#ifdef CONFIG_PM
	if (chip->in_suspend) {
		mdelay((1000 + HZ - 1) / HZ);
		return;
	}
#endif
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(1);
}

static int snd_intel8x0_ich_chip_init(intel8x0_t *chip)
{
	unsigned long end_time;
	unsigned int cnt, status, nstatus;
	
	/* put logic to right state */
	/* first clear status bits */
	cnt = igetdword(chip, ICHREG(GLOB_STA));
	iputdword(chip, ICHREG(GLOB_STA), cnt & (ICH_RCS | ICH_MCINT | ICH_POINT | ICH_PIINT));

	/* ACLink on, 2 channels */
	cnt = igetdword(chip, ICHREG(GLOB_CNT));
	cnt &= ~(ICH_ACLINK | ICH_PCM_246_MASK);
	/* finish cold or do warm reset */
	cnt |= (cnt & ICH_AC97COLD) == 0 ? ICH_AC97COLD : ICH_AC97WARM;
	iputdword(chip, ICHREG(GLOB_CNT), cnt);
	end_time = (jiffies + (HZ / 4)) + 1;
	do {
		if ((igetdword(chip, ICHREG(GLOB_CNT)) & ICH_AC97WARM) == 0)
			goto __ok;
		do_delay(chip);
	} while (time_after_eq(end_time, jiffies));
	snd_printk("AC'97 warm reset still in progress? [0x%x]\n", igetdword(chip, ICHREG(GLOB_CNT)));
	return -EIO;

      __ok:
	/* wait for any codec ready status.
	 * Once it becomes ready it should remain ready
	 * as long as we do not disable the ac97 link.
 	 */
	end_time = jiffies + HZ;
	do {
		status = igetdword(chip, ICHREG(GLOB_STA)) & (ICH_PCR | ICH_SCR | ICH_TCR);
		if (status)
			goto __ok1;
		do_delay(chip);
	} while (time_after_eq(end_time, jiffies));
	snd_printk(KERN_ERR "codec_ready: codec is not ready [0x%x]\n", igetdword(chip, ICHREG(GLOB_STA)));
	return -EIO;

      __ok1:
      	if (status == (ICH_PCR | ICH_SCR | ICH_TCR))
      		goto __ok3;
	/* wait for other codecs ready status. No secondary codecs? , ok */
	end_time = jiffies + HZ / 4;
	do {
		nstatus = igetdword(chip, ICHREG(GLOB_STA)) & (ICH_PCR | ICH_SCR | ICH_TCR);
		if (nstatus != status) {
			status = nstatus;
			goto __ok2;
		}
		do_delay(chip);
	} while (time_after_eq(end_time, jiffies));

      __ok2:
      	if (status == (ICH_PCR | ICH_SCR | ICH_TCR))
      		goto __ok3;
	/* wait for other codecs ready status. No other secondary codecs? , ok */
	/* end_time is not initialized here */
	do {
		nstatus = igetdword(chip, ICHREG(GLOB_STA)) & (ICH_PCR | ICH_SCR | ICH_TCR);
		if (nstatus != status) {
			status = nstatus;
			goto __ok2;
		}
		do_delay(chip);
	} while (time_after_eq(end_time, jiffies));

      __ok3:      
	if (chip->device_type == DEVICE_SIS) {
		/* unmute the output on SIS7012 */
		iputword(chip, 0x4c, igetword(chip, 0x4c) | 1);
	}
      	return 0;
}

static int snd_intel8x0_ali_chip_init(intel8x0_t *chip)
{
	u32 reg;
	int i = 0;

	reg = igetdword(chip, ICHREG(ALI_SCR));
	if ((reg & 2) == 0)	/* Cold required */
		reg |= 2;
	else
		reg |= 1;	/* Warm */
	reg &= ~0x80000000;	/* ACLink on */
	iputdword(chip, ICHREG(ALI_SCR), reg);

	for (i = 0; i < HZ / 2; i++) {
		if (! (igetdword(chip, ICHREG(ALI_INTERRUPTSR)) & ALI_INT_GPIO))
			goto __ok;
		do_delay(chip);
	}
	snd_printk(KERN_ERR "AC'97 reset failed.\n");
	return -EIO;

 __ok:
	for (i = 0; i < HZ / 2; i++) {
		reg = igetdword(chip, ICHREG(ALI_RTSR));
		if (reg & 0x80) /* primary codec */
			break;
		iputdword(chip, ICHREG(ALI_RTSR), reg | 0x80);
		do_delay(chip);
	}

	do_ali_reset(chip);
	return 0;
}

static int snd_intel8x0_chip_init(intel8x0_t *chip)
{
	unsigned int i;
	int err;
	
	if (chip->device_type != DEVICE_ALI)
		err = snd_intel8x0_ich_chip_init(chip);
	else
		err = snd_intel8x0_ali_chip_init(chip);
	if (err < 0)
		return err;

	iagetword(chip, 0);	/* clear semaphore flag */

	/* disable interrupts */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, 0x00);
	/* reset channels */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, ICH_RESETREGS);
	/* initialize Buffer Descriptor Lists */
	for (i = 0; i < chip->bdbars_count; i++)
		iputdword(chip, ICH_REG_OFF_BDBAR + chip->ichd[i].reg_offset, chip->ichd[i].bdbar_addr);
	return 0;
}

static int snd_intel8x0_free(intel8x0_t *chip)
{
	unsigned int i;

	if (chip->irq < 0)
		goto __hw_end;
	/* disable interrupts */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, 0x00);
	/* reset channels */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, ICH_RESETREGS);
	/* --- */
	synchronize_irq(chip->irq);
      __hw_end:
	if (chip->bdbars)
		snd_free_pci_pages(chip->pci, chip->bdbars_count * sizeof(u32) * ICH_MAX_FRAGS * 2, chip->bdbars, chip->bdbars_addr);
	if (chip->remap_addr)
		iounmap((void *) chip->remap_addr);
	if (chip->remap_bmaddr)
		iounmap((void *) chip->remap_bmaddr);
	if (chip->res) {
		release_resource(chip->res);
		kfree_nocheck(chip->res);
	}
	if (chip->res_bm) {
		release_resource(chip->res_bm);
		kfree_nocheck(chip->res_bm);
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

	if (chip->in_suspend ||
	    card->power_state == SNDRV_CTL_POWER_D3hot)
		return;

	chip->in_suspend = 1;
	snd_pcm_suspend_all(chip->pcm);
	if (chip->pcm_mic)
		snd_pcm_suspend_all(chip->pcm_mic);
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
}

static void intel8x0_resume(intel8x0_t *chip)
{
	snd_card_t *card = chip->card;
	int i;

	if (! chip->in_suspend ||
	    card->power_state == SNDRV_CTL_POWER_D0)
		return;

	pci_enable_device(chip->pci);
	snd_intel8x0_chip_init(chip);
	for (i = 0; i < 3; i++)
		if (chip->ac97[i])
			snd_ac97_resume(chip->ac97[i]);

	chip->in_suspend = 0;
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
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
	ichdev_t *ichdev;
	unsigned long port;
	unsigned long pos, t;
	unsigned long flags;
	struct timeval start_time, stop_time;

	if (chip->ac97[0]->clock != 48000)
		return; /* specified in module option */

	subs = chip->pcm->streams[0].substream;
	if (! subs || subs->dma_buffer.bytes < INTEL8X0_TESTBUF_SIZE) {
		snd_printk("no playback buffer allocated - aborting measure ac97 clock\n");
		return;
	}
	ichdev = &chip->ichd[ICHD_PCMOUT];
	ichdev->physbuf = subs->dma_buffer.addr;
	ichdev->size = chip->ichd[ICHD_PCMOUT].fragsize = INTEL8X0_TESTBUF_SIZE;
	ichdev->substream = NULL; /* don't process interrupts */

	/* set rate */
	if (snd_ac97_set_rate(chip->ac97[0], AC97_PCM_FRONT_DAC_RATE, 48000) < 0) {
		snd_printk(KERN_ERR "cannot set ac97 rate: clock = %d\n", chip->ac97[0]->clock);
		return;
	}
	snd_intel8x0_setup_periods(chip, ichdev);
	port = ichdev->reg_offset;
	spin_lock_irqsave(&chip->reg_lock, flags);
	/* trigger */
	if (chip->device_type != DEVICE_ALI)
		iputbyte(chip, port + ICH_REG_OFF_CR, ICH_IOCE | ICH_STARTBM);
	else {
		iputbyte(chip, port + ICH_REG_OFF_CR, ICH_IOCE);
		iputbyte(chip, ICHREG(ALI_DMACR), 1 << ichdev->ali_slot);
	}
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
	pos = ichdev->fragsize1;
	if (chip->device_type == DEVICE_SIS)
		pos -= igetword(chip, ichdev->reg_offset + ichdev->roff_picb);
	else
		pos -= igetword(chip, ichdev->reg_offset + ichdev->roff_picb) << 1;
	pos += ichdev->position;
	do_gettimeofday(&stop_time);
	/* stop */
	if (chip->device_type == DEVICE_ALI)
		iputbyte(chip, ICHREG(ALI_DMACR), 1 << (ichdev->ali_slot + 8));
	iputbyte(chip, port + ICH_REG_OFF_CR, 0);
	/* reset whole DMA things */
	while (!(igetbyte(chip, port + ichdev->roff_sr) & ICH_DCH))
		;
	iputbyte(chip, port + ICH_REG_OFF_CR, ICH_RESETREGS);
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
		chip->ac97[0]->clock = (chip->ac97[0]->clock * 48000) / pos;
	printk(KERN_INFO "intel8x0: clocking to %d\n", chip->ac97[0]->clock);
}

static void snd_intel8x0_proc_read(snd_info_entry_t * entry,
				   snd_info_buffer_t * buffer)
{
	intel8x0_t *chip = snd_magic_cast(intel8x0_t, entry->private_data, return);
	unsigned int tmp;

	snd_iprintf(buffer, "Intel8x0\n\n");
	if (chip->device_type == DEVICE_ALI)
		return;
	tmp = igetdword(chip, ICHREG(GLOB_STA));
	snd_iprintf(buffer, "Global control        : 0x%08x\n", igetdword(chip, ICHREG(GLOB_CNT)));
	snd_iprintf(buffer, "Global status         : 0x%08x\n", tmp);
	if (chip->device_type == DEVICE_INTEL_ICH4)
		snd_iprintf(buffer, "SDM                   : 0x%08x\n", igetdword(chip, ICHREG(SDM)));
	snd_iprintf(buffer, "AC'97 codecs ready    :%s%s%s%s\n",
			tmp & ICH_PCR ? " primary" : "",
			tmp & ICH_SCR ? " secondary" : "",
			tmp & ICH_TCR ? " tertiary" : "",
			(tmp & (ICH_PCR | ICH_SCR | ICH_TCR)) == 0 ? " none" : "");
	if (chip->device_type == DEVICE_INTEL_ICH4)
		snd_iprintf(buffer, "AC'97 codecs SDIN     : %i %i %i\n",
			chip->ac97_sdin[0],
			chip->ac97_sdin[1],
			chip->ac97_sdin[2]);
}

static void __devinit snd_intel8x0_proc_init(intel8x0_t * chip)
{
	snd_info_entry_t *entry;

	if (! snd_card_proc_new(chip->card, "intel8x0", &entry))
		snd_info_set_text_ops(entry, chip, snd_intel8x0_proc_read);
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
	unsigned int i;
	unsigned int int_sta_masks;
	ichdev_t *ichdev;
	static snd_device_ops_t ops = {
		.dev_free =	snd_intel8x0_dev_free,
	};
	static u32 intel_int_sta_masks[6] = {
		ICH_PIINT, ICH_POINT, ICH_MCINT, ICH_M2INT, ICH_P2INT, ICH_SPINT
	};
	static u32 ali_int_sta_masks[6] = {
		ALI_INT_PCMIN, ALI_INT_PCMOUT, ALI_INT_MICIN,
		ALI_INT_CODECSPDIFOUT, ALI_INT_SPDIFIN, ALI_INT_SPDIFOUT
	};
	static u32 ali_reg_offsets[6] = {
		0x40, 0x50, 0x60, 0x70, 0xa0, 0xb0
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
	snd_intel8x0_proc_init(chip);
	sprintf(chip->ac97_name, "%s - AC'97", card->shortname);
	sprintf(chip->ctrl_name, "%s - Controller", card->shortname);
	if (pci_resource_flags(pci, 2) & IORESOURCE_MEM) {	/* ICH4 and Nforce */
		chip->mmio = 1;
		chip->addr = pci_resource_start(pci, 2);
		if ((chip->res = request_mem_region(chip->addr, 512, chip->ac97_name)) == NULL) {
			snd_intel8x0_free(chip);
			snd_printk("unable to grab I/O memory 0x%lx-0x%lx\n", chip->addr, chip->addr + 512 - 1);
			return -EBUSY;
		}
		chip->remap_addr = (unsigned long) ioremap_nocache(chip->addr, 512);
		if (chip->remap_addr == 0) {
			snd_intel8x0_free(chip);
			snd_printk("AC'97 space ioremap problem\n");
			return -EIO;
		}
	} else {
		chip->addr = pci_resource_start(pci, 0);
		if ((chip->res = request_region(chip->addr, 256, chip->ac97_name)) == NULL) {
			snd_intel8x0_free(chip);
			snd_printk("unable to grab ports 0x%lx-0x%lx\n", chip->addr, chip->addr + 256 - 1);
			return -EBUSY;
		}
	}
	if (pci_resource_flags(pci, 3) & IORESOURCE_MEM) {	/* ICH4 */
		chip->bm_mmio = 1;
		chip->bmaddr = pci_resource_start(pci, 3);
		if ((chip->res_bm = request_mem_region(chip->bmaddr, 256, chip->ctrl_name)) == NULL) {
			snd_intel8x0_free(chip);
			snd_printk("unable to grab I/O memory 0x%lx-0x%lx\n", chip->bmaddr, chip->bmaddr + 512 - 1);
			return -EBUSY;
		}
		chip->remap_bmaddr = (unsigned long) ioremap_nocache(chip->bmaddr, 256);
		if (chip->remap_bmaddr == 0) {
			snd_intel8x0_free(chip);
			snd_printk("Controller space ioremap problem\n");
			return -EIO;
		}
	} else {
		chip->bmaddr = pci_resource_start(pci, 1);
		if ((chip->res_bm = request_region(chip->bmaddr, 64, chip->ctrl_name)) == NULL) {
			snd_intel8x0_free(chip);
			snd_printk("unable to grab ports 0x%lx-0x%lx\n", chip->bmaddr, chip->bmaddr + 64 - 1);
			return -EBUSY;
		}
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
	for (i = 0; i <= ICHD_LAST; i++) {
		ichdev = &chip->ichd[i];
		ichdev->ichd = i;
		ichdev->reg_offset = i * 0x10 + (i >= 0x30 ? 0x10 : 0);
		ichdev->roff_sr = ICH_REG_OFF_SR;
		ichdev->roff_picb = ICH_REG_OFF_PICB;
		ichdev->int_sta_mask = device_type == DEVICE_ALI ? ali_int_sta_masks[i] : intel_int_sta_masks[i];
	}
	switch (device_type) {
	case DEVICE_SIS:
		for (i = 0; i <= ICHD_LAST; i++) {
			ichdev = &chip->ichd[i];
			ichdev->roff_sr = ICH_REG_OFF_PICB;
			ichdev->roff_picb = ICH_REG_OFF_SR;
		}
		break;
	case DEVICE_ALI:
		for (i = 0; i <= ALID_LAST; i++) {
			ichdev = &chip->ichd[i];
			ichdev->reg_offset = ali_reg_offsets[i];
			ichdev->ali_slot = i + 1;	/* is this right for last three devices? --jk */
		}
	}

	/* allocate buffer descriptor lists */
	/* the start of each lists must be aligned to 8 bytes */
	chip->bdbars_count = 3;
	if (device_type == DEVICE_INTEL_ICH4 || device_type == DEVICE_ALI)
		chip->bdbars_count = 6;
	chip->bdbars = (u32 *)snd_malloc_pci_pages(pci, chip->bdbars_count * sizeof(u32) * ICH_MAX_FRAGS * 2, &chip->bdbars_addr);
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
	int_sta_masks = 0;
	for (i = 0; i < chip->bdbars_count; i++) {
		ichdev = &chip->ichd[i];
		ichdev->bdbar = chip->bdbars + (i * ICH_MAX_FRAGS * 2);
		ichdev->bdbar_addr = chip->bdbars_addr + (i * sizeof(u32) * ICH_MAX_FRAGS * 2);
		int_sta_masks |= ichdev->int_sta_mask;
	}
	chip->int_sta_reg = device_type == DEVICE_ALI ? ICH_REG_ALI_INTERRUPTSR : ICH_REG_GLOB_STA;
	chip->int_sta_mask = int_sta_masks;

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
	{ PCI_DEVICE_ID_INTEL_ICH5, "Intel ICH5" },
	{ PCI_DEVICE_ID_SI_7012, "SiS SI7012" },
	{ PCI_DEVICE_ID_NVIDIA_MCP_AUDIO, "NVidia NForce" },
	{ PCI_DEVICE_ID_NVIDIA_MCP2_AUDIO, "NVidia NForce2" },
	{ PCI_DEVICE_ID_NVIDIA_MCP3_AUDIO, "NVidia NForce3" },
	{ 0x746d, "AMD AMD8111" },
	{ 0x7445, "AMD AMD768" },
	{ 0x5455, "ALi M5455" },
	{ 0, 0 },
};

static int __devinit snd_intel8x0_probe(struct pci_dev *pci,
					const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	intel8x0_t *chip;
	int pcm_dev = 0, err;
	struct shortname_table *name;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
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

	if ((err = snd_intel8x0_create(card, pci, pci_id->driver_data, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_intel8x0_mixer(chip, ac97_clock[dev])) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_intel8x0_pcm(chip, pcm_dev++, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	/* activate MIC PCM only when associated AC'97 codec */
	if (chip->ichd[ICHD_MIC].ac97) {
		if ((err = snd_intel8x0_pcm_mic(chip, pcm_dev++, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	if (chip->device_type == DEVICE_ALI) {
		if ((err = snd_intel8x0_ali_spdif(chip, pcm_dev++, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	/* activate AC'97 S/PDIF only when associated AC'97 codec */
	if (chip->bdbars_count > 3) {
		err = 0;
		if (chip->device_type == DEVICE_ALI) {
			if (chip->ichd[ALID_AC97SPDIFOUT].ac97)
				err = snd_intel8x0_ali_ac97spdif(chip, pcm_dev++, NULL);
		} else {
			if (chip->ichd[ICHD_SPBAR].ac97)
				err = snd_intel8x0_pcm_spdif(chip, pcm_dev++, NULL);
		}
		if (err < 0) {
			snd_card_free(card);
			return err;
		}
		if (chip->device_type != DEVICE_ALI) {
			/* activate MIC2 only when associated AC'97 codec */
			if (chip->ichd[ICHD_MIC2].ac97)
				if ((err = snd_intel8x0_pcm_mic2(chip, pcm_dev++, NULL)) < 0) {
					snd_card_free(card);
					return err;
				}
			/* activate PCM2IN only when associated AC'97 codec */
			if (chip->ichd[ICHD_PCM2IN].ac97)
				if ((err = snd_intel8x0_pcm_capture2(chip, pcm_dev++, NULL)) < 0) {
					snd_card_free(card);
					return err;
				}
		}
	}
	
	
	if (mpu_port[dev] == 0x300 || mpu_port[dev] == 0x330) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_INTEL8X0,
					       mpu_port[dev], 0,
					       -1, 0, &chip->rmidi)) < 0) {
			printk(KERN_ERR "intel8x0: no UART401 device at 0x%x, skipping.\n", mpu_port[dev]);
			mpu_port[dev] = 0;
		}
	} else
		mpu_port[dev] = 0;

	sprintf(card->longname, "%s at 0x%lx, irq %i",
		card->shortname, chip->addr, chip->irq);

	if (! ac97_clock[dev])
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
	.name = "Intel ICH",
	.id_table = snd_intel8x0_ids,
	.probe = snd_intel8x0_probe,
	.remove = __devexit_p(snd_intel8x0_remove),
#ifdef CONFIG_PM
	.suspend = snd_intel8x0_suspend,
	.resume = snd_intel8x0_resume,
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
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	if (joystick_port[dev] > 0 || mpu_port[dev] > 0) {
		u16 val;
		pci_read_config_word(pci, 0xe6, &val);
		if (joystick_port[dev] > 0)
			val |= 0x100;
		if (mpu_port[dev] == 0x300 || mpu_port[dev] == 0x330)
			val |= 0x20;
		pci_write_config_word(pci, 0xe6, val | 0x100);

		if (mpu_port[dev] == 0x300 || mpu_port[dev] == 0x330) {
			u8 b;
			pci_read_config_byte(pci, 0xe2, &b);
			if (mpu_port[dev] == 0x300)
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
	{ 0x10de, 0x006b, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* NFORCE2 */
	{ 0x10de, 0x00db, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },	/* NFORCE3 */
	{ 0, }
};

static struct pci_driver joystick_driver = {
	.name = "Intel ICH Joystick",
	.id_table = snd_intel8x0_joystick_ids,
	.probe = snd_intel8x0_joystick_probe,
};

static int have_joystick;
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
	if (pci_module_init(&joystick_driver) < 0) {
		snd_printdd(KERN_INFO "no joystick found\n");
		have_joystick = 0;
	} else {
		snd_printdd(KERN_INFO "joystick(s) found\n");
		have_joystick = 1;
	}
#endif
        return 0;

}

static void __exit alsa_card_intel8x0_exit(void)
{
	pci_unregister_driver(&driver);
#if defined(SUPPORT_JOYSTICK) || defined(SUPPORT_MIDI)
	if (have_joystick)
		pci_unregister_driver(&joystick_driver);
#endif
}

module_init(alsa_card_intel8x0_init)
module_exit(alsa_card_intel8x0_exit)

#ifndef MODULE

/* format is: snd-intel8x0=enable,index,id,ac97_clock */

static int __init alsa_card_intel8x0_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,&ac97_clock[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-intel8x0=", alsa_card_intel8x0_setup);

#endif /* ifndef MODULE */
