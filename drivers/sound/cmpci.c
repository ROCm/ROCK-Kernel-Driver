/*****************************************************************************/

/*
 *      cmpci.c  --  C-Media PCI audio driver.
 *
 *      Copyright (C) 1999  ChenLi Tien (cltien@home.com)
 *
 *	Based on the PCI drivers by Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Special thanks to David C. Niemi, Jan Pfeifer
 *
 *
 * Module command line parameters:
 *   none so far
 *
 *
 *  Supported devices:
 *  /dev/dsp    standard /dev/dsp device, (mostly) OSS compatible
 *  /dev/mixer  standard /dev/mixer device, (mostly) OSS compatible
 *  /dev/midi   simple MIDI UART interface, no ioctl
 *
 *  The card has both an FM and a Wavetable synth, but I have to figure
 *  out first how to drive them...
 *
 *  Revision history
 *    06.05.98   0.1   Initial release
 *    10.05.98   0.2   Fixed many bugs, esp. ADC rate calculation
 *                     First stab at a simple midi interface (no bells&whistles)
 *    13.05.98   0.3   Fix stupid cut&paste error: set_adc_rate was called instead of
 *                     set_dac_rate in the FMODE_WRITE case in cm_open
 *                     Fix hwptr out of bounds (now mpg123 works)
 *    14.05.98   0.4   Don't allow excessive interrupt rates
 *    08.06.98   0.5   First release using Alan Cox' soundcore instead of miscdevice
 *    03.08.98   0.6   Do not include modversions.h
 *                     Now mixer behaviour can basically be selected between
 *                     "OSS documented" and "OSS actual" behaviour
 *    31.08.98   0.7   Fix realplayer problems - dac.count issues
 *    10.12.98   0.8   Fix drain_dac trying to wait on not yet initialized DMA
 *    16.12.98   0.9   Fix a few f_file & FMODE_ bugs
 *    06.01.99   0.10  remove the silly SA_INTERRUPT flag.
 *                     hopefully killed the egcs section type conflict
 *    12.03.99   0.11  cinfo.blocks should be reset after GETxPTR ioctl.
 *                     reported by Johan Maes <joma@telindus.be>
 *    22.03.99   0.12  return EAGAIN instead of EBUSY when O_NONBLOCK
 *                     read/write cannot be executed
 *    20 09 99   0.13  merged the generic changes in sonicvibes since this
 *		       diverged.
 *    18.08.99   1.5   Only deallocate DMA buffer when unloading.
 *    02.09.99   1.6   Enable SPDIF LOOP
 *                     Change the mixer read back
 *    21.09.99   2.33  Use RCS version as driver version.
 *                     Add support for modem, S/PDIF loop and 4 channels.
 *                     (8738 only)
 *                     Fix bug cause x11amp cannot play.
 *    $Log: cmpci.c,v $
 *    Revision 2.41  1999/10/27 02:00:05  cltien
 *    Now the fragsize for modem is activated by parameter.
 *
 *    Revision 2.40  1999/10/26 23:38:26  cltien
 *    Remove debugging message in cm_write which may cause module counter not 0.
 *
 *    Revision 2.39  1999/10/26 21:52:50  cltien
 *    I forgor too adjust mic recording volume, as it should be moved to 5MUTEMONO.
 *    Change the DYNAMIC macro to FIXEDDMA, which means static DMA buffer.
 *
 *    Revision 2.38  1999/10/08 21:59:03  cltien
 *    Set FLINKON and reset FLINKOFF for modem.
 *
 *    Revision 2.37  1999/09/28 02:57:04  cltien
 *    Add set_bus_master() to make sure bus master enabled.
 *
 *    Revision 2.36  1999/09/22 14:15:03  cltien
 *    Use open_sem to avoid multiple access to open_mode.
 *    Use wakeup in IntrClose to activate process in waiting queue.
 *
 *    Revision 2.35  1999/09/22 13:20:53  cltien
 *    Use open_mode to check if DAC in used. Also more check in IntrWrite and IntrClose. Now the modem can access DAC safely.
 *
 *    Revision 2.34  1999/09/22 03:29:57  cltien
 *    Use module count to decide which one to access the dac.
 *
 *
 */
 
/*****************************************************************************/
      
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/malloc.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <linux/wrapper.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>

#include "dm.h"

/* --------------------------------------------------------------------- */

#undef OSS_DOCUMENTED_MIXER_SEMANTICS

/* --------------------------------------------------------------------- */

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

#define CM_MAGIC  ((PCI_VENDOR_ID_CMEDIA<<16)|PCI_DEVICE_ID_CMEDIA_CM8338A)

/*
 * CM8338 registers definition
 */

#define CODEC_CMI_FUNCTRL0      (0x00)
#define CODEC_CMI_FUNCTRL1      (0x04)
#define CODEC_CMI_CHFORMAT      (0x08)
#define CODEC_CMI_INT_HLDCLR    (0x0C)
#define CODEC_CMI_INT_STATUS    (0x10)
#define CODEC_CMI_LEGACY_CTRL   (0x14)
#define CODEC_CMI_MISC_CTRL     (0x18)
#define CODEC_CMI_TDMA_POS      (0x1C)
#define CODEC_CMI_MIXER         (0x20)
#define CODEC_SB16_DATA         (0x22)
#define CODEC_SB16_ADDR         (0x23)
#define CODEC_CMI_MIXER1        (0x24)
#define CODEC_CMI_MIXER2        (0x25)
#define CODEC_CMI_AUX_VOL       (0x26)
#define CODEC_CMI_MISC          (0x27)
#define CODEC_CMI_AC97          (0x28)

#define CODEC_CMI_CH0_FRAME1    (0x80)
#define CODEC_CMI_CH0_FRAME2    (0x84)
#define CODEC_CMI_CH1_FRAME1    (0x88)
#define CODEC_CMI_CH1_FRAME2    (0x8C)

#define CODEC_CMI_EXT_REG       (0xF0)
#define UCHAR	unsigned char
/*
**  Mixer registers for SB16
*/

#define DSP_MIX_DATARESETIDX    ((UCHAR)(0x00))

#define DSP_MIX_MASTERVOLIDX_L  ((UCHAR)(0x30))
#define DSP_MIX_MASTERVOLIDX_R  ((UCHAR)(0x31))
#define DSP_MIX_VOICEVOLIDX_L   ((UCHAR)(0x32))
#define DSP_MIX_VOICEVOLIDX_R   ((UCHAR)(0x33))
#define DSP_MIX_FMVOLIDX_L      ((UCHAR)(0x34))
#define DSP_MIX_FMVOLIDX_R      ((UCHAR)(0x35))
#define DSP_MIX_CDVOLIDX_L      ((UCHAR)(0x36))
#define DSP_MIX_CDVOLIDX_R      ((UCHAR)(0x37))
#define DSP_MIX_LINEVOLIDX_L    ((UCHAR)(0x38))
#define DSP_MIX_LINEVOLIDX_R    ((UCHAR)(0x39))

#define DSP_MIX_MICVOLIDX       ((UCHAR)(0x3A))
#define DSP_MIX_SPKRVOLIDX      ((UCHAR)(0x3B))

#define DSP_MIX_OUTMIXIDX       ((UCHAR)(0x3C))

#define DSP_MIX_ADCMIXIDX_L     ((UCHAR)(0x3D))
#define DSP_MIX_ADCMIXIDX_R     ((UCHAR)(0x3E))

#define DSP_MIX_INGAINIDX_L     ((UCHAR)(0x3F))
#define DSP_MIX_INGAINIDX_R     ((UCHAR)(0x40))
#define DSP_MIX_OUTGAINIDX_L    ((UCHAR)(0x41))
#define DSP_MIX_OUTGAINIDX_R    ((UCHAR)(0x42))

#define DSP_MIX_AGCIDX          ((UCHAR)(0x43))

#define DSP_MIX_TREBLEIDX_L     ((UCHAR)(0x44))
#define DSP_MIX_TREBLEIDX_R     ((UCHAR)(0x45))
#define DSP_MIX_BASSIDX_L       ((UCHAR)(0x46))
#define DSP_MIX_BASSIDX_R       ((UCHAR)(0x47))
#define CM_CH0_RESET	  0x04
#define CM_CH1_RESET	  0x08
#define CM_EXTENT_CODEC	  0x100
#define CM_EXTENT_MIDI	  0x2
#define CM_EXTENT_SYNTH	  0x4
#define CM_INT_CH0	  1
#define CM_INT_CH1	  2

#define CM_CFMT_STEREO     0x01
#define CM_CFMT_16BIT      0x02
#define CM_CFMT_MASK       0x03
#define CM_CFMT_DACSHIFT   0   
#define CM_CFMT_ADCSHIFT   2

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

#define CM_CENABLE_RE      0x2
#define CM_CENABLE_PE      0x1


/* MIDI buffer sizes */

#define MIDIINBUF  256
#define MIDIOUTBUF 256

#define FMODE_MIDI_SHIFT 2
#define FMODE_MIDI_READ  (FMODE_READ << FMODE_MIDI_SHIFT)
#define FMODE_MIDI_WRITE (FMODE_WRITE << FMODE_MIDI_SHIFT)

#define FMODE_DMFM 0x10

/* --------------------------------------------------------------------- */

struct cm_state {
	/* magic */
	unsigned int magic;

	/* we keep cm cards in a linked list */
	struct cm_state *next;

	/* soundcore stuff */
	int dev_audio;
	int dev_mixer;
	int dev_midi;
	int dev_dmfm;

	/* hardware resources */
	unsigned int iosb, iobase, iosynth, iomidi, iogame, irq;

        /* mixer stuff */
        struct {
                unsigned int modcnt;
#ifndef OSS_DOCUMENTED_MIXER_SEMANTICS
		unsigned short vol[13];
#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */
        } mix;

	/* wave stuff */
	unsigned int rateadc, ratedac;
	unsigned char fmt, enable;

	spinlock_t lock;
	struct semaphore open_sem;
	mode_t open_mode;
	wait_queue_head_t open_wait;

	struct dmabuf {
		void *rawbuf;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;
		unsigned hwptr, swptr;
		unsigned total_bytes;
		int count;
		unsigned error; /* over/underrun */
		wait_queue_head_t wait;
		/* redundant, but makes calculations easier */
		unsigned fragsize;
		unsigned dmasize;
		unsigned fragsamples;
		unsigned dmasamples;
		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned endcleared:1;
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;
	} dma_dac, dma_adc;

	/* midi stuff */
	struct {
		unsigned ird, iwr, icnt;
		unsigned ord, owr, ocnt;
		wait_queue_head_t iwait;
		wait_queue_head_t owait;
		struct timer_list timer;
		unsigned char ibuf[MIDIINBUF];
		unsigned char obuf[MIDIOUTBUF];
	} midi;
};

/* --------------------------------------------------------------------- */

static struct cm_state *devs = NULL;
static unsigned long wavetable_mem = 0;

/* --------------------------------------------------------------------- */

extern __inline__ unsigned ld2(unsigned int x)
{
	unsigned r = 0;
	
	if (x >= 0x10000) {
		x >>= 16;
		r += 16;
	}
	if (x >= 0x100) {
		x >>= 8;
		r += 8;
	}
	if (x >= 0x10) {
		x >>= 4;
		r += 4;
	}
	if (x >= 4) {
		x >>= 2;
		r += 2;
	}
	if (x >= 2)
		r++;
	return r;
}

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#ifdef hweight32
#undef hweight32
#endif

extern __inline__ unsigned int hweight32(unsigned int w)
{
        unsigned int res = (w & 0x55555555) + ((w >> 1) & 0x55555555);
        res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
        res = (res & 0x0F0F0F0F) + ((res >> 4) & 0x0F0F0F0F);
        res = (res & 0x00FF00FF) + ((res >> 8) & 0x00FF00FF);
        return (res & 0x0000FFFF) + ((res >> 16) & 0x0000FFFF);
}

/* --------------------------------------------------------------------- */

static void set_dmadac(struct cm_state *s, unsigned int addr, unsigned int count)
{
	count--;
	outl(addr, s->iobase + CODEC_CMI_CH0_FRAME1);
	outw(count, s->iobase + CODEC_CMI_CH0_FRAME2);
	outb(inb(s->iobase + CODEC_CMI_FUNCTRL0) & ~1, s->iobase + CODEC_CMI_FUNCTRL0);
//	outb(inb(s->iobase + CODEC_CMI_FUNCTRL0 + 2) | 1, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
}

static void set_dmaadc(struct cm_state *s, unsigned int addr, unsigned int count)
{
	count--;
	outl(addr, s->iobase + CODEC_CMI_CH1_FRAME1);
	outw(count, s->iobase + CODEC_CMI_CH1_FRAME2);
	outb(inb(s->iobase + CODEC_CMI_FUNCTRL0) | 2, s->iobase + CODEC_CMI_FUNCTRL0);
//	outb(inb(s->iobase + CODEC_CMI_FUNCTRL0 + 2) | 2, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
}

extern __inline__ unsigned get_dmadac(struct cm_state *s)
{
	unsigned int curr_addr;

	if (!s->dma_dac.dmasize || !(s->enable & CM_CENABLE_PE))
		return 0;

	curr_addr = inl(s->iobase + CODEC_CMI_CH0_FRAME1);
	curr_addr -= virt_to_bus(s->dma_dac.rawbuf);
	curr_addr = s->dma_dac.dmasize - curr_addr;
	curr_addr &= ~(sample_size[(s->fmt >> CM_CFMT_DACSHIFT) & CM_CFMT_MASK]-1);
	return curr_addr;
}

extern __inline__ unsigned get_dmaadc(struct cm_state *s)
{
	unsigned int curr_addr;

	if (!s->dma_adc.dmasize || !(s->enable & CM_CENABLE_RE))
		return 0;

	curr_addr = inl(s->iobase + CODEC_CMI_CH1_FRAME1);
	curr_addr -= virt_to_bus(s->dma_adc.rawbuf);
	curr_addr = s->dma_adc.dmasize - curr_addr;
	curr_addr &= ~(sample_size[(s->fmt >> CM_CFMT_ADCSHIFT) & CM_CFMT_MASK]-1);
	return curr_addr;
}

static void wrmixer(struct cm_state *s, unsigned char idx, unsigned char data)
{
	outb(idx, s->iobase + CODEC_SB16_ADDR);
	udelay(10);
	outb(data, s->iobase + CODEC_SB16_DATA);
	udelay(10);
}

static unsigned char rdmixer(struct cm_state *s, unsigned char idx)
{
	unsigned char v;

	outb(idx, s->iobase + CODEC_SB16_ADDR);
	udelay(10);
	v = inb(s->iobase + CODEC_SB16_DATA);
	udelay(10);
	return v;
}

static void set_fmt(struct cm_state *s, unsigned char mask, unsigned char data)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if (mask) {
		s->fmt = inb(s->iobase + CODEC_CMI_CHFORMAT);
		udelay(10);
	}
	s->fmt = (s->fmt & mask) | data;
	outb(s->fmt, s->iobase + CODEC_CMI_CHFORMAT);
	spin_unlock_irqrestore(&s->lock, flags);
	udelay(10);
}

static void frobindir(struct cm_state *s, unsigned char idx, unsigned char mask, unsigned char data)
{
	outb(idx, s->iobase + CODEC_SB16_ADDR);
	udelay(10);
	outb((inb(s->iobase + CODEC_SB16_DATA) & mask) | data, s->iobase + CODEC_SB16_DATA);
	udelay(10);
}

static struct {
	unsigned	rate;
	unsigned	lower;
	unsigned	upper;
	unsigned char	freq;
} rate_lookup[] =
{
	{ 5512,		(0 + 5512) / 2,		(5512 + 8000) / 2,	0 },
	{ 8000,		(5512 + 8000) / 2,	(8000 + 11025) / 2,	4 },
	{ 11025,	(8000 + 11025) / 2,	(11025 + 16000) / 2,	1 },
	{ 16000,	(11025 + 16000) / 2,	(16000 + 22050) / 2,	5 },
	{ 22050,	(16000 + 22050) / 2,	(22050 + 32000) / 2,	2 },
	{ 32000,	(22050 + 32000) / 2,	(32000 + 44100) / 2,	6 },
	{ 44100,	(32000 + 44100) / 2,	(44100 + 48000) / 2,	3 },
	{ 48000,	(44100 + 48000) /2,	48000,			7 }
};

static void set_dac_rate(struct cm_state *s, unsigned rate)
{
	unsigned long flags;
	unsigned char freq = 4, val;
	int	i;

	if (rate > 48000)
		rate = 48000;
	if (rate < 5512)
		rate = 5512;
	for (i = 0; i < sizeof(rate_lookup) / sizeof(rate_lookup[0]); i++)
	{
		if (rate > rate_lookup[i].lower && rate <= rate_lookup[i].upper)
		{
			rate = rate_lookup[i].rate;
			freq = rate_lookup[i].freq;
			break;
	    	}
	}
	s->ratedac = rate;
	freq <<= 2;
	spin_lock_irqsave(&s->lock, flags);
	val = inb(s->iobase + CODEC_CMI_FUNCTRL1 + 1) & ~0x1c; 
	outb(val | freq, s->iobase + CODEC_CMI_FUNCTRL1 + 1);
	spin_unlock_irqrestore(&s->lock, flags);
}

static void set_adc_rate(struct cm_state *s, unsigned rate)
{
	unsigned long flags;
	unsigned char freq = 4, val;
	int	i;

	if (rate > 48000)
		rate = 48000;
	if (rate < 5512)
		rate = 5512;
	for (i = 0; i < sizeof(rate_lookup) / sizeof(rate_lookup[0]); i++)
	{
		if (rate > rate_lookup[i].lower && rate <= rate_lookup[i].upper)
		{
			rate = rate_lookup[i].rate;
			freq = rate_lookup[i].freq;
			break;
	    	}
	}
	s->rateadc = rate;
	freq <<= 5;
	spin_lock_irqsave(&s->lock, flags);
	val = inb(s->iobase + CODEC_CMI_FUNCTRL1 + 1) & ~0xe0; 
	outb(val | freq, s->iobase + CODEC_CMI_FUNCTRL1 + 1);
	spin_unlock_irqrestore(&s->lock, flags);
}

/* --------------------------------------------------------------------- */

extern inline void stop_adc(struct cm_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	/* disable channel */
	outb(s->enable, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
	s->enable &= ~CM_CENABLE_RE;
	/* disable interrupt */
	outb(inb(s->iobase + CODEC_CMI_INT_HLDCLR + 2) & ~2, s->iobase + CODEC_CMI_INT_HLDCLR + 2);
	/* reset */
	outb(s->enable | CM_CH1_RESET, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
	udelay(10);
	outb(s->enable & ~CM_CH1_RESET, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
	spin_unlock_irqrestore(&s->lock, flags);
}	

extern inline void stop_dac(struct cm_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	/* disable channel */
	s->enable &= ~CM_CENABLE_PE;
	outb(s->enable, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
	/* disable interrupt */
	outb(inb(s->iobase + CODEC_CMI_INT_HLDCLR + 2) & ~1, s->iobase + CODEC_CMI_INT_HLDCLR + 2);
	/* reset */
	outb(s->enable | CM_CH0_RESET, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
	udelay(10);
	outb(s->enable & ~CM_CH0_RESET, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_dac(struct cm_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if ((s->dma_dac.mapped || s->dma_dac.count > 0) && s->dma_dac.ready) {
		outb(inb(s->iobase + CODEC_CMI_INT_HLDCLR + 2) | 1, s->iobase + CODEC_CMI_INT_HLDCLR + 2);
		s->enable |= CM_CENABLE_PE;
		outb(s->enable, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

static void start_adc(struct cm_state *s)
{
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if ((s->dma_adc.mapped || s->dma_adc.count < (signed)(s->dma_adc.dmasize - 2*s->dma_adc.fragsize)) 
	    && s->dma_adc.ready) {
		outb(inb(s->iobase + CODEC_CMI_INT_HLDCLR + 2) | 2, s->iobase + CODEC_CMI_INT_HLDCLR + 2);
		s->enable |= CM_CENABLE_RE;
		outb(s->enable, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
	}
	spin_unlock_irqrestore(&s->lock, flags);
}	

/* --------------------------------------------------------------------- */

#define DMABUF_DEFAULTORDER (16-PAGE_SHIFT)
#define DMABUF_MINORDER 1

static void dealloc_dmabuf(struct dmabuf *db)
{
	struct page *pstart, *pend;

	if (db->rawbuf) {
		/* undo marking the pages as reserved */
		pend = virt_to_page(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
		for (pstart = virt_to_page(db->rawbuf); pstart <= pend; pstart++)
			mem_map_unreserve(pstart);
		free_pages((unsigned long)db->rawbuf, db->buforder);
	}
	db->rawbuf = NULL;
	db->mapped = db->ready = 0;
}


/* Ch0 is used for playback, Ch1 is used for recording */

static int prog_dmabuf(struct cm_state *s, unsigned rec)
{
	struct dmabuf *db = rec ? &s->dma_adc : &s->dma_dac;
	unsigned rate = rec ? s->rateadc : s->ratedac;
	int order;
	unsigned bytepersec;
	unsigned bufs;
	struct page *pstart, *pend;
	unsigned char fmt;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	fmt = s->fmt;
	if (rec) {
		s->enable &= ~CM_CENABLE_RE;
		fmt >>= CM_CFMT_ADCSHIFT;
	} else {
		s->enable &= ~CM_CENABLE_PE;
		fmt >>= CM_CFMT_DACSHIFT;
	}
	outb(s->enable, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
	spin_unlock_irqrestore(&s->lock, flags);
	fmt &= CM_CFMT_MASK;
	db->hwptr = db->swptr = db->total_bytes = db->count = db->error = db->endcleared = 0;
	if (!db->rawbuf) {
		db->ready = db->mapped = 0;
		for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER; order--)
			if ((db->rawbuf = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA, order)))
				break;
		if (!db->rawbuf)
			return -ENOMEM;
		db->buforder = order;
		if ((virt_to_bus(db->rawbuf) ^ (virt_to_bus(db->rawbuf) + (PAGE_SIZE << db->buforder) - 1)) & ~0xffff)
			printk(KERN_DEBUG "cmpci: DMA buffer crosses 64k boundary: busaddr 0x%lx  size %ld\n", 
			       virt_to_bus(db->rawbuf), PAGE_SIZE << db->buforder);
		if ((virt_to_bus(db->rawbuf) + (PAGE_SIZE << db->buforder) - 1) & ~0xffffff)
			printk(KERN_DEBUG "cmpci: DMA buffer beyond 16MB: busaddr 0x%lx  size %ld\n", 
			       virt_to_bus(db->rawbuf), PAGE_SIZE << db->buforder);
		/* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
		pend = virt_to_page(db->rawbuf + (PAGE_SIZE << db->buforder) - 1);
		for (pstart = virt_to_page(db->rawbuf); pstart <= pend; pstart++)
			mem_map_reserve(pstart);
	}
	bytepersec = rate << sample_shift[fmt];
	bufs = PAGE_SIZE << db->buforder;
	if (db->ossfragshift) {
		if ((1000 << db->ossfragshift) < bytepersec)
			db->fragshift = ld2(bytepersec/1000);
		else
			db->fragshift = db->ossfragshift;
	} else {
		db->fragshift = ld2(bytepersec/100/(db->subdivision ? db->subdivision : 1));
		if (db->fragshift < 3)
			db->fragshift = 3;
	}
	db->numfrag = bufs >> db->fragshift;
	while (db->numfrag < 4 && db->fragshift > 3) {
		db->fragshift--;
		db->numfrag = bufs >> db->fragshift;
	}
	db->fragsize = 1 << db->fragshift;
	if (db->ossmaxfrags >= 4 && db->ossmaxfrags < db->numfrag)
		db->numfrag = db->ossmaxfrags;
 	/* to make fragsize >= 4096 */
#if 0 	
 	if(s->modem)
 	{
	 	while (db->fragsize < 4096 && db->numfrag >= 4)
		{
			db->fragsize *= 2;
 			db->fragshift++;
 			db->numfrag /= 2;
 		}
	}
#endif	
	db->fragsamples = db->fragsize >> sample_shift[fmt];
	db->dmasize = db->numfrag << db->fragshift;
	db->dmasamples = db->dmasize >> sample_shift[fmt];
	memset(db->rawbuf, (fmt & CM_CFMT_16BIT) ? 0 : 0x80, db->dmasize);
	spin_lock_irqsave(&s->lock, flags);
	if (rec) {
		set_dmaadc(s, virt_to_bus(db->rawbuf), db->dmasize >> sample_shift[fmt]);
		/* program sample counts */
		outw(db->fragsamples-1, s->iobase + CODEC_CMI_CH1_FRAME2 + 2);
	} else {
		set_dmadac(s, virt_to_bus(db->rawbuf), db->dmasize >> sample_shift[fmt]);
		/* program sample counts */
		outw(db->fragsamples-1, s->iobase + CODEC_CMI_CH0_FRAME2 + 2);
	}
	spin_unlock_irqrestore(&s->lock, flags);
	db->ready = 1;
	return 0;
}

extern __inline__ void clear_advance(struct cm_state *s)
{
	unsigned char c = (s->fmt & (CM_CFMT_16BIT << CM_CFMT_DACSHIFT)) ? 0 : 0x80;
	unsigned char *buf = s->dma_dac.rawbuf;
	unsigned bsize = s->dma_dac.dmasize;
	unsigned bptr = s->dma_dac.swptr;
	unsigned len = s->dma_dac.fragsize;

	if (bptr + len > bsize) {
		unsigned x = bsize - bptr;
		memset(buf + bptr, c, x);
		bptr = 0;
		len -= x;
	}
	memset(buf + bptr, c, len);
	outb(s->enable, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
}

/* call with spinlock held! */
static void cm_update_ptr(struct cm_state *s)
{
	unsigned hwptr;
	int diff;

	/* update ADC pointer */
	if (s->dma_adc.ready) {
		hwptr = (s->dma_adc.dmasize - get_dmaadc(s)) % s->dma_adc.dmasize;
		diff = (s->dma_adc.dmasize + hwptr - s->dma_adc.hwptr) % s->dma_adc.dmasize;
		s->dma_adc.hwptr = hwptr;
		s->dma_adc.total_bytes += diff;
		s->dma_adc.count += diff;
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize) 
			wake_up(&s->dma_adc.wait);
		if (!s->dma_adc.mapped) {
			if (s->dma_adc.count > (signed)(s->dma_adc.dmasize - ((3 * s->dma_adc.fragsize) >> 1))) {
				s->enable &= ~CM_CENABLE_RE;
				outb(s->enable, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
				s->dma_adc.error++;
			}
		}
	}
	/* update DAC pointer */
	if (s->dma_dac.ready) {
		hwptr = (s->dma_dac.dmasize - get_dmadac(s)) % s->dma_dac.dmasize;
		diff = (s->dma_dac.dmasize + hwptr - s->dma_dac.hwptr) % s->dma_dac.dmasize;
		s->dma_dac.hwptr = hwptr;
		s->dma_dac.total_bytes += diff;
		if (s->dma_dac.mapped) {
			s->dma_dac.count += diff;
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize)
				wake_up(&s->dma_dac.wait);
		} else {
			s->dma_dac.count -= diff;
			if (s->dma_dac.count <= 0) {
				s->enable &= ~CM_CENABLE_PE;
				outb(s->enable, s->iobase + CODEC_CMI_FUNCTRL0 + 2);
				s->dma_dac.error++;
			} else if (s->dma_dac.count <= (signed)s->dma_dac.fragsize && !s->dma_dac.endcleared) {
				clear_advance(s);
				s->dma_dac.endcleared = 1;
			}
			if (s->dma_dac.count + (signed)s->dma_dac.fragsize <= (signed)s->dma_dac.dmasize)
				wake_up(&s->dma_dac.wait);
		}
	}
}

/* hold spinlock for the following! */
static void cm_handle_midi(struct cm_state *s)
{
	unsigned char ch;
	int wake;

	wake = 0;
	while (!(inb(s->iomidi+1) & 0x80)) {
		ch = inb(s->iomidi);
		if (s->midi.icnt < MIDIINBUF) {
			s->midi.ibuf[s->midi.iwr] = ch;
			s->midi.iwr = (s->midi.iwr + 1) % MIDIINBUF;
			s->midi.icnt++;
		}
		wake = 1;
	}
	if (wake)
		wake_up(&s->midi.iwait);
	wake = 0;
	while (!(inb(s->iomidi+1) & 0x40) && s->midi.ocnt > 0) {
		outb(s->midi.obuf[s->midi.ord], s->iomidi);
		s->midi.ord = (s->midi.ord + 1) % MIDIOUTBUF;
		s->midi.ocnt--;
		if (s->midi.ocnt < MIDIOUTBUF-16)
			wake = 1;
	}
	if (wake)
		wake_up(&s->midi.owait);
}

static void cm_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        struct cm_state *s = (struct cm_state *)dev_id;
	unsigned int intsrc, intstat;
	
	/* fastpath out, to ease interrupt sharing */
	intsrc = inl(s->iobase + CODEC_CMI_INT_STATUS);
	if (!(intsrc & 0x80000000))
		return;
	spin_lock(&s->lock);
	intstat = inb(s->iobase + CODEC_CMI_INT_HLDCLR + 2);
	/* acknowledge interrupt */
	if (intsrc & CM_INT_CH0)
	{
		outb(intstat & ~1, s->iobase + CODEC_CMI_INT_HLDCLR + 2);
		udelay(10);
		outb(intstat | 1, s->iobase + CODEC_CMI_INT_HLDCLR + 2);
	}
	if (intsrc & CM_INT_CH1)
	{
		outb(intstat & ~2, s->iobase + CODEC_CMI_INT_HLDCLR + 2);
		udelay(10);
		outb(intstat | 2, s->iobase + CODEC_CMI_INT_HLDCLR + 2);
	}
	cm_update_ptr(s);
	cm_handle_midi(s);
	spin_unlock(&s->lock);
}

static void cm_midi_timer(unsigned long data)
{
	struct cm_state *s = (struct cm_state *)data;
	unsigned long flags;
	
	spin_lock_irqsave(&s->lock, flags);
	cm_handle_midi(s);
	spin_unlock_irqrestore(&s->lock, flags);
	s->midi.timer.expires = jiffies+1;
	add_timer(&s->midi.timer);
}

/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT "cmpci: invalid magic value\n";

#ifdef CONFIG_SOUND_CMPCI	/* support multiple chips */
#define VALIDATE_STATE(s)
#else
#define VALIDATE_STATE(s)                         \
({                                                \
	if (!(s) || (s)->magic != CM_MAGIC) { \
		printk(invalid_magic);            \
		return -ENXIO;                    \
	}                                         \
})
#endif

/* --------------------------------------------------------------------- */

#define MT_4          1
#define MT_5MUTE      2
#define MT_4MUTEMONO  3
#define MT_6MUTE      4
#define MT_5MUTEMONO  5

static const struct {
	unsigned left;
	unsigned right;
	unsigned type;
	unsigned rec;
	unsigned play;
} mixtable[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_CD]     = { DSP_MIX_CDVOLIDX_L,     DSP_MIX_CDVOLIDX_R,     MT_5MUTE,     0x04, 0x02 },
	[SOUND_MIXER_LINE]   = { DSP_MIX_LINEVOLIDX_L,   DSP_MIX_LINEVOLIDX_R,   MT_5MUTE,     0x10, 0x08 },
	[SOUND_MIXER_MIC]    = { DSP_MIX_MICVOLIDX,      DSP_MIX_MICVOLIDX,      MT_5MUTEMONO, 0x01, 0x01 },
	[SOUND_MIXER_SYNTH]  = { DSP_MIX_FMVOLIDX_L,  	 DSP_MIX_FMVOLIDX_R,     MT_5MUTE,     0x40, 0x00 },
	[SOUND_MIXER_VOLUME] = { DSP_MIX_MASTERVOLIDX_L, DSP_MIX_MASTERVOLIDX_R, MT_5MUTE,     0x00, 0x00 },
	[SOUND_MIXER_PCM]    = { DSP_MIX_VOICEVOLIDX_L,  DSP_MIX_VOICEVOLIDX_R,  MT_5MUTE,     0x00, 0x00 }
};

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS

static int return_mixval(struct cm_state *s, unsigned i, int *arg)
{
	unsigned long flags;
	unsigned char l, r, rl, rr;

	spin_lock_irqsave(&s->lock, flags);
	l = rdmixer(s, mixtable[i].left);
	r = rdmixer(s, mixtable[i].right);
	spin_unlock_irqrestore(&s->lock, flags);
	switch (mixtable[i].type) {
	case MT_4:
		r &= 0xf;
		l &= 0xf;
		rl = 10 + 6 * (l & 15);
		rr = 10 + 6 * (r & 15);
		break;

	case MT_4MUTEMONO:
		rl = 55 - 3 * (l & 15);
		if (r & 0x10)
			rl += 45;
		rr = rl;
		r = l;
		break;

	case MT_5MUTEMONO:
		r = l;
		rl = 100 - 3 * ((l >> 3) & 31);
		rr = rl;
		break;
				
	case MT_5MUTE:
	default:
		rl = 100 - 3 * ((l >> 3) & 31);
		rr = 100 - 3 * ((r >> 3) & 31);
		break;
				
	case MT_6MUTE:
		rl = 100 - 3 * (l & 63) / 2;
		rr = 100 - 3 * (r & 63) / 2;
		break;
	}
	if (l & 0x80)
		rl = 0;
	if (r & 0x80)
		rr = 0;
	return put_user((rr << 8) | rl, arg);
}

#else /* OSS_DOCUMENTED_MIXER_SEMANTICS */

static const unsigned char volidx[SOUND_MIXER_NRDEVICES] = 
{
	[SOUND_MIXER_CD]     = 1,
	[SOUND_MIXER_LINE]   = 2,
	[SOUND_MIXER_MIC]    = 3,
	[SOUND_MIXER_SYNTH]  = 4,
	[SOUND_MIXER_VOLUME] = 5,
	[SOUND_MIXER_PCM]    = 6
};

#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */

static unsigned mixer_recmask(struct cm_state *s)
{
	unsigned long flags;
	int i, j, k;

	spin_lock_irqsave(&s->lock, flags);
	j = rdmixer(s, DSP_MIX_ADCMIXIDX_L);
	spin_unlock_irqrestore(&s->lock, flags);
	j &= 0x7f;
	for (k = i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		if (j & mixtable[i].rec)
			k |= 1 << i;
	return k;
}

static int mixer_ioctl(struct cm_state *s, unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	int i, val, j;
	unsigned char l, r, rl, rr;

	VALIDATE_STATE(s);
        if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		strncpy(info.id, "cmpci", sizeof(info.id));
		strncpy(info.name, "C-Media PCI", sizeof(info.name));
		info.modify_counter = s->mix.modcnt;
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		strncpy(info.id, "cmpci", sizeof(info.id));
		strncpy(info.name, "C-Media cmpci", sizeof(info.name));
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == OSS_GETVERSION)
		return put_user(SOUND_VERSION, (int *)arg);
	if (_IOC_TYPE(cmd) != 'M' || _SIOC_SIZE(cmd) != sizeof(int))
                return -EINVAL;
        if (_SIOC_DIR(cmd) == _SIOC_READ) {
                switch (_IOC_NR(cmd)) {
                case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
			return put_user(mixer_recmask(s), (int *)arg);
			
                case SOUND_MIXER_OUTSRC: /* Arg contains a bit for each recording source */
			return put_user(mixer_recmask(s), (int *)arg);//need fix
			
                case SOUND_MIXER_DEVMASK: /* Arg contains a bit for each supported device */
			for (val = i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				if (mixtable[i].type)
					val |= 1 << i;
			return put_user(val, (int *)arg);

                case SOUND_MIXER_RECMASK: /* Arg contains a bit for each supported recording source */
			for (val = i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				if (mixtable[i].rec)
					val |= 1 << i;
			return put_user(val, (int *)arg);
			
                case SOUND_MIXER_OUTMASK: /* Arg contains a bit for each supported recording source */
			for (val = i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				if (mixtable[i].play)
					val |= 1 << i;
			return put_user(val, (int *)arg);
			
                 case SOUND_MIXER_STEREODEVS: /* Mixer channels supporting stereo */
			for (val = i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				if (mixtable[i].type && mixtable[i].type != MT_4MUTEMONO)
					val |= 1 << i;
			return put_user(val, (int *)arg);
			
                case SOUND_MIXER_CAPS:
			return put_user(0, (int *)arg);

		default:
			i = _IOC_NR(cmd);
                        if (i >= SOUND_MIXER_NRDEVICES || !mixtable[i].type)
                                return -EINVAL;
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
			return return_mixval(s, i, (int *)arg);
#else /* OSS_DOCUMENTED_MIXER_SEMANTICS */
			if (!volidx[i])
				return -EINVAL;
			return put_user(s->mix.vol[volidx[i]-1], (int *)arg);
#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */
		}
	}
        if (_SIOC_DIR(cmd) != (_SIOC_READ|_SIOC_WRITE)) 
		return -EINVAL;
	s->mix.modcnt++;
	switch (_IOC_NR(cmd)) {
	case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
		if (get_user(val, (int *)arg))
			return -EFAULT;
		i = hweight32(val);
		for (j = i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
			if (!(val & (1 << i)))
				continue;
			if (!mixtable[i].rec) {
				val &= ~(1 << i);
				continue;
			}
			j |= mixtable[i].rec;
		}
		spin_lock_irqsave(&s->lock, flags);
		wrmixer(s, DSP_MIX_ADCMIXIDX_L, j);
		wrmixer(s, DSP_MIX_ADCMIXIDX_R, (j & 1) | (j>>1));
		spin_unlock_irqrestore(&s->lock, flags);
		return 0;

	case SOUND_MIXER_OUTSRC: /* Arg contains a bit for each recording source */
		if (get_user(val, (int *)arg))
			return -EFAULT;
		for (j = i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
			if (!(val & (1 << i)))
				continue;
			if (!mixtable[i].play) {
				val &= ~(1 << i);
				continue;
			}
			j |= mixtable[i].play;
		}
		spin_lock_irqsave(&s->lock, flags);
		frobindir(s, DSP_MIX_OUTMIXIDX, 0x1f, j);
		spin_unlock_irqrestore(&s->lock, flags);
		return 0;

		default:
		i = _IOC_NR(cmd);
		if (i >= SOUND_MIXER_NRDEVICES || !mixtable[i].type)
			return -EINVAL;
		if (get_user(val, (int *)arg))
			return -EFAULT;
		l = val & 0xff;
		r = (val >> 8) & 0xff;
		if (l > 100)
			l = 100;
		if (r > 100)
			r = 100;
		spin_lock_irqsave(&s->lock, flags);
		switch (mixtable[i].type) {
		case MT_4:
			if (l >= 10)
				l -= 10;
			if (r >= 10)
				r -= 10;
			frobindir(s, mixtable[i].left, 0xf0, l / 6);
			frobindir(s, mixtable[i].right, 0xf0, l / 6);
			break;

		case MT_4MUTEMONO:
			rl = (l < 4 ? 0 : (l - 5) / 3) & 31;
			rr = (rl >> 2) & 7;
			wrmixer(s, mixtable[i].left, rl<<3);
			outb((inb(s->iobase + CODEC_CMI_MIXER2) & ~0x0e) | rr<<1, s->iobase + CODEC_CMI_MIXER2);
			break;
			
		case MT_5MUTEMONO:
			r = l;
			rl = l < 4 ? 0 : (l - 5) / 3;
			rr = rl >> 2;
 			wrmixer(s, mixtable[i].left, rl<<3);
			outb((inb(s->iobase + CODEC_CMI_MIXER2) & ~0x0e) | rr<<1, s->iobase + CODEC_CMI_MIXER2);
			break;
				
		case MT_5MUTE:
			rl = l < 4 ? 0 : (l - 5) / 3;
			rr = r < 4 ? 0 : (r - 5) / 3;
 			wrmixer(s, mixtable[i].left, rl<<3);
			wrmixer(s, mixtable[i].right, rr<<3);
			break;
				
		case MT_6MUTE:
			if (l < 6)
				rl = 0x00;
			else
				rl = l * 2 / 3;
			if (r < 6)
				rr = 0x00;
			else
				rr = r * 2 / 3;
			wrmixer(s, mixtable[i].left, rl);
			wrmixer(s, mixtable[i].right, rr);
			break;
		}
		spin_unlock_irqrestore(&s->lock, flags);
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
                return return_mixval(s, i, (int *)arg);
#else /* OSS_DOCUMENTED_MIXER_SEMANTICS */
		if (!volidx[i])
			return -EINVAL;
		s->mix.vol[volidx[i]-1] = val;
		return put_user(s->mix.vol[volidx[i]-1], (int *)arg);
#endif /* OSS_DOCUMENTED_MIXER_SEMANTICS */
	}
}

/* --------------------------------------------------------------------- */

static loff_t cm_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

/* --------------------------------------------------------------------- */

static int cm_open_mixdev(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct cm_state *s = devs;

	while (s && s->dev_mixer != minor)
		s = s->next;
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
	file->private_data = s;
	return 0;
}

static int cm_release_mixdev(struct inode *inode, struct file *file)
{
	struct cm_state *s = (struct cm_state *)file->private_data;
	
	VALIDATE_STATE(s);
	return 0;
}

static int cm_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	return mixer_ioctl((struct cm_state *)file->private_data, cmd, arg);
}

static /*const*/ struct file_operations cm_mixer_fops = {
	owner:		THIS_MODULE,
	llseek:		cm_llseek,
	ioctl:		cm_ioctl_mixdev,
	open:		cm_open_mixdev,
	release:	cm_release_mixdev,
};

/* --------------------------------------------------------------------- */

static int drain_dac(struct cm_state *s, int nonblock)
{
        DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int count, tmo;

	if (s->dma_dac.mapped || !s->dma_dac.ready)
		return 0;
        add_wait_queue(&s->dma_dac.wait, &wait);
        for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
                spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
                spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
                        break;
                if (nonblock) {
                        remove_wait_queue(&s->dma_dac.wait, &wait);
                        current->state = TASK_RUNNING;
                        return -EBUSY;
                }
		tmo = (count * HZ) / s->ratedac;
		tmo >>= sample_shift[(s->fmt >> CM_CFMT_DACSHIFT) & CM_CFMT_MASK];
		if (!schedule_timeout(tmo ? : 1) && tmo)
			printk(KERN_DEBUG "cmpci: dma timed out??\n");
        }
        remove_wait_queue(&s->dma_dac.wait, &wait);
        current->state = TASK_RUNNING;
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

/* --------------------------------------------------------------------- */

static ssize_t cm_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct cm_state *s = (struct cm_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (s->dma_adc.mapped)
		return -ENXIO;
	if (!s->dma_adc.ready && (ret = prog_dmabuf(s, 1)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;
#if 0
   spin_lock_irqsave(&s->lock, flags);
   cm_update_ptr(s);
   spin_unlock_irqrestore(&s->lock, flags);
#endif
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		swptr = s->dma_adc.swptr;
		cnt = s->dma_adc.dmasize-swptr;
		if (s->dma_adc.count < cnt)
			cnt = s->dma_adc.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			start_adc(s);
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EAGAIN;
			if (!interruptible_sleep_on_timeout(&s->dma_adc.wait, HZ)) {
				printk(KERN_DEBUG "cmpci: read: chip lockup? dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       s->dma_adc.dmasize, s->dma_adc.fragsize, s->dma_adc.count,
				       s->dma_adc.hwptr, s->dma_adc.swptr);
				stop_adc(s);
				spin_lock_irqsave(&s->lock, flags);
				set_dmaadc(s, virt_to_bus(s->dma_adc.rawbuf), s->dma_adc.dmasamples);
				/* program sample counts */
				outw(s->dma_adc.fragsamples-1, s->iobase + CODEC_CMI_CH1_FRAME2 + 2);
				s->dma_adc.count = s->dma_adc.hwptr = s->dma_adc.swptr = 0;
				spin_unlock_irqrestore(&s->lock, flags);
			}
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_to_user(buffer, s->dma_adc.rawbuf + swptr, cnt))
			return ret ? ret : -EFAULT;
		swptr = (swptr + cnt) % s->dma_adc.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_adc.swptr = swptr;
		s->dma_adc.count -= cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_adc(s);
	}
	return ret;
}

static ssize_t cm_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct cm_state *s = (struct cm_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (s->dma_dac.mapped)
		return -ENXIO;
	if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;
#if 0
   spin_lock_irqsave(&s->lock, flags);
   cm_update_ptr(s);
   spin_unlock_irqrestore(&s->lock, flags);
#endif
	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		if (s->dma_dac.count < 0) {
			s->dma_dac.count = 0;
			s->dma_dac.swptr = s->dma_dac.hwptr;
		}
		swptr = s->dma_dac.swptr;
		cnt = s->dma_dac.dmasize-swptr;
		if (s->dma_dac.count + cnt > s->dma_dac.dmasize)
			cnt = s->dma_dac.dmasize - s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			start_dac(s);
			if (file->f_flags & O_NONBLOCK)
				return ret ? ret : -EAGAIN;
			if (!interruptible_sleep_on_timeout(&s->dma_dac.wait, HZ)) {
				printk(KERN_DEBUG "cmpci: write: chip lockup? dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       s->dma_dac.dmasize, s->dma_dac.fragsize, s->dma_dac.count,
				       s->dma_dac.hwptr, s->dma_dac.swptr);
				stop_dac(s);
				spin_lock_irqsave(&s->lock, flags);
				set_dmadac(s, virt_to_bus(s->dma_dac.rawbuf), s->dma_dac.dmasamples);
				/* program sample counts */
				outw(s->dma_dac.fragsamples-1, s->iobase + CODEC_CMI_CH0_FRAME2 + 2);
				s->dma_dac.count = s->dma_dac.hwptr = s->dma_dac.swptr = 0;
				spin_unlock_irqrestore(&s->lock, flags);
			}
			if (signal_pending(current))
				return ret ? ret : -ERESTARTSYS;
			continue;
		}
		if (copy_from_user(s->dma_dac.rawbuf + swptr, buffer, cnt))
			return ret ? ret : -EFAULT;
		swptr = (swptr + cnt) % s->dma_dac.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_dac.swptr = swptr;
		s->dma_dac.count += cnt;
		s->dma_dac.endcleared = 0;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac(s);
	}
	return ret;
}

/* No kernel lock - fine (we have our own spinlock) */
static unsigned int cm_poll(struct file *file, struct poll_table_struct *wait)
{
	struct cm_state *s = (struct cm_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE) {
		if (!s->dma_dac.ready && prog_dmabuf(s, 0))
			return 0;
		poll_wait(file, &s->dma_dac.wait, wait);
	}
	if (file->f_mode & FMODE_READ) {
		if (!s->dma_adc.ready && prog_dmabuf(s, 1))
			return 0;
		poll_wait(file, &s->dma_adc.wait, wait);
	}

	spin_lock_irqsave(&s->lock, flags);
	cm_update_ptr(s);
	if (file->f_mode & FMODE_READ) {
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->dma_dac.mapped) {
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) 
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)s->dma_dac.dmasize >= s->dma_dac.count + (signed)s->dma_dac.fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int cm_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct cm_state *s = (struct cm_state *)file->private_data;
	struct dmabuf *db;
	int ret = -EINVAL;
	unsigned long size;

	VALIDATE_STATE(s);
	lock_kernel();
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf(s, 1)) != 0)
			goto out;
		db = &s->dma_dac;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf(s, 0)) != 0)
			goto out;
		db = &s->dma_adc;
	} else 
		goto out;
	ret = -EINVAL;
	if (vma->vm_pgoff != 0)
		goto out;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << db->buforder))
		goto out;
	ret = -EAGAIN;
	if (remap_page_range(vma->vm_start, virt_to_phys(db->rawbuf), size, vma->vm_page_prot))
		goto out;
	db->mapped = 1;
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

static int cm_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct cm_state *s = (struct cm_state *)file->private_data;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int val, mapped, ret;
	unsigned char fmtm, fmtd;

	VALIDATE_STATE(s);
        mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
		((file->f_mode & FMODE_READ) && s->dma_adc.mapped);
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_dac(s, 0/*file->f_flags & O_NONBLOCK*/);
		return 0;
		
	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | DSP_CAP_MMAP, (int *)arg);
		
        case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			synchronize_irq();
			s->dma_dac.swptr = s->dma_dac.hwptr = s->dma_dac.count = s->dma_dac.total_bytes = 0;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			synchronize_irq();
			s->dma_adc.swptr = s->dma_adc.hwptr = s->dma_adc.count = s->dma_adc.total_bytes = 0;
		}
		return 0;

        case SNDCTL_DSP_SPEED:
                if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val >= 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				set_adc_rate(s, val);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				set_dac_rate(s, val);
			}
		}
		return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, (int *)arg);
		
        case SNDCTL_DSP_STEREO:
                if (get_user(val, (int *)arg))
			return -EFAULT;
		fmtd = 0;
		fmtm = ~0;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.ready = 0;
			if (val)
				fmtd |= CM_CFMT_STEREO << CM_CFMT_ADCSHIFT;
			else
				fmtm &= ~(CM_CFMT_STEREO << CM_CFMT_ADCSHIFT);
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			s->dma_dac.ready = 0;
			if (val)
				fmtd |= CM_CFMT_STEREO << CM_CFMT_DACSHIFT;
			else
				fmtm &= ~(CM_CFMT_STEREO << CM_CFMT_DACSHIFT);
		}
		set_fmt(s, fmtm, fmtd);
		return 0;

        case SNDCTL_DSP_CHANNELS:
                if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != 0) {
			fmtd = 0;
			fmtm = ~0;
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val >= 2)
					fmtd |= CM_CFMT_STEREO << CM_CFMT_ADCSHIFT;
				else
					fmtm &= ~(CM_CFMT_STEREO << CM_CFMT_ADCSHIFT);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val >= 2)
					fmtd |= CM_CFMT_STEREO << CM_CFMT_DACSHIFT;
				else
					fmtm &= ~(CM_CFMT_STEREO << CM_CFMT_DACSHIFT);
			}
			set_fmt(s, fmtm, fmtd);
		}
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (CM_CFMT_STEREO << CM_CFMT_ADCSHIFT) 
					   : (CM_CFMT_STEREO << CM_CFMT_DACSHIFT))) ? 2 : 1, (int *)arg);
		
	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(AFMT_S16_LE|AFMT_U8, (int *)arg);
		
	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			fmtd = 0;
			fmtm = ~0;
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val == AFMT_S16_LE)
					fmtd |= CM_CFMT_16BIT << CM_CFMT_ADCSHIFT;
				else
					fmtm &= ~(CM_CFMT_16BIT << CM_CFMT_ADCSHIFT);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val == AFMT_S16_LE)
					fmtd |= CM_CFMT_16BIT << CM_CFMT_DACSHIFT;
				else
					fmtm &= ~(CM_CFMT_16BIT << CM_CFMT_DACSHIFT);
			}
			set_fmt(s, fmtm, fmtd);
		}
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (CM_CFMT_16BIT << CM_CFMT_ADCSHIFT) 
					   : (CM_CFMT_16BIT << CM_CFMT_DACSHIFT))) ? AFMT_S16_LE : AFMT_U8, (int *)arg);
		
	case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if (file->f_mode & FMODE_READ && s->enable & CM_CENABLE_RE) 
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & FMODE_WRITE && s->enable & CM_CENABLE_PE) 
			val |= PCM_ENABLE_OUTPUT;
		return put_user(val, (int *)arg);
		
	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!s->dma_adc.ready && (ret =  prog_dmabuf(s, 1)))
					return ret;
				start_adc(s);
			} else
				stop_adc(s);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
					return ret;
				start_dac(s);
			} else
				stop_dac(s);
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
			return ret;
		spin_lock_irqsave(&s->lock, flags);
		cm_update_ptr(s);
		abinfo.fragsize = s->dma_dac.fragsize;
                abinfo.bytes = s->dma_dac.dmasize - s->dma_dac.count;
                abinfo.fragstotal = s->dma_dac.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_dac.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!s->dma_adc.ready && (ret =  prog_dmabuf(s, 1)))
			return ret;
		spin_lock_irqsave(&s->lock, flags);
		cm_update_ptr(s);
		abinfo.fragsize = s->dma_adc.fragsize;
                abinfo.bytes = s->dma_adc.count;
                abinfo.fragstotal = s->dma_adc.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_adc.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
		
        case SNDCTL_DSP_NONBLOCK:
                file->f_flags |= O_NONBLOCK;
                return 0;

        case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
			return ret;
		spin_lock_irqsave(&s->lock, flags);
		cm_update_ptr(s);
                val = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		return put_user(val, (int *)arg);

        case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!s->dma_adc.ready && (ret =  prog_dmabuf(s, 1)))
			return ret;
		spin_lock_irqsave(&s->lock, flags);
		cm_update_ptr(s);
                cinfo.bytes = s->dma_adc.total_bytes;
                cinfo.blocks = s->dma_adc.count >> s->dma_adc.fragshift;
                cinfo.ptr = s->dma_adc.hwptr;
		if (s->dma_adc.mapped)
			s->dma_adc.count &= s->dma_adc.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

        case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
			return ret;
		spin_lock_irqsave(&s->lock, flags);
		cm_update_ptr(s);
                cinfo.bytes = s->dma_dac.total_bytes;
                cinfo.blocks = s->dma_dac.count >> s->dma_dac.fragshift;
                cinfo.ptr = s->dma_dac.hwptr;
		if (s->dma_dac.mapped)
			s->dma_dac.count &= s->dma_dac.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

        case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf(s, 0)))
				return val;
			return put_user(s->dma_dac.fragsize, (int *)arg);
		}
		if ((val = prog_dmabuf(s, 1)))
			return val;
		return put_user(s->dma_adc.fragsize, (int *)arg);

        case SNDCTL_DSP_SETFRAGMENT:
                if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			s->dma_adc.ossfragshift = val & 0xffff;
			s->dma_adc.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_adc.ossfragshift < 4)
				s->dma_adc.ossfragshift = 4;
			if (s->dma_adc.ossfragshift > 15)
				s->dma_adc.ossfragshift = 15;
			if (s->dma_adc.ossmaxfrags < 4)
				s->dma_adc.ossmaxfrags = 4;
		}
		if (file->f_mode & FMODE_WRITE) {
			s->dma_dac.ossfragshift = val & 0xffff;
			s->dma_dac.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_dac.ossfragshift < 4)
				s->dma_dac.ossfragshift = 4;
			if (s->dma_dac.ossfragshift > 15)
				s->dma_dac.ossfragshift = 15;
			if (s->dma_dac.ossmaxfrags < 4)
				s->dma_dac.ossmaxfrags = 4;
		}
		return 0;

        case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision) ||
		    (file->f_mode & FMODE_WRITE && s->dma_dac.subdivision))
			return -EINVAL;
                if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		if (file->f_mode & FMODE_READ)
			s->dma_adc.subdivision = val;
		if (file->f_mode & FMODE_WRITE)
			s->dma_dac.subdivision = val;
		return 0;

        case SOUND_PCM_READ_RATE:
		return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, (int *)arg);

        case SOUND_PCM_READ_CHANNELS:
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (CM_CFMT_STEREO << CM_CFMT_ADCSHIFT) : (CM_CFMT_STEREO << CM_CFMT_DACSHIFT))) ? 2 : 1, (int *)arg);

        case SOUND_PCM_READ_BITS:
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (CM_CFMT_16BIT << CM_CFMT_ADCSHIFT) : (CM_CFMT_16BIT << CM_CFMT_DACSHIFT))) ? 16 : 8, (int *)arg);

        case SOUND_PCM_READ_FILTER:
		return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, (int *)arg);

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
                return -EINVAL;
		
	}
	return mixer_ioctl(s, cmd, arg);
}

static int cm_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct cm_state *s = devs;
	unsigned char fmtm = ~0, fmts = 0;

	while (s && ((s->dev_audio ^ minor) & ~0xf))
		s = s->next;
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & file->f_mode) {
		if (file->f_flags & O_NONBLOCK) {
			up(&s->open_sem);
			return -EBUSY;
		}
		up(&s->open_sem);
		interruptible_sleep_on(&s->open_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		down(&s->open_sem);
	}
	if (file->f_mode & FMODE_READ) {
		fmtm &= ~((CM_CFMT_STEREO | CM_CFMT_16BIT) << CM_CFMT_ADCSHIFT);
		if ((minor & 0xf) == SND_DEV_DSP16)
			fmts |= CM_CFMT_16BIT << CM_CFMT_ADCSHIFT;
		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags = s->dma_adc.subdivision = 0;
		set_adc_rate(s, 8000);
	}
	if (file->f_mode & FMODE_WRITE) {
		fmtm &= ~((CM_CFMT_STEREO | CM_CFMT_16BIT) << CM_CFMT_DACSHIFT);
		if ((minor & 0xf) == SND_DEV_DSP16)
			fmts |= CM_CFMT_16BIT << CM_CFMT_DACSHIFT;
		s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags = s->dma_dac.subdivision = 0;
		set_dac_rate(s, 8000);
	}
	set_fmt(s, fmtm, fmts);
	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	up(&s->open_sem);
	return 0;
}

static int cm_release(struct inode *inode, struct file *file)
{
	struct cm_state *s = (struct cm_state *)file->private_data;

	VALIDATE_STATE(s);
	lock_kernel();
	if (file->f_mode & FMODE_WRITE)
		drain_dac(s, file->f_flags & O_NONBLOCK);
	down(&s->open_sem);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac(s);
		dealloc_dmabuf(&s->dma_dac);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(s);
		dealloc_dmabuf(&s->dma_adc);
	}
	s->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);
	up(&s->open_sem);
	wake_up(&s->open_wait);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations cm_audio_fops = {
	owner:		THIS_MODULE,
	llseek:		cm_llseek,
	read:		cm_read,
	write:		cm_write,
	poll:		cm_poll,
	ioctl:		cm_ioctl,
	mmap:		cm_mmap,
	open:		cm_open,
	release:	cm_release,
};

/* --------------------------------------------------------------------- */

static ssize_t cm_midi_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct cm_state *s = (struct cm_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	unsigned long flags;
	unsigned ptr;
	int cnt;

	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	if (count == 0)
		return 0;
	ret = 0;
	add_wait_queue(&s->midi.iwait, &wait);
	while (count > 0) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&s->lock, flags);
		ptr = s->midi.ird;
		cnt = MIDIINBUF - ptr;
		if (s->midi.icnt < cnt)
			cnt = s->midi.icnt;
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (file->f_flags & O_NONBLOCK) 
			{
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			schedule();
			if (signal_pending(current)) 
			{
				if (!ret)
					ret = -ERESTARTSYS;
				break;
			}
			continue;
		}
		if (copy_to_user(buffer, s->midi.ibuf + ptr, cnt)) {
			if (!ret)
				ret = -EFAULT;
			break;
		}
		ptr = (ptr + cnt) % MIDIINBUF;
		spin_lock_irqsave(&s->lock, flags);
		s->midi.ird = ptr;
		s->midi.icnt -= cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		break;
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&s->midi.iwait, &wait);
	return ret;
}

static ssize_t cm_midi_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct cm_state *s = (struct cm_state *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	unsigned long flags;
	unsigned ptr;
	int cnt;

	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	if (count == 0)
		return 0;
	ret = 0;
	add_wait_queue(&s->midi.owait, &wait);
	while (count > 0) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&s->lock, flags);
		ptr = s->midi.owr;
		cnt = MIDIOUTBUF - ptr;
		if (s->midi.ocnt + cnt > MIDIOUTBUF)
			cnt = MIDIOUTBUF - s->midi.ocnt;
		if (cnt <= 0)
			cm_handle_midi(s);
		spin_unlock_irqrestore(&s->lock, flags);
		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			schedule();
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				break;
			}
			continue;
		}
		if (copy_from_user(s->midi.obuf + ptr, buffer, cnt)) {
			if (!ret)
				ret = -EFAULT;
			break;
		}
		ptr = (ptr + cnt) % MIDIOUTBUF;
		spin_lock_irqsave(&s->lock, flags);
		s->midi.owr = ptr;
		s->midi.ocnt += cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		spin_lock_irqsave(&s->lock, flags);
		cm_handle_midi(s);
		spin_unlock_irqrestore(&s->lock, flags);
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&s->midi.owait, &wait);
	return ret;
}

static unsigned int cm_midi_poll(struct file *file, struct poll_table_struct *wait)
{
	struct cm_state *s = (struct cm_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &s->midi.owait, wait);
	if (file->f_mode & FMODE_READ)
		poll_wait(file, &s->midi.iwait, wait);
	spin_lock_irqsave(&s->lock, flags);
	if (file->f_mode & FMODE_READ) {
		if (s->midi.icnt > 0)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->midi.ocnt < MIDIOUTBUF)
			mask |= POLLOUT | POLLWRNORM;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int cm_midi_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct cm_state *s = devs;
	unsigned long flags;

	while (s && s->dev_midi != minor)
		s = s->next;
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & (file->f_mode << FMODE_MIDI_SHIFT)) {
		if (file->f_flags & O_NONBLOCK) {
			up(&s->open_sem);
			return -EBUSY;
		}
		up(&s->open_sem);
		interruptible_sleep_on(&s->open_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		down(&s->open_sem);
	}
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
		s->midi.ord = s->midi.owr = s->midi.ocnt = 0;
		/* enable MPU-401 */
		outb(inb(s->iobase + CODEC_CMI_FUNCTRL1) | 4, s->iobase + CODEC_CMI_FUNCTRL1);
		outb(0xff, s->iomidi+1); /* reset command */
		if (!(inb(s->iomidi+1) & 0x80))
			inb(s->iomidi);
		outb(0x3f, s->iomidi+1); /* uart command */
		if (!(inb(s->iomidi+1) & 0x80))
			inb(s->iomidi);
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
		init_timer(&s->midi.timer);
		s->midi.timer.expires = jiffies+1;
		s->midi.timer.data = (unsigned long)s;
		s->midi.timer.function = cm_midi_timer;
		add_timer(&s->midi.timer);
	}
	if (file->f_mode & FMODE_READ) {
		s->midi.ird = s->midi.iwr = s->midi.icnt = 0;
	}
	if (file->f_mode & FMODE_WRITE) {
		s->midi.ord = s->midi.owr = s->midi.ocnt = 0;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	s->open_mode |= (file->f_mode << FMODE_MIDI_SHIFT) & (FMODE_MIDI_READ | FMODE_MIDI_WRITE);
	up(&s->open_sem);
	return 0;
}

static int cm_midi_release(struct inode *inode, struct file *file)
{
	struct cm_state *s = (struct cm_state *)file->private_data;
        DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	unsigned count, tmo;

	VALIDATE_STATE(s);

	lock_kernel();
	if (file->f_mode & FMODE_WRITE) {
		add_wait_queue(&s->midi.owait, &wait);
		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_lock_irqsave(&s->lock, flags);
			count = s->midi.ocnt;
			spin_unlock_irqrestore(&s->lock, flags);
			if (count <= 0)
				break;
			if (signal_pending(current))
				break;
			if (file->f_flags & O_NONBLOCK) {
				remove_wait_queue(&s->midi.owait, &wait);
				set_current_state(TASK_RUNNING);
				return -EBUSY;
			}
			tmo = (count * HZ) / 3100;
			if (!schedule_timeout(tmo ? : 1) && tmo)
				printk(KERN_DEBUG "cmpci: midi timed out??\n");
		}
		remove_wait_queue(&s->midi.owait, &wait);
		set_current_state(TASK_RUNNING);
	}
	down(&s->open_sem);
	s->open_mode &= (~(file->f_mode << FMODE_MIDI_SHIFT)) & (FMODE_MIDI_READ|FMODE_MIDI_WRITE);
	spin_lock_irqsave(&s->lock, flags);
	if (!(s->open_mode & (FMODE_MIDI_READ | FMODE_MIDI_WRITE))) {
		del_timer(&s->midi.timer);		
		outb(0xff, s->iomidi+1); /* reset command */
		if (!(inb(s->iomidi+1) & 0x80))
			inb(s->iomidi);
		/* disable MPU-401 */
		outb(inb(s->iobase + CODEC_CMI_FUNCTRL1) & ~4, s->iobase + CODEC_CMI_FUNCTRL1);
	}
	spin_unlock_irqrestore(&s->lock, flags);
	up(&s->open_sem);
	wake_up(&s->open_wait);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations cm_midi_fops = {
	owner:		THIS_MODULE,
	llseek:		cm_llseek,
	read:		cm_midi_read,
	write:		cm_midi_write,
	poll:		cm_midi_poll,
	open:		cm_midi_open,
	release:	cm_midi_release,
};

/* --------------------------------------------------------------------- */

static int cm_dmfm_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	static const unsigned char op_offset[18] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15
	};
	struct cm_state *s = (struct cm_state *)file->private_data;
	struct dm_fm_voice v;
	struct dm_fm_note n;
	struct dm_fm_params p;
	unsigned int io;
	unsigned int regb;

	switch (cmd) {		
	case FM_IOCTL_RESET:
		for (regb = 0xb0; regb < 0xb9; regb++) {
			outb(regb, s->iosynth);
			outb(0, s->iosynth+1);
			outb(regb, s->iosynth+2);
			outb(0, s->iosynth+3);
		}
		return 0;

	case FM_IOCTL_PLAY_NOTE:
		if (copy_from_user(&n, (void *)arg, sizeof(n)))
			return -EFAULT;
		if (n.voice >= 18)
			return -EINVAL;
		if (n.voice >= 9) {
			regb = n.voice - 9;
			io = s->iosynth+2;
		} else {
			regb = n.voice;
			io = s->iosynth;
		}
		outb(0xa0 + regb, io);
		outb(n.fnum & 0xff, io+1);
		outb(0xb0 + regb, io);
		outb(((n.fnum >> 8) & 3) | ((n.octave & 7) << 2) | ((n.key_on & 1) << 5), io+1);
		return 0;

	case FM_IOCTL_SET_VOICE:
		if (copy_from_user(&v, (void *)arg, sizeof(v)))
			return -EFAULT;
		if (v.voice >= 18)
			return -EINVAL;
		regb = op_offset[v.voice];
		io = s->iosynth + ((v.op & 1) << 1);
		outb(0x20 + regb, io);
		outb(((v.am & 1) << 7) | ((v.vibrato & 1) << 6) | ((v.do_sustain & 1) << 5) | 
		     ((v.kbd_scale & 1) << 4) | (v.harmonic & 0xf), io+1);
		outb(0x40 + regb, io);
		outb(((v.scale_level & 0x3) << 6) | (v.volume & 0x3f), io+1);
		outb(0x60 + regb, io);
		outb(((v.attack & 0xf) << 4) | (v.decay & 0xf), io+1);
		outb(0x80 + regb, io);
		outb(((v.sustain & 0xf) << 4) | (v.release & 0xf), io+1);
		outb(0xe0 + regb, io);
		outb(v.waveform & 0x7, io+1);
		if (n.voice >= 9) {
			regb = n.voice - 9;
			io = s->iosynth+2;
		} else {
			regb = n.voice;
			io = s->iosynth;
		}
		outb(0xc0 + regb, io);
		outb(((v.right & 1) << 5) | ((v.left & 1) << 4) | ((v.feedback & 7) << 1) |
		     (v.connection & 1), io+1);
		return 0;
		
	case FM_IOCTL_SET_PARAMS:
		if (copy_from_user(&p, (void *)arg, sizeof(p)))
			return -EFAULT;
		outb(0x08, s->iosynth);
		outb((p.kbd_split & 1) << 6, s->iosynth+1);
		outb(0xbd, s->iosynth);
		outb(((p.am_depth & 1) << 7) | ((p.vib_depth & 1) << 6) | ((p.rhythm & 1) << 5) | ((p.bass & 1) << 4) |
		     ((p.snare & 1) << 3) | ((p.tomtom & 1) << 2) | ((p.cymbal & 1) << 1) | (p.hihat & 1), s->iosynth+1);
		return 0;

	case FM_IOCTL_SET_OPL:
		outb(4, s->iosynth+2);
		outb(arg, s->iosynth+3);
		return 0;

	case FM_IOCTL_SET_MODE:
		outb(5, s->iosynth+2);
		outb(arg & 1, s->iosynth+3);
		return 0;

	default:
		return -EINVAL;
	}
}

static int cm_dmfm_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct cm_state *s = devs;

	while (s && s->dev_dmfm != minor)
		s = s->next;
	if (!s)
		return -ENODEV;
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & FMODE_DMFM) {
		if (file->f_flags & O_NONBLOCK) {
			up(&s->open_sem);
			return -EBUSY;
		}
		up(&s->open_sem);
		interruptible_sleep_on(&s->open_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		down(&s->open_sem);
	}
	/* init the stuff */
	outb(1, s->iosynth);
	outb(0x20, s->iosynth+1); /* enable waveforms */
	outb(4, s->iosynth+2);
	outb(0, s->iosynth+3);  /* no 4op enabled */
	outb(5, s->iosynth+2);
	outb(1, s->iosynth+3);  /* enable OPL3 */
	s->open_mode |= FMODE_DMFM;
	up(&s->open_sem);
	return 0;
}

static int cm_dmfm_release(struct inode *inode, struct file *file)
{
	struct cm_state *s = (struct cm_state *)file->private_data;
	unsigned int regb;

	VALIDATE_STATE(s);
	lock_kernel();
	down(&s->open_sem);
	s->open_mode &= ~FMODE_DMFM;
	for (regb = 0xb0; regb < 0xb9; regb++) {
		outb(regb, s->iosynth);
		outb(0, s->iosynth+1);
		outb(regb, s->iosynth+2);
		outb(0, s->iosynth+3);
	}
	up(&s->open_sem);
	wake_up(&s->open_wait);
	unlock_kernel();
	return 0;
}

static /*const*/ struct file_operations cm_dmfm_fops = {
	owner:		THIS_MODULE,
	llseek:		cm_llseek,
	ioctl:		cm_dmfm_ioctl,
	open:		cm_dmfm_open,
	release:	cm_dmfm_release,
};

/* --------------------------------------------------------------------- */

/* maximum number of devices */
#define NR_DEVICE 5

#if 0
static int reverb[NR_DEVICE] = { 0, };

static int wavetable[NR_DEVICE] = { 0, };
#endif

/* --------------------------------------------------------------------- */

static struct initvol {
	int mixch;
	int vol;
} initvol[] __initdata = {
	{ SOUND_MIXER_WRITE_CD, 0x4040 },
	{ SOUND_MIXER_WRITE_LINE, 0x4040 },
	{ SOUND_MIXER_WRITE_MIC, 0x4040 },
	{ SOUND_MIXER_WRITE_SYNTH, 0x4040 },
	{ SOUND_MIXER_WRITE_VOLUME, 0x4040 },
	{ SOUND_MIXER_WRITE_PCM, 0x4040 }
};

#ifdef MODULE
static int	spdif_loop = 0;
static int	four_ch = 0;
static int	rear_out = 0;
MODULE_PARM(spdif_loop, "i");
MODULE_PARM(four_ch, "i");
MODULE_PARM(rear_out, "i");
#else
#ifdef CONFIG_SOUND_CMPCI_SPDIFLOOP
static int	spdif_loop = 1;
#else
static int	spdif_loop = 0;
#endif
#ifdef CONFIG_SOUND_CMPCI_4CH
static int	four_ch = 1;
#else
static int	four_ch = 0;
#endif
#ifdef CONFIG_SOUND_CMPCI_REAR
static int	rear_out = 1;
#else
static int	rear_out = 0;
#endif
#endif

static int __init init_cmpci(void)
{
	struct cm_state *s;
	struct pci_dev *pcidev = NULL;
	mm_segment_t fs;
	int i, val, index = 0;
	
	struct {
		unsigned short	deviceid;
		char		*devicename;
	} devicetable[] =
	{
		{ PCI_DEVICE_ID_CMEDIA_CM8338A, "CM8338A" },
		{ PCI_DEVICE_ID_CMEDIA_CM8338B, "CM8338B" },
		{ PCI_DEVICE_ID_CMEDIA_CM8738,  "CM8738" },
	};
	char	*devicename = "unknown";

#ifdef CONFIG_PCI
	if (!pci_present())   /* No PCI bus in this machine! */
#endif
		return -ENODEV;
	printk(KERN_INFO "cmpci: version v2.41-nomodem time " __TIME__ " " __DATE__ "\n");
#if 0
	if (!(wavetable_mem = __get_free_pages(GFP_KERNEL, 20-PAGE_SHIFT)))
		printk(KERN_INFO "cmpci: cannot allocate 1MB of contiguous nonpageable memory for wavetable data\n");
#endif
	while (index < NR_DEVICE && pcidev == NULL && (
 	       (pcidev = pci_find_device(PCI_VENDOR_ID_CMEDIA, PCI_DEVICE_ID_CMEDIA_CM8338A, pcidev)) ||
	       (pcidev = pci_find_device(PCI_VENDOR_ID_CMEDIA, PCI_DEVICE_ID_CMEDIA_CM8338B, pcidev)) ||
	       (pcidev = pci_find_device(PCI_VENDOR_ID_CMEDIA, PCI_DEVICE_ID_CMEDIA_CM8738, pcidev)))) {
		if (pci_enable_device(pcidev))
			continue;
		if (pcidev->irq == 0)
			continue;
		if (!(s = kmalloc(sizeof(struct cm_state), GFP_KERNEL))) {
			printk(KERN_WARNING "cmpci: out of memory\n");
			continue;
		}
		/* search device name */
		for (i = 0; i < sizeof(devicetable) / sizeof(devicetable[0]); i++)
		{
			if (devicetable[i].deviceid == pcidev->device)
			{
				devicename = devicetable[i].devicename;
				break;
			}
		}
		memset(s, 0, sizeof(struct cm_state));
		init_waitqueue_head(&s->dma_adc.wait);
		init_waitqueue_head(&s->dma_dac.wait);
		init_waitqueue_head(&s->open_wait);
		init_waitqueue_head(&s->midi.iwait);
		init_waitqueue_head(&s->midi.owait);
		init_MUTEX(&s->open_sem);
		spin_lock_init(&s->lock);
		s->magic = CM_MAGIC;
		s->iobase = pci_resource_start(pcidev, 0);
		s->iosynth = 0x388;
		s->iomidi = 0x330;
		spin_lock_init(&s->lock);
		if (s->iobase == 0)
			continue;
		s->irq = pcidev->irq;

		if (!request_region(s->iobase, CM_EXTENT_CODEC, "cmpci")) {
			printk(KERN_ERR "cmpci: io ports %#x-%#x in use\n", s->iobase, s->iobase+CM_EXTENT_CODEC-1);
			goto err_region5;
		}
		if (!request_region(s->iomidi, CM_EXTENT_MIDI, "cmpci Midi")) {
			printk(KERN_WARNING "cmpci: io ports %#x-%#x in use, midi disabled.\n", s->iomidi, s->iomidi+CM_EXTENT_MIDI-1);
			s->iomidi = 0;
		}
		else
		{
			/* set IO based at 0x330 */
			outb(inb(s->iobase + CODEC_CMI_LEGACY_CTRL + 3) & ~0x60, s->iobase + CODEC_CMI_LEGACY_CTRL + 3);
		}
		if (!request_region(s->iosynth, CM_EXTENT_SYNTH, "cmpci FM")) {
			printk(KERN_WARNING "cmpci: io ports %#x-%#x in use, synth disabled.\n", s->iosynth, s->iosynth+CM_EXTENT_SYNTH-1);
			s->iosynth = 0;
		}
		else
		{
			/* enable FM */
			outb(inb(s->iobase + CODEC_CMI_MISC_CTRL + 2) | 8, s->iobase + CODEC_CMI_MISC_CTRL);
		}
		/* initialize codec registers */
		outb(0, s->iobase + CODEC_CMI_INT_HLDCLR + 2);  /* disable ints */
		outb(0, s->iobase + CODEC_CMI_FUNCTRL0 + 2); /* disable channels */
		/* reset mixer */
		wrmixer(s, DSP_MIX_DATARESETIDX, 0);

		/* request irq */
		if (request_irq(s->irq, cm_interrupt, SA_SHIRQ, "cmpci", s)) {
			printk(KERN_ERR "cmpci: irq %u in use\n", s->irq);
			goto err_irq;
		}
		printk(KERN_INFO "cmpci: found %s adapter at io %#06x irq %u\n",
		       devicename, s->iobase, s->irq);
		/* register devices */
		if ((s->dev_audio = register_sound_dsp(&cm_audio_fops, -1)) < 0)
			goto err_dev1;
		if ((s->dev_mixer = register_sound_mixer(&cm_mixer_fops, -1)) < 0)
			goto err_dev2;
		if (s->iomidi && (s->dev_midi = register_sound_midi(&cm_midi_fops, -1)) < 0)
			goto err_dev3;
		if (s->iosynth && (s->dev_dmfm = register_sound_special(&cm_dmfm_fops, 15 /* ?? */)) < 0)
			goto err_dev4;
		pci_set_master(pcidev);
		/* initialize the chips */
		fs = get_fs();
		set_fs(KERNEL_DS);
		/* set mixer output */
		frobindir(s, DSP_MIX_OUTMIXIDX, 0x1f, 0x1f);
		/* set mixer input */
		val = SOUND_MASK_LINE|SOUND_MASK_SYNTH|SOUND_MASK_CD|SOUND_MASK_MIC;
		mixer_ioctl(s, SOUND_MIXER_WRITE_RECSRC, (unsigned long)&val);
		for (i = 0; i < sizeof(initvol)/sizeof(initvol[0]); i++) {
			val = initvol[i].vol;
			mixer_ioctl(s, initvol[i].mixch, (unsigned long)&val);
		}
		set_fs(fs);
		if (pcidev->device == PCI_DEVICE_ID_CMEDIA_CM8738)
		{
			/* enable SPDIF loop */
			if (spdif_loop)
			{
				/* turn on spdif-in to spdif-out */
				outb(inb(s->iobase + CODEC_CMI_FUNCTRL1) | 0x80, s->iobase + CODEC_CMI_FUNCTRL1);
				printk(KERN_INFO "cmpci: Enable SPDIF loop\n");
			}
			else
				outb(inb(s->iobase + CODEC_CMI_FUNCTRL1) & ~0x80, s->iobase + CODEC_CMI_FUNCTRL1);
			/* enable 4 channels mode */
			if (four_ch)
			{
				/* 4 channel mode (analog duplicate) */
				outb(inb(s->iobase + CODEC_CMI_MISC_CTRL + 3) | 0x04, s->iobase + CODEC_CMI_MISC_CTRL + 3);
				printk(KERN_INFO "cmpci: Enable 4 channels mode\n");
				/* has separate rear-out jack ? */
				if (rear_out)
				{
					/* has separate rear out jack */
					outb(inb(s->iobase + CODEC_CMI_MIXER1) & ~0x20, s->iobase + CODEC_CMI_MIXER1);
				}
				else
				{
					outb(inb(s->iobase + CODEC_CMI_MIXER1) | 0x20, s->iobase + CODEC_CMI_MIXER1);
					printk(KERN_INFO "cmpci: line-in routed as rear-out\n");
				}
			}
			else
				outb(inb(s->iobase + CODEC_CMI_MISC_CTRL + 3) & ~0x04, s->iobase + CODEC_CMI_MISC_CTRL + 3);
		}
		/* queue it for later freeing */
		s->next = devs;
		devs = s;
		index++;
		continue;

	err_dev4:
		unregister_sound_midi(s->dev_midi);
	err_dev3:
		unregister_sound_mixer(s->dev_mixer);
	err_dev2:
		unregister_sound_dsp(s->dev_audio);
	err_dev1:
		printk(KERN_ERR "cmpci: cannot register misc device\n");
		free_irq(s->irq, s);
	err_irq:
		if(s->iosynth)
			release_region(s->iosynth, CM_EXTENT_SYNTH);
		if(s->iomidi)
			release_region(s->iomidi, CM_EXTENT_MIDI);
		release_region(s->iobase, CM_EXTENT_CODEC);
	err_region5:
		kfree(s);
	}
	if (!devs) {
		if (wavetable_mem)
			free_pages(wavetable_mem, 20-PAGE_SHIFT);
		return -ENODEV;
	}
	return 0;
}

/* --------------------------------------------------------------------- */

MODULE_AUTHOR("ChenLi Tien, cltien@home.com");
MODULE_DESCRIPTION("CMPCI Audio Driver");

static void __exit cleanup_cmpci(void)
{
	struct cm_state *s;

	while ((s = devs)) {
		devs = devs->next;
		outb(0, s->iobase + CODEC_CMI_INT_HLDCLR + 2);  /* disable ints */
		synchronize_irq();
		outb(0, s->iobase + CODEC_CMI_FUNCTRL0 + 2); /* disable channels */
		free_irq(s->irq, s);

		/* reset mixer */
		wrmixer(s, DSP_MIX_DATARESETIDX, 0);

		release_region(s->iobase, CM_EXTENT_CODEC);
		if(s->iomidi)
		{
			release_region(s->iomidi, CM_EXTENT_MIDI);
			unregister_sound_midi(s->dev_midi);
		}
		if(s->iosynth)
		{
			release_region(s->iosynth, CM_EXTENT_SYNTH);
			unregister_sound_special(s->dev_dmfm);
		}
		unregister_sound_dsp(s->dev_audio);
		unregister_sound_mixer(s->dev_mixer);
		kfree(s);
	}
	if (wavetable_mem)
		free_pages(wavetable_mem, 20-PAGE_SHIFT);
	printk(KERN_INFO "cmpci: unloading\n");
}

module_init(init_cmpci);
module_exit(cleanup_cmpci);
