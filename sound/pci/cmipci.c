/*
 * Driver for C-Media CMI8338 and 8738 PCI soundcards.
 * Copyright (c) 2000 by Takashi Iwai <tiwai@suse.de>
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
 */
 
/* Does not work. Warning may block system in capture mode */
/* #define USE_VAR48KRATE */

/* Define this if you want soft ac3 encoding */
#define DO_SOFT_AC3
#define USE_AES_IEC958

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/sb.h>
#include <sound/asoundef.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("C-Media CMI8x38 PCI");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{C-Media,CMI8738},"
		"{C-Media,CMI8738B},"
		"{C-Media,CMI8338A},"
		"{C-Media,CMI8338B}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable switches */
static long mpu_port[SNDRV_CARDS] = {0x330, [1 ... (SNDRV_CARDS-1)]=-1};
static long fm_port[SNDRV_CARDS] = {0x388, [1 ... (SNDRV_CARDS-1)]=-1};
#ifdef DO_SOFT_AC3
static int soft_ac3[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)]=1};
#endif

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for C-Media PCI soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for C-Media PCI soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable C-Media PCI soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(mpu_port, "MPU-401 port.");
MODULE_PARM_SYNTAX(mpu_port, SNDRV_ENABLED ",allows:{{-1},{0x330},{0x320},{0x310},{0x300}},dialog:list");
MODULE_PARM(fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(fm_port, "FM port.");
MODULE_PARM_SYNTAX(fm_port, SNDRV_ENABLED ",allows:{{-1},{0x388},{0x3c8},{0x3e0},{0x3e8}},dialog:list");
#ifdef DO_SOFT_AC3
MODULE_PARM(soft_ac3, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(soft_ac3, "Sofware-conversion of raw SPDIF packets (model 033 only).");
MODULE_PARM_SYNTAX(soft_ac3, SNDRV_ENABLED "," SNDRV_BOOLEAN_TRUE_DESC);
#endif

#ifndef PCI_DEVICE_ID_CMEDIA_CM8738
#define PCI_DEVICE_ID_CMEDIA_CM8738	0x0111
#endif
#ifndef PCI_DEVICE_ID_CMEDIA_CM8738B
#define PCI_DEVICE_ID_CMEDIA_CM8738B	0x0112
#endif

/*
 * CM8x38 registers definition
 */

#define CM_REG_FUNCTRL0		0x00
#define CM_RST_CH1		0x00080000
#define CM_RST_CH0		0x00040000
#define CM_CHEN1		0x00020000	/* ch1: enable */
#define CM_CHEN0		0x00010000	/* ch0: enable */
#define CM_PAUSE1		0x00000008	/* ch1: pause */
#define CM_PAUSE0		0x00000004	/* ch0: pause */
#define CM_CHADC1		0x00000002	/* ch1, 0:playback, 1:record */
#define CM_CHADC0		0x00000001	/* ch0, 0:playback, 1:record */

#define CM_REG_FUNCTRL1		0x04
#define CM_ASFC_MASK		0x0000E000	/* ADC sampling frequency */
#define CM_ASFC_SHIFT		13
#define CM_DSFC_MASK		0x00001C00	/* DAC sampling frequency */
#define CM_DSFC_SHIFT		10
#define CM_SPDF_1		0x00000200	/* SPDIF IN/OUT at channel B */
#define CM_SPDF_0		0x00000100	/* SPDIF OUT only channel A */
#define CM_SPDFLOOP		0x00000080	/* ext. SPDIIF/OUT -> IN loopback */
#define CM_SPDO2DAC		0x00000040	/* SPDIF/OUT can be heard from internal DAC */
#define CM_INTRM		0x00000020	/* master control block (MCB) interrupt enabled */
#define CM_BREQ			0x00000010	/* bus master enabled */
#define CM_VOICE_EN		0x00000008	/* legacy voice (SB16,FM) */
#define CM_UART_EN		0x00000004	/* UART */
#define CM_JYSTK_EN		0x00000002	/* joy stick */

#define CM_REG_CHFORMAT		0x08

#define CM_CHB3D5C		0x80000000	/* 5,6 channels */
#define CM_CHB3D		0x20000000	/* 4 channels */

#define CM_CHIP_MASK1		0x1f000000
#define CM_CHIP_037		0x01000000

#define CM_SPDIF_SELECT1	0x00080000	/* for model <= 037 ? */
#define CM_AC3EN1		0x00100000	/* enable AC3: model 037 */
#define CM_SPD24SEL		0x00020000	/* 24bit spdif: model 037 */
/* #define CM_SPDIF_INVERSE	0x00010000 */ /* ??? */

#define CM_ADCBITLEN_MASK	0x0000C000	
#define CM_ADCBITLEN_16		0x00000000
#define CM_ADCBITLEN_15		0x00004000
#define CM_ADCBITLEN_14		0x00008000
#define CM_ADCBITLEN_13		0x0000C000

#define CM_ADCDACLEN_MASK	0x00003000
#define CM_ADCDACLEN_060	0x00000000
#define CM_ADCDACLEN_066	0x00001000
#define CM_ADCDACLEN_130	0x00002000
#define CM_ADCDACLEN_280	0x00003000

#define CM_CH1_SRATE_176K	0x00000800
#define CM_CH1_SRATE_88K	0x00000400
#define CM_CH0_SRATE_176K	0x00000200
#define CM_CH0_SRATE_88K	0x00000100

#define CM_SPDIF_INVERSE2	0x00000080	/* model 055? */

#define CM_CH1FMT_MASK		0x0000000C
#define CM_CH1FMT_SHIFT		2
#define CM_CH0FMT_MASK		0x00000003
#define CM_CH0FMT_SHIFT		0

#define CM_REG_INT_HLDCLR	0x0C
#define CM_CHIP_MASK2		0xff000000
#define CM_CHIP_039		0x04000000
#define CM_CHIP_039_6CH		0x01000000
#define CM_TDMA_INT_EN		0x00040000
#define CM_CH1_INT_EN		0x00020000
#define CM_CH0_INT_EN		0x00010000
#define CM_INT_HOLD		0x00000002
#define CM_INT_CLEAR		0x00000001

#define CM_REG_INT_STATUS	0x10
#define CM_INTR			0x80000000
#define CM_VCO			0x08000000	/* Voice Control? CMI8738 */
#define CM_MCBINT		0x04000000	/* Master Control Block abort cond.? */
#define CM_UARTINT		0x00010000
#define CM_LTDMAINT		0x00008000
#define CM_HTDMAINT		0x00004000
#define CM_XDO46		0x00000080	/* Modell 033? Direct programming EEPROM (read data register) */
#define CM_LHBTOG		0x00000040	/* High/Low status from DMA ctrl register */
#define CM_LEG_HDMA		0x00000020	/* Legacy is in High DMA channel */
#define CM_LEG_STEREO		0x00000010	/* Legacy is in Stereo mode */
#define CM_CH1BUSY		0x00000008
#define CM_CH0BUSY		0x00000004
#define CM_CHINT1		0x00000002
#define CM_CHINT0		0x00000001

#define CM_REG_LEGACY_CTRL	0x14
#define CM_NXCHG		0x80000000	/* h/w multi channels? */
#define CM_VMPU_MASK		0x60000000	/* MPU401 i/o port address */
#define CM_VMPU_330		0x00000000
#define CM_VMPU_320		0x20000000
#define CM_VMPU_310		0x40000000
#define CM_VMPU_300		0x60000000
#define CM_VSBSEL_MASK		0x0C000000	/* SB16 base address */
#define CM_VSBSEL_220		0x00000000
#define CM_VSBSEL_240		0x04000000
#define CM_VSBSEL_260		0x08000000
#define CM_VSBSEL_280		0x0C000000
#define CM_FMSEL_MASK		0x03000000	/* FM OPL3 base address */
#define CM_FMSEL_388		0x00000000
#define CM_FMSEL_3C8		0x01000000
#define CM_FMSEL_3E0		0x02000000
#define CM_FMSEL_3E8		0x03000000
#define CM_ENSPDOUT		0x00800000	/* enable XPDIF/OUT to I/O interface */
#define CM_SPDCOPYRHT		0x00400000	/* set copyright spdif in/out */
#define CM_DAC2SPDO		0x00200000	/* enable wave+fm_midi -> SPDIF/OUT */
#define CM_SETRETRY		0x00010000	/* 0: legacy i/o wait (default), 1: legacy i/o bus retry */
#define CM_CHB3D6C		0x00008000	/* 5.1 channels support */
#define CM_LINE_AS_BASS		0x00006000	/* use line-in as bass */

#define CM_REG_MISC_CTRL	0x18
#define CM_PWD			0x80000000
#define CM_RESET		0x40000000
#define CM_SFIL_MASK		0x30000000
#define CM_TXVX			0x08000000
#define CM_N4SPK3D		0x04000000	/* 4ch output */
#define CM_SPDO5V		0x02000000	/* 5V spdif output (1 = 0.5v (coax)) */
#define CM_SPDIF48K		0x01000000	/* write */
#define CM_SPATUS48K		0x01000000	/* read */
#define CM_ENDBDAC		0x00800000	/* enable dual dac */
#define CM_XCHGDAC		0x00400000	/* 0: front=ch0, 1: front=ch1 */
#define CM_SPD32SEL		0x00200000	/* 0: 16bit SPDIF, 1: 32bit */
#define CM_SPDFLOOPI		0x00100000	/* int. SPDIF-IN -> int. OUT */
#define CM_FM_EN		0x00080000	/* enalbe FM */
#define CM_AC3EN2		0x00040000	/* enable AC3: model 039 */
#define CM_VIDWPDSB		0x00010000 
#define CM_SPDF_AC97		0x00008000	/* 0: SPDIF/OUT 44.1K, 1: 48K */
#define CM_MASK_EN		0x00004000
#define CM_VIDWPPRT		0x00002000
#define CM_SFILENB		0x00001000
#define CM_MMODE_MASK		0x00000E00
#define CM_SPDIF_SELECT2	0x00000100	/* for model > 039 ? */
#define CM_ENCENTER		0x00000080
#define CM_FLINKON		0x00000040
#define CM_FLINKOFF		0x00000020
#define CM_MIDSMP		0x00000010
#define CM_UPDDMA_MASK		0x0000000C
#define CM_TWAIT_MASK		0x00000003

	/* byte */
#define CM_REG_MIXER0		0x20

#define CM_REG_SB16_DATA	0x22
#define CM_REG_SB16_ADDR	0x23

#define CM_REFFREQ_XIN		(315*1000*1000)/22	/* 14.31818 Mhz reference clock frequency pin XIN */
#define CM_ADCMULT_XIN		512			/* Guessed (487 best for 44.1kHz, not for 88/176kHz) */
#define CM_TOLERANCE_RATE	0.001			/* Tolerance sample rate pitch (1000ppm) */
#define CM_MAXIMUM_RATE		80000000		/* Note more than 80MHz */

#define CM_REG_MIXER1		0x24
#define CM_FMMUTE		0x80	/* mute FM */
#define CM_FMMUTE_SHIFT		7
#define CM_WSMUTE		0x40	/* mute PCM */
#define CM_WSMUTE_SHIFT		6
#define CM_SPK4			0x20	/* lin-in -> rear line out */
#define CM_SPK4_SHIFT		5
#define CM_REAR2FRONT		0x10	/* exchange rear/front */
#define CM_REAR2FRONT_SHIFT	4
#define CM_WAVEINL		0x08	/* digital wave rec. left chan */
#define CM_WAVEINL_SHIFT	3
#define CM_WAVEINR		0x04	/* digical wave rec. right */
#define CM_WAVEINR_SHIFT	2
#define CM_X3DEN		0x02	/* 3D surround enable */
#define CM_X3DEN_SHIFT		1
#define CM_CDPLAY		0x01	/* enable SPDIF/IN PCM -> DAC */
#define CM_CDPLAY_SHIFT		0

#define CM_REG_MIXER2		0x25
#define CM_RAUXREN		0x80	/* AUX right capture */
#define CM_RAUXREN_SHIFT	7
#define CM_RAUXLEN		0x40	/* AUX left capture */
#define CM_RAUXLEN_SHIFT	6
#define CM_VAUXRM		0x20	/* AUX right mute */
#define CM_VAUXRM_SHIFT		5
#define CM_VAUXLM		0x10	/* AUX left mute */
#define CM_VAUXLM_SHIFT		4
#define CM_VADMIC_MASK		0x0e	/* mic gain level (0-3) << 1 */
#define CM_VADMIC_SHIFT		1
#define CM_MICGAINZ		0x01	/* mic boost */
#define CM_MICGAINZ_SHIFT	0

#define CM_REG_AUX_VOL		0x26
#define CM_VAUXL_MASK		0xf0
#define CM_VAUXR_MASK		0x0f

#define CM_REG_MISC		0x27
#define CM_XGPO1		0x20
// #define CM_XGPBIO		0x04
#define CM_MIC_CENTER_LFE	0x04	/* mic as center/lfe out? (model 039 or later?) */
#define CM_SPDIF_INVERSE	0x04	/* spdif input phase inverse (model 037) */
#define CM_SPDVALID		0x02	/* spdif input valid check */
#define CM_DMAUTO		0x01

#define CM_REG_AC97		0x28	/* hmmm.. do we have ac97 link? */
/*
 * For CMI-8338 (0x28 - 0x2b) .. is this valid for CMI-8738
 * or identical with AC97 codec?
 */
#define CM_REG_EXTERN_CODEC	CM_REG_AC97

/*
 * MPU401 pci port index address 0x40 - 0x4f (CMI-8738 spec ver. 0.6)
 */
#define CM_REG_MPU_PCI		0x40

/*
 * FM pci port index address 0x50 - 0x5f (CMI-8738 spec ver. 0.6)
 */
#define CM_REG_FM_PCI		0x50

/*
 * for CMI-8338 .. this is not valid for CMI-8738.
 */
#define CM_REG_EXTENT_IND	0xf0
#define CM_VPHONE_MASK		0xe0	/* Phone volume control (0-3) << 5 */
#define CM_VPHONE_SHIFT		5
#define CM_VPHOM		0x10	/* Phone mute control */
#define CM_VSPKM		0x08	/* Speaker mute control, default high */
#define CM_RLOOPREN		0x04    /* Rec. R-channel enable */
#define CM_RLOOPLEN		0x02	/* Rec. L-channel enable */

/*
 * CMI-8338 spec ver 0.5 (this is not valid for CMI-8738):
 * the 8 registers 0xf8 - 0xff are used for programming m/n counter by the PLL
 * unit (readonly?).
 */
#define CM_REG_PLL		0xf8

/*
 * extended registers
 */
#define CM_REG_CH0_FRAME1	0x80	/* base address */
#define CM_REG_CH0_FRAME2	0x84
#define CM_REG_CH1_FRAME1	0x88	/* 0-15: count of samples at bus master; buffer size */
#define CM_REG_CH1_FRAME2	0x8C	/* 16-31: count of samples at codec; fragment size */

/*
 * size of i/o region
 */
#define CM_EXTENT_CODEC	  0x100
#define CM_EXTENT_MIDI	  0x2
#define CM_EXTENT_SYNTH	  0x4

/*
 * pci ids
 */
#ifndef PCI_VENDOR_ID_CMEDIA
#define PCI_VENDOR_ID_CMEDIA         0x13F6
#endif
#ifndef PCI_DEVICE_ID_CMEDIA_CM8338A
#define PCI_DEVICE_ID_CMEDIA_CM8338A 0x0100
#endif
#ifndef PCI_DEVICE_ID_CMEDIA_CM8338B
#define PCI_DEVICE_ID_CMEDIA_CM8338B 0x0101
#endif
#ifndef PCI_DEVICE_ID_CMEDIA_CM8738
#define PCI_DEVICE_ID_CMEDIA_CM8738  0x0111
#endif
#ifndef PCI_DEVICE_ID_CMEDIA_CM8738B
#define PCI_DEVICE_ID_CMEDIA_CM8738B 0x0112
#endif

/*
 * channels for playback / capture
 */
#define CM_CH_PLAY	0
#define CM_CH_CAPT	1

/*
 * flags to check device open/close
 */
#define CM_OPEN_NONE	0
#define CM_OPEN_CH_MASK	0x01
#define CM_OPEN_DAC	0x10
#define CM_OPEN_ADC	0x20
#define CM_OPEN_SPDIF	0x40
#define CM_OPEN_MCHAN	0x80
#define CM_OPEN_PLAYBACK	(CM_CH_PLAY | CM_OPEN_DAC)
#define CM_OPEN_PLAYBACK2	(CM_CH_CAPT | CM_OPEN_DAC)
#define CM_OPEN_PLAYBACK_MULTI	(CM_CH_PLAY | CM_OPEN_DAC | CM_OPEN_MCHAN)
#define CM_OPEN_CAPTURE		(CM_CH_CAPT | CM_OPEN_ADC)
#define CM_OPEN_SPDIF_PLAYBACK	(CM_CH_PLAY | CM_OPEN_DAC | CM_OPEN_SPDIF)
#define CM_OPEN_SPDIF_CAPTURE	(CM_CH_CAPT | CM_OPEN_ADC | CM_OPEN_SPDIF)


#if CM_CH_PLAY == 1
#define CM_PLAYBACK_SRATE_176K	CM_CH1_SRATE_176K
#define CM_PLAYBACK_SPDF	CM_SPDF_1
#define CM_CAPTURE_SPDF		CM_SPDF_0
#else
#define CM_PLAYBACK_SRATE_176K CM_CH0_SRATE_176K
#define CM_PLAYBACK_SPDF	CM_SPDF_0
#define CM_CAPTURE_SPDF		CM_SPDF_1
#endif


/*
 * driver data
 */

typedef struct snd_stru_cmipci cmipci_t;
typedef struct snd_stru_cmipci_pcm cmipci_pcm_t;

#define chip_t cmipci_t

struct snd_stru_cmipci_pcm {
	snd_pcm_substream_t *substream;
	int running;		/* dac/adc running? */
	unsigned int dma_size;	/* in frames */
	unsigned int period_size;	/* in frames */
	unsigned int offset;	/* physical address of the buffer */
	unsigned int fmt;	/* format bits */
	int ch;			/* channel (0/1) */
	unsigned int is_dac;		/* is dac? */
	int bytes_per_frame;
	int shift;
	int ac3_shift;	/* extra shift: 1 on soft ac3 mode */
};

/* mixer elements toggled/resumed during ac3 playback */
struct cmipci_mixer_auto_switches {
	const char *name;	/* switch to toggle */
	int toggle_on;		/* value to change when ac3 mode */
};
static const struct cmipci_mixer_auto_switches cm_saved_mixer[] = {
	{"PCM Playback Switch", 0},
	{"IEC958 Output Switch", 1},
	{"IEC958 Mix Analog", 0},
	// {"IEC958 Out To DAC", 1}, // no longer used
	{"IEC958 Loop", 0},
};
#define CM_SAVED_MIXERS		ARRAY_SIZE(cm_saved_mixer)

struct snd_stru_cmipci {
	snd_card_t *card;

	struct pci_dev *pci;
	unsigned int device;	/* device ID */
	int irq;

	unsigned long iobase;
	struct resource *res_iobase;
	unsigned int ctrl;	/* FUNCTRL0 current value */

	snd_pcm_t *pcm;		/* DAC/ADC PCM */
	snd_pcm_t *pcm2;	/* 2nd DAC */
	snd_pcm_t *pcm_spdif;	/* SPDIF */

	int chip_version;
	int max_channels;
	unsigned int has_dual_dac: 1;
	unsigned int can_ac3_sw: 1;
	unsigned int can_ac3_hw: 1;
	unsigned int can_multi_ch: 1;
	unsigned int do_soft_ac3: 1;

	unsigned int spdif_playback_avail: 1;	/* spdif ready? */
	unsigned int spdif_playback_enabled: 1;	/* spdif switch enabled? */
	int spdif_counter;	/* for software AC3 */

	unsigned int dig_status;
	unsigned int dig_pcm_status;
#ifdef USE_AES_IEC958
	snd_ctl_elem_value_t *spdif_channel;
#endif
	snd_kcontrol_t *spdif_pcm_ctl;

	snd_pcm_hardware_t *hw_info[3]; /* for playbacks */

	int opened[2];	/* open mode */
	struct semaphore open_mutex;

	int mixer_insensitive: 1;
	snd_kcontrol_t *mixer_res_ctl[CM_SAVED_MIXERS];
	int mixer_res_status[CM_SAVED_MIXERS];

	opl3_t *opl3;
	snd_hwdep_t *opl3hwdep;

	cmipci_pcm_t channel[2];	/* ch0 - DAC, ch1 - ADC or 2nd DAC */

	/* external MIDI */
	snd_rawmidi_t *rmidi;

	spinlock_t reg_lock;
};


/* read/write operations for dword register */
inline static void snd_cmipci_write(cmipci_t *cm, unsigned int cmd, unsigned int data)
{
	outl(data, cm->iobase + cmd);
}
inline static unsigned int snd_cmipci_read(cmipci_t *cm, unsigned int cmd)
{
	return inl(cm->iobase + cmd);
}

/* read/write operations for word register */
inline static void snd_cmipci_write_w(cmipci_t *cm, unsigned int cmd, unsigned short data)
{
	outw(data, cm->iobase + cmd);
}
inline static unsigned short snd_cmipci_read_w(cmipci_t *cm, unsigned int cmd)
{
	return inw(cm->iobase + cmd);
}

/* read/write operations for byte register */
inline static void snd_cmipci_write_b(cmipci_t *cm, unsigned int cmd, unsigned char data)
{
	outb(data, cm->iobase + cmd);
}

inline static unsigned char snd_cmipci_read_b(cmipci_t *cm, unsigned int cmd)
{
	return inb(cm->iobase + cmd);
}

/* bit operations for dword register */
static void snd_cmipci_set_bit(cmipci_t *cm, unsigned int cmd, unsigned int flag)
{
	unsigned int val;
	val = inl(cm->iobase + cmd);
	val |= flag;
	outl(val, cm->iobase + cmd);
}

static void snd_cmipci_clear_bit(cmipci_t *cm, unsigned int cmd, unsigned int flag)
{
	unsigned int val;
	val = inl(cm->iobase + cmd);
	val &= ~flag;
	outl(val, cm->iobase + cmd);
}

#if 0 // not used
/* bit operations for byte register */
static void snd_cmipci_set_bit_b(cmipci_t *cm, unsigned int cmd, unsigned char flag)
{
	unsigned char val;
	val = inb(cm->iobase + cmd);
	val |= flag;
	outb(val, cm->iobase + cmd);
}

static void snd_cmipci_clear_bit_b(cmipci_t *cm, unsigned int cmd, unsigned char flag)
{
	unsigned char val;
	val = inb(cm->iobase + cmd);
	val &= ~flag;
	outb(val, cm->iobase + cmd);
}
#endif


/*
 * PCM interface
 */

/*
 * calculate frequency
 */

static unsigned int rates[] = { 5512, 11025, 22050, 44100, 8000, 16000, 32000, 48000 };

static unsigned int snd_cmipci_rate_freq(unsigned int rate)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(rates); i++) {
		if (rates[i] == rate)
			return i;
	}
	snd_BUG();
	return 0;
}

#ifdef USE_VAR48KRATE
/*
 * Determine PLL values for frequency setup, maybe the CMI8338 (CMI8738???)
 * does it this way .. maybe not.  Never get any information from C-Media about
 * that <werner@suse.de>.
 */
static int snd_cmipci_pll_rmn(unsigned int rate, unsigned int adcmult, int *r, int *m, int *n)
{
	unsigned int delta, tolerance;
	int xm, xn, xr;

	for (*r = 0; rate < CM_MAXIMUM_RATE/adcmult; *r += (1<<5))
		rate <<= 1;
	*n = -1;
	if (*r > 0xff)
		goto out;
	tolerance = rate*CM_TOLERANCE_RATE;

	for (xn = (1+2); xn < (0x1f+2); xn++) {
		for (xm = (1+2); xm < (0xff+2); xm++) {
			xr = ((CM_REFFREQ_XIN/adcmult) * xm) / xn;

			if (xr < rate)
				delta = rate - xr;
			else
				delta = xr - rate;

			/*
			 * If we found one, remember this,
			 * and try to find a closer one
			 */
			if (delta < tolerance) {
				tolerance = delta;
				*m = xm - 2;
				*n = xn - 2;
			}
		}
	}
out:
	return (*n > -1);
}

/*
 * Program pll register bits, I assume that the 8 registers 0xf8 upto 0xff
 * are mapped onto the 8 ADC/DAC sampling frequency which can be choosen
 * at the register CM_REG_FUNCTRL1 (0x04).
 * Problem: other ways are also possible (any information about that?)
 */
static void snd_cmipci_set_pll(cmipci_t *cm, unsigned int rate, unsigned int slot)
{
	unsigned int reg = CM_REG_PLL + slot;
	/*
	 * Guess that this programs at reg. 0x04 the pos 15:13/12:10
	 * for DSFC/ASFC (000 upto 111).
	 */

	/* FIXME: Init (Do we've to set an other register first before programming?) */

	/* FIXME: Is this correct? Or shouldn't the m/n/r values be used for that? */
	snd_cmipci_write_b(cm, reg, rate>>8);
	snd_cmipci_write_b(cm, reg, rate&0xff);

	/* FIXME: Setup (Do we've to set an other register first to enable this?) */
}
#endif /* USE_VAR48KRATE */

static int snd_cmipci_hw_params(snd_pcm_substream_t * substream,
				snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_cmipci_playback2_hw_params(snd_pcm_substream_t * substream,
					  snd_pcm_hw_params_t * hw_params)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	if (params_channels(hw_params) > 2) {
		down(&cm->open_mutex);
		if (cm->opened[CM_CH_PLAY]) {
			up(&cm->open_mutex);
			return -EBUSY;
		}
		/* reserve the channel A */
		cm->opened[CM_CH_PLAY] = CM_OPEN_PLAYBACK_MULTI;
		up(&cm->open_mutex);
	}
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static void snd_cmipci_ch_reset(cmipci_t *cm, int ch)
{
	int reset = CM_RST_CH0 << (cm->channel[ch].ch);
	snd_cmipci_write(cm, CM_REG_FUNCTRL0, cm->ctrl | reset);
	snd_cmipci_write(cm, CM_REG_FUNCTRL0, cm->ctrl & ~reset);
	udelay(10);
}

static int snd_cmipci_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}


/*
 */

static unsigned int hw_channels[] = {1, 2, 4, 5, 6};
static snd_pcm_hw_constraint_list_t hw_constraints_channels_4 = {
	.count = 3,
	.list = hw_channels,
	.mask = 0,
};
static snd_pcm_hw_constraint_list_t hw_constraints_channels_6 = {
	.count = 5,
	.list = hw_channels,
	.mask = 0,
};

static int set_dac_channels(cmipci_t *cm, cmipci_pcm_t *rec, int channels)
{
	unsigned long flags;

	if (channels > 2) {
		if (! cm->can_multi_ch)
			return -EINVAL;
		if (rec->fmt != 0x03) /* stereo 16bit only */
			return -EINVAL;

		spin_lock_irqsave(&cm->reg_lock, flags);
		snd_cmipci_set_bit(cm, CM_REG_LEGACY_CTRL, CM_NXCHG);
		if (channels > 4) {
			snd_cmipci_clear_bit(cm, CM_REG_CHFORMAT, CM_CHB3D);
			snd_cmipci_set_bit(cm, CM_REG_CHFORMAT, CM_CHB3D5C);
		} else {
			snd_cmipci_clear_bit(cm, CM_REG_CHFORMAT, CM_CHB3D5C);
			snd_cmipci_set_bit(cm, CM_REG_CHFORMAT, CM_CHB3D);
		}
		if (channels == 6) {
			snd_cmipci_set_bit(cm, CM_REG_LEGACY_CTRL, CM_CHB3D6C);
			snd_cmipci_set_bit(cm, CM_REG_MISC_CTRL, CM_ENCENTER);
		} else {
			snd_cmipci_clear_bit(cm, CM_REG_LEGACY_CTRL, CM_CHB3D6C);
			snd_cmipci_clear_bit(cm, CM_REG_MISC_CTRL, CM_ENCENTER);
		}
		spin_unlock_irqrestore(&cm->reg_lock, flags);

	} else {
		if (cm->can_multi_ch) {
			spin_lock_irqsave(&cm->reg_lock, flags);
			snd_cmipci_clear_bit(cm, CM_REG_LEGACY_CTRL, CM_NXCHG);
			snd_cmipci_clear_bit(cm, CM_REG_CHFORMAT, CM_CHB3D);
			snd_cmipci_clear_bit(cm, CM_REG_CHFORMAT, CM_CHB3D5C);
			snd_cmipci_clear_bit(cm, CM_REG_LEGACY_CTRL, CM_CHB3D6C);
			snd_cmipci_clear_bit(cm, CM_REG_MISC_CTRL, CM_ENCENTER);
			spin_unlock_irqrestore(&cm->reg_lock, flags);
		}
	}
	return 0;
}


/*
 * prepare playback/capture channel
 * channel to be used must have been set in rec->ch.
 */
static int snd_cmipci_pcm_prepare(cmipci_t *cm, cmipci_pcm_t *rec,
				 snd_pcm_substream_t *substream)
{
	unsigned long flags;
	unsigned int reg, freq, val;
	snd_pcm_runtime_t *runtime = substream->runtime;

	rec->fmt = 0;
	rec->shift = 0;
	if (snd_pcm_format_width(runtime->format) >= 16) {
		rec->fmt |= 0x02;
		if (snd_pcm_format_width(runtime->format) > 16)
			rec->shift++; /* 24/32bit */
	}
	if (runtime->channels > 1)
		rec->fmt |= 0x01;
	if (rec->is_dac && set_dac_channels(cm, rec, runtime->channels) < 0) {
		snd_printd("cannot set dac channels\n");
		return -EINVAL;
	}

	rec->offset = runtime->dma_addr;
	/* buffer and period sizes in frame */
	rec->dma_size = runtime->buffer_size << rec->shift;
	rec->period_size = runtime->period_size << rec->shift;
	rec->dma_size <<= rec->ac3_shift;
	rec->period_size <<= rec->ac3_shift;
	if (runtime->channels > 2) {
		/* multi-channels */
		rec->dma_size = (rec->dma_size * runtime->channels) / 2;
		rec->period_size = (rec->period_size * runtime->channels) / 2;
	}

	spin_lock_irqsave(&cm->reg_lock, flags);

	/* set buffer address */
	reg = rec->ch ? CM_REG_CH1_FRAME1 : CM_REG_CH0_FRAME1;
	snd_cmipci_write(cm, reg, rec->offset);
	/* program sample counts */
	reg = rec->ch ? CM_REG_CH1_FRAME2 : CM_REG_CH0_FRAME2;
	snd_cmipci_write_w(cm, reg, rec->dma_size - 1);
	snd_cmipci_write_w(cm, reg + 2, rec->period_size - 1);

	/* set adc/dac flag */
	val = rec->ch ? CM_CHADC1 : CM_CHADC0;
	if (rec->is_dac)
		cm->ctrl &= ~val;
	else
		cm->ctrl |= val;
	snd_cmipci_write(cm, CM_REG_FUNCTRL0, cm->ctrl);
	//snd_printd("cmipci: functrl0 = %08x\n", cm->ctrl);

	/* set sample rate */
	freq = snd_cmipci_rate_freq(runtime->rate);
	val = snd_cmipci_read(cm, CM_REG_FUNCTRL1);
	if (rec->ch) {
		val &= ~CM_ASFC_MASK;
		val |= (freq << CM_ASFC_SHIFT) & CM_ASFC_MASK;
	} else {
		val &= ~CM_DSFC_MASK;
		val |= (freq << CM_DSFC_SHIFT) & CM_DSFC_MASK;
	}
	snd_cmipci_write(cm, CM_REG_FUNCTRL1, val);
	//snd_printd("cmipci: functrl1 = %08x\n", val);

	/* set format */
	val = snd_cmipci_read(cm, CM_REG_CHFORMAT);
	if (rec->ch) {
		val &= ~CM_CH1FMT_MASK;
		val |= rec->fmt << CM_CH1FMT_SHIFT;
	} else {
		val &= ~CM_CH0FMT_MASK;
		val |= rec->fmt << CM_CH0FMT_SHIFT;
	}
	snd_cmipci_write(cm, CM_REG_CHFORMAT, val);
	//snd_printd("cmipci: chformat = %08x\n", val);

	rec->running = 0;
	spin_unlock_irqrestore(&cm->reg_lock, flags);

	return 0;
}

/*
 * PCM trigger/stop
 */
static int snd_cmipci_pcm_trigger(cmipci_t *cm, cmipci_pcm_t *rec,
				 snd_pcm_substream_t *substream, int cmd)
{
	unsigned int inthld, chen, reset, pause;
	int result = 0;

	inthld = CM_CH0_INT_EN << rec->ch;
	chen = CM_CHEN0 << rec->ch;
	reset = CM_RST_CH0 << rec->ch;
	pause = CM_PAUSE0 << rec->ch;

	spin_lock(&cm->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		rec->running = 1;
		/* set interrupt */
		snd_cmipci_set_bit(cm, CM_REG_INT_HLDCLR, inthld);
		cm->ctrl |= chen;
		/* enable channel */
		snd_cmipci_write(cm, CM_REG_FUNCTRL0, cm->ctrl);
		//snd_printd("cmipci: functrl0 = %08x\n", cm->ctrl);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		rec->running = 0;
		/* disable interrupt */
		snd_cmipci_clear_bit(cm, CM_REG_INT_HLDCLR, inthld);
		/* reset */
		cm->ctrl &= ~chen;
		snd_cmipci_write(cm, CM_REG_FUNCTRL0, cm->ctrl | reset);
		snd_cmipci_write(cm, CM_REG_FUNCTRL0, cm->ctrl & ~reset);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		cm->ctrl |= pause;
		snd_cmipci_write(cm, CM_REG_FUNCTRL0, cm->ctrl);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		cm->ctrl &= ~pause;
		snd_cmipci_write(cm, CM_REG_FUNCTRL0, cm->ctrl);
		break;
	default:
		result = -EINVAL;
		break;
	}
	spin_unlock(&cm->reg_lock);
	return result;
}

/*
 * return the current pointer
 */
static snd_pcm_uframes_t snd_cmipci_pcm_pointer(cmipci_t *cm, cmipci_pcm_t *rec,
					  snd_pcm_substream_t *substream)
{
	size_t ptr;
	unsigned int reg;
	if (!rec->running)
		return 0;
#if 1 // this seems better..
	reg = rec->ch ? CM_REG_CH1_FRAME2 : CM_REG_CH0_FRAME2;
	ptr = rec->dma_size - (snd_cmipci_read_w(cm, reg) + 1);
	ptr >>= rec->shift;
#else
	reg = rec->ch ? CM_REG_CH1_FRAME1 : CM_REG_CH0_FRAME1;
	ptr = snd_cmipci_read(cm, reg) - rec->offset;
	ptr = bytes_to_frames(substream->runtime, ptr);
#endif
	ptr >>= rec->ac3_shift;
	if (substream->runtime->channels > 2)
		ptr = (ptr * 2) / substream->runtime->channels;
	return ptr;
}

/*
 * playback
 */

static int snd_cmipci_playback_trigger(snd_pcm_substream_t *substream,
				       int cmd)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	return snd_cmipci_pcm_trigger(cm, &cm->channel[CM_CH_PLAY], substream, cmd);
}

static snd_pcm_uframes_t snd_cmipci_playback_pointer(snd_pcm_substream_t *substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	return snd_cmipci_pcm_pointer(cm, &cm->channel[CM_CH_PLAY], substream);
}



/*
 * capture
 */

static int snd_cmipci_capture_trigger(snd_pcm_substream_t *substream,
				     int cmd)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	return snd_cmipci_pcm_trigger(cm, &cm->channel[CM_CH_CAPT], substream, cmd);
}

static snd_pcm_uframes_t snd_cmipci_capture_pointer(snd_pcm_substream_t *substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	return snd_cmipci_pcm_pointer(cm, &cm->channel[CM_CH_CAPT], substream);
}

#ifdef DO_SOFT_AC3
/*
 * special tricks for soft ac3 transfer:
 *
 * we compose an iec958 subframe from 16bit ac3 sample and
 * write the raw subframe via 32bit data mode.
 */

# ifndef USE_AES_IEC958

/* find parity for bit 4~30 */
static unsigned int parity(unsigned int data)
{
	unsigned int parity = 0;
	int counter = 4;

	data >>= 4;	/* start from bit 4 */
	while (counter <= 30) {
		if (data & 1)
			parity++;
		data >>= 1;
		counter++;
	}
	return parity & 1;
}

/*
 * compose 32bit iec958 subframe with non-audio data.
 * bit 0-3  = preamble
 *     4-7  = aux (=0)
 *     8-27 = data (12-27 for 16bit)
 *     28   = validity (=0)
 *     29   = user data (=0)
 *     30   = channel status
 *     31   = parity
 *
 * channel status is assumed as consumer, non-audio
 * thus all 0 except bit 1
 */
inline static u32 convert_ac3_32bit(cmipci_t *cm, u32 val)
{
	u32 data = (u32)val << 12;

	if (cm->spdif_counter == 2 || cm->spdif_counter == 3) /* bit 1 */
		data |= 0x40000000;	/* indicate AC-3 raw data */
	if (parity(data))		/* parity bit 4-30 */
		data |= 0x80000000;
	if (cm->spdif_counter == 0)
		data |= 3;		/* preamble 'M' */
	else if (cm->spdif_counter & 1)
		data |= 5;		/* odd, 'W' */
	else
		data |= 9;		/* even, 'M' */

	cm->spdif_counter++;
	if (cm->spdif_counter == 384)
		cm->spdif_counter = 0;

	return data;
}

# else  /* if USE_AES_IEC958 */

/*
 * The bitstream handling
 */
typedef struct iec958_stru_bitstream {
	u32 *data;		/* Holds the current position */
	u32  left;		/* Bits left in current 32bit frame */
	u32  word;		/* The 32bit frame of the current position */
	u32  bits;		/* All bits together */
	int   err;		/* Error condition */
} iec958_bitstream_t ;

static iec958_bitstream_t bs;

/* Initialize ptr on the buffer */
static void iec958_init_bitstream(u8 *buf, u32 size)
{
	bs.data = (u32 *)buf;		/* Set initial position */
	bs.word = *bs.data;		/* The first 32bit frame */
	bs.left = 32;			/* has exactly 32bits */
	bs.bits = size;
	bs.err = 0;
}

/* Remove ptr on the buffer */
static void iec958_clear_bitstream(void)
{
	bs.data = NULL;
	bs.left = 0;
	bs.err = 0;
}

/* Get bits from bitstream (max 32) */
static inline u32 iec958_getbits(u32 bits)
{
	u32 res;

	if (bs.bits < bits) {
		bits = bs.bits;
		bs.err = 1;
	}
	if (bits > 32) {
		bits = 32;
		bs.err = 1;
	}
	bs.bits -= bits;

#  ifdef WORDS_BIGENDIAN
	if (bits < bs.left) {		/* Within 32bit frame */
		res = (bs.word << (32 - bs.left)) >> (32 - bits);
		bs.left -= bits;
		goto out;
	}				/* We may cross the frame boundary */
	res   = (bs.word << (32 - bs.left)) >> (32 - bs.left);
	bits -= bs.left;

	bs.word = *(++bs.data);		/* Next 32bit frame */

	if (bits)			/* Add remaining bits, if any */
		res = (res << bits) | (bs.word >> (32 - bits));

#  else  /* not WORDS_BIGENDIAN */

	if (bits < bs.left) {		/* Within 32bit frame */
		res = (bs.word << (32 - bits)) >> (32 - bits);
		bs.word >>= bits;
		bs.left -= bits;
		goto out;
	}				/* We may cross the frame boundary */
	res   = bs.word;
	bits -= bs.left;

	bs.word = *(++bs.data);		/* Next 32bit frame */

	if (bits) {			/* Add remaining bits, if any */
		res = res | (((bs.word << (32 - bits)) >> (32 - bits)) << bits);
		bs.word >>= bits;
	}
#  endif /* not WORDS_BIGENDIAN */

	bs.left = (32 - bits);
out:
	return res;
}

static inline u32 iec958_bits_avail(void)
{
	return bs.bits;
}

static inline int iec958_error(void)
{
	return bs.err;
}

/*
 * Determine parity for time slots 4 upto 30
 * to be sure that bit 4 upt 31 will carry
 * an even number of ones and zeros.
 */
static u32 iec958_parity(u32 data)
{
	u32 parity = 0;
	int counter = 4;

	data >>= 4;     /* start from bit 4 */
	while (counter++ <= 30) {
		if (data & 0x00000001)
			parity++;
		data >>= 1;
	}
	return (parity & 0x00000001);
}

/*
 * Compose 32bit iec958 subframe, two sub frames
 * build one frame with two channels.
 *
 * bit 0-3  = preamble
 *     4-7  = AUX (=0)
 *     8-27 = data (12-27 for 16bit, 8-27 for 20bit, and 24bit without AUX)
 *     28   = validity (0 for valid data, else 'in error')
 *     29   = user data (0)
 *     30   = channel status (24 bytes for 192 frames)
 *     31   = parity
 */

static inline u32 iec958_subframe(cmipci_t *cm, snd_ctl_elem_value_t * ucontrol)
{
	u32 data;
	u32 byte = cm->spdif_counter >> 4;
	u32 mask = 1 << ((cm->spdif_counter >> 1) - (byte << 3));
	u8 * status = ucontrol->value.iec958.status;

	if (status[2] & IEC958_AES2_PRO_SBITS_24) {
		/* Does this work for LE systems ??? */
		if (status[2] & IEC958_AES2_PRO_WORDLEN_24_20) {
			data = iec958_getbits(24);
			data <<= 4;
		} else {
			data = iec958_getbits(20);
			data <<= 8;
		}
	} else {
		if (status[2] & IEC958_AES2_PRO_WORDLEN_24_20) {
			/* Does this work for LE systems ??? */
			data = iec958_getbits(20);
			data <<= 8;
		} else {
			data = iec958_getbits(16);
			data <<= 12;
		}
	}

	/*
	 * Set one of the 192 bits of the channel status (AES3 and higher)
	 */
	if (status[byte] & mask)
		data |= 0x40000000;

	if (iec958_parity(data))	/* parity bit 4-30 */
		data |= 0x80000000;

	/* Preamble */
	if      (!cm->spdif_counter)
		data |= 0x03;		/* Block start, 'Z' */
	else if (cm->spdif_counter % 2)
		data |= 0x05;		/* odd sub frame, 'Y' */
	else
		data |= 0x09;		/* even sub frame, 'X' */

	/*
	 * sub frame counter: 2 sub frame are one audio frame
	 * and 192 frames are one block
	 */
	cm->spdif_counter = (++cm->spdif_counter) % 384;

	return data;
}
# endif /* if USE_AES_IEC958 */

static int snd_cmipci_ac3_copy(snd_pcm_substream_t *subs, int channel,
			       snd_pcm_uframes_t pos, void *src,
			       snd_pcm_uframes_t count)
{
	cmipci_t *cm = snd_pcm_substream_chip(subs);
	u32 *dst;
	snd_pcm_uframes_t offset;
	snd_pcm_runtime_t *runtime = subs->runtime;
#ifndef USE_AES_IEC958
	u16 *srcp = src, val;
#else
	char buf[480];         /* bits can be divided by 20, 24, 16 */
	size_t bytes = frames_to_bytes(runtime, count);
#endif


	if (!cm->channel[CM_CH_PLAY].ac3_shift) {
		if (copy_from_user(runtime->dma_area +
				   frames_to_bytes(runtime, pos), src,
				   frames_to_bytes(runtime, count)))
			return -EFAULT;
		return 0;
	}

	if (! access_ok(VERIFY_READ, src, count))
		return -EFAULT;

	/* frame = 16bit stereo */
	offset = (pos << 1) % (cm->channel[CM_CH_PLAY].dma_size << 2);
	dst = (u32*)(runtime->dma_area + offset);
# ifndef USE_AES_IEC958
	count /= 2;
	while (count-- > 0) {
		get_user(val, srcp);
		srcp++;
		*dst++ = convert_ac3_32bit(cm, val);
	}
# else
	while (bytes) {
		size_t c = bytes;

		if (c > sizeof(buf))
			c = sizeof(buf);

		if (copy_from_user(buf, src, c))
			return -EFAULT;
		bytes -= c;
		src   += c;

		iec958_init_bitstream(buf, c*8);
		while (iec958_bits_avail()) {
			*(dst++) = iec958_subframe(cm, cm->spdif_channel);
			if (iec958_error())
				return -EINVAL;
		}
		iec958_clear_bitstream();
	}
# endif
	return 0;
}

static int snd_cmipci_ac3_silence(snd_pcm_substream_t *subs, int channel,
				  snd_pcm_uframes_t pos,
				  snd_pcm_uframes_t count)
{
	cmipci_t *cm = snd_pcm_substream_chip(subs);
	u32 *dst;
	snd_pcm_uframes_t offset;
	snd_pcm_runtime_t *runtime = subs->runtime;
# ifdef USE_AES_IEC958
	char buf[480];		/* bits can be divided by 20, 24, 16 */
	size_t bytes = frames_to_bytes(runtime, count);
# endif
	if (! cm->channel[CM_CH_PLAY].ac3_shift)
		return snd_pcm_format_set_silence(runtime->format,
						  runtime->dma_area + frames_to_bytes(runtime, pos), count);
	
	/* frame = 16bit stereo */
	offset = (pos << 1) % (cm->channel[CM_CH_PLAY].dma_size << 2);
	dst = (u32*)(subs->runtime->dma_area + offset);
# ifndef USE_AES_IEC958
	count /= 2;
	while (count-- > 0) {
		*dst++ = convert_ac3_32bit(cm, 0);
	}
# else
	while (bytes) {
		size_t c = bytes;

		if (c > sizeof(buf))
			c = sizeof(buf);

		/* Q: Does this function know about 24bit silence? */
		if (snd_pcm_format_set_silence(runtime->format, buf, bytes_to_frames(runtime, c)))
			return -EINVAL;

		iec958_init_bitstream(buf, c*8);
		while (iec958_bits_avail()) {
			*(dst++) = iec958_subframe(cm, cm->spdif_channel);
			if (iec958_error())
				return -EINVAL;
		}
		iec958_clear_bitstream();
	}
# endif
	return 0;
}
#endif /* DO_SOFT_AC3 */


/*
 * hw preparation for spdif
 */

static int snd_cmipci_spdif_default_info(snd_kcontrol_t *kcontrol,
					 snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_cmipci_spdif_default_get(snd_kcontrol_t *kcontrol,
					snd_ctl_elem_value_t *ucontrol)
{
	cmipci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&chip->reg_lock, flags);
	for (i = 0; i < 4; i++)
		ucontrol->value.iec958.status[i] = (chip->dig_status >> (i * 8)) & 0xff;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_cmipci_spdif_default_put(snd_kcontrol_t * kcontrol,
					 snd_ctl_elem_value_t * ucontrol)
{
	cmipci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int i, change;
	unsigned int val;

	val = 0;
	spin_lock_irqsave(&chip->reg_lock, flags);
	for (i = 0; i < 4; i++)
		val |= (unsigned int)ucontrol->value.iec958.status[i] << (i * 8);
	change = val != chip->dig_status;
	chip->dig_status = val;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_cmipci_spdif_default __devinitdata =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.info =		snd_cmipci_spdif_default_info,
	.get =		snd_cmipci_spdif_default_get,
	.put =		snd_cmipci_spdif_default_put
};

static int snd_cmipci_spdif_mask_info(snd_kcontrol_t *kcontrol,
				      snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_cmipci_spdif_mask_get(snd_kcontrol_t * kcontrol,
				     snd_ctl_elem_value_t *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
	return 0;
}

static snd_kcontrol_new_t snd_cmipci_spdif_mask __devinitdata =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
	.info =		snd_cmipci_spdif_mask_info,
	.get =		snd_cmipci_spdif_mask_get,
};

static int snd_cmipci_spdif_stream_info(snd_kcontrol_t *kcontrol,
					snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_cmipci_spdif_stream_get(snd_kcontrol_t *kcontrol,
				       snd_ctl_elem_value_t *ucontrol)
{
	cmipci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&chip->reg_lock, flags);
	for (i = 0; i < 4; i++)
		ucontrol->value.iec958.status[i] = (chip->dig_pcm_status >> (i * 8)) & 0xff;
#ifdef USE_AES_IEC958
	ucontrol = chip->spdif_channel;
#endif
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_cmipci_spdif_stream_put(snd_kcontrol_t *kcontrol,
				       snd_ctl_elem_value_t *ucontrol)
{
	cmipci_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int i, change;
	unsigned int val;

	val = 0;
	spin_lock_irqsave(&chip->reg_lock, flags);
	for (i = 0; i < 4; i++)
		val |= (unsigned int)ucontrol->value.iec958.status[i] << (i * 8);
	change = val != chip->dig_pcm_status;
	chip->dig_pcm_status = val;
#ifdef USE_AES_IEC958
	chip->spdif_channel = ucontrol;
#endif
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_cmipci_spdif_stream __devinitdata =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	.info =		snd_cmipci_spdif_stream_info,
	.get =		snd_cmipci_spdif_stream_get,
	.put =		snd_cmipci_spdif_stream_put
};

/*
 */

/* save mixer setting and mute for AC3 playback */
static void save_mixer_state(cmipci_t *cm)
{
	if (! cm->mixer_insensitive) {
		unsigned int i;
		for (i = 0; i < CM_SAVED_MIXERS; i++) {
			snd_kcontrol_t *ctl = cm->mixer_res_ctl[i];
			if (ctl) {
				snd_ctl_elem_value_t val;
				int event;
				memset(&val, 0, sizeof(val));
				ctl->get(ctl, &val);
				cm->mixer_res_status[i] = val.value.integer.value[0];
				val.value.integer.value[0] = cm_saved_mixer[i].toggle_on;
				event = SNDRV_CTL_EVENT_MASK_INFO;
				if (cm->mixer_res_status[i] != val.value.integer.value[0]) {
					ctl->put(ctl, &val); /* toggle */
					event |= SNDRV_CTL_EVENT_MASK_VALUE;
				}
				ctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
				snd_ctl_notify(cm->card, event, &ctl->id);
			}
		}
		cm->mixer_insensitive = 1;
	}
}


/* restore the previously saved mixer status */
static void restore_mixer_state(cmipci_t *cm)
{
	if (cm->mixer_insensitive) {
		unsigned int i;
		cm->mixer_insensitive = 0; /* at first clear this;
					      otherwise the changes will be ignored */
		for (i = 0; i < CM_SAVED_MIXERS; i++) {
			snd_kcontrol_t *ctl = cm->mixer_res_ctl[i];
			if (ctl) {
				snd_ctl_elem_value_t val;
				int event;

				memset(&val, 0, sizeof(val));
				ctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
				ctl->get(ctl, &val);
				event = SNDRV_CTL_EVENT_MASK_INFO;
				if (val.value.integer.value[0] != cm->mixer_res_status[i]) {
					val.value.integer.value[0] = cm->mixer_res_status[i];
					ctl->put(ctl, &val);
					event |= SNDRV_CTL_EVENT_MASK_VALUE;
				}
				snd_ctl_notify(cm->card, event, &ctl->id);
			}
		}
	}
}

/* spinlock held! */
static void setup_ac3(cmipci_t *cm, snd_pcm_substream_t *subs, int do_ac3, int rate)
{
	cm->channel[CM_CH_PLAY].ac3_shift = 0;
	cm->spdif_counter = 0;

	if (do_ac3) {
		/* AC3EN for 037 */
		snd_cmipci_set_bit(cm, CM_REG_CHFORMAT, CM_AC3EN1);
		/* AC3EN for 039 */
		snd_cmipci_set_bit(cm, CM_REG_MISC_CTRL, CM_AC3EN2);
	
		if (cm->can_ac3_hw) {
			/* SPD24SEL for 037, 0x02 */
			/* SPD24SEL for 039, 0x20, but cannot be set */
			snd_cmipci_set_bit(cm, CM_REG_CHFORMAT, CM_SPD24SEL);
			snd_cmipci_clear_bit(cm, CM_REG_MISC_CTRL, CM_SPD32SEL);
		} else { /* can_ac3_sw */
#ifdef DO_SOFT_AC3
			/* FIXME: ugly hack! */
			subs->runtime->buffer_size /= 2;
			/* SPD32SEL for 037 & 039, 0x20 */
			snd_cmipci_set_bit(cm, CM_REG_MISC_CTRL, CM_SPD32SEL);
			/* set 176K sample rate to fix 033 HW bug */
			if (cm->chip_version == 33) {
				if (rate >= 48000) {
					snd_cmipci_set_bit(cm, CM_REG_CHFORMAT, CM_PLAYBACK_SRATE_176K);
				} else {
					snd_cmipci_clear_bit(cm, CM_REG_CHFORMAT, CM_PLAYBACK_SRATE_176K);
				}
			}
			cm->channel[CM_CH_PLAY].ac3_shift = 1; /* use 32bit */
#endif /* DO_SOFT_AC3 */
		}

	} else {
		snd_cmipci_clear_bit(cm, CM_REG_CHFORMAT, CM_AC3EN1);
		snd_cmipci_clear_bit(cm, CM_REG_MISC_CTRL, CM_AC3EN2);

		if (cm->can_ac3_hw) {
			/* chip model >= 37 */
			if (snd_pcm_format_width(subs->runtime->format) > 16) {
				snd_cmipci_set_bit(cm, CM_REG_MISC_CTRL, CM_SPD32SEL);
				snd_cmipci_set_bit(cm, CM_REG_CHFORMAT, CM_SPD24SEL);
			} else {
				snd_cmipci_clear_bit(cm, CM_REG_MISC_CTRL, CM_SPD32SEL);
				snd_cmipci_clear_bit(cm, CM_REG_CHFORMAT, CM_SPD24SEL);
			}
		} else {
#ifdef DO_SOFT_AC3
			snd_cmipci_clear_bit(cm, CM_REG_MISC_CTRL, CM_SPD32SEL);
			snd_cmipci_clear_bit(cm, CM_REG_CHFORMAT, CM_SPD24SEL);
			snd_cmipci_clear_bit(cm, CM_REG_CHFORMAT, CM_PLAYBACK_SRATE_176K);
#endif /* DO_SOFT_AC3 */
		}
	}
}

static void setup_spdif_playback(cmipci_t *cm, snd_pcm_substream_t *subs, int up, int do_ac3)
{
	int rate;
	unsigned long flags;

	rate = subs->runtime->rate;

	if (up && do_ac3)
		save_mixer_state(cm);

	spin_lock_irqsave(&cm->reg_lock, flags);
	cm->spdif_playback_avail = up;
	if (up) {
		/* they are controlled via "IEC958 Output Switch" */
		/* snd_cmipci_set_bit(cm, CM_REG_LEGACY_CTRL, CM_ENSPDOUT); */
		/* snd_cmipci_set_bit(cm, CM_REG_FUNCTRL1, CM_SPDO2DAC); */
		if (cm->spdif_playback_enabled)
			snd_cmipci_set_bit(cm, CM_REG_FUNCTRL1, CM_PLAYBACK_SPDF);
		setup_ac3(cm, subs, do_ac3, rate);

		if (rate == 48000)
			snd_cmipci_set_bit(cm, CM_REG_MISC_CTRL, CM_SPDIF48K | CM_SPDF_AC97);
		else
			snd_cmipci_clear_bit(cm, CM_REG_MISC_CTRL, CM_SPDIF48K | CM_SPDF_AC97);

	} else {
		/* they are controlled via "IEC958 Output Switch" */
		/* snd_cmipci_clear_bit(cm, CM_REG_LEGACY_CTRL, CM_ENSPDOUT); */
		/* snd_cmipci_clear_bit(cm, CM_REG_FUNCTRL1, CM_SPDO2DAC); */
		snd_cmipci_clear_bit(cm, CM_REG_FUNCTRL1, CM_PLAYBACK_SPDF);
		setup_ac3(cm, subs, 0, 0);
	}
	spin_unlock_irqrestore(&cm->reg_lock, flags);
}


/*
 * preparation
 */

/* playback - enable spdif only on the certain condition */
static int snd_cmipci_playback_prepare(snd_pcm_substream_t *substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	int rate = substream->runtime->rate;
	int do_spdif, do_ac3;
	do_spdif = ((rate == 44100 || rate == 48000) &&
		    substream->runtime->format == SNDRV_PCM_FORMAT_S16_LE &&
		    substream->runtime->channels == 2);
	do_ac3 = cm->dig_pcm_status & IEC958_AES0_NONAUDIO;
#ifdef DO_SOFT_AC3
	if (do_ac3 && cm->can_ac3_sw)
		do_spdif = 0;
#endif
	setup_spdif_playback(cm, substream, do_spdif, do_ac3);
	return snd_cmipci_pcm_prepare(cm, &cm->channel[CM_CH_PLAY], substream);
}

/* playback  (via device #2) - enable spdif always */
static int snd_cmipci_playback_spdif_prepare(snd_pcm_substream_t *substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	setup_spdif_playback(cm, substream, 1, cm->dig_pcm_status & IEC958_AES0_NONAUDIO);
	return snd_cmipci_pcm_prepare(cm, &cm->channel[CM_CH_PLAY], substream);
}

static int snd_cmipci_playback_hw_free(snd_pcm_substream_t *substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	setup_spdif_playback(cm, substream, 0, 0);
	restore_mixer_state(cm);
	return snd_cmipci_hw_free(substream);
}

/* capture */
static int snd_cmipci_capture_prepare(snd_pcm_substream_t *substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	return snd_cmipci_pcm_prepare(cm, &cm->channel[CM_CH_CAPT], substream);
}

/* capture with spdif (via device #2) */
static int snd_cmipci_capture_spdif_prepare(snd_pcm_substream_t *substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&cm->reg_lock, flags);
	snd_cmipci_set_bit(cm, CM_REG_FUNCTRL1, CM_CAPTURE_SPDF);
	spin_unlock_irqrestore(&cm->reg_lock, flags);

	return snd_cmipci_pcm_prepare(cm, &cm->channel[CM_CH_CAPT], substream);
}

static int snd_cmipci_capture_spdif_hw_free(snd_pcm_substream_t *subs)
{
	cmipci_t *cm = snd_pcm_substream_chip(subs);
	unsigned long flags;

	spin_lock_irqsave(&cm->reg_lock, flags);
	snd_cmipci_clear_bit(cm, CM_REG_FUNCTRL1, CM_CAPTURE_SPDF);
	spin_unlock_irqrestore(&cm->reg_lock, flags);

	return snd_cmipci_hw_free(subs);
}


/*
 * interrupt handler
 */
static irqreturn_t snd_cmipci_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	cmipci_t *cm = snd_magic_cast(cmipci_t, dev_id, return IRQ_NONE);
	unsigned int status, mask = 0;
	
	/* fastpath out, to ease interrupt sharing */
	status = snd_cmipci_read(cm, CM_REG_INT_STATUS);
	if (!(status & CM_INTR))
		return IRQ_NONE;

	/* acknowledge interrupt */
	spin_lock(&cm->reg_lock);
	if (status & CM_CHINT0)
		mask |= CM_CH0_INT_EN;
	if (status & CM_CHINT1)
		mask |= CM_CH1_INT_EN;
	snd_cmipci_clear_bit(cm, CM_REG_INT_HLDCLR, mask);
	snd_cmipci_set_bit(cm, CM_REG_INT_HLDCLR, mask);
	spin_unlock(&cm->reg_lock);

	if (cm->rmidi && (status & CM_UARTINT))
		snd_mpu401_uart_interrupt(irq, cm->rmidi->private_data, regs);

	if (cm->pcm) {
		if ((status & CM_CHINT0) && cm->channel[0].running)
			snd_pcm_period_elapsed(cm->channel[0].substream);
		if ((status & CM_CHINT1) && cm->channel[1].running)
			snd_pcm_period_elapsed(cm->channel[1].substream);
	}
	return IRQ_HANDLED;
}

/*
 * h/w infos
 */

/* playback on channel A */
static snd_pcm_hardware_t snd_cmipci_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_5512 | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5512,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =		0,
};

/* capture on channel B */
static snd_pcm_hardware_t snd_cmipci_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_5512 | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5512,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =		0,
};

/* playback on channel B - stereo 16bit only? */
static snd_pcm_hardware_t snd_cmipci_playback2 =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_5512 | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5512,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =		0,
};

/* spdif playback on channel A */
static snd_pcm_hardware_t snd_cmipci_playback_spdif =
{
	.info =			(SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_PAUSE),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
	.rate_min =		44100,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =		0,
};

/* spdif capture on channel B */
static snd_pcm_hardware_t snd_cmipci_capture_spdif =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER | SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =	        SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
	.rate_min =		44100,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =		0,
};

/*
 * check device open/close
 */
static int open_device_check(cmipci_t *cm, int mode, snd_pcm_substream_t *subs)
{
	unsigned long flags;
	int ch = mode & CM_OPEN_CH_MASK;

	/* FIXME: a file should wait until the device becomes free
	 * when it's opened on blocking mode.  however, since the current
	 * pcm framework doesn't pass file pointer before actually opened,
	 * we can't know whether blocking mode or not in open callback..
	 */
	down(&cm->open_mutex);
	if (cm->opened[ch]) {
		up(&cm->open_mutex);
		return -EBUSY;
	}
	cm->opened[ch] = mode;
	cm->channel[ch].substream = subs;
	if (! (mode & CM_OPEN_DAC)) {
		/* disable dual DAC mode */
		cm->channel[ch].is_dac = 0;
		spin_lock_irqsave(&cm->reg_lock, flags);
		snd_cmipci_clear_bit(cm, CM_REG_MISC_CTRL, CM_ENDBDAC);
		spin_unlock_irqrestore(&cm->reg_lock, flags);
	}
	up(&cm->open_mutex);
	return 0;
}

static void close_device_check(cmipci_t *cm, int mode)
{
	unsigned long flags;
	int ch = mode & CM_OPEN_CH_MASK;

	down(&cm->open_mutex);
	if (cm->opened[ch] == mode) {
		if (cm->channel[ch].substream) {
			snd_cmipci_ch_reset(cm, ch);
			cm->channel[ch].running = 0;
			cm->channel[ch].substream = NULL;
		}
		cm->opened[ch] = 0;
		if (! cm->channel[ch].is_dac) {
			/* enable dual DAC mode again */
			cm->channel[ch].is_dac = 1;
			spin_lock_irqsave(&cm->reg_lock, flags);
			snd_cmipci_set_bit(cm, CM_REG_MISC_CTRL, CM_ENDBDAC);
			spin_unlock_irqrestore(&cm->reg_lock, flags);
		}
	}
	up(&cm->open_mutex);
}

/*
 */

static int snd_cmipci_playback_open(snd_pcm_substream_t *substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	if ((err = open_device_check(cm, CM_OPEN_PLAYBACK, substream)) < 0)
		return err;
	runtime->hw = snd_cmipci_playback;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 0x10000);
	return 0;
}

static int snd_cmipci_capture_open(snd_pcm_substream_t *substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	if ((err = open_device_check(cm, CM_OPEN_CAPTURE, substream)) < 0)
		return err;
	runtime->hw = snd_cmipci_capture;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 0x10000);
	return 0;
}

static int snd_cmipci_playback2_open(snd_pcm_substream_t *substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	if ((err = open_device_check(cm, CM_OPEN_PLAYBACK2, substream)) < 0) /* use channel B */
		return err;
	runtime->hw = snd_cmipci_playback2;
	down(&cm->open_mutex);
	if (! cm->opened[CM_CH_PLAY]) {
		if (cm->can_multi_ch) {
			runtime->hw.channels_max = cm->max_channels;
			if (cm->max_channels == 4)
				snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, &hw_constraints_channels_4);
			else
				snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, &hw_constraints_channels_6);
		}
		snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 0x10000);
	}
	up(&cm->open_mutex);
	return 0;
}

static int snd_cmipci_playback_spdif_open(snd_pcm_substream_t *substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	if ((err = open_device_check(cm, CM_OPEN_SPDIF_PLAYBACK, substream)) < 0) /* use channel A */
		return err;
	runtime->hw = snd_cmipci_playback_spdif;
#ifdef DO_SOFT_AC3
	if (cm->can_ac3_hw)
#endif
		runtime->hw.info |= SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID;
	if (cm->chip_version >= 37)
		runtime->hw.formats |= SNDRV_PCM_FMTBIT_S32_LE;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 0x40000);
	cm->dig_pcm_status = cm->dig_status;
	return 0;
}

static int snd_cmipci_capture_spdif_open(snd_pcm_substream_t * substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	if ((err = open_device_check(cm, CM_OPEN_SPDIF_CAPTURE, substream)) < 0) /* use channel B */
		return err;
	runtime->hw = snd_cmipci_capture_spdif;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 0x40000);
	return 0;
}


/*
 */

static int snd_cmipci_playback_close(snd_pcm_substream_t * substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	close_device_check(cm, CM_OPEN_PLAYBACK);
	return 0;
}

static int snd_cmipci_capture_close(snd_pcm_substream_t * substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	close_device_check(cm, CM_OPEN_CAPTURE);
	return 0;
}

static int snd_cmipci_playback2_close(snd_pcm_substream_t * substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	close_device_check(cm, CM_OPEN_PLAYBACK2);
	close_device_check(cm, CM_OPEN_PLAYBACK_MULTI);
	return 0;
}

static int snd_cmipci_playback_spdif_close(snd_pcm_substream_t * substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	close_device_check(cm, CM_OPEN_SPDIF_PLAYBACK);
	return 0;
}

static int snd_cmipci_capture_spdif_close(snd_pcm_substream_t * substream)
{
	cmipci_t *cm = snd_pcm_substream_chip(substream);
	close_device_check(cm, CM_OPEN_SPDIF_CAPTURE);
	return 0;
}


/*
 */

static snd_pcm_ops_t snd_cmipci_playback_ops = {
	.open =		snd_cmipci_playback_open,
	.close =	snd_cmipci_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_cmipci_hw_params,
	.hw_free =	snd_cmipci_playback_hw_free,
	.prepare =	snd_cmipci_playback_prepare,
	.trigger =	snd_cmipci_playback_trigger,
	.pointer =	snd_cmipci_playback_pointer,
};

static snd_pcm_ops_t snd_cmipci_capture_ops = {
	.open =		snd_cmipci_capture_open,
	.close =	snd_cmipci_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_cmipci_hw_params,
	.hw_free =	snd_cmipci_hw_free,
	.prepare =	snd_cmipci_capture_prepare,
	.trigger =	snd_cmipci_capture_trigger,
	.pointer =	snd_cmipci_capture_pointer,
};

static snd_pcm_ops_t snd_cmipci_playback2_ops = {
	.open =		snd_cmipci_playback2_open,
	.close =	snd_cmipci_playback2_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_cmipci_playback2_hw_params,
	.hw_free =	snd_cmipci_hw_free,
	.prepare =	snd_cmipci_capture_prepare,	/* channel B */
	.trigger =	snd_cmipci_capture_trigger,	/* channel B */
	.pointer =	snd_cmipci_capture_pointer,	/* channel B */
};

static snd_pcm_ops_t snd_cmipci_playback_spdif_ops = {
	.open =		snd_cmipci_playback_spdif_open,
	.close =	snd_cmipci_playback_spdif_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_cmipci_hw_params,
	.hw_free =	snd_cmipci_playback_hw_free,
	.prepare =	snd_cmipci_playback_spdif_prepare,	/* set up rate */
	.trigger =	snd_cmipci_playback_trigger,
	.pointer =	snd_cmipci_playback_pointer,
};

#ifdef DO_SOFT_AC3
static snd_pcm_ops_t snd_cmipci_playback_spdif_soft_ops = {
	.open =		snd_cmipci_playback_spdif_open,
	.close =	snd_cmipci_playback_spdif_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_cmipci_hw_params,
	.hw_free =	snd_cmipci_playback_hw_free,
	.prepare =	snd_cmipci_playback_spdif_prepare,	/* set up rate */
	.trigger =	snd_cmipci_playback_trigger,
	.pointer =	snd_cmipci_playback_pointer,
	.copy =		snd_cmipci_ac3_copy,
	.silence =	snd_cmipci_ac3_silence,
};
#endif

static snd_pcm_ops_t snd_cmipci_capture_spdif_ops = {
	.open =		snd_cmipci_capture_spdif_open,
	.close =	snd_cmipci_capture_spdif_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_cmipci_hw_params,
	.hw_free =	snd_cmipci_capture_spdif_hw_free,
	.prepare =	snd_cmipci_capture_spdif_prepare,
	.trigger =	snd_cmipci_capture_trigger,
	.pointer =	snd_cmipci_capture_pointer,
};


/*
 */

static void snd_cmipci_pcm_free(snd_pcm_t *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __devinit snd_cmipci_pcm_new(cmipci_t *cm, int device)
{
	snd_pcm_t *pcm;
	int err;

	err = snd_pcm_new(cm->card, cm->card->driver, device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cmipci_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_cmipci_capture_ops);

	pcm->private_data = cm;
	pcm->private_free = snd_cmipci_pcm_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, "C-Media PCI DAC/ADC");
	cm->pcm = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(cm->pci, pcm, 64*1024, 128*1024);

	return 0;
}

static int __devinit snd_cmipci_pcm2_new(cmipci_t *cm, int device)
{
	snd_pcm_t *pcm;
	int err;

	err = snd_pcm_new(cm->card, cm->card->driver, device, 1, 0, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cmipci_playback2_ops);

	pcm->private_data = cm;
	pcm->private_free = snd_cmipci_pcm_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, "C-Media PCI 2nd DAC");
	cm->pcm2 = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(cm->pci, pcm, 64*1024, 128*1024);

	return 0;
}

static int __devinit snd_cmipci_pcm_spdif_new(cmipci_t *cm, int device)
{
	snd_pcm_t *pcm;
	int err;

	err = snd_pcm_new(cm->card, cm->card->driver, device, 1, 1, &pcm);
	if (err < 0)
		return err;

#ifdef DO_SOFT_AC3
	if (cm->can_ac3_hw)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cmipci_playback_spdif_ops);
	else
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cmipci_playback_spdif_soft_ops);
#else
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cmipci_playback_spdif_ops);
#endif
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_cmipci_capture_spdif_ops);

	pcm->private_data = cm;
	pcm->private_free = snd_cmipci_pcm_free;
	pcm->info_flags = 0;
	strcpy(pcm->name, "C-Media PCI IEC958");
	cm->pcm_spdif = pcm;

	snd_pcm_lib_preallocate_pci_pages_for_all(cm->pci, pcm, 64*1024, 128*1024);

	return 0;
}

/*
 * mixer interface:
 * - CM8338/8738 has a compatible mixer interface with SB16, but
 *   lack of some elements like tone control, i/o gain and AGC.
 * - Access to native registers:
 *   - A 3D switch
 *   - Output mute switches
 */

static void snd_cmipci_mixer_write(cmipci_t *s, unsigned char idx, unsigned char data)
{
	outb(idx, s->iobase + CM_REG_SB16_ADDR);
	outb(data, s->iobase + CM_REG_SB16_DATA);
}

static unsigned char snd_cmipci_mixer_read(cmipci_t *s, unsigned char idx)
{
	unsigned char v;

	outb(idx, s->iobase + CM_REG_SB16_ADDR);
	v = inb(s->iobase + CM_REG_SB16_DATA);
	return v;
}

/*
 * general mixer element
 */
typedef struct cmipci_sb_reg {
	unsigned int left_reg, right_reg;
	unsigned int left_shift, right_shift;
	unsigned int mask;
	unsigned int invert: 1;
	unsigned int stereo: 1;
} cmipci_sb_reg_t;

#define COMPOSE_SB_REG(lreg,rreg,lshift,rshift,mask,invert,stereo) \
 ((lreg) | ((rreg) << 8) | (lshift << 16) | (rshift << 19) | (mask << 24) | (invert << 22) | (stereo << 23))

#define CMIPCI_DOUBLE(xname, left_reg, right_reg, left_shift, right_shift, mask, invert, stereo) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_cmipci_info_volume, \
  .get = snd_cmipci_get_volume, .put = snd_cmipci_put_volume, \
  .private_value = COMPOSE_SB_REG(left_reg, right_reg, left_shift, right_shift, mask, invert, stereo), \
}

#define CMIPCI_SB_VOL_STEREO(xname,reg,shift,mask) CMIPCI_DOUBLE(xname, reg, reg+1, shift, shift, mask, 0, 1)
#define CMIPCI_SB_VOL_MONO(xname,reg,shift,mask) CMIPCI_DOUBLE(xname, reg, reg, shift, shift, mask, 0, 0)
#define CMIPCI_SB_SW_STEREO(xname,lshift,rshift) CMIPCI_DOUBLE(xname, SB_DSP4_OUTPUT_SW, SB_DSP4_OUTPUT_SW, lshift, rshift, 1, 0, 1)
#define CMIPCI_SB_SW_MONO(xname,shift) CMIPCI_DOUBLE(xname, SB_DSP4_OUTPUT_SW, SB_DSP4_OUTPUT_SW, shift, shift, 1, 0, 0)

static void cmipci_sb_reg_decode(cmipci_sb_reg_t *r, unsigned long val)
{
	r->left_reg = val & 0xff;
	r->right_reg = (val >> 8) & 0xff;
	r->left_shift = (val >> 16) & 0x07;
	r->right_shift = (val >> 19) & 0x07;
	r->invert = (val >> 22) & 1;
	r->stereo = (val >> 23) & 1;
	r->mask = (val >> 24) & 0xff;
}

static int snd_cmipci_info_volume(snd_kcontrol_t * kcontrol, snd_ctl_elem_info_t * uinfo)
{
	cmipci_sb_reg_t reg;

	cmipci_sb_reg_decode(&reg, kcontrol->private_value);
	uinfo->type = reg.mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = reg.stereo + 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = reg.mask;
	return 0;
}
 
static int snd_cmipci_get_volume(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cmipci_t *cm = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	cmipci_sb_reg_t reg;
	int val;

	cmipci_sb_reg_decode(&reg, kcontrol->private_value);
	spin_lock_irqsave(&cm->reg_lock, flags);
	val = (snd_cmipci_mixer_read(cm, reg.left_reg) >> reg.left_shift) & reg.mask;
	if (reg.invert)
		val = reg.mask - val;
	ucontrol->value.integer.value[0] = val;
	if (reg.stereo) {
		val = (snd_cmipci_mixer_read(cm, reg.right_reg) >> reg.right_shift) & reg.mask;
		if (reg.invert)
			val = reg.mask - val;
		 ucontrol->value.integer.value[1] = val;
	}
	spin_unlock_irqrestore(&cm->reg_lock, flags);
	return 0;
}

static int snd_cmipci_put_volume(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cmipci_t *cm = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	cmipci_sb_reg_t reg;
	int change;
	int left, right, oleft, oright;

	cmipci_sb_reg_decode(&reg, kcontrol->private_value);
	left = ucontrol->value.integer.value[0] & reg.mask;
	if (reg.invert)
		left = reg.mask - left;
	left <<= reg.left_shift;
	if (reg.stereo) {
		right = ucontrol->value.integer.value[1] & reg.mask;
		if (reg.invert)
			right = reg.mask - right;
		right <<= reg.right_shift;
	} else
		right = 0;
	spin_lock_irqsave(&cm->reg_lock, flags);
	oleft = snd_cmipci_mixer_read(cm, reg.left_reg);
	left |= oleft & ~(reg.mask << reg.left_shift);
	change = left != oleft;
	if (reg.stereo) {
		if (reg.left_reg != reg.right_reg) {
			snd_cmipci_mixer_write(cm, reg.left_reg, left);
			oright = snd_cmipci_mixer_read(cm, reg.right_reg);
		} else
			oright = left;
		right |= oright & ~(reg.mask << reg.right_shift);
		change |= right != oright;
		snd_cmipci_mixer_write(cm, reg.right_reg, right);
	} else
		snd_cmipci_mixer_write(cm, reg.left_reg, left);
	spin_unlock_irqrestore(&cm->reg_lock, flags);
	return change;
}

/*
 * input route (left,right) -> (left,right)
 */
#define CMIPCI_SB_INPUT_SW(xname, left_shift, right_shift) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_cmipci_info_input_sw, \
  .get = snd_cmipci_get_input_sw, .put = snd_cmipci_put_input_sw, \
  .private_value = COMPOSE_SB_REG(SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT, left_shift, right_shift, 1, 0, 1), \
}

static int snd_cmipci_info_input_sw(snd_kcontrol_t * kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 4;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}
 
static int snd_cmipci_get_input_sw(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cmipci_t *cm = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	cmipci_sb_reg_t reg;
	int val1, val2;

	cmipci_sb_reg_decode(&reg, kcontrol->private_value);
	spin_lock_irqsave(&cm->reg_lock, flags);
	val1 = snd_cmipci_mixer_read(cm, reg.left_reg);
	val2 = snd_cmipci_mixer_read(cm, reg.right_reg);
	spin_unlock_irqrestore(&cm->reg_lock, flags);
	ucontrol->value.integer.value[0] = (val1 >> reg.left_shift) & 1;
	ucontrol->value.integer.value[1] = (val2 >> reg.left_shift) & 1;
	ucontrol->value.integer.value[2] = (val1 >> reg.right_shift) & 1;
	ucontrol->value.integer.value[3] = (val2 >> reg.right_shift) & 1;
	return 0;
}

static int snd_cmipci_put_input_sw(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cmipci_t *cm = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	cmipci_sb_reg_t reg;
	int change;
	int val1, val2, oval1, oval2;

	cmipci_sb_reg_decode(&reg, kcontrol->private_value);
	spin_lock_irqsave(&cm->reg_lock, flags);
	oval1 = snd_cmipci_mixer_read(cm, reg.left_reg);
	oval2 = snd_cmipci_mixer_read(cm, reg.right_reg);
	val1 = oval1 & ~((1 << reg.left_shift) | (1 << reg.right_shift));
	val2 = oval2 & ~((1 << reg.left_shift) | (1 << reg.right_shift));
	val1 |= (ucontrol->value.integer.value[0] & 1) << reg.left_shift;
	val2 |= (ucontrol->value.integer.value[1] & 1) << reg.left_shift;
	val1 |= (ucontrol->value.integer.value[2] & 1) << reg.right_shift;
	val2 |= (ucontrol->value.integer.value[3] & 1) << reg.right_shift;
	change = val1 != oval1 || val2 != oval2;
	snd_cmipci_mixer_write(cm, reg.left_reg, val1);
	snd_cmipci_mixer_write(cm, reg.right_reg, val2);
	spin_unlock_irqrestore(&cm->reg_lock, flags);
	return change;
}

/*
 * native mixer switches/volumes
 */

#define CMIPCI_MIXER_SW_STEREO(xname, reg, lshift, rshift, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_cmipci_info_native_mixer, \
  .get = snd_cmipci_get_native_mixer, .put = snd_cmipci_put_native_mixer, \
  .private_value = COMPOSE_SB_REG(reg, reg, lshift, rshift, 1, invert, 1), \
}

#define CMIPCI_MIXER_SW_MONO(xname, reg, shift, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_cmipci_info_native_mixer, \
  .get = snd_cmipci_get_native_mixer, .put = snd_cmipci_put_native_mixer, \
  .private_value = COMPOSE_SB_REG(reg, reg, shift, shift, 1, invert, 0), \
}

#define CMIPCI_MIXER_VOL_STEREO(xname, reg, lshift, rshift, mask) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_cmipci_info_native_mixer, \
  .get = snd_cmipci_get_native_mixer, .put = snd_cmipci_put_native_mixer, \
  .private_value = COMPOSE_SB_REG(reg, reg, lshift, rshift, mask, 0, 1), \
}

#define CMIPCI_MIXER_VOL_MONO(xname, reg, shift, mask) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
  .info = snd_cmipci_info_native_mixer, \
  .get = snd_cmipci_get_native_mixer, .put = snd_cmipci_put_native_mixer, \
  .private_value = COMPOSE_SB_REG(reg, reg, shift, shift, mask, 0, 0), \
}

static int snd_cmipci_info_native_mixer(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	cmipci_sb_reg_t reg;

	cmipci_sb_reg_decode(&reg, kcontrol->private_value);
	uinfo->type = reg.mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = reg.stereo + 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = reg.mask;
	return 0;

}

static int snd_cmipci_get_native_mixer(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cmipci_t *cm = snd_kcontrol_chip(kcontrol);
	cmipci_sb_reg_t reg;
	unsigned long flags;
	unsigned char oreg, val;

	cmipci_sb_reg_decode(&reg, kcontrol->private_value);
	spin_lock_irqsave(&cm->reg_lock, flags);
	oreg = inb(cm->iobase + reg.left_reg);
	val = (oreg >> reg.left_shift) & reg.mask;
	if (reg.invert)
		val = reg.mask - val;
	ucontrol->value.integer.value[0] = val;
	if (reg.stereo) {
		val = (oreg >> reg.right_shift) & reg.mask;
		if (reg.invert)
			val = reg.mask - val;
		ucontrol->value.integer.value[1] = val;
	}
	spin_unlock_irqrestore(&cm->reg_lock, flags);
	return 0;
}

static int snd_cmipci_put_native_mixer(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	cmipci_t *cm = snd_kcontrol_chip(kcontrol);
	cmipci_sb_reg_t reg;
	unsigned long flags;
	unsigned char oreg, nreg, val;

	cmipci_sb_reg_decode(&reg, kcontrol->private_value);
	spin_lock_irqsave(&cm->reg_lock, flags);
	oreg = inb(cm->iobase + reg.left_reg);
	val = ucontrol->value.integer.value[0] & reg.mask;
	if (reg.invert)
		val = reg.mask - val;
	nreg = oreg & ~(reg.mask << reg.left_shift);
	nreg |= (val << reg.left_shift);
	if (reg.stereo) {
		val = ucontrol->value.integer.value[1] & reg.mask;
		if (reg.invert)
			val = reg.mask - val;
		nreg &= ~(reg.mask << reg.right_shift);
		nreg |= (val << reg.right_shift);
	}
	outb(nreg, cm->iobase + reg.left_reg);
	spin_unlock_irqrestore(&cm->reg_lock, flags);
	return (nreg != oreg);
}

/*
 * special case - check mixer sensitivity
 */
static int snd_cmipci_get_native_mixer_sensitive(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	//cmipci_t *cm = snd_kcontrol_chip(kcontrol);
	return snd_cmipci_get_native_mixer(kcontrol, ucontrol);
}

static int snd_cmipci_put_native_mixer_sensitive(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	cmipci_t *cm = snd_kcontrol_chip(kcontrol);
	if (cm->mixer_insensitive) {
		/* ignored */
		return 0;
	}
	return snd_cmipci_put_native_mixer(kcontrol, ucontrol);
}


#define num_controls(ary) (sizeof(ary) / sizeof(snd_kcontrol_new_t))

static snd_kcontrol_new_t snd_cmipci_mixers[] __devinitdata = {
	CMIPCI_SB_VOL_STEREO("Master Playback Volume", SB_DSP4_MASTER_DEV, 3, 31),
	CMIPCI_MIXER_SW_MONO("3D Control - Switch", CM_REG_MIXER1, CM_X3DEN_SHIFT, 0),
	CMIPCI_SB_VOL_STEREO("PCM Playback Volume", SB_DSP4_PCM_DEV, 3, 31),
	//CMIPCI_MIXER_SW_MONO("PCM Playback Switch", CM_REG_MIXER1, CM_WSMUTE_SHIFT, 1),
	{ /* switch with sensitivity */
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "PCM Playback Switch",
		.info = snd_cmipci_info_native_mixer,
		.get = snd_cmipci_get_native_mixer_sensitive,
		.put = snd_cmipci_put_native_mixer_sensitive,
		.private_value = COMPOSE_SB_REG(CM_REG_MIXER1, CM_REG_MIXER1, CM_WSMUTE_SHIFT, CM_WSMUTE_SHIFT, 1, 1, 0),
	},
	CMIPCI_MIXER_SW_STEREO("PCM Capture Switch", CM_REG_MIXER1, CM_WAVEINL_SHIFT, CM_WAVEINR_SHIFT, 0),
	CMIPCI_SB_VOL_STEREO("Synth Playback Volume", SB_DSP4_SYNTH_DEV, 3, 31),
	CMIPCI_MIXER_SW_MONO("Synth Playback Switch", CM_REG_MIXER1, CM_FMMUTE_SHIFT, 1),
	CMIPCI_SB_INPUT_SW("Synth Capture Route", 6, 5),
	CMIPCI_SB_VOL_STEREO("CD Playback Volume", SB_DSP4_CD_DEV, 3, 31),
	CMIPCI_SB_SW_STEREO("CD Playback Switch", 2, 1),
	CMIPCI_SB_INPUT_SW("CD Capture Route", 2, 1),
	CMIPCI_SB_VOL_STEREO("Line Playback Volume", SB_DSP4_LINE_DEV, 3, 31),
	CMIPCI_SB_SW_STEREO("Line Playback Switch", 4, 3),
	CMIPCI_SB_INPUT_SW("Line Capture Route", 4, 3),
	CMIPCI_SB_VOL_MONO("Mic Playback Volume", SB_DSP4_MIC_DEV, 3, 31),
	CMIPCI_SB_SW_MONO("Mic Playback Switch", 0),
	CMIPCI_DOUBLE("Mic Capture Switch", SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT, 0, 0, 1, 0, 0),
	CMIPCI_SB_VOL_MONO("PC Speaker Playback Volume", SB_DSP4_SPEAKER_DEV, 6, 3),
	CMIPCI_MIXER_VOL_STEREO("Aux Playback Volume", CM_REG_AUX_VOL, 4, 0, 15),
	CMIPCI_MIXER_SW_STEREO("Aux Playback Switch", CM_REG_MIXER2, CM_VAUXLM_SHIFT, CM_VAUXRM_SHIFT, 0),
	CMIPCI_MIXER_SW_STEREO("Aux Capture Switch", CM_REG_MIXER2, CM_RAUXLEN_SHIFT, CM_RAUXREN_SHIFT, 0),
	CMIPCI_MIXER_SW_MONO("Mic Boost", CM_REG_MIXER2, CM_MICGAINZ_SHIFT, 1),
	CMIPCI_MIXER_VOL_MONO("Mic Capture Volume", CM_REG_MIXER2, CM_VADMIC_SHIFT, 7),
};

/*
 * other switches
 */

typedef struct snd_cmipci_switch_args {
	int reg;		/* register index */
	unsigned int mask;	/* mask bits */
	unsigned int mask_on;	/* mask bits to turn on */
	int is_byte: 1;		/* byte access? */
	int ac3_sensitive: 1;	/* access forbidden during non-audio operation? */
} snd_cmipci_switch_args_t;

static int snd_cmipci_uswitch_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int _snd_cmipci_uswitch_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol, snd_cmipci_switch_args_t *args)
{
	unsigned long flags;
	unsigned int val;
	cmipci_t *cm = snd_kcontrol_chip(kcontrol);

	spin_lock_irqsave(&cm->reg_lock, flags);
	if (args->ac3_sensitive && cm->mixer_insensitive) {
		ucontrol->value.integer.value[0] = 0;
		spin_unlock_irqrestore(&cm->reg_lock, flags);
		return 0;
	}
	if (args->is_byte)
		val = inb(cm->iobase + args->reg);
	else
		val = snd_cmipci_read(cm, args->reg);
	ucontrol->value.integer.value[0] = ((val & args->mask) == args->mask_on) ? 1 : 0;
	spin_unlock_irqrestore(&cm->reg_lock, flags);
	return 0;
}

static int snd_cmipci_uswitch_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	snd_cmipci_switch_args_t *args = (snd_cmipci_switch_args_t*)kcontrol->private_value;
	snd_assert(args != NULL, return -EINVAL);
	return _snd_cmipci_uswitch_get(kcontrol, ucontrol, args);
}

static int _snd_cmipci_uswitch_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol, snd_cmipci_switch_args_t *args)
{
	unsigned long flags;
	unsigned int val;
	int change;
	cmipci_t *cm = snd_kcontrol_chip(kcontrol);

	spin_lock_irqsave(&cm->reg_lock, flags);
	if (args->ac3_sensitive && cm->mixer_insensitive) {
		/* ignored */
		spin_unlock_irqrestore(&cm->reg_lock, flags);
		return 0;
	}
	if (args->is_byte)
		val = inb(cm->iobase + args->reg);
	else
		val = snd_cmipci_read(cm, args->reg);
	change = (val & args->mask) != (ucontrol->value.integer.value[0] ? args->mask : 0);
	if (change) {
		val &= ~args->mask;
		if (ucontrol->value.integer.value[0])
			val |= args->mask_on;
		else
			val |= (args->mask & ~args->mask_on);
		if (args->is_byte)
			outb((unsigned char)val, cm->iobase + args->reg);
		else
			snd_cmipci_write(cm, args->reg, val);
	}
	spin_unlock_irqrestore(&cm->reg_lock, flags);
	return change;
}

static int snd_cmipci_uswitch_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	snd_cmipci_switch_args_t *args = (snd_cmipci_switch_args_t*)kcontrol->private_value;
	snd_assert(args != NULL, return -EINVAL);
	return _snd_cmipci_uswitch_put(kcontrol, ucontrol, args);
}

#define DEFINE_SWITCH_ARG(sname, xreg, xmask, xmask_on, xis_byte, xac3) \
static snd_cmipci_switch_args_t cmipci_switch_arg_##sname = { \
  .reg = xreg, \
  .mask = xmask, \
  .mask_on = xmask_on, \
  .is_byte = xis_byte, \
  .ac3_sensitive = xac3, \
}
	
#define DEFINE_BIT_SWITCH_ARG(sname, xreg, xmask, xis_byte, xac3) \
	DEFINE_SWITCH_ARG(sname, xreg, xmask, xmask, xis_byte, xac3)

#if 0 /* these will be controlled in pcm device */
DEFINE_BIT_SWITCH_ARG(spdif_in, CM_REG_FUNCTRL1, CM_SPDF_1, 0, 0);
DEFINE_BIT_SWITCH_ARG(spdif_out, CM_REG_FUNCTRL1, CM_SPDF_0, 0, 0);
#endif
DEFINE_BIT_SWITCH_ARG(spdif_in_sel1, CM_REG_CHFORMAT, CM_SPDIF_SELECT1, 0, 0);
DEFINE_BIT_SWITCH_ARG(spdif_in_sel2, CM_REG_MISC_CTRL, CM_SPDIF_SELECT2, 0, 0);
DEFINE_BIT_SWITCH_ARG(spdif_enable, CM_REG_LEGACY_CTRL, CM_ENSPDOUT, 0, 0);
DEFINE_BIT_SWITCH_ARG(spdo2dac, CM_REG_FUNCTRL1, CM_SPDO2DAC, 0, 1);
DEFINE_BIT_SWITCH_ARG(spdi_valid, CM_REG_MISC, CM_SPDVALID, 1, 0);
DEFINE_BIT_SWITCH_ARG(spdif_copyright, CM_REG_LEGACY_CTRL, CM_SPDCOPYRHT, 0, 0);
DEFINE_BIT_SWITCH_ARG(spdif_dac_out, CM_REG_LEGACY_CTRL, CM_DAC2SPDO, 0, 1);
DEFINE_SWITCH_ARG(spdo_5v, CM_REG_MISC_CTRL, CM_SPDO5V, 0, 0, 0); /* inverse: 0 = 5V */
// DEFINE_BIT_SWITCH_ARG(spdo_48k, CM_REG_MISC_CTRL, CM_SPDF_AC97|CM_SPDIF48K, 0, 1);
DEFINE_BIT_SWITCH_ARG(spdif_loop, CM_REG_FUNCTRL1, CM_SPDFLOOP, 0, 1);
DEFINE_BIT_SWITCH_ARG(spdi_monitor, CM_REG_MIXER1, CM_CDPLAY, 1, 0);
/* DEFINE_BIT_SWITCH_ARG(spdi_phase, CM_REG_CHFORMAT, CM_SPDIF_INVERSE, 0, 0); */
DEFINE_BIT_SWITCH_ARG(spdi_phase, CM_REG_MISC, CM_SPDIF_INVERSE, 1, 0);
DEFINE_BIT_SWITCH_ARG(spdi_phase2, CM_REG_CHFORMAT, CM_SPDIF_INVERSE2, 0, 0);
#if CM_CH_PLAY == 1
DEFINE_SWITCH_ARG(exchange_dac, CM_REG_MISC_CTRL, CM_XCHGDAC, 0, 0, 0); /* reversed */
#else
DEFINE_SWITCH_ARG(exchange_dac, CM_REG_MISC_CTRL, CM_XCHGDAC, CM_XCHGDAC, 0, 0);
#endif
DEFINE_BIT_SWITCH_ARG(fourch, CM_REG_MISC_CTRL, CM_N4SPK3D, 0, 0);
DEFINE_BIT_SWITCH_ARG(line_rear, CM_REG_MIXER1, CM_SPK4, 1, 0);
DEFINE_BIT_SWITCH_ARG(line_bass, CM_REG_LEGACY_CTRL, CM_LINE_AS_BASS, 0, 0);
DEFINE_BIT_SWITCH_ARG(joystick, CM_REG_FUNCTRL1, CM_JYSTK_EN, 0, 0);
DEFINE_SWITCH_ARG(modem, CM_REG_MISC_CTRL, CM_FLINKON|CM_FLINKOFF, CM_FLINKON, 0, 0);

#define DEFINE_SWITCH(sname, stype, sarg) \
{ .name = sname, \
  .iface = stype, \
  .info = snd_cmipci_uswitch_info, \
  .get = snd_cmipci_uswitch_get, \
  .put = snd_cmipci_uswitch_put, \
  .private_value = (unsigned long)&cmipci_switch_arg_##sarg,\
}

#define DEFINE_CARD_SWITCH(sname, sarg) DEFINE_SWITCH(sname, SNDRV_CTL_ELEM_IFACE_CARD, sarg)
#define DEFINE_MIXER_SWITCH(sname, sarg) DEFINE_SWITCH(sname, SNDRV_CTL_ELEM_IFACE_MIXER, sarg)


/*
 * callbacks for spdif output switch
 * needs toggle two registers..
 */
static int snd_cmipci_spdout_enable_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	int changed;
	changed = _snd_cmipci_uswitch_get(kcontrol, ucontrol, &cmipci_switch_arg_spdif_enable);
	changed |= _snd_cmipci_uswitch_get(kcontrol, ucontrol, &cmipci_switch_arg_spdo2dac);
	return changed;
}

static int snd_cmipci_spdout_enable_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	cmipci_t *chip = snd_kcontrol_chip(kcontrol);
	int changed;
	changed = _snd_cmipci_uswitch_put(kcontrol, ucontrol, &cmipci_switch_arg_spdif_enable);
	changed |= _snd_cmipci_uswitch_put(kcontrol, ucontrol, &cmipci_switch_arg_spdo2dac);
	if (changed) {
		if (ucontrol->value.integer.value[0]) {
			if (chip->spdif_playback_avail)
				snd_cmipci_set_bit(chip, CM_REG_FUNCTRL1, CM_PLAYBACK_SPDF);
		} else {
			if (chip->spdif_playback_avail)
				snd_cmipci_clear_bit(chip, CM_REG_FUNCTRL1, CM_PLAYBACK_SPDF);
		}
	}
	chip->spdif_playback_enabled = ucontrol->value.integer.value[0];
	return changed;
}


/* both for CM8338/8738 */
static snd_kcontrol_new_t snd_cmipci_mixer_switches[] __devinitdata = {
	DEFINE_MIXER_SWITCH("Exchange DAC", exchange_dac),
	DEFINE_MIXER_SWITCH("Four Channel Mode", fourch),
	DEFINE_MIXER_SWITCH("Line-In As Rear", line_rear),
};

/* only for CM8738 */
static snd_kcontrol_new_t snd_cmipci_8738_mixer_switches[] __devinitdata = {
#if 0 /* controlled in pcm device */
	DEFINE_MIXER_SWITCH("IEC958 In Record", spdif_in),
	DEFINE_MIXER_SWITCH("IEC958 Out", spdif_out),
	DEFINE_MIXER_SWITCH("IEC958 Out To DAC", spdo2dac),
#endif
	// DEFINE_MIXER_SWITCH("IEC958 Output Switch", spdif_enable),
	{ .name = "IEC958 Output Switch",
	  .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .info = snd_cmipci_uswitch_info,
	  .get = snd_cmipci_spdout_enable_get,
	  .put = snd_cmipci_spdout_enable_put,
	},
	DEFINE_MIXER_SWITCH("IEC958 In Valid", spdi_valid),
	DEFINE_MIXER_SWITCH("IEC958 Copyright", spdif_copyright),
	DEFINE_MIXER_SWITCH("IEC958 5V", spdo_5v),
//	DEFINE_MIXER_SWITCH("IEC958 In/Out 48KHz", spdo_48k),
	DEFINE_MIXER_SWITCH("IEC958 Loop", spdif_loop),
	DEFINE_MIXER_SWITCH("IEC958 In Monitor", spdi_monitor),
};

/* only for model 033/037 */
static snd_kcontrol_new_t snd_cmipci_old_mixer_switches[] __devinitdata = {
	DEFINE_MIXER_SWITCH("IEC958 Mix Analog", spdif_dac_out),
	DEFINE_MIXER_SWITCH("IEC958 In Phase Inverse", spdi_phase),
	DEFINE_MIXER_SWITCH("IEC958 In Select", spdif_in_sel1),
};

/* only for model 039 or later */
static snd_kcontrol_new_t snd_cmipci_extra_mixer_switches[] __devinitdata = {
	DEFINE_MIXER_SWITCH("Line-In As Bass", line_bass),
	DEFINE_MIXER_SWITCH("IEC958 In Select", spdif_in_sel2),
	DEFINE_MIXER_SWITCH("IEC958 In Phase Inverse", spdi_phase2),
	DEFINE_MIXER_SWITCH("Mic As Center/LFE", spdi_phase), /* same bit as spdi_phase */
};

/* card control switches */
static snd_kcontrol_new_t snd_cmipci_control_switches[] __devinitdata = {
	DEFINE_CARD_SWITCH("Joystick", joystick),
	DEFINE_CARD_SWITCH("Modem", modem),
};


static int __devinit snd_cmipci_mixer_new(cmipci_t *cm, int pcm_spdif_device)
{
	unsigned long flags;
	snd_card_t *card;
	snd_kcontrol_new_t *sw;
	snd_kcontrol_t *kctl;
	unsigned int idx;
	int err;

	snd_assert(cm != NULL && cm->card != NULL, return -EINVAL);

	card = cm->card;

	strcpy(card->mixername, "CMedia PCI");

	spin_lock_irqsave(&cm->reg_lock, flags);
	snd_cmipci_mixer_write(cm, 0x00, 0x00);		/* mixer reset */
	spin_unlock_irqrestore(&cm->reg_lock, flags);

	for (idx = 0; idx < num_controls(snd_cmipci_mixers); idx++) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&snd_cmipci_mixers[idx], cm))) < 0)
			return err;
	}

	/* mixer switches */
	sw = snd_cmipci_mixer_switches;
	for (idx = 0; idx < num_controls(snd_cmipci_mixer_switches); idx++, sw++) {
		err = snd_ctl_add(cm->card, snd_ctl_new1(sw, cm));
		if (err < 0)
			return err;
	}
	if (cm->device == PCI_DEVICE_ID_CMEDIA_CM8738 ||
	    cm->device == PCI_DEVICE_ID_CMEDIA_CM8738B) {
		sw = snd_cmipci_8738_mixer_switches;
		for (idx = 0; idx < num_controls(snd_cmipci_8738_mixer_switches); idx++, sw++) {
			err = snd_ctl_add(cm->card, snd_ctl_new1(sw, cm));
			if (err < 0)
				return err;
		}
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_cmipci_spdif_default, cm))) < 0)
			return err;
		kctl->id.device = pcm_spdif_device;
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_cmipci_spdif_mask, cm))) < 0)
			return err;
		kctl->id.device = pcm_spdif_device;
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_cmipci_spdif_stream, cm))) < 0)
			return err;
		kctl->id.device = pcm_spdif_device;
		cm->spdif_pcm_ctl = kctl;
		if (cm->chip_version <= 37) {
			sw = snd_cmipci_old_mixer_switches;
			for (idx = 0; idx < num_controls(snd_cmipci_old_mixer_switches); idx++, sw++) {
				err = snd_ctl_add(cm->card, snd_ctl_new1(sw, cm));
				if (err < 0)
					return err;
			}
		}
	}
	if (cm->chip_version >= 39) {
		sw = snd_cmipci_extra_mixer_switches;
		for (idx = 0; idx < num_controls(snd_cmipci_extra_mixer_switches); idx++, sw++) {
			err = snd_ctl_add(cm->card, snd_ctl_new1(sw, cm));
			if (err < 0)
				return err;
		}
	}

	/* card switches */
	sw = snd_cmipci_control_switches;
	for (idx = 0; idx < num_controls(snd_cmipci_control_switches); idx++, sw++) {
		err = snd_ctl_add(cm->card, snd_ctl_new1(sw, cm));
		if (err < 0)
			return err;
	}

	for (idx = 0; idx < CM_SAVED_MIXERS; idx++) {
		snd_ctl_elem_id_t id;
		snd_kcontrol_t *ctl;
		memset(&id, 0, sizeof(id));
		id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		strcpy(id.name, cm_saved_mixer[idx].name);
		if ((ctl = snd_ctl_find_id(cm->card, &id)) != NULL)
			cm->mixer_res_ctl[idx] = ctl;
	}

	return 0;
}


/*
 * proc interface
 */

#ifdef CONFIG_PROC_FS
static void snd_cmipci_proc_read(snd_info_entry_t *entry, 
				 snd_info_buffer_t *buffer)
{
	cmipci_t *cm = snd_magic_cast(cmipci_t, entry->private_data, return);
	int i;
	
	snd_iprintf(buffer, "%s\n\n", cm->card->longname);
	for (i = 0; i < 0x40; i++) {
		int v = inb(cm->iobase + i);
		if (i % 4 == 0)
			snd_iprintf(buffer, "%02x: ", i);
		snd_iprintf(buffer, "%02x", v);
		if (i % 4 == 3)
			snd_iprintf(buffer, "\n");
		else
			snd_iprintf(buffer, " ");
	}
}

static void __devinit snd_cmipci_proc_init(cmipci_t *cm)
{
	snd_info_entry_t *entry;

	if (! snd_card_proc_new(cm->card, "cmipci", &entry))
		snd_info_set_text_ops(entry, cm, snd_cmipci_proc_read);
}
#else /* !CONFIG_PROC_FS */
static inline void snd_cmipci_proc_init(cmipci_t *cm) {}
#endif


static struct pci_device_id snd_cmipci_ids[] = {
	{PCI_VENDOR_ID_CMEDIA, PCI_DEVICE_ID_CMEDIA_CM8338A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CMEDIA, PCI_DEVICE_ID_CMEDIA_CM8338B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CMEDIA, PCI_DEVICE_ID_CMEDIA_CM8738, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_CMEDIA, PCI_DEVICE_ID_CMEDIA_CM8738B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_AL, PCI_DEVICE_ID_CMEDIA_CM8738, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,},
};


/*
 * check chip version and capabilities
 * driver name is modified according to the chip model
 */
static void __devinit query_chip(cmipci_t *cm)
{
	unsigned int detect;

	/* check reg 0Ch, bit 24-31 */
	detect = snd_cmipci_read(cm, CM_REG_INT_HLDCLR) & CM_CHIP_MASK2;
	if (! detect) {
		/* check reg 08h, bit 24-28 */
		detect = snd_cmipci_read(cm, CM_REG_CHFORMAT) & CM_CHIP_MASK1;
		if (! detect) {
			cm->chip_version = 33;
			cm->max_channels = 2;
			if (cm->do_soft_ac3)
				cm->can_ac3_sw = 1;
			else
				cm->can_ac3_hw = 1;
			cm->has_dual_dac = 1;
		} else {
			cm->chip_version = 37;
			cm->max_channels = 2;
			cm->can_ac3_hw = 1;
			cm->has_dual_dac = 1;
		}
	} else {
		/* check reg 0Ch, bit 26 */
		if (detect & CM_CHIP_039) {
			cm->chip_version = 39;
			if (detect & CM_CHIP_039_6CH)
				cm->max_channels  = 6;
			else
				cm->max_channels = 4;
			cm->can_ac3_hw = 1;
			cm->has_dual_dac = 1;
			cm->can_multi_ch = 1;
		} else {
			cm->chip_version = 55; /* 4 or 6 channels */
			cm->max_channels  = 6;
			cm->can_ac3_hw = 1;
			cm->has_dual_dac = 1;
			cm->can_multi_ch = 1;
		}
	}

	/* added -MCx suffix for chip supporting multi-channels */
	if (cm->can_multi_ch)
		sprintf(cm->card->driver + strlen(cm->card->driver),
			"-MC%d", cm->max_channels);
}


static int snd_cmipci_free(cmipci_t *cm)
{
	if (cm->irq >= 0) {
		snd_cmipci_clear_bit(cm, CM_REG_MISC_CTRL, CM_FM_EN);
		snd_cmipci_clear_bit(cm, CM_REG_LEGACY_CTRL, CM_ENSPDOUT);
		snd_cmipci_write(cm, CM_REG_INT_HLDCLR, 0);  /* disable ints */
		snd_cmipci_ch_reset(cm, CM_CH_PLAY);
		snd_cmipci_ch_reset(cm, CM_CH_CAPT);
		snd_cmipci_write(cm, CM_REG_FUNCTRL0, 0); /* disable channels */
		snd_cmipci_write(cm, CM_REG_FUNCTRL1, 0);

		/* reset mixer */
		snd_cmipci_mixer_write(cm, 0, 0);

		synchronize_irq(cm->irq);

		free_irq(cm->irq, (void *)cm);
	}
	if (cm->res_iobase) {
		release_resource(cm->res_iobase);
		kfree_nocheck(cm->res_iobase);
	}
	snd_magic_kfree(cm);
	return 0;
}

static int snd_cmipci_dev_free(snd_device_t *device)
{
	cmipci_t *cm = snd_magic_cast(cmipci_t, device->device_data, return -ENXIO);
	return snd_cmipci_free(cm);
}

static int __devinit snd_cmipci_create(snd_card_t *card, struct pci_dev *pci,
				       int dev, cmipci_t **rcmipci)
{
	cmipci_t *cm;
	int err;
	static snd_device_ops_t ops = {
		.dev_free =	snd_cmipci_dev_free,
	};
	unsigned int val = 0;
	unsigned long iomidi = mpu_port[dev];
	unsigned long iosynth = fm_port[dev];
	int pcm_index, pcm_spdif_index;

	*rcmipci = NULL;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	cm = snd_magic_kcalloc(cmipci_t, 0, GFP_KERNEL);
	if (cm == NULL)
		return -ENOMEM;

	spin_lock_init(&cm->reg_lock);
	init_MUTEX(&cm->open_mutex);
	cm->device = pci->device;
	cm->card = card;
	cm->pci = pci;
	cm->irq = -1;
	cm->iobase = pci_resource_start(pci, 0);
	cm->channel[0].ch = 0;
	cm->channel[1].ch = 1;
	cm->channel[0].is_dac = cm->channel[1].is_dac = 1; /* dual DAC mode */

	if ((cm->res_iobase = request_region(cm->iobase, CM_EXTENT_CODEC, card->driver)) == NULL) {
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", cm->iobase, cm->iobase + CM_EXTENT_CODEC - 1);
		err = -EBUSY;
		goto __error;
	}
	if (request_irq(pci->irq, snd_cmipci_interrupt, SA_INTERRUPT|SA_SHIRQ, card->driver, (void *)cm)) {
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		err = -EBUSY;
		goto __error;
	}
	cm->irq = pci->irq;

	pci_set_master(cm->pci);

	/*
	 * check chip version, max channels and capabilities
	 */

	cm->chip_version = 0;
	cm->max_channels = 2;
#ifdef DO_SOFT_AC3
	cm->do_soft_ac3 = soft_ac3[dev];
#endif

	query_chip(cm);

	cm->dig_status = SNDRV_PCM_DEFAULT_CON_SPDIF;
	cm->dig_pcm_status = SNDRV_PCM_DEFAULT_CON_SPDIF;

#if CM_CH_PLAY == 1
	cm->ctrl = CM_CHADC0;	/* default FUNCNTRL0 */
#else
	cm->ctrl = CM_CHADC1;	/* default FUNCNTRL0 */
#endif

	/* initialize codec registers */
	snd_cmipci_write(cm, CM_REG_INT_HLDCLR, 0);	/* disable ints */
	snd_cmipci_ch_reset(cm, CM_CH_PLAY);
	snd_cmipci_ch_reset(cm, CM_CH_CAPT);
	snd_cmipci_write(cm, CM_REG_FUNCTRL0, 0);	/* disable channels */
	snd_cmipci_write(cm, CM_REG_FUNCTRL1, 0);

	snd_cmipci_write(cm, CM_REG_CHFORMAT, 0);
	snd_cmipci_set_bit(cm, CM_REG_MISC_CTRL, CM_ENDBDAC|CM_N4SPK3D);
#if CM_CH_PLAY == 1
	snd_cmipci_set_bit(cm, CM_REG_MISC_CTRL, CM_XCHGDAC);
#else
	snd_cmipci_clear_bit(cm, CM_REG_MISC_CTRL, CM_XCHGDAC);
#endif
	/* Set Bus Master Request */
	snd_cmipci_set_bit(cm, CM_REG_FUNCTRL1, CM_BREQ);

	/* Assume TX and compatible chip set (Autodetection required for VX chip sets) */
	switch (pci->device) {
	case PCI_DEVICE_ID_CMEDIA_CM8738:
	case PCI_DEVICE_ID_CMEDIA_CM8738B:
		/* PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82437VX */
		if (! pci_find_device(0x8086, 0x7030, NULL))
			snd_cmipci_set_bit(cm, CM_REG_MISC_CTRL, CM_TXVX);
		break;
	default:
		break;
	}

	/* set MPU address */
	switch (iomidi) {
	case 0x320: val = CM_VMPU_320; break;
	case 0x310: val = CM_VMPU_310; break;
	case 0x300: val = CM_VMPU_300; break;
	case 0x330: val = CM_VMPU_330; break;
	default:
		iomidi = 0; break;
	}
	if (iomidi > 0) {
		snd_cmipci_write(cm, CM_REG_LEGACY_CTRL, val);
		/* enable UART */
		snd_cmipci_set_bit(cm, CM_REG_FUNCTRL1, CM_UART_EN);
	}

	/* set FM address */
	val = snd_cmipci_read(cm, CM_REG_LEGACY_CTRL) & ~CM_FMSEL_MASK;
	switch (iosynth) {
	case 0x3E8: val |= CM_FMSEL_3E8; break;
	case 0x3E0: val |= CM_FMSEL_3E0; break;
	case 0x3C8: val |= CM_FMSEL_3C8; break;
	case 0x388: val |= CM_FMSEL_388; break;
	default:
		iosynth = 0; break;
	}
	if (iosynth > 0) {
		snd_cmipci_write(cm, CM_REG_LEGACY_CTRL, val);
		/* enable FM */
		snd_cmipci_set_bit(cm, CM_REG_MISC_CTRL, CM_FM_EN);

		if (snd_opl3_create(card, iosynth, iosynth + 2,
				    OPL3_HW_OPL3, 0, &cm->opl3) < 0) {
			printk(KERN_ERR "cmipci: no OPL device at 0x%lx, skipping...\n", iosynth);
			iosynth = 0;
		} else {
			if ((err = snd_opl3_hwdep_new(cm->opl3, 0, 1, &cm->opl3hwdep)) < 0) {
				printk(KERN_ERR "cmipci: cannot create OPL3 hwdep\n");
				return err;
			}
		}
	}
	if (! iosynth) {
		/* disable FM */
		snd_cmipci_write(cm, CM_REG_LEGACY_CTRL, val & ~CM_FMSEL_MASK);
		snd_cmipci_clear_bit(cm, CM_REG_MISC_CTRL, CM_FM_EN);
	}

	/* reset mixer */
	snd_cmipci_mixer_write(cm, 0, 0);

	snd_cmipci_proc_init(cm);

	/* create pcm devices */
	pcm_index = pcm_spdif_index = 0;
	if ((err = snd_cmipci_pcm_new(cm, pcm_index)) < 0)
		goto __error;
	pcm_index++;
	if (cm->has_dual_dac) {
		if ((err = snd_cmipci_pcm2_new(cm, pcm_index)) < 0)
			goto __error;
		pcm_index++;
	}
	if (cm->can_ac3_hw || cm->can_ac3_sw) {
		pcm_spdif_index = pcm_index;
		if ((err = snd_cmipci_pcm_spdif_new(cm, pcm_index)) < 0)
			goto __error;
	}

	/* create mixer interface & switches */
	if ((err = snd_cmipci_mixer_new(cm, pcm_spdif_index)) < 0)
		goto __error;

	if (iomidi > 0) {
		if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_CMIPCI,
					       iomidi, 0,
					       cm->irq, 0, &cm->rmidi)) < 0) {
			printk(KERN_ERR "cmipci: no UART401 device at 0x%lx\n", iomidi);
		}
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, cm, &ops)) < 0) {
		snd_cmipci_free(cm);
		return err;
	}
#ifdef USE_VAR48KRATE
	for (val = 0; val < ARRAY_SIZE(rates); val++)
		snd_cmipci_set_pll(cm, rates[val], val);

	/*
	 * (Re-)Enable external switch spdo_48k
	 */
	snd_cmipci_set_bit(cm, CM_REG_MISC_CTRL, CM_SPDIF48K|CM_SPDF_AC97);
#endif /* USE_VAR48KRATE */

	*rcmipci = cm;
	return 0;

 __error:
	snd_cmipci_free(cm);
	return err;
}

/*
 */

MODULE_DEVICE_TABLE(pci, snd_cmipci_ids);

static int __devinit snd_cmipci_probe(struct pci_dev *pci,
				      const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	cmipci_t *cm;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (! enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	
	switch (pci->device) {
	case PCI_DEVICE_ID_CMEDIA_CM8738:
	case PCI_DEVICE_ID_CMEDIA_CM8738B:
		strcpy(card->driver, "CMI8738");
		break;
	case PCI_DEVICE_ID_CMEDIA_CM8338A:
	case PCI_DEVICE_ID_CMEDIA_CM8338B:
		strcpy(card->driver, "CMI8338");
		break;
	default:
		strcpy(card->driver, "CMIPCI");
		break;
	}

	if ((err = snd_cmipci_create(card, pci, dev, &cm)) < 0) {
		snd_card_free(card);
		return err;
	}

	sprintf(card->shortname, "C-Media PCI %s", card->driver);
	sprintf(card->longname, "%s (model %d) at 0x%lx, irq %i",
		card->shortname,
		cm->chip_version,
		cm->iobase,
		cm->irq);

	//snd_printd("%s is detected\n", card->longname);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;

}

static void __devexit snd_cmipci_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}


static struct pci_driver driver = {
	.name = "C-Media PCI",
	.id_table = snd_cmipci_ids,
	.probe = snd_cmipci_probe,
	.remove = __devexit_p(snd_cmipci_remove),
};
	
static int __init alsa_card_cmipci_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		printk(KERN_ERR "C-Media PCI soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_cmipci_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_cmipci_init)
module_exit(alsa_card_cmipci_exit)

#ifndef MODULE

/* format is: snd-cmipci=enable,index,id,
			 mpu_port,fm_port */

static int __init alsa_card_cmipci_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&mpu_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&fm_port[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-cmipci=", alsa_card_cmipci_setup);

#endif /* ifndef MODULE */
