/*
 *   ALSA driver for RME Digi32, Digi32/8 and Digi32 PRO audio interfaces
 *
 *	Copyright (c) 2002, 2003 Martin Langer <martin-langer@gmx.de>
 *
 *      Thanks to :        Anders Torger <torger@ludd.luth.se>,
 *                         Henk Hesselink <henk@anda.nl>
 *                         for writing the digi96-driver 
 *                         and RME for all informations.
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
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * 
 * ****************************************************************************
 * 
 * Note #1 "Sek'd models" ................................... martin 2002-12-07
 * 
 * Identical soundcards by Sek'd were labeled:
 * RME Digi 32     = Sek'd Prodif 32
 * RME Digi 32 Pro = Sek'd Prodif 96
 * RME Digi 32/8   = Sek'd Prodif Gold
 * 
 * ****************************************************************************
 * 
 * Note #2 "full duplex mode" ............................... martin 2002-12-07
 * 
 * Full duplex doesn't work. All cards (32, 32/8, 32Pro) are working identical
 * in this mode. Rec data and play data are using the same buffer therefore. At
 * first you have got the playing bits in the buffer and then (after playing
 * them) they were overwitten by the captured sound of the CS8412/14. Both 
 * modes (play/record) are running harmonically hand in hand in the same buffer
 * and you have only one start bit plus one interrupt bit to control this 
 * paired action.
 * This is opposite to the latter rme96 where playing and capturing is totally
 * separated and so their full duplex mode is supported by alsa (using two 
 * start bits and two interrupts for two different buffers). 
 * But due to the wrong sequence of playing and capturing ALSA shows no solved
 * full duplex support for the rme32 at the moment. That's bad, but I'm not
 * able to solve it. Are you motivated enough to solve this problem now? Your
 * patch would be welcome!
 * 
 * ****************************************************************************
 */


#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>

#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/asoundef.h>
#include <sound/initval.h>

#include <asm/io.h>

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int boot_devs;

module_param_array(index, int, boot_devs, 0444);
MODULE_PARM_DESC(index, "Index value for RME Digi32 soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
module_param_array(id, charp, boot_devs, 0444);
MODULE_PARM_DESC(id, "ID string for RME Digi32 soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
module_param_array(enable, bool, boot_devs, 0444);
MODULE_PARM_DESC(enable, "Enable RME Digi32 soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_AUTHOR("Martin Langer <martin-langer@gmx.de>");
MODULE_DESCRIPTION("RME Digi32, Digi32/8, Digi32 PRO");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{RME,Digi32}," "{RME,Digi32/8}," "{RME,Digi32 PRO}}");

/* Defines for RME Digi32 series */
#define RME32_SPDIF_NCHANNELS 2

/* Playback and capture buffer size */
#define RME32_BUFFER_SIZE 0x20000

/* IO area size */
#define RME32_IO_SIZE 0x30000

/* IO area offsets */
#define RME32_IO_DATA_BUFFER        0x0
#define RME32_IO_CONTROL_REGISTER   0x20000
#define RME32_IO_GET_POS            0x20000
#define RME32_IO_CONFIRM_ACTION_IRQ 0x20004
#define RME32_IO_RESET_POS          0x20100

/* Write control register bits */
#define RME32_WCR_START     (1 << 0)    /* startbit */
#define RME32_WCR_MONO      (1 << 1)    /* 0=stereo, 1=mono
                                           Setting the whole card to mono
                                           doesn't seem to be very useful.
                                           A software-solution can handle 
                                           full-duplex with one direction in
                                           stereo and the other way in mono. 
                                           So, the hardware should work all 
                                           the time in stereo! */
#define RME32_WCR_MODE24    (1 << 2)    /* 0=16bit, 1=32bit */
#define RME32_WCR_SEL       (1 << 3)    /* 0=input on output, 1=normal playback/capture */
#define RME32_WCR_FREQ_0    (1 << 4)    /* frequency (play) */
#define RME32_WCR_FREQ_1    (1 << 5)
#define RME32_WCR_INP_0     (1 << 6)    /* input switch */
#define RME32_WCR_INP_1     (1 << 7)
#define RME32_WCR_RESET     (1 << 8)    /* Reset address */
#define RME32_WCR_MUTE      (1 << 9)    /* digital mute for output */
#define RME32_WCR_PRO       (1 << 10)   /* 1=professional, 0=consumer */
#define RME32_WCR_DS_BM     (1 << 11)	/* 1=DoubleSpeed (only PRO-Version); 1=BlockMode (only Adat-Version) */
#define RME32_WCR_ADAT      (1 << 12)	/* Adat Mode (only Adat-Version) */
#define RME32_WCR_AUTOSYNC  (1 << 13)   /* AutoSync */
#define RME32_WCR_PD        (1 << 14)	/* DAC Reset (only PRO-Version) */
#define RME32_WCR_EMP       (1 << 15)	/* 1=Emphasis on (only PRO-Version) */

#define RME32_WCR_BITPOS_FREQ_0 4
#define RME32_WCR_BITPOS_FREQ_1 5
#define RME32_WCR_BITPOS_INP_0 6
#define RME32_WCR_BITPOS_INP_1 7

/* Read control register bits */
#define RME32_RCR_AUDIO_ADDR_MASK 0x10001
#define RME32_RCR_LOCK      (1 << 23)   /* 1=locked, 0=not locked */
#define RME32_RCR_ERF       (1 << 26)   /* 1=Error, 0=no Error */
#define RME32_RCR_FREQ_0    (1 << 27)   /* CS841x frequency (record) */
#define RME32_RCR_FREQ_1    (1 << 28)
#define RME32_RCR_FREQ_2    (1 << 29)
#define RME32_RCR_KMODE     (1 << 30)   /* card mode: 1=PLL, 0=quartz */
#define RME32_RCR_IRQ       (1 << 31)   /* interrupt */

#define RME32_RCR_BITPOS_F0 27
#define RME32_RCR_BITPOS_F1 28
#define RME32_RCR_BITPOS_F2 29

/* Input types */
#define RME32_INPUT_OPTICAL 0
#define RME32_INPUT_COAXIAL 1
#define RME32_INPUT_INTERNAL 2
#define RME32_INPUT_XLR 3

/* Clock modes */
#define RME32_CLOCKMODE_SLAVE 0
#define RME32_CLOCKMODE_MASTER_32 1
#define RME32_CLOCKMODE_MASTER_44 2
#define RME32_CLOCKMODE_MASTER_48 3

/* Block sizes in bytes */
#define RME32_BLOCK_SIZE 8192

/* Hardware revisions */
#define RME32_32_REVISION 192
#define RME32_328_REVISION_OLD 100
#define RME32_328_REVISION_NEW 101
#define RME32_PRO_REVISION_WITH_8412 192
#define RME32_PRO_REVISION_WITH_8414 150


/* PCI vendor/device ID's */
#ifndef PCI_VENDOR_ID_XILINX_RME
# define PCI_VENDOR_ID_XILINX_RME 0xea60
#endif
#ifndef PCI_DEVICE_ID_DIGI32
# define PCI_DEVICE_ID_DIGI32 0x9896
#endif
#ifndef PCI_DEVICE_ID_DIGI32_PRO
# define PCI_DEVICE_ID_DIGI32_PRO 0x9897
#endif
#ifndef PCI_DEVICE_ID_DIGI32_8
# define PCI_DEVICE_ID_DIGI32_8 0x9898
#endif

typedef struct snd_rme32 {
	spinlock_t lock;
	int irq;
	unsigned long port;
	struct resource *res_port;
	unsigned long iobase;

	u32 wcreg;		/* cached write control register value */
	u32 wcreg_spdif;	/* S/PDIF setup */
	u32 wcreg_spdif_stream;	/* S/PDIF setup (temporary) */
	u32 rcreg;		/* cached read control register value */

	u8 rev;			/* card revision number */

	snd_pcm_substream_t *playback_substream;
	snd_pcm_substream_t *capture_substream;

	int playback_frlog;	/* log2 of framesize */
	int capture_frlog;

	size_t playback_periodsize;	/* in bytes, zero if not used */
	size_t capture_periodsize;	/* in bytes, zero if not used */

	snd_pcm_uframes_t playback_last_appl_ptr;
	size_t playback_ptr;
	size_t capture_ptr;

	snd_card_t *card;
	snd_pcm_t *spdif_pcm;
	snd_pcm_t *adat_pcm;
	struct pci_dev *pci;
	snd_kcontrol_t *spdif_ctl;
} rme32_t;

static struct pci_device_id snd_rme32_ids[] = {
	{PCI_VENDOR_ID_XILINX_RME, PCI_DEVICE_ID_DIGI32,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0,},
	{PCI_VENDOR_ID_XILINX_RME, PCI_DEVICE_ID_DIGI32_8,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0,},
	{PCI_VENDOR_ID_XILINX_RME, PCI_DEVICE_ID_DIGI32_PRO,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0,},
	{0,}
};

MODULE_DEVICE_TABLE(pci, snd_rme32_ids);

#define RME32_ISWORKING(rme32) ((rme32)->wcreg & RME32_WCR_START)
#define RME32_PRO_WITH_8414(rme32) ((rme32)->pci->device == PCI_DEVICE_ID_DIGI32_PRO && (rme32)->rev == RME32_PRO_REVISION_WITH_8414)

static int snd_rme32_playback_prepare(snd_pcm_substream_t * substream);

static int snd_rme32_capture_prepare(snd_pcm_substream_t * substream);

static int
snd_rme32_playback_trigger(snd_pcm_substream_t * substream, int cmd);

static int
snd_rme32_capture_trigger(snd_pcm_substream_t * substream, int cmd);

static snd_pcm_uframes_t
snd_rme32_playback_pointer(snd_pcm_substream_t * substream);

static snd_pcm_uframes_t
snd_rme32_capture_pointer(snd_pcm_substream_t * substream);

static void snd_rme32_proc_init(rme32_t * rme32);

static int snd_rme32_create_switches(snd_card_t * card, rme32_t * rme32);

static inline unsigned int snd_rme32_playback_ptr(rme32_t * rme32)
{

	return (readl(rme32->iobase + RME32_IO_GET_POS)
		& RME32_RCR_AUDIO_ADDR_MASK) >> rme32->playback_frlog;
}

static inline unsigned int snd_rme32_capture_ptr(rme32_t * rme32)
{
	return (readl(rme32->iobase + RME32_IO_GET_POS)
		& RME32_RCR_AUDIO_ADDR_MASK) >> rme32->capture_frlog;
}

static int snd_rme32_ratecode(int rate)
{
	switch (rate) {
	case 32000: return SNDRV_PCM_RATE_32000;
	case 44100: return SNDRV_PCM_RATE_44100;
	case 48000: return SNDRV_PCM_RATE_48000;
	case 64000: return SNDRV_PCM_RATE_64000;
	case 88200: return SNDRV_PCM_RATE_88200;
	case 96000: return SNDRV_PCM_RATE_96000;
	}
	return 0;
}

static int snd_rme32_playback_silence(snd_pcm_substream_t * substream, int channel,	/* not used (interleaved data) */
				      snd_pcm_uframes_t pos,
				      snd_pcm_uframes_t count)
{
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	count <<= rme32->playback_frlog;
	pos <<= rme32->playback_frlog;
	memset_io(rme32->iobase + RME32_IO_DATA_BUFFER + pos, 0, count);
	return 0;
}

static int snd_rme32_playback_copy(snd_pcm_substream_t * substream, int channel,	/* not used (interleaved data) */
				   snd_pcm_uframes_t pos,
				   void *src, snd_pcm_uframes_t count)
{
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	count <<= rme32->playback_frlog;
	pos <<= rme32->playback_frlog;
	if (copy_from_user_toio(rme32->iobase + RME32_IO_DATA_BUFFER + pos,
			    src, count))
		return -EFAULT;
	return 0;
}

static int snd_rme32_capture_copy(snd_pcm_substream_t * substream, int channel,	/* not used (interleaved data) */
				  snd_pcm_uframes_t pos,
				  void *dst, snd_pcm_uframes_t count)
{
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	count <<= rme32->capture_frlog;
	pos <<= rme32->capture_frlog;
	if (copy_to_user_fromio(dst,
			    rme32->iobase + RME32_IO_DATA_BUFFER + pos,
			    count))
		return -EFAULT;
	return 0;
}

/*
 * Digital output capabilites (S/PDIF)
 */
static snd_pcm_hardware_t snd_rme32_playback_spdif_info = {
	.info =		(SNDRV_PCM_INFO_MMAP |
			 SNDRV_PCM_INFO_MMAP_VALID |
			 SNDRV_PCM_INFO_INTERLEAVED | 
			 SNDRV_PCM_INFO_PAUSE),
	.formats =	(SNDRV_PCM_FMTBIT_S16_LE | 
			 SNDRV_PCM_FMTBIT_S32_LE),
	.rates =	(SNDRV_PCM_RATE_32000 |
			 SNDRV_PCM_RATE_44100 | 
			 SNDRV_PCM_RATE_48000),
	.rate_min =	32000,
	.rate_max =	48000,
	.channels_min =	2,
	.channels_max =	2,
	.buffer_bytes_max = RME32_BUFFER_SIZE,
	.period_bytes_min = RME32_BLOCK_SIZE,
	.period_bytes_max = RME32_BLOCK_SIZE,
	.periods_min =	RME32_BUFFER_SIZE / RME32_BLOCK_SIZE,
	.periods_max =	RME32_BUFFER_SIZE / RME32_BLOCK_SIZE,
	.fifo_size =	0,
};

/*
 * Digital input capabilites (S/PDIF)
 */
static snd_pcm_hardware_t snd_rme32_capture_spdif_info = {
	.info =		(SNDRV_PCM_INFO_MMAP |
			 SNDRV_PCM_INFO_MMAP_VALID |
			 SNDRV_PCM_INFO_INTERLEAVED | 
			 SNDRV_PCM_INFO_PAUSE),
	.formats =	(SNDRV_PCM_FMTBIT_S16_LE | 
			 SNDRV_PCM_FMTBIT_S32_LE),
	.rates =	(SNDRV_PCM_RATE_32000 |
			 SNDRV_PCM_RATE_44100 | 
			 SNDRV_PCM_RATE_48000),
	.rate_min =	32000,
	.rate_max =	48000,
	.channels_min =	2,
	.channels_max =	2,
	.buffer_bytes_max = RME32_BUFFER_SIZE,
	.period_bytes_min = RME32_BLOCK_SIZE,
	.period_bytes_max = RME32_BLOCK_SIZE,
	.periods_min =	RME32_BUFFER_SIZE / RME32_BLOCK_SIZE,
	.periods_max =	RME32_BUFFER_SIZE / RME32_BLOCK_SIZE,
	.fifo_size =	0,
};

/*
 * Digital output capabilites (ADAT)
 */
static snd_pcm_hardware_t snd_rme32_playback_adat_info =
{
	.info =		     (SNDRV_PCM_INFO_MMAP |
			      SNDRV_PCM_INFO_MMAP_VALID |
			      SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_PAUSE),
	.formats=            SNDRV_PCM_FMTBIT_S16_LE,
	.rates =             (SNDRV_PCM_RATE_44100 | 
			      SNDRV_PCM_RATE_48000),
	.rate_min =          44100,
	.rate_max =          48000,
	.channels_min =      8,
	.channels_max =	     8,
	.buffer_bytes_max =  RME32_BUFFER_SIZE,
	.period_bytes_min =  RME32_BLOCK_SIZE,
	.period_bytes_max =  RME32_BLOCK_SIZE,
	.periods_min =	    RME32_BUFFER_SIZE / RME32_BLOCK_SIZE,
	.periods_max =	    RME32_BUFFER_SIZE / RME32_BLOCK_SIZE,
	.fifo_size =	    0,
};

/*
 * Digital input capabilites (ADAT)
 */
static snd_pcm_hardware_t snd_rme32_capture_adat_info =
{
	.info =		     (SNDRV_PCM_INFO_MMAP |
			      SNDRV_PCM_INFO_MMAP_VALID |
			      SNDRV_PCM_INFO_INTERLEAVED |
			      SNDRV_PCM_INFO_PAUSE),
	.formats =           SNDRV_PCM_FMTBIT_S16_LE,
	.rates =             (SNDRV_PCM_RATE_44100 | 
			      SNDRV_PCM_RATE_48000),
	.rate_min =          44100,
	.rate_max =          48000,
	.channels_min =	     8,
	.channels_max =	     8,
	.buffer_bytes_max =  RME32_BUFFER_SIZE,
	.period_bytes_min =  RME32_BLOCK_SIZE,
	.period_bytes_max =  RME32_BLOCK_SIZE,
	.periods_min =	     RME32_BUFFER_SIZE / RME32_BLOCK_SIZE,
	.periods_max =	     RME32_BUFFER_SIZE / RME32_BLOCK_SIZE,
	.fifo_size =         0,
};

static void snd_rme32_reset_dac(rme32_t *rme32)
{
        writel(rme32->wcreg | RME32_WCR_PD,
               rme32->iobase + RME32_IO_CONTROL_REGISTER);
        writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
}

static int snd_rme32_playback_getrate(rme32_t * rme32)
{
	int rate;

	rate = ((rme32->wcreg >> RME32_WCR_BITPOS_FREQ_0) & 1) +
	       (((rme32->wcreg >> RME32_WCR_BITPOS_FREQ_1) & 1) << 1);
	switch (rate) {
	case 1:
		rate = 32000;
		break;
	case 2:
		rate = 44100;
		break;
	case 3:
		rate = 48000;
		break;
	default:
		return -1;
	}
	return (rme32->wcreg & RME32_WCR_DS_BM) ? rate << 1 : rate;
}

static int snd_rme32_capture_getrate(rme32_t * rme32, int *is_adat)
{
	int n;

	*is_adat = 0;
	if (rme32->rcreg & RME32_RCR_LOCK) { 
                /* ADAT rate */
                *is_adat = 1;
	}
	if (rme32->rcreg & RME32_RCR_ERF) {
		return -1;
	}

        /* S/PDIF rate */
	n = ((rme32->rcreg >> RME32_RCR_BITPOS_F0) & 1) +
		(((rme32->rcreg >> RME32_RCR_BITPOS_F1) & 1) << 1) +
		(((rme32->rcreg >> RME32_RCR_BITPOS_F2) & 1) << 2);

	if (RME32_PRO_WITH_8414(rme32))
		switch (n) {	/* supporting the CS8414 */
		case 0:
		case 1:
		case 2:
			return -1;
		case 3:
			return 96000;
		case 4:
			return 88200;
		case 5:
			return 48000;
		case 6:
			return 44100;
		case 7:
			return 32000;
		default:
			return -1;
			break;
		} 
	else
		switch (n) {	/* supporting the CS8412 */
		case 0:
			return -1;
		case 1:
			return 48000;
		case 2:
			return 44100;
		case 3:
			return 32000;
		case 4:
			return 48000;
		case 5:
			return 44100;
		case 6:
			return 44056;
		case 7:
			return 32000;
		default:
			break;
		}
	return -1;
}

static int snd_rme32_playback_setrate(rme32_t * rme32, int rate)
{
        int ds;

        ds = rme32->wcreg & RME32_WCR_DS_BM;
	switch (rate) {
	case 32000:
		rme32->wcreg &= ~RME32_WCR_DS_BM;
		rme32->wcreg = (rme32->wcreg | RME32_WCR_FREQ_0) & 
			~RME32_WCR_FREQ_1;
		break;
	case 44100:
		rme32->wcreg &= ~RME32_WCR_DS_BM;
		rme32->wcreg = (rme32->wcreg | RME32_WCR_FREQ_1) & 
			~RME32_WCR_FREQ_0;
		break;
	case 48000:
		rme32->wcreg &= ~RME32_WCR_DS_BM;
		rme32->wcreg = (rme32->wcreg | RME32_WCR_FREQ_0) | 
			RME32_WCR_FREQ_1;
		break;
	case 64000:
		if (rme32->pci->device != PCI_DEVICE_ID_DIGI32_PRO)
			return -EINVAL;
		rme32->wcreg |= RME32_WCR_DS_BM;
		rme32->wcreg = (rme32->wcreg | RME32_WCR_FREQ_0) & 
			~RME32_WCR_FREQ_1;
		break;
	case 88200:
		if (rme32->pci->device != PCI_DEVICE_ID_DIGI32_PRO)
			return -EINVAL;
		rme32->wcreg |= RME32_WCR_DS_BM;
		rme32->wcreg = (rme32->wcreg | RME32_WCR_FREQ_1) & 
			~RME32_WCR_FREQ_0;
		break;
	case 96000:
		if (rme32->pci->device != PCI_DEVICE_ID_DIGI32_PRO)
			return -EINVAL;
		rme32->wcreg |= RME32_WCR_DS_BM;
		rme32->wcreg = (rme32->wcreg | RME32_WCR_FREQ_0) | 
			RME32_WCR_FREQ_1;
		break;
	default:
		return -EINVAL;
	}
        if ((!ds && rme32->wcreg & RME32_WCR_DS_BM) ||
            (ds && !(rme32->wcreg & RME32_WCR_DS_BM)))
        {
                /* change to/from double-speed: reset the DAC (if available) */
                snd_rme32_reset_dac(rme32);
        } else {
                writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
	}
	return 0;
}

static int snd_rme32_setclockmode(rme32_t * rme32, int mode)
{
	switch (mode) {
	case RME32_CLOCKMODE_SLAVE:
		/* AutoSync */
		rme32->wcreg = (rme32->wcreg & ~RME32_WCR_FREQ_0) & 
			~RME32_WCR_FREQ_1;
		break;
	case RME32_CLOCKMODE_MASTER_32:
		/* Internal 32.0kHz */
		rme32->wcreg = (rme32->wcreg | RME32_WCR_FREQ_0) & 
			~RME32_WCR_FREQ_1;
		break;
	case RME32_CLOCKMODE_MASTER_44:
		/* Internal 44.1kHz */
		rme32->wcreg = (rme32->wcreg & ~RME32_WCR_FREQ_0) | 
			RME32_WCR_FREQ_1;
		break;
	case RME32_CLOCKMODE_MASTER_48:
		/* Internal 48.0kHz */
		rme32->wcreg = (rme32->wcreg | RME32_WCR_FREQ_0) | 
			RME32_WCR_FREQ_1;
		break;
	default:
		return -EINVAL;
	}
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
	return 0;
}

static int snd_rme32_getclockmode(rme32_t * rme32)
{
	return ((rme32->wcreg >> RME32_WCR_BITPOS_FREQ_0) & 1) +
	    (((rme32->wcreg >> RME32_WCR_BITPOS_FREQ_1) & 1) << 1);
}

static int snd_rme32_setinputtype(rme32_t * rme32, int type)
{
	switch (type) {
	case RME32_INPUT_OPTICAL:
		rme32->wcreg = (rme32->wcreg & ~RME32_WCR_INP_0) & 
			~RME32_WCR_INP_1;
		break;
	case RME32_INPUT_COAXIAL:
		rme32->wcreg = (rme32->wcreg | RME32_WCR_INP_0) & 
			~RME32_WCR_INP_1;
		break;
	case RME32_INPUT_INTERNAL:
		rme32->wcreg = (rme32->wcreg & ~RME32_WCR_INP_0) | 
			RME32_WCR_INP_1;
		break;
	case RME32_INPUT_XLR:
		rme32->wcreg = (rme32->wcreg | RME32_WCR_INP_0) | 
			RME32_WCR_INP_1;
		break;
	default:
		return -EINVAL;
	}
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
	return 0;
}

static int snd_rme32_getinputtype(rme32_t * rme32)
{
	return ((rme32->wcreg >> RME32_WCR_BITPOS_INP_0) & 1) +
	    (((rme32->wcreg >> RME32_WCR_BITPOS_INP_1) & 1) << 1);
}

static void
snd_rme32_setframelog(rme32_t * rme32, int n_channels, int is_playback)
{
	int frlog;

	if (n_channels == 2) {
		frlog = 1;
	} else {
		/* assume 8 channels */
		frlog = 3;
	}
	if (is_playback) {
		frlog += (rme32->wcreg & RME32_WCR_MODE24) ? 2 : 1;
		rme32->playback_frlog = frlog;
	} else {
		frlog += (rme32->wcreg & RME32_WCR_MODE24) ? 2 : 1;
		rme32->capture_frlog = frlog;
	}
}

static int snd_rme32_setformat(rme32_t * rme32, int format)
{
	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		rme32->wcreg &= ~RME32_WCR_MODE24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		rme32->wcreg |= RME32_WCR_MODE24;
		break;
	default:
		return -EINVAL;
	}
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
	return 0;
}

static int
snd_rme32_playback_hw_params(snd_pcm_substream_t * substream,
			     snd_pcm_hw_params_t * params)
{
	int err, rate, dummy;
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);

	if ((err = snd_pcm_lib_malloc_pages(substream,
				      params_buffer_bytes(params))) < 0)
		return err;
	spin_lock_irq(&rme32->lock);
	if ((rme32->rcreg & RME32_RCR_KMODE) &&
	    (rate = snd_rme32_capture_getrate(rme32, &dummy)) > 0) {
		/* AutoSync */
		if ((int)params_rate(params) != rate) {
			spin_unlock_irq(&rme32->lock);
			return -EIO;
		}
	} else if ((err = snd_rme32_playback_setrate(rme32, params_rate(params))) < 0) {
		spin_unlock_irq(&rme32->lock);
		return err;
	}
	if ((err = snd_rme32_setformat(rme32, params_format(params))) < 0) {
		spin_unlock_irq(&rme32->lock);
		return err;
	}

	snd_rme32_setframelog(rme32, params_channels(params), 1);
	if (rme32->capture_periodsize != 0) {
		if (params_period_size(params) << rme32->playback_frlog != rme32->capture_periodsize) {
			spin_unlock_irq(&rme32->lock);
			return -EBUSY;
		}
	}
	rme32->playback_periodsize = params_period_size(params) << rme32->playback_frlog;
	/* S/PDIF setup */
	if ((rme32->wcreg & RME32_WCR_ADAT) == 0) {
		rme32->wcreg &= ~(RME32_WCR_PRO | RME32_WCR_EMP);
		writel(rme32->wcreg |=
		       rme32->wcreg_spdif_stream,
		       rme32->iobase + RME32_IO_CONTROL_REGISTER);
	}
	spin_unlock_irq(&rme32->lock);

	return 0;
}

static int snd_rme32_playback_hw_free(snd_pcm_substream_t * substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int
snd_rme32_capture_hw_params(snd_pcm_substream_t * substream,
			    snd_pcm_hw_params_t * params)
{
	unsigned long flags;
	int err, isadat, rate;
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	if ((err = snd_pcm_lib_malloc_pages(substream,
				      params_buffer_bytes(params))) < 0)
		return err;
	spin_lock_irqsave(&rme32->lock, flags);
	/* enable AutoSync for record-preparing */
	rme32->wcreg |= RME32_WCR_AUTOSYNC;
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);

	if ((err = snd_rme32_setformat(rme32, params_format(params))) < 0) {
		spin_unlock_irqrestore(&rme32->lock, flags);
		return err;
	}
	if ((err = snd_rme32_playback_setrate(rme32, params_rate(params))) < 0) {
		spin_unlock_irqrestore(&rme32->lock, flags);
		return err;
	}
	if ((rate = snd_rme32_capture_getrate(rme32, &isadat)) > 0) {
                if ((int)params_rate(params) != rate) {
                        spin_unlock_irqrestore(&rme32->lock, flags);
                        return -EIO;                    
                }
                if ((isadat && runtime->hw.channels_min == 2) ||
                    (!isadat && runtime->hw.channels_min == 8)) {
                        spin_unlock_irqrestore(&rme32->lock, flags);
                        return -EIO;
                }
	}
	/* AutoSync off for recording */
	rme32->wcreg &= ~RME32_WCR_AUTOSYNC;
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);

	snd_rme32_setframelog(rme32, params_channels(params), 0);
	if (rme32->playback_periodsize != 0) {
		if (params_period_size(params) << rme32->capture_frlog !=
		    rme32->playback_periodsize) {
			spin_unlock_irqrestore(&rme32->lock, flags);
			return -EBUSY;
		}
	}
	rme32->capture_periodsize =
	    params_period_size(params) << rme32->capture_frlog;
	spin_unlock_irqrestore(&rme32->lock, flags);

	return 0;
}

static int snd_rme32_capture_hw_free(snd_pcm_substream_t * substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static void snd_rme32_playback_start(rme32_t * rme32, int from_pause)
{
	if (!from_pause) {
		writel(0, rme32->iobase + RME32_IO_RESET_POS);
		rme32->playback_last_appl_ptr = 0;
		rme32->playback_ptr = 0;
	}

	rme32->wcreg |= RME32_WCR_START;
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
}

static void snd_rme32_capture_start(rme32_t * rme32, int from_pause)
{
	if (!from_pause) {
		writel(0, rme32->iobase + RME32_IO_RESET_POS);
	}

	rme32->wcreg |= RME32_WCR_START;
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
}

static void snd_rme32_playback_stop(rme32_t * rme32)
{
	/*
	 * Check if there is an unconfirmed IRQ, if so confirm it, or else
	 * the hardware will not stop generating interrupts
	 */
	rme32->rcreg = readl(rme32->iobase + RME32_IO_CONTROL_REGISTER);
	if (rme32->rcreg & RME32_RCR_IRQ) {
		writel(0, rme32->iobase + RME32_IO_CONFIRM_ACTION_IRQ);
	}
	rme32->wcreg &= ~RME32_WCR_START;
	if (rme32->wcreg & RME32_WCR_SEL)
		rme32->wcreg |= RME32_WCR_MUTE;
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
}

static void snd_rme32_capture_stop(rme32_t * rme32)
{
	rme32->rcreg = readl(rme32->iobase + RME32_IO_CONTROL_REGISTER);
	if (rme32->rcreg & RME32_RCR_IRQ) {
		writel(0, rme32->iobase + RME32_IO_CONFIRM_ACTION_IRQ);
	}
	rme32->wcreg &= ~RME32_WCR_START;
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
}

static irqreturn_t
snd_rme32_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	rme32_t *rme32 = (rme32_t *) dev_id;

	rme32->rcreg = readl(rme32->iobase + RME32_IO_CONTROL_REGISTER);
	if (!(rme32->rcreg & RME32_RCR_IRQ)) {
		return IRQ_NONE;
	} else {
		if (rme32->capture_substream) {
			snd_pcm_period_elapsed(rme32->capture_substream);
		}
		if (rme32->playback_substream) {
			snd_pcm_period_elapsed(rme32->playback_substream);
		}
		writel(0, rme32->iobase + RME32_IO_CONFIRM_ACTION_IRQ);
	}
	return IRQ_HANDLED;
}

static unsigned int period_bytes[] = { RME32_BLOCK_SIZE };


#define PERIOD_BYTES sizeof(period_bytes) / sizeof(period_bytes[0])

static snd_pcm_hw_constraint_list_t hw_constraints_period_bytes = {
	.count = PERIOD_BYTES,
	.list = period_bytes,
	.mask = 0
};

static int snd_rme32_playback_spdif_open(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	int rate, dummy;
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_pcm_set_sync(substream);

	spin_lock_irqsave(&rme32->lock, flags);
	if (rme32->playback_substream != NULL) {
		spin_unlock_irqrestore(&rme32->lock, flags);
		return -EBUSY;
	}
	rme32->wcreg &= ~RME32_WCR_ADAT;
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
	rme32->playback_substream = substream;
	rme32->playback_last_appl_ptr = 0;
	rme32->playback_ptr = 0;
	spin_unlock_irqrestore(&rme32->lock, flags);

	runtime->hw = snd_rme32_playback_spdif_info;
	if (rme32->pci->device == PCI_DEVICE_ID_DIGI32_PRO) {
		runtime->hw.rates |= SNDRV_PCM_RATE_64000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000;
		runtime->hw.rate_max = 96000;
	}
	if ((rme32->rcreg & RME32_RCR_KMODE) &&
	    (rate = snd_rme32_capture_getrate(rme32, &dummy)) > 0) {
		/* AutoSync */
		runtime->hw.rates = snd_rme32_ratecode(rate);
		runtime->hw.rate_min = rate;
		runtime->hw.rate_max = rate;
	}       
	snd_pcm_hw_constraint_minmax(runtime,
				     SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				     RME32_BUFFER_SIZE, RME32_BUFFER_SIZE);
	snd_pcm_hw_constraint_list(runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   &hw_constraints_period_bytes);

	rme32->wcreg_spdif_stream = rme32->wcreg_spdif;
	rme32->spdif_ctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(rme32->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &rme32->spdif_ctl->id);
	return 0;
}

static int snd_rme32_capture_spdif_open(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	int isadat, rate;
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	snd_pcm_set_sync(substream);

	spin_lock_irqsave(&rme32->lock, flags);
        if (rme32->capture_substream != NULL) {
	        spin_unlock_irqrestore(&rme32->lock, flags);
                return -EBUSY;
        }
	rme32->capture_substream = substream;
	rme32->capture_ptr = 0;
	spin_unlock_irqrestore(&rme32->lock, flags);

	runtime->hw = snd_rme32_capture_spdif_info;
	if (RME32_PRO_WITH_8414(rme32)) {
		runtime->hw.rates |= SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000;
		runtime->hw.rate_max = 96000;
	}
	if ((rate = snd_rme32_capture_getrate(rme32, &isadat)) > 0) {
		if (isadat) {
			return -EIO;
		}
		runtime->hw.rates = snd_rme32_ratecode(rate);
		runtime->hw.rate_min = rate;
		runtime->hw.rate_max = rate;
	}

	snd_pcm_hw_constraint_minmax(runtime,
				     SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				     RME32_BUFFER_SIZE, RME32_BUFFER_SIZE);
	snd_pcm_hw_constraint_list(runtime, 0,
				   SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   &hw_constraints_period_bytes);

	return 0;
}

static int
snd_rme32_playback_adat_open(snd_pcm_substream_t *substream)
{
	unsigned long flags;
	int rate, dummy;
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	
	snd_pcm_set_sync(substream);

	spin_lock_irqsave(&rme32->lock, flags);	
        if (rme32->playback_substream != NULL) {
	        spin_unlock_irqrestore(&rme32->lock, flags);
                return -EBUSY;
        }
	rme32->wcreg |= RME32_WCR_ADAT;
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
	rme32->playback_substream = substream;
	rme32->playback_last_appl_ptr = 0;
	rme32->playback_ptr = 0;
	spin_unlock_irqrestore(&rme32->lock, flags);
	
	runtime->hw = snd_rme32_playback_adat_info;
	if ((rme32->rcreg & RME32_RCR_KMODE) &&
	    (rate = snd_rme32_capture_getrate(rme32, &dummy)) > 0) {
                /* AutoSync */
                runtime->hw.rates = snd_rme32_ratecode(rate);
                runtime->hw.rate_min = rate;
                runtime->hw.rate_max = rate;
	}        
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				     RME32_BUFFER_SIZE, RME32_BUFFER_SIZE);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   &hw_constraints_period_bytes);
	return 0;
}

static int
snd_rme32_capture_adat_open(snd_pcm_substream_t *substream)
{
	unsigned long flags;
	int isadat, rate;
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;

	runtime->hw = snd_rme32_capture_adat_info;
	if ((rate = snd_rme32_capture_getrate(rme32, &isadat)) > 0) {
		if (!isadat) {
			return -EIO;
		}
                runtime->hw.rates = snd_rme32_ratecode(rate);
                runtime->hw.rate_min = rate;
                runtime->hw.rate_max = rate;
        }

	snd_pcm_set_sync(substream);
        
	spin_lock_irqsave(&rme32->lock, flags);	
	if (rme32->capture_substream != NULL) {
		spin_unlock_irqrestore(&rme32->lock, flags);
		return -EBUSY;
        }
	rme32->capture_substream = substream;
	rme32->capture_ptr = 0;
	spin_unlock_irqrestore(&rme32->lock, flags);

	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				     RME32_BUFFER_SIZE, RME32_BUFFER_SIZE);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				   &hw_constraints_period_bytes);
	return 0;
}

static int snd_rme32_playback_close(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	int spdif = 0;

	spin_lock_irqsave(&rme32->lock, flags);
	rme32->playback_substream = NULL;
	rme32->playback_periodsize = 0;
	spdif = (rme32->wcreg & RME32_WCR_ADAT) == 0;
	spin_unlock_irqrestore(&rme32->lock, flags);
	if (spdif) {
		rme32->spdif_ctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		snd_ctl_notify(rme32->card, SNDRV_CTL_EVENT_MASK_VALUE |
			       SNDRV_CTL_EVENT_MASK_INFO,
			       &rme32->spdif_ctl->id);
	}
	return 0;
}

static int snd_rme32_capture_close(snd_pcm_substream_t * substream)
{
	unsigned long flags;
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);

	spin_lock_irqsave(&rme32->lock, flags);
	rme32->capture_substream = NULL;
	rme32->capture_periodsize = 0;
	spin_unlock_irqrestore(&rme32->lock, flags);
	return 0;
}

static int snd_rme32_playback_prepare(snd_pcm_substream_t * substream)
{
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&rme32->lock, flags);
	if (RME32_ISWORKING(rme32)) {
		snd_rme32_playback_stop(rme32);
	}
	writel(0, rme32->iobase + RME32_IO_RESET_POS);
	if (rme32->wcreg & RME32_WCR_SEL)
		rme32->wcreg &= ~RME32_WCR_MUTE;
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);
	spin_unlock_irqrestore(&rme32->lock, flags);
	return 0;
}

static int snd_rme32_capture_prepare(snd_pcm_substream_t * substream)
{
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&rme32->lock, flags);
	if (RME32_ISWORKING(rme32)) {
		snd_rme32_capture_stop(rme32);
	}
	writel(0, rme32->iobase + RME32_IO_RESET_POS);
	spin_unlock_irqrestore(&rme32->lock, flags);
	return 0;
}

static int
snd_rme32_playback_trigger(snd_pcm_substream_t * substream, int cmd)
{
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (!RME32_ISWORKING(rme32)) {
			if (substream != rme32->playback_substream) {
				return -EBUSY;
			}
			snd_rme32_playback_start(rme32, 0);
		}
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		if (RME32_ISWORKING(rme32)) {
			if (substream != rme32->playback_substream) {
				return -EBUSY;
			}
			snd_rme32_playback_stop(rme32);
		}
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (RME32_ISWORKING(rme32)) {
			snd_rme32_playback_stop(rme32);
		}
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!RME32_ISWORKING(rme32)) {
			snd_rme32_playback_start(rme32, 1);
		}
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int
snd_rme32_capture_trigger(snd_pcm_substream_t * substream, int cmd)
{
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (!RME32_ISWORKING(rme32)) {
			if (substream != rme32->capture_substream) {
				return -EBUSY;
			}
			snd_rme32_capture_start(rme32, 0);
		}
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		if (RME32_ISWORKING(rme32)) {
			if (substream != rme32->capture_substream) {
				return -EBUSY;
			}
			snd_rme32_capture_stop(rme32);
		}
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (RME32_ISWORKING(rme32)) {
			snd_rme32_capture_stop(rme32);
		}
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!RME32_ISWORKING(rme32)) {
			snd_rme32_capture_start(rme32, 1);
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t
snd_rme32_playback_pointer(snd_pcm_substream_t * substream)
{
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_sframes_t diff;
	size_t bytes;


	if (runtime->access == SNDRV_PCM_ACCESS_MMAP_INTERLEAVED) {
		diff = runtime->control->appl_ptr -
		       rme32->playback_last_appl_ptr;
		rme32->playback_last_appl_ptr = runtime->control->appl_ptr;
		if (diff != 0 && diff < -(snd_pcm_sframes_t) (runtime->boundary >> 1)) {
			diff += runtime->boundary;
		}
		bytes = diff << rme32->playback_frlog;
		if (bytes > RME32_BUFFER_SIZE - rme32->playback_ptr) {
			memcpy_toio((void *)(rme32->iobase + RME32_IO_DATA_BUFFER + rme32->playback_ptr),
				    runtime->dma_area + rme32->playback_ptr,
				    RME32_BUFFER_SIZE - rme32->playback_ptr);
			bytes -= RME32_BUFFER_SIZE - rme32->playback_ptr;
			if (bytes > RME32_BUFFER_SIZE) {
				bytes = RME32_BUFFER_SIZE;
			}
			memcpy_toio((void *)(rme32->iobase + RME32_IO_DATA_BUFFER),
				    runtime->dma_area, bytes);
			rme32->playback_ptr = bytes;
		} else if (bytes != 0) {
			memcpy_toio((void *)(rme32->iobase + RME32_IO_DATA_BUFFER + rme32->playback_ptr),
				    runtime->dma_area + rme32->playback_ptr, bytes);
			rme32->playback_ptr += bytes;
		}
	}
	return snd_rme32_playback_ptr(rme32);
}

static snd_pcm_uframes_t
snd_rme32_capture_pointer(snd_pcm_substream_t * substream)
{
	rme32_t *rme32 = _snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_uframes_t frameptr;
	size_t ptr;

	frameptr = snd_rme32_capture_ptr(rme32);
	if (runtime->access == SNDRV_PCM_ACCESS_MMAP_INTERLEAVED) {
		ptr = frameptr << rme32->capture_frlog;
		if (ptr > rme32->capture_ptr) {
			memcpy_fromio(runtime->dma_area + rme32->capture_ptr,
				      (void *)(rme32->iobase + RME32_IO_DATA_BUFFER +
					       rme32->capture_ptr),
				      ptr - rme32->capture_ptr);
			rme32->capture_ptr += ptr - rme32->capture_ptr;
		} else if (ptr < rme32->capture_ptr) {
			memcpy_fromio(runtime->dma_area + rme32->capture_ptr,
				      (void *)(rme32->iobase + RME32_IO_DATA_BUFFER +
					       rme32->capture_ptr),
				      RME32_BUFFER_SIZE - rme32->capture_ptr);
			memcpy_fromio(runtime->dma_area,
				      (void *)(rme32->iobase + RME32_IO_DATA_BUFFER),
				      ptr);
			rme32->capture_ptr = ptr;
		}
	}
	return frameptr;
}

static snd_pcm_ops_t snd_rme32_playback_spdif_ops = {
	.open =		snd_rme32_playback_spdif_open,
	.close =	snd_rme32_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_rme32_playback_hw_params,
	.hw_free =	snd_rme32_playback_hw_free,
	.prepare =	snd_rme32_playback_prepare,
	.trigger =	snd_rme32_playback_trigger,
	.pointer =	snd_rme32_playback_pointer,
	.copy =		snd_rme32_playback_copy,
	.silence =	snd_rme32_playback_silence,
};

static snd_pcm_ops_t snd_rme32_capture_spdif_ops = {
	.open =		snd_rme32_capture_spdif_open,
	.close =	snd_rme32_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_rme32_capture_hw_params,
	.hw_free =	snd_rme32_capture_hw_free,
	.prepare =	snd_rme32_capture_prepare,
	.trigger =	snd_rme32_capture_trigger,
	.pointer =	snd_rme32_capture_pointer,
	.copy =		snd_rme32_capture_copy,
};

static snd_pcm_ops_t snd_rme32_playback_adat_ops = {
	.open =		snd_rme32_playback_adat_open,
	.close =	snd_rme32_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_rme32_playback_hw_params,
	.hw_free =	snd_rme32_playback_hw_free,
	.prepare =	snd_rme32_playback_prepare,
	.trigger =	snd_rme32_playback_trigger,
	.pointer =	snd_rme32_playback_pointer,
	.copy =		snd_rme32_playback_copy,
	.silence =	snd_rme32_playback_silence,
};

static snd_pcm_ops_t snd_rme32_capture_adat_ops = {
	.open =		snd_rme32_capture_adat_open,
	.close =	snd_rme32_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_rme32_capture_hw_params,
	.hw_free =	snd_rme32_capture_hw_free,
	.prepare =	snd_rme32_capture_prepare,
	.trigger =	snd_rme32_capture_trigger,
	.pointer =	snd_rme32_capture_pointer,
	.copy =		snd_rme32_capture_copy,
};

static void snd_rme32_free(void *private_data)
{
	rme32_t *rme32 = (rme32_t *) private_data;

	if (rme32 == NULL) {
		return;
	}
	if (rme32->irq >= 0) {
		snd_rme32_playback_stop(rme32);
		snd_rme32_capture_stop(rme32);
		free_irq(rme32->irq, (void *) rme32);
		rme32->irq = -1;
	}
	if (rme32->iobase) {
		iounmap((void *) rme32->iobase);
		rme32->iobase = 0;
	}
	if (rme32->res_port != NULL) {
		release_resource(rme32->res_port);
		rme32->res_port = NULL;
	}
}

static void snd_rme32_free_spdif_pcm(snd_pcm_t * pcm)
{
	rme32_t *rme32 = (rme32_t *) pcm->private_data;
	rme32->spdif_pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static void
snd_rme32_free_adat_pcm(snd_pcm_t *pcm)
{
	rme32_t *rme32 = (rme32_t *) pcm->private_data;
	rme32->adat_pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int __devinit snd_rme32_create(rme32_t * rme32)
{
	struct pci_dev *pci = rme32->pci;
	int err;

	rme32->irq = -1;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	rme32->port = pci_resource_start(rme32->pci, 0);

	if ((rme32->res_port = request_mem_region(rme32->port, RME32_IO_SIZE, "RME32")) == NULL) {
		snd_printk("unable to grab memory region 0x%lx-0x%lx\n",
			   rme32->port, rme32->port + RME32_IO_SIZE - 1);
		return -EBUSY;
	}

	if (request_irq(pci->irq, snd_rme32_interrupt, SA_INTERRUPT | SA_SHIRQ, "RME32", (void *) rme32)) {
		snd_printk("unable to grab IRQ %d\n", pci->irq);
		return -EBUSY;
	}
	rme32->irq = pci->irq;

	spin_lock_init(&rme32->lock);
	if ((rme32->iobase = (unsigned long) ioremap_nocache(rme32->port, RME32_IO_SIZE)) == 0) {
		snd_printk("unable to remap memory region 0x%lx-0x%lx\n",
			   rme32->port, rme32->port + RME32_IO_SIZE - 1);
		return -ENOMEM;
	}

	/* read the card's revision number */
	pci_read_config_byte(pci, 8, &rme32->rev);

	/* set up ALSA pcm device for S/PDIF */
	if ((err = snd_pcm_new(rme32->card, "Digi32 IEC958", 0, 1, 1, &rme32->spdif_pcm)) < 0) {
		return err;
	}
	rme32->spdif_pcm->private_data = rme32;
	rme32->spdif_pcm->private_free = snd_rme32_free_spdif_pcm;
	strcpy(rme32->spdif_pcm->name, "Digi32 IEC958");
	snd_pcm_set_ops(rme32->spdif_pcm, SNDRV_PCM_STREAM_PLAYBACK,
			&snd_rme32_playback_spdif_ops);
	snd_pcm_set_ops(rme32->spdif_pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_rme32_capture_spdif_ops);

	rme32->spdif_pcm->info_flags = 0;

	snd_pcm_lib_preallocate_pages_for_all(rme32->spdif_pcm,
					      SNDRV_DMA_TYPE_CONTINUOUS,
					      snd_dma_continuous_data(GFP_KERNEL),
					      RME32_BUFFER_SIZE,
					      RME32_BUFFER_SIZE);

	/* set up ALSA pcm device for ADAT */
	if ((pci->device == PCI_DEVICE_ID_DIGI32) ||
	    (pci->device == PCI_DEVICE_ID_DIGI32_PRO)) {
		/* ADAT is not available on DIGI32 and DIGI32 Pro */
		rme32->adat_pcm = NULL;
	}
	else {
		if ((err = snd_pcm_new(rme32->card, "Digi32 ADAT", 1,
				       1, 1, &rme32->adat_pcm)) < 0)
		{
			return err;
		}		
		rme32->adat_pcm->private_data = rme32;
		rme32->adat_pcm->private_free = snd_rme32_free_adat_pcm;
		strcpy(rme32->adat_pcm->name, "Digi32 ADAT");
		snd_pcm_set_ops(rme32->adat_pcm, SNDRV_PCM_STREAM_PLAYBACK, 
				&snd_rme32_playback_adat_ops);
		snd_pcm_set_ops(rme32->adat_pcm, SNDRV_PCM_STREAM_CAPTURE, 
				&snd_rme32_capture_adat_ops);
		
		rme32->adat_pcm->info_flags = 0;

		snd_pcm_lib_preallocate_pages_for_all(rme32->adat_pcm, 
						      SNDRV_DMA_TYPE_CONTINUOUS,
						      snd_dma_continuous_data(GFP_KERNEL),
						      RME32_BUFFER_SIZE, 
						      RME32_BUFFER_SIZE);
	}


	rme32->playback_periodsize = 0;
	rme32->capture_periodsize = 0;

	/* make sure playback/capture is stopped, if by some reason active */
	snd_rme32_playback_stop(rme32);
	snd_rme32_capture_stop(rme32);

        /* reset DAC */
        snd_rme32_reset_dac(rme32);

	/* reset buffer pointer */
	writel(0, rme32->iobase + RME32_IO_RESET_POS);

	/* set default values in registers */
	rme32->wcreg = RME32_WCR_SEL |	 /* normal playback */
		RME32_WCR_INP_0 | /* input select */
		RME32_WCR_MUTE;	 /* muting on */
	writel(rme32->wcreg, rme32->iobase + RME32_IO_CONTROL_REGISTER);


	/* init switch interface */
	if ((err = snd_rme32_create_switches(rme32->card, rme32)) < 0) {
		return err;
	}

	/* init proc interface */
	snd_rme32_proc_init(rme32);

	rme32->capture_substream = NULL;
	rme32->playback_substream = NULL;

	return 0;
}

/*
 * proc interface
 */

static void
snd_rme32_proc_read(snd_info_entry_t * entry, snd_info_buffer_t * buffer)
{
	int n;
	rme32_t *rme32 = (rme32_t *) entry->private_data;

	rme32->rcreg = readl(rme32->iobase + RME32_IO_CONTROL_REGISTER);

	snd_iprintf(buffer, rme32->card->longname);
	snd_iprintf(buffer, " (index #%d)\n", rme32->card->number + 1);

	snd_iprintf(buffer, "\nGeneral settings\n");
	if (RME32_PRO_WITH_8414(rme32)) {
		snd_iprintf(buffer, "  receiver: CS8414\n");
	} else {
		snd_iprintf(buffer, "  receiver: CS8412\n");
	}
	if (rme32->wcreg & RME32_WCR_MODE24) {
		snd_iprintf(buffer, "  format: 24 bit");
	} else {
		snd_iprintf(buffer, "  format: 16 bit");
	}
	if (rme32->wcreg & RME32_WCR_MONO) {
		snd_iprintf(buffer, ", Mono\n");
	} else {
		snd_iprintf(buffer, ", Stereo\n");
	}

	snd_iprintf(buffer, "\nInput settings\n");
	switch (snd_rme32_getinputtype(rme32)) {
	case RME32_INPUT_OPTICAL:
		snd_iprintf(buffer, "  input: optical");
		break;
	case RME32_INPUT_COAXIAL:
		snd_iprintf(buffer, "  input: coaxial");
		break;
	case RME32_INPUT_INTERNAL:
		snd_iprintf(buffer, "  input: internal");
		break;
	case RME32_INPUT_XLR:
		snd_iprintf(buffer, "  input: XLR");
		break;
	}
	if (snd_rme32_capture_getrate(rme32, &n) < 0) {
		snd_iprintf(buffer, "\n  sample rate: no valid signal\n");
	} else {
		if (n) {
			snd_iprintf(buffer, " (8 channels)\n");
		} else {
			snd_iprintf(buffer, " (2 channels)\n");
		}
		snd_iprintf(buffer, "  sample rate: %d Hz\n",
			    snd_rme32_capture_getrate(rme32, &n));
	}

	snd_iprintf(buffer, "\nOutput settings\n");
	if (rme32->wcreg & RME32_WCR_SEL) {
		snd_iprintf(buffer, "  output signal: normal playback");
	} else {
		snd_iprintf(buffer, "  output signal: same as input");
	}
	if (rme32->wcreg & RME32_WCR_MUTE) {
		snd_iprintf(buffer, " (muted)\n");
	} else {
		snd_iprintf(buffer, "\n");
	}

	/* master output frequency */
	if (!
	    ((!(rme32->wcreg & RME32_WCR_FREQ_0))
	     && (!(rme32->wcreg & RME32_WCR_FREQ_1)))) {
		snd_iprintf(buffer, "  sample rate: %d Hz\n",
			    snd_rme32_playback_getrate(rme32));
	}
	if (rme32->rcreg & RME32_RCR_KMODE) {
		snd_iprintf(buffer, "  sample clock source: AutoSync\n");
	} else {
		snd_iprintf(buffer, "  sample clock source: Internal\n");
	}
	if (rme32->wcreg & RME32_WCR_PRO) {
		snd_iprintf(buffer, "  format: AES/EBU (professional)\n");
	} else {
		snd_iprintf(buffer, "  format: IEC958 (consumer)\n");
	}
	if (rme32->wcreg & RME32_WCR_EMP) {
		snd_iprintf(buffer, "  emphasis: on\n");
	} else {
		snd_iprintf(buffer, "  emphasis: off\n");
	}
}

static void __devinit snd_rme32_proc_init(rme32_t * rme32)
{
	snd_info_entry_t *entry;

	if (! snd_card_proc_new(rme32->card, "rme32", &entry))
		snd_info_set_text_ops(entry, rme32, 1024, snd_rme32_proc_read);
}

/*
 * control interface
 */

static int
snd_rme32_info_loopback_control(snd_kcontrol_t * kcontrol,
				snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}
static int
snd_rme32_get_loopback_control(snd_kcontrol_t * kcontrol,
			       snd_ctl_elem_value_t * ucontrol)
{
	rme32_t *rme32 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&rme32->lock, flags);
	ucontrol->value.integer.value[0] =
	    rme32->wcreg & RME32_WCR_SEL ? 0 : 1;
	spin_unlock_irqrestore(&rme32->lock, flags);
	return 0;
}
static int
snd_rme32_put_loopback_control(snd_kcontrol_t * kcontrol,
			       snd_ctl_elem_value_t * ucontrol)
{
	rme32_t *rme32 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;

	val = ucontrol->value.integer.value[0] ? 0 : RME32_WCR_SEL;
	spin_lock_irqsave(&rme32->lock, flags);
	val = (rme32->wcreg & ~RME32_WCR_SEL) | val;
	change = val != rme32->wcreg;
	if (ucontrol->value.integer.value[0])
		val &= ~RME32_WCR_MUTE;
	else
		val |= RME32_WCR_MUTE;
	writel(rme32->wcreg =
	       val, rme32->iobase + RME32_IO_CONTROL_REGISTER);
	spin_unlock_irqrestore(&rme32->lock, flags);
	return change;
}

static int
snd_rme32_info_inputtype_control(snd_kcontrol_t * kcontrol,
				 snd_ctl_elem_info_t * uinfo)
{
	rme32_t *rme32 = _snd_kcontrol_chip(kcontrol);
	static char *texts[4] = { "Optical", "Coaxial", "Internal", "XLR" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	switch (rme32->pci->device) {
	case PCI_DEVICE_ID_DIGI32:
	case PCI_DEVICE_ID_DIGI32_8:
		uinfo->value.enumerated.items = 3;
		break;
	case PCI_DEVICE_ID_DIGI32_PRO:
		uinfo->value.enumerated.items = 4;
		break;
	default:
		snd_BUG();
		break;
	}
	if (uinfo->value.enumerated.item >
	    uinfo->value.enumerated.items - 1) {
		uinfo->value.enumerated.item =
		    uinfo->value.enumerated.items - 1;
	}
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}
static int
snd_rme32_get_inputtype_control(snd_kcontrol_t * kcontrol,
				snd_ctl_elem_value_t * ucontrol)
{
	rme32_t *rme32 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int items = 3;

	spin_lock_irqsave(&rme32->lock, flags);
	ucontrol->value.enumerated.item[0] = snd_rme32_getinputtype(rme32);

	switch (rme32->pci->device) {
	case PCI_DEVICE_ID_DIGI32:
	case PCI_DEVICE_ID_DIGI32_8:
		items = 3;
		break;
	case PCI_DEVICE_ID_DIGI32_PRO:
		items = 4;
		break;
	default:
		snd_BUG();
		break;
	}
	if (ucontrol->value.enumerated.item[0] >= items) {
		ucontrol->value.enumerated.item[0] = items - 1;
	}

	spin_unlock_irqrestore(&rme32->lock, flags);
	return 0;
}
static int
snd_rme32_put_inputtype_control(snd_kcontrol_t * kcontrol,
				snd_ctl_elem_value_t * ucontrol)
{
	rme32_t *rme32 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change, items = 3;

	switch (rme32->pci->device) {
	case PCI_DEVICE_ID_DIGI32:
	case PCI_DEVICE_ID_DIGI32_8:
		items = 3;
		break;
	case PCI_DEVICE_ID_DIGI32_PRO:
		items = 4;
		break;
	default:
		snd_BUG();
		break;
	}
	val = ucontrol->value.enumerated.item[0] % items;

	spin_lock_irqsave(&rme32->lock, flags);
	change = val != (unsigned int)snd_rme32_getinputtype(rme32);
	snd_rme32_setinputtype(rme32, val);
	spin_unlock_irqrestore(&rme32->lock, flags);
	return change;
}

static int
snd_rme32_info_clockmode_control(snd_kcontrol_t * kcontrol,
				 snd_ctl_elem_info_t * uinfo)
{
	static char *texts[4] = { "AutoSync", 
				  "Internal 32.0kHz", 
				  "Internal 44.1kHz", 
				  "Internal 48.0kHz" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3) {
		uinfo->value.enumerated.item = 3;
	}
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}
static int
snd_rme32_get_clockmode_control(snd_kcontrol_t * kcontrol,
				snd_ctl_elem_value_t * ucontrol)
{
	rme32_t *rme32 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;

	spin_lock_irqsave(&rme32->lock, flags);
	ucontrol->value.enumerated.item[0] = snd_rme32_getclockmode(rme32);
	spin_unlock_irqrestore(&rme32->lock, flags);
	return 0;
}
static int
snd_rme32_put_clockmode_control(snd_kcontrol_t * kcontrol,
				snd_ctl_elem_value_t * ucontrol)
{
	rme32_t *rme32 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned int val;
	int change;

	val = ucontrol->value.enumerated.item[0] % 3;
	spin_lock_irqsave(&rme32->lock, flags);
	change = val != (unsigned int)snd_rme32_getclockmode(rme32);
	snd_rme32_setclockmode(rme32, val);
	spin_unlock_irqrestore(&rme32->lock, flags);
	return change;
}

static u32 snd_rme32_convert_from_aes(snd_aes_iec958_t * aes)
{
	u32 val = 0;
	val |= (aes->status[0] & IEC958_AES0_PROFESSIONAL) ? RME32_WCR_PRO : 0;
	if (val & RME32_WCR_PRO)
		val |= (aes->status[0] & IEC958_AES0_PRO_EMPHASIS_5015) ? RME32_WCR_EMP : 0;
	else
		val |= (aes->status[0] & IEC958_AES0_CON_EMPHASIS_5015) ? RME32_WCR_EMP : 0;
	return val;
}

static void snd_rme32_convert_to_aes(snd_aes_iec958_t * aes, u32 val)
{
	aes->status[0] = ((val & RME32_WCR_PRO) ? IEC958_AES0_PROFESSIONAL : 0);
	if (val & RME32_WCR_PRO)
		aes->status[0] |= (val & RME32_WCR_EMP) ? IEC958_AES0_PRO_EMPHASIS_5015 : 0;
	else
		aes->status[0] |= (val & RME32_WCR_EMP) ? IEC958_AES0_CON_EMPHASIS_5015 : 0;
}

static int snd_rme32_control_spdif_info(snd_kcontrol_t * kcontrol,
					snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_rme32_control_spdif_get(snd_kcontrol_t * kcontrol,
				       snd_ctl_elem_value_t * ucontrol)
{
	rme32_t *rme32 = _snd_kcontrol_chip(kcontrol);

	snd_rme32_convert_to_aes(&ucontrol->value.iec958,
				 rme32->wcreg_spdif);
	return 0;
}

static int snd_rme32_control_spdif_put(snd_kcontrol_t * kcontrol,
				       snd_ctl_elem_value_t * ucontrol)
{
	rme32_t *rme32 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	u32 val;

	val = snd_rme32_convert_from_aes(&ucontrol->value.iec958);
	spin_lock_irqsave(&rme32->lock, flags);
	change = val != rme32->wcreg_spdif;
	rme32->wcreg_spdif = val;
	spin_unlock_irqrestore(&rme32->lock, flags);
	return change;
}

static int snd_rme32_control_spdif_stream_info(snd_kcontrol_t * kcontrol,
					       snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_rme32_control_spdif_stream_get(snd_kcontrol_t * kcontrol,
					      snd_ctl_elem_value_t *
					      ucontrol)
{
	rme32_t *rme32 = _snd_kcontrol_chip(kcontrol);

	snd_rme32_convert_to_aes(&ucontrol->value.iec958,
				 rme32->wcreg_spdif_stream);
	return 0;
}

static int snd_rme32_control_spdif_stream_put(snd_kcontrol_t * kcontrol,
					      snd_ctl_elem_value_t *
					      ucontrol)
{
	rme32_t *rme32 = _snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	u32 val;

	val = snd_rme32_convert_from_aes(&ucontrol->value.iec958);
	spin_lock_irqsave(&rme32->lock, flags);
	change = val != rme32->wcreg_spdif_stream;
	rme32->wcreg_spdif_stream = val;
	rme32->wcreg &= ~(RME32_WCR_PRO | RME32_WCR_EMP);
	writel(rme32->wcreg |= val, rme32->iobase + RME32_IO_CONTROL_REGISTER);
	spin_unlock_irqrestore(&rme32->lock, flags);
	return change;
}

static int snd_rme32_control_spdif_mask_info(snd_kcontrol_t * kcontrol,
					     snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_rme32_control_spdif_mask_get(snd_kcontrol_t * kcontrol,
					    snd_ctl_elem_value_t *
					    ucontrol)
{
	ucontrol->value.iec958.status[0] = kcontrol->private_value;
	return 0;
}

static snd_kcontrol_new_t snd_rme32_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name =	SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.info =	snd_rme32_control_spdif_info,
		.get =	snd_rme32_control_spdif_get,
		.put =	snd_rme32_control_spdif_put
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name =	SNDRV_CTL_NAME_IEC958("", PLAYBACK, PCM_STREAM),
		.info =	snd_rme32_control_spdif_stream_info,
		.get =	snd_rme32_control_spdif_stream_get,
		.put =	snd_rme32_control_spdif_stream_put
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name =	SNDRV_CTL_NAME_IEC958("", PLAYBACK, CON_MASK),
		.info =	snd_rme32_control_spdif_mask_info,
		.get =	snd_rme32_control_spdif_mask_get,
		.private_value = IEC958_AES0_PROFESSIONAL | IEC958_AES0_CON_EMPHASIS
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name =	SNDRV_CTL_NAME_IEC958("", PLAYBACK, PRO_MASK),
		.info =	snd_rme32_control_spdif_mask_info,
		.get =	snd_rme32_control_spdif_mask_get,
		.private_value = IEC958_AES0_PROFESSIONAL | IEC958_AES0_PRO_EMPHASIS
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name =	"Input Connector",
		.info =	snd_rme32_info_inputtype_control,
		.get =	snd_rme32_get_inputtype_control,
		.put =	snd_rme32_put_inputtype_control
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name =	"Loopback Input",
		.info =	snd_rme32_info_loopback_control,
		.get =	snd_rme32_get_loopback_control,
		.put =	snd_rme32_put_loopback_control
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name =	"Sample Clock Source",
		.info =	snd_rme32_info_clockmode_control,
		.get =	snd_rme32_get_clockmode_control,
		.put =	snd_rme32_put_clockmode_control
	}
};

static int snd_rme32_create_switches(snd_card_t * card, rme32_t * rme32)
{
	int idx, err;
	snd_kcontrol_t *kctl;

	for (idx = 0; idx < 7; idx++) {
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_rme32_controls[idx], rme32))) < 0)
			return err;
		if (idx == 1)	/* IEC958 (S/PDIF) Stream */
			rme32->spdif_ctl = kctl;
	}

	return 0;
}

/*
 * Card initialisation
 */

static void snd_rme32_card_free(snd_card_t * card)
{
	snd_rme32_free(card->private_data);
}

static int __devinit
snd_rme32_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	static int dev;
	rme32_t *rme32;
	snd_card_t *card;
	int err;

	for (; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev]) {
			dev++;
			return -ENOENT;
		}
		break;
	}
	if (dev >= SNDRV_CARDS) {
		return -ENODEV;
	}
	if ((card = snd_card_new(index[dev], id[dev], THIS_MODULE,
				 sizeof(rme32_t))) == NULL)
		return -ENOMEM;
	card->private_free = snd_rme32_card_free;
	rme32 = (rme32_t *) card->private_data;
	rme32->card = card;
	rme32->pci = pci;
	snd_card_set_dev(card, &pci->dev);
	if ((err = snd_rme32_create(rme32)) < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->driver, "Digi32");
	switch (rme32->pci->device) {
	case PCI_DEVICE_ID_DIGI32:
		strcpy(card->shortname, "RME Digi32");
		break;
	case PCI_DEVICE_ID_DIGI32_8:
		strcpy(card->shortname, "RME Digi32/8");
		break;
	case PCI_DEVICE_ID_DIGI32_PRO:
		strcpy(card->shortname, "RME Digi32 PRO");
		break;
	}
	sprintf(card->longname, "%s (Rev. %d) at 0x%lx, irq %d",
		card->shortname, rme32->rev, rme32->port, rme32->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_rme32_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name =		"RME Digi32",
	.id_table =	snd_rme32_ids,
	.probe =	snd_rme32_probe,
	.remove =	__devexit_p(snd_rme32_remove),
};

static int __init alsa_card_rme32_init(void)
{
	return pci_module_init(&driver);
}

static void __exit alsa_card_rme32_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_rme32_init)
module_exit(alsa_card_rme32_exit)
