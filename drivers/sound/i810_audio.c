/*
 *	Intel i810 and friends ICH driver for Linux
 *	Alan Cox <alan@redhat.com>
 *
 *  Built from:
 *	Low level code:  Zach Brown (original nonworking i810 OSS driver)
 *			 Jaroslav Kysela <perex@suse.cz> (working ALSA driver)
 *
 *	Framework: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *	Extended by: Zach Brown <zab@redhat.com>  
 *			and others..
 *
 *  Hardware Provided By:
 *	Analog Devices (A major AC97 codec maker)
 *	Intel Corp  (you've probably heard of them already)
 *
 * AC97 clues and assistance provided by
 *	Analog Devices
 *	Zach 'Fufu' Brown
 *	Jeff Garzik
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	Intel 810 theory of operation
 *
 *	The chipset provides three DMA channels that talk to an AC97
 *	CODEC (AC97 is a digital/analog mixer standard). At its simplest
 *	you get 48Khz audio with basic volume and mixer controls. At the
 *	best you get rate adaption in the codec. We set the card up so
 *	that we never take completion interrupts but instead keep the card
 *	chasing its tail around a ring buffer. This is needed for mmap
 *	mode audio and happens to work rather well for non-mmap modes too.
 *
 *	The board has one output channel for PCM audio (supported) and
 *	a stereo line in and mono microphone input. Again these are normally
 *	locked to 48Khz only. Right now recording is not finished.
 *
 *	There is no midi support, no synth support. Use timidity. To get
 *	esd working you need to use esd -r 48000 as it won't probe 48KHz
 *	by default. mpg123 can't handle 48Khz only audio so use xmms.
 *
 *	Fix The Sound On Dell
 *
 *	Not everyone uses 48KHz. We know of no way to detect this reliably
 *	and certainly not to get the right data. If your i810 audio sounds
 *	stupid you may need to investigate other speeds. According to Analog
 *	they tend to use a 14.318MHz clock which gives you a base rate of
 *	41194Hz.
 *
 *	This is available via the 'ftsodell=1' option. 
 *
 *	If you need to force a specific rate set the clocking= option
 */
 
#include <linux/module.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/malloc.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/ac97_codec.h>
#include <linux/wrapper.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>

#ifndef PCI_DEVICE_ID_INTEL_82801
#define PCI_DEVICE_ID_INTEL_82801	0x2415
#endif
#ifndef PCI_DEVICE_ID_INTEL_82901
#define PCI_DEVICE_ID_INTEL_82901	0x2425
#endif
#ifndef PCI_DEVICE_ID_INTEL_ICH2
#define PCI_DEVICE_ID_INTEL_ICH2	0x2445
#endif
#ifndef PCI_DEVICE_ID_INTEL_440MX
#define PCI_DEVICE_ID_INTEL_440MX	0x7195
#endif

static int ftsodell=0;
static unsigned int clocking=48000;


#define ADC_RUNNING	1
#define DAC_RUNNING	2

#define I810_FMT_16BIT	1
#define I810_FMT_STEREO	2
#define I810_FMT_MASK	3

/* the 810's array of pointers to data buffers */

struct sg_item {
#define BUSADDR_MASK	0xFFFFFFFE
	u32 busaddr;	
#define CON_IOC 	0x80000000 /* interrupt on completion */
#define CON_BUFPAD	0x40000000 /* pad underrun with last sample, else 0 */
#define CON_BUFLEN_MASK	0x0000ffff /* buffer length in samples */
	u32 control;
};

/* an instance of the i810 channel */
#define SG_LEN 32
struct i810_channel 
{
	/* these sg guys should probably be allocated
	   seperately as nocache. Must be 8 byte aligned */
	struct sg_item sg[SG_LEN];	/* 32*8 */
	u32 offset;			/* 4 */
	u32 port;			/* 4 */
	u32 used;
	u32 num;
};

/*
 * we have 3 seperate dma engines.  pcm in, pcm out, and mic.
 * each dma engine has controlling registers.  These goofy
 * names are from the datasheet, but make it easy to write
 * code while leafing through it.
 */

#define ENUM_ENGINE(PRE,DIG) 									\
enum {												\
	PRE##_BDBAR =	0x##DIG##0,		/* Buffer Descriptor list Base Address */	\
	PRE##_CIV =	0x##DIG##4,		/* Current Index Value */			\
	PRE##_LVI =	0x##DIG##5,		/* Last Valid Index */				\
	PRE##_SR =	0x##DIG##6,		/* Status Register */				\
	PRE##_PICB =	0x##DIG##8,		/* Position In Current Buffer */		\
	PRE##_PIV =	0x##DIG##a,		/* Prefetched Index Value */			\
	PRE##_CR =	0x##DIG##b		/* Control Register */				\
}

ENUM_ENGINE(OFF,0);	/* Offsets */
ENUM_ENGINE(PI,0);	/* PCM In */
ENUM_ENGINE(PO,1);	/* PCM Out */
ENUM_ENGINE(MC,2);	/* Mic In */

enum {
	GLOB_CNT =	0x2c,			/* Global Control */
	GLOB_STA = 	0x30,			/* Global Status */
	CAS	 = 	0x34			/* Codec Write Semaphore Register */
};

/* interrupts for a dma engine */
#define DMA_INT_FIFO		(1<<4)  /* fifo under/over flow */
#define DMA_INT_COMPLETE	(1<<3)  /* buffer read/write complete and ioc set */
#define DMA_INT_LVI		(1<<2)  /* last valid done */
#define DMA_INT_CELV		(1<<1)  /* last valid is current */
#define DMA_INT_MASK (DMA_INT_FIFO|DMA_INT_COMPLETE|DMA_INT_LVI)

/* interrupts for the whole chip */
#define INT_SEC		(1<<11)
#define INT_PRI		(1<<10)
#define INT_MC		(1<<7)
#define INT_PO		(1<<6)
#define INT_PI		(1<<5)
#define INT_MO		(1<<2)
#define INT_NI		(1<<1)
#define INT_GPI		(1<<0)
#define INT_MASK (INT_SEC|INT_PRI|INT_MC|INT_PO|INT_PI|INT_MO|INT_NI|INT_GPI)


#define DRIVER_VERSION "0.01"

/* magic numbers to protect our data structures */
#define I810_CARD_MAGIC		0x5072696E /* "Prin" */
#define I810_STATE_MAGIC	0x63657373 /* "cess" */
#define I810_DMA_MASK		0xffffffff /* DMA buffer mask for pci_alloc_consist */
#define NR_HW_CH		3

/* maxinum number of AC97 codecs connected, AC97 2.0 defined 4 */
#define NR_AC97		2

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

enum {
	ICH82801AA = 0,
	ICH82901AB,
	INTEL440MX,
	INTELICH2,
};

static char * card_names[] = {
	"Intel ICH 82801AA",
	"Intel ICH 82901AB",
	"Intel 440MX",
	"Intel ICH2"
};

static struct pci_device_id i810_pci_tbl [] __initdata = {
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, ICH82801AA},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82901,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, ICH82901AB},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_440MX,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, INTEL440MX},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH2,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, INTELICH2},
	{0,}
};

MODULE_DEVICE_TABLE (pci, i810_pci_tbl);

/* "software" or virtual channel, an instance of opened /dev/dsp */
struct i810_state {
	unsigned int magic;
	struct i810_card *card;	/* Card info */

	/* single open lock mechanism, only used for recording */
	struct semaphore open_sem;
	wait_queue_head_t open_wait;

	/* file mode */
	mode_t open_mode;

	/* virtual channel number */
	int virt;

	struct dmabuf {
		/* wave sample stuff */
		unsigned int rate;
		unsigned char fmt, enable;

		/* hardware channel */
		struct i810_channel *channel;

		/* OSS buffer management stuff */
		void *rawbuf;
		dma_addr_t dma_handle;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;

		/* our buffer acts like a circular ring */
		unsigned hwptr;		/* where dma last started, updated by update_ptr */
		unsigned swptr;		/* where driver last clear/filled, updated by read/write */
		int count;		/* bytes to be comsumed or been generated by dma machine */
		unsigned total_bytes;	/* total bytes dmaed by hardware */

		unsigned error;		/* number of over/underruns */
		wait_queue_head_t wait;	/* put process on wait queue when no more space in buffer */

		/* redundant, but makes calculations easier */
		unsigned fragsize;
		unsigned dmasize;
		unsigned fragsamples;

		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned endcleared:1;
		unsigned update_flag;
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;
	} dmabuf;
};


struct i810_card {
	struct i810_channel channel[3];
	unsigned int magic;

	/* We keep i810 cards in a linked list */
	struct i810_card *next;

	/* The i810 has a certain amount of cross channel interaction
	   so we use a single per card lock */
	spinlock_t lock;

	/* PCI device stuff */
	struct pci_dev * pci_dev;
	u16 pci_id;

	/* soundcore stuff */
	int dev_audio;

	/* structures for abstraction of hardware facilities, codecs, banks and channels*/
	struct ac97_codec *ac97_codec[NR_AC97];
	struct i810_state *states[NR_HW_CH];

	u16 ac97_features;
	
	/* hardware resources */
	unsigned long iobase;
	unsigned long ac97base;
	u32 irq;
	
	/* Function support */
	struct i810_channel *(*alloc_pcm_channel)(struct i810_card *);
	struct i810_channel *(*alloc_rec_pcm_channel)(struct i810_card *);
	void (*free_pcm_channel)(struct i810_card *, int chan);
};

static struct i810_card *devs = NULL;

static int i810_open_mixdev(struct inode *inode, struct file *file);
static int i810_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd,
				unsigned long arg);
static loff_t i810_llseek(struct file *file, loff_t offset, int origin);

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

static u16 i810_ac97_get(struct ac97_codec *dev, u8 reg);
static void i810_ac97_set(struct ac97_codec *dev, u8 reg, u16 data);

static struct i810_channel *i810_alloc_pcm_channel(struct i810_card *card)
{
	if(card->channel[1].used==1)
		return NULL;
	card->channel[1].used=1;
	card->channel[1].offset = 0;
	card->channel[1].port = 0x10;
	card->channel[1].num=1;
	return &card->channel[1];
}

static struct i810_channel *i810_alloc_rec_pcm_channel(struct i810_card *card)
{
	if(card->channel[0].used==1)
		return NULL;
	card->channel[0].used=1;
	card->channel[0].offset = 0;
	card->channel[0].port = 0x00;
	card->channel[1].num=0;
	return &card->channel[0];
}

static void i810_free_pcm_channel(struct i810_card *card, int channel)
{
	card->channel[channel].used=0;
}

/* set playback sample rate */
static unsigned int i810_set_dac_rate(struct i810_state * state, unsigned int rate)
{	
	struct dmabuf *dmabuf = &state->dmabuf;
	u32 dacp;
	struct ac97_codec *codec=state->card->ac97_codec[0];
	
	if(!(state->card->ac97_features&0x0001))
	{
		dmabuf->rate = clocking;
		return clocking;
	}
			
	if (rate > 48000)
		rate = 48000;
	if (rate < 8000)
		rate = 8000;
		
	/*
	 *	Adjust for misclocked crap
	 */
	 
	rate = ( rate * clocking)/48000;
	
	/* Analog codecs can go lower via magic registers but others
	   might not */
	   
	if(rate < 8000)
		rate = 8000;

	if(rate != i810_ac97_get(codec, AC97_PCM_FRONT_DAC_RATE))
	{
		/* Power down the DAC */
		dacp=i810_ac97_get(codec, AC97_POWER_CONTROL);
		i810_ac97_set(codec, AC97_POWER_CONTROL, dacp|0x0200);
		/* Load the rate and read the effective rate */
		i810_ac97_set(codec, AC97_PCM_FRONT_DAC_RATE, rate);
		rate=i810_ac97_get(codec, AC97_PCM_FRONT_DAC_RATE);
		/* Power it back up */
		i810_ac97_set(codec, AC97_POWER_CONTROL, dacp);
	}
	rate=(rate * 48000) / clocking;
	dmabuf->rate = rate;
#ifdef DEBUG
	printk("i810_audio: called i810_set_dac_rate : rate = %d\n", rate);
#endif

	return rate;
}

/* set recording sample rate */
static unsigned int i810_set_adc_rate(struct i810_state * state, unsigned int rate)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	u32 dacp;
	struct ac97_codec *codec=state->card->ac97_codec[0];
	
	if(!(state->card->ac97_features&0x0001))
	{
		dmabuf->rate = clocking;
		return clocking;
	}
			
	if (rate > 48000)
		rate = 48000;
	if (rate < 8000)
		rate = 8000;

	/*
	 *	Adjust for misclocked crap
	 */
	 
	rate = ( rate * clocking)/48000;
	
	/* Analog codecs can go lower via magic registers but others
	   might not */
	   
	if(rate < 8000)
		rate = 8000;

	if(rate != i810_ac97_get(codec, AC97_PCM_LR_DAC_RATE))
	{
		/* Power down the ADC */
		dacp=i810_ac97_get(codec, AC97_POWER_CONTROL);
		i810_ac97_set(codec, AC97_POWER_CONTROL, dacp|0x0100);
		/* Load the rate and read the effective rate */
		i810_ac97_set(codec, AC97_PCM_LR_DAC_RATE, rate);
		rate=i810_ac97_get(codec, AC97_PCM_LR_DAC_RATE);
		/* Power it back up */
		i810_ac97_set(codec, AC97_POWER_CONTROL, dacp);
	}
	rate = (rate * 48000) / clocking;
	dmabuf->rate = rate;
#ifdef DEBUG
	printk("i810_audio: called i810_set_adc_rate : rate = %d\n", rate);
#endif
	return rate;
}

/* prepare channel attributes for playback */ 
static void i810_play_setup(struct i810_state *state)
{
//	struct dmabuf *dmabuf = &state->dmabuf;
//	struct i810_channel *channel = dmabuf->channel;
	/* Fixed format. .. */
	//if (dmabuf->fmt & I810_FMT_16BIT)
	//if (dmabuf->fmt & I810_FMT_STEREO)
}

/* prepare channel attributes for recording */
static void i810_rec_setup(struct i810_state *state)
{
//	u16 w;
//	struct i810_card *card = state->card;
//	struct dmabuf *dmabuf = &state->dmabuf;
//	struct i810_channel *channel = dmabuf->channel;

	/* Enable AC-97 ADC (capture) */
//	if (dmabuf->fmt & I810_FMT_16BIT) {
//	if (dmabuf->fmt & I810_FMT_STEREO)
}


/* get current playback/recording dma buffer pointer (byte offset from LBA),
   called with spinlock held! */
   
extern __inline__ unsigned i810_get_dma_addr(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int civ, offset;
	struct i810_channel *c = dmabuf->channel;
	
	if (!dmabuf->enable)
		return 0;
	do {
		civ = inb(state->card->iobase+c->port+OFF_CIV);
		offset = (civ + 1) * (dmabuf->dmasize/SG_LEN) -
			      2 * inw(state->card->iobase+c->port+OFF_PICB);
		/* CIV changed before we read PICB (very seldom) ?
		 * then PICB was rubbish, so try again */
	} while (civ != inb(state->card->iobase+c->port+OFF_CIV));
		 
	return offset;
}

static void resync_dma_ptrs(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct i810_channel *c = dmabuf->channel;
	int offset;
	
	offset = inb(state->card->iobase+c->port+OFF_CIV);
	offset *= (dmabuf->dmasize/SG_LEN);
	
	dmabuf->hwptr=dmabuf->swptr = offset;
}
	
/* Stop recording (lock held) */
extern __inline__ void __stop_adc(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct i810_card *card = state->card;

	dmabuf->enable &= ~ADC_RUNNING;
	outb(0, card->iobase + PI_CR);
}

static void stop_adc(struct i810_state *state)
{
	struct i810_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	__stop_adc(state);
	spin_unlock_irqrestore(&card->lock, flags);
}

static void start_adc(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct i810_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	if ((dmabuf->mapped || dmabuf->count < (signed)dmabuf->dmasize) && dmabuf->ready) {
		dmabuf->enable |= ADC_RUNNING;
		outb((1<<4) | 1<<2 | 1, card->iobase + PI_CR);
	}
	spin_unlock_irqrestore(&card->lock, flags);
}

/* stop playback (lock held) */
extern __inline__ void __stop_dac(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct i810_card *card = state->card;

	dmabuf->enable &= ~DAC_RUNNING;
	outb(0, card->iobase + PO_CR);
}

static void stop_dac(struct i810_state *state)
{
	struct i810_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	__stop_dac(state);
	spin_unlock_irqrestore(&card->lock, flags);
}	

static void start_dac(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct i810_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	if ((dmabuf->mapped || dmabuf->count > 0) && dmabuf->ready) {
		if(!(dmabuf->enable&DAC_RUNNING))
		{
			dmabuf->enable |= DAC_RUNNING;
			outb((1<<4) | 1<<2 | 1, card->iobase + PO_CR);
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);
}

#define DMABUF_DEFAULTORDER (15-PAGE_SHIFT)
#define DMABUF_MINORDER 1

/* allocate DMA buffer, playback and recording buffer should be allocated seperately */
static int alloc_dmabuf(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	void *rawbuf;
	int order;
	struct page *page, *pend;

	/* alloc as big a chunk as we can, FIXME: is this necessary ?? */
	for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER; order--)
		if ((rawbuf = pci_alloc_consistent(state->card->pci_dev,
						   PAGE_SIZE << order,
						   &dmabuf->dma_handle)))
			break;
	if (!rawbuf)
		return -ENOMEM;

#ifdef DEBUG
	printk("i810_audio: allocated %ld (order = %d) bytes at %p\n",
	       PAGE_SIZE << order, order, rawbuf);
#endif

	dmabuf->ready  = dmabuf->mapped = 0;
	dmabuf->rawbuf = rawbuf;
	dmabuf->buforder = order;
	
	/* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
	pend = virt_to_page(rawbuf + (PAGE_SIZE << order) - 1);
	for (page = virt_to_page(rawbuf); page <= pend; page++)
		mem_map_reserve(page);

	return 0;
}

/* free DMA buffer */
static void dealloc_dmabuf(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct page *page, *pend;

	if (dmabuf->rawbuf) {
		/* undo marking the pages as reserved */
		pend = virt_to_page(dmabuf->rawbuf + (PAGE_SIZE << dmabuf->buforder) - 1);
		for (page = virt_to_page(dmabuf->rawbuf); page <= pend; page++)
			mem_map_unreserve(page);
		pci_free_consistent(state->card->pci_dev, PAGE_SIZE << dmabuf->buforder,
				    dmabuf->rawbuf, dmabuf->dma_handle);
	}
	dmabuf->rawbuf = NULL;
	dmabuf->mapped = dmabuf->ready = 0;
}

static int prog_dmabuf(struct i810_state *state, unsigned rec)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct sg_item *sg;
	unsigned bytepersec;
	unsigned bufsize;
	unsigned long flags;
	int ret;
	unsigned fragsize;
	int i;

	spin_lock_irqsave(&state->card->lock, flags);
	resync_dma_ptrs(state);
	dmabuf->total_bytes = 0;
	dmabuf->count = dmabuf->error = 0;
	spin_unlock_irqrestore(&state->card->lock, flags);

	/* allocate DMA buffer if not allocated yet */
	if (!dmabuf->rawbuf)
		if ((ret = alloc_dmabuf(state)))
			return ret;

	/* FIXME: figure out all this OSS fragment stuff */
	bytepersec = dmabuf->rate << sample_shift[dmabuf->fmt];
	bufsize = PAGE_SIZE << dmabuf->buforder;
	if (dmabuf->ossfragshift) {
		if ((1000 << dmabuf->ossfragshift) < bytepersec)
			dmabuf->fragshift = ld2(bytepersec/1000);
		else
			dmabuf->fragshift = dmabuf->ossfragshift;
	} else {
		/* lets hand out reasonable big ass buffers by default */
		dmabuf->fragshift = (dmabuf->buforder + PAGE_SHIFT -2);
	}
	dmabuf->numfrag = bufsize >> dmabuf->fragshift;
	while (dmabuf->numfrag < 4 && dmabuf->fragshift > 3) {
		dmabuf->fragshift--;
		dmabuf->numfrag = bufsize >> dmabuf->fragshift;
	}
	dmabuf->fragsize = 1 << dmabuf->fragshift;
	if (dmabuf->ossmaxfrags >= 4 && dmabuf->ossmaxfrags < dmabuf->numfrag)
		dmabuf->numfrag = dmabuf->ossmaxfrags;
	dmabuf->fragsamples = dmabuf->fragsize >> sample_shift[dmabuf->fmt];
	dmabuf->dmasize = dmabuf->numfrag << dmabuf->fragshift;

	memset(dmabuf->rawbuf, (dmabuf->fmt & I810_FMT_16BIT) ? 0 : 0x80,
	       dmabuf->dmasize);

	/*
	 *	Now set up the ring 
	 */

	sg=&dmabuf->channel->sg[0];
	fragsize = bufsize / SG_LEN;
	
	/*
	 *	Load up 32 sg entries and take an interrupt at half
	 *	way (we might want more interrupts later..) 
	 */
	  
	for(i=0;i<32;i++)
	{
		sg->busaddr=virt_to_bus(dmabuf->rawbuf+fragsize*i);
		sg->control=(fragsize>>1);
		sg->control|=CON_IOC;
		sg++;
	}

	spin_lock_irqsave(&state->card->lock, flags);
	outb(2, state->card->iobase+dmabuf->channel->port+OFF_CR);   /* reset DMA machine */
	outl(virt_to_bus(&dmabuf->channel->sg[0]), state->card->iobase+dmabuf->channel->port+OFF_BDBAR);
	outb(16, state->card->iobase+dmabuf->channel->port+OFF_LVI);
	outb(0, state->card->iobase+dmabuf->channel->port+OFF_CIV);

	if (rec) {
		i810_rec_setup(state);
	} else {
		i810_play_setup(state);
	}
	spin_unlock_irqrestore(&state->card->lock, flags);

	/* set the ready flag for the dma buffer */
	dmabuf->ready = 1;

#ifdef DEBUG
	printk("i810_audio: prog_dmabuf, sample rate = %d, format = %d, numfrag = %d, "
	       "fragsize = %d dmasize = %d\n",
	       dmabuf->rate, dmabuf->fmt, dmabuf->numfrag,
	       dmabuf->fragsize, dmabuf->dmasize);
#endif

	return 0;
}
/*
 * Clear the rest of the last i810 dma buffer, normally there is no rest
 * because the OSS fragment size is the same as the size of this buffer.
 */
static void i810_clear_tail(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned swptr;
	unsigned char silence = (dmabuf->fmt & I810_FMT_16BIT) ? 0 : 0x80;
	unsigned int len;
	unsigned long flags;

	spin_lock_irqsave(&state->card->lock, flags);
	swptr = dmabuf->swptr;
	spin_unlock_irqrestore(&state->card->lock, flags);

	if(dmabuf->dmasize)
		len = swptr % (dmabuf->dmasize/SG_LEN);
	else
		len = 0;
	
	memset(dmabuf->rawbuf + swptr, silence, len);

	spin_lock_irqsave(&state->card->lock, flags);
	dmabuf->swptr += len;
	dmabuf->count += len;
	spin_unlock_irqrestore(&state->card->lock, flags);

	/* restart the dma machine in case it is halted */
	start_dac(state);
}

static int drain_dac(struct i810_state *state, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned long tmo;
	int count;

	if (dmabuf->mapped || !dmabuf->ready)
		return 0;

	add_wait_queue(&dmabuf->wait, &wait);
	for (;;) {
		/* It seems that we have to set the current state to TASK_INTERRUPTIBLE
		   every time to make the process really go to sleep */
		current->state = TASK_INTERRUPTIBLE;

		spin_lock_irqsave(&state->card->lock, flags);
		count = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (count <= 0)
			break;

		if (signal_pending(current))
			break;

		if (nonblock) {
			remove_wait_queue(&dmabuf->wait, &wait);
			current->state = TASK_RUNNING;
			return -EBUSY;
		}

		tmo = (dmabuf->dmasize * HZ) / dmabuf->rate;
		tmo >>= sample_shift[dmabuf->fmt];
		if (!schedule_timeout(tmo ? tmo : 1) && tmo){
			printk(KERN_ERR "i810_audio: drain_dac, dma timeout?\n");
			break;
		}
	}
	remove_wait_queue(&dmabuf->wait, &wait);
	current->state = TASK_RUNNING;
	if (signal_pending(current))
		return -ERESTARTSYS;

	return 0;
}

/* update buffer manangement pointers, especially, dmabuf->count and dmabuf->hwptr */
static void i810_update_ptr(struct i810_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned hwptr, swptr;
	int clear_cnt = 0;
	int diff;
	unsigned char silence;
//	unsigned half_dmasize;

	/* update hardware pointer */
	hwptr = i810_get_dma_addr(state);
	diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;
//	printk("HWP %d,%d,%d\n", hwptr, dmabuf->hwptr, diff);
	dmabuf->hwptr = hwptr;
	dmabuf->total_bytes += diff;

	/* error handling and process wake up for DAC */
	if (dmabuf->enable == ADC_RUNNING) {
		if (dmabuf->mapped) {
			dmabuf->count -= diff;
			if (dmabuf->count >= (signed)dmabuf->fragsize)
				wake_up(&dmabuf->wait);
		} else {
			dmabuf->count += diff;

			if (dmabuf->count < 0 || dmabuf->count > dmabuf->dmasize) {
				/* buffer underrun or buffer overrun, we have no way to recover
				   it here, just stop the machine and let the process force hwptr
				   and swptr to sync */
				__stop_adc(state);
				dmabuf->error++;
			}
			else if (!dmabuf->endcleared) {
				swptr = dmabuf->swptr;
				silence = (dmabuf->fmt & I810_FMT_16BIT ? 0 : 0x80);
				if (dmabuf->count < (signed) dmabuf->fragsize) 
				{
					clear_cnt = dmabuf->fragsize;
					if ((swptr + clear_cnt) > dmabuf->dmasize)
						clear_cnt = dmabuf->dmasize - swptr;
					memset (dmabuf->rawbuf + swptr, silence, clear_cnt);
					dmabuf->endcleared = 1;
				}
			}			
			wake_up(&dmabuf->wait);
		}
	}
	/* error handling and process wake up for DAC */
	if (dmabuf->enable == DAC_RUNNING) {
		if (dmabuf->mapped) {
			dmabuf->count += diff;
			if (dmabuf->count >= (signed)dmabuf->fragsize)
				wake_up(&dmabuf->wait);
		} else {
			dmabuf->count -= diff;

			if (dmabuf->count < 0 || dmabuf->count > dmabuf->dmasize) {
				/* buffer underrun or buffer overrun, we have no way to recover
				   it here, just stop the machine and let the process force hwptr
				   and swptr to sync */
				__stop_dac(state);
				printk("DMA overrun on send\n");
				dmabuf->error++;
			}
			wake_up(&dmabuf->wait);
		}
	}
}

static void i810_channel_interrupt(struct i810_card *card)
{
	int i;

//	printk("CHANNEL IRQ .. ");	
	for(i=0;i<NR_HW_CH;i++)
	{
		struct i810_state *state = card->states[i];
		struct i810_channel *c;
		unsigned long port = card->iobase;
		u16 status;
		
		if(!state)
			continue;
		if(!state->dmabuf.ready)
			continue;
		c=state->dmabuf.channel;
		
		port+=c->port;
		
//		printk("PORT %lX (", port);
		
		status = inw(port + OFF_SR);
		
//		printk("ST%d ", status);
		
		if(status & DMA_INT_LVI)
		{
			/* Back to the start */
//			printk("LVI - STOP");
			outb((inb(port+OFF_CIV)-1)&31, port+OFF_LVI);
			i810_update_ptr(state);
			outb(0, port + OFF_CR);
		}
		if(status & DMA_INT_COMPLETE)
		{
			int x;
			/* Keep the card chasing its tail */
			outb(x=((inb(port+OFF_CIV)-1)&31), port+OFF_LVI);
			i810_update_ptr(state);
//			printk("COMP%d ",x);
		}
//		printk(")");
		outw(status & DMA_INT_MASK, port + OFF_SR);
	}
//	printk("\n");
}

static void i810_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct i810_card *card = (struct i810_card *)dev_id;
	u32 status;

	spin_lock(&card->lock);

	status = inl(card->iobase + GLOB_STA);
	if(!(status & INT_MASK)) 
	{
		spin_unlock(&card->lock);
		return;  /* not for us */
	}

//	printk("Interrupt %X: ", status);
	if(status & (INT_PO|INT_PI|INT_MC))
		i810_channel_interrupt(card);

 	/* clear 'em */
	outl(status & INT_MASK, card->iobase + GLOB_STA);
	spin_unlock(&card->lock);
}

static loff_t i810_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

/* in this loop, dmabuf.count signifies the amount of data that is waiting to be copied to
   the user's buffer.  it is filled by the dma machine and drained by this loop. */
static ssize_t i810_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

#ifdef DEBUG
	printk("i810_audio: i810_read called, count = %d\n", count);
#endif

	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (dmabuf->mapped)
		return -ENXIO;
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 1)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;

	while (count > 0) {
		spin_lock_irqsave(&state->card->lock, flags);
		if (dmabuf->count > (signed) dmabuf->dmasize) {
			/* buffer overrun, we are recovering from sleep_on_timeout,
			   resync hwptr and swptr, make process flush the buffer */
			dmabuf->count = dmabuf->dmasize;
			dmabuf->swptr = dmabuf->hwptr;
		}
		swptr = dmabuf->swptr;
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count < cnt)
			cnt = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			unsigned long tmo;
			/* buffer is empty, start the dma machine and wait for data to be
			   recorded */
			start_adc(state);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret) ret = -EAGAIN;
				return ret;
			}
			/* This isnt strictly right for the 810  but it'll do */
			tmo = (dmabuf->dmasize * HZ) / (dmabuf->rate * 2);
			tmo >>= sample_shift[dmabuf->fmt];
			/* There are two situations when sleep_on_timeout returns, one is when
			   the interrupt is serviced correctly and the process is waked up by
			   ISR ON TIME. Another is when timeout is expired, which means that
			   either interrupt is NOT serviced correctly (pending interrupt) or it
			   is TOO LATE for the process to be scheduled to run (scheduler latency)
			   which results in a (potential) buffer overrun. And worse, there is
			   NOTHING we can do to prevent it. */
			if (!interruptible_sleep_on_timeout(&dmabuf->wait, tmo)) {
#ifdef DEBUG
				printk(KERN_ERR "i810_audio: recording schedule timeout, "
				       "dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       dmabuf->dmasize, dmabuf->fragsize, dmabuf->count,
				       dmabuf->hwptr, dmabuf->swptr);
#endif
				/* a buffer overrun, we delay the recovery untill next time the
				   while loop begin and we REALLY have space to record */
			}
			if (signal_pending(current)) {
				ret = ret ? ret : -ERESTARTSYS;
				return ret;
			}
			continue;
		}

		if (copy_to_user(buffer, dmabuf->rawbuf + swptr, cnt)) {
			if (!ret) ret = -EFAULT;
			return ret;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;

		spin_lock_irqsave(&state->card->lock, flags);
		dmabuf->swptr = swptr;
		dmabuf->count -= cnt;
		spin_unlock_irqrestore(&state->card->lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_adc(state);
	}
	return ret;
}

/* in this loop, dmabuf.count signifies the amount of data that is waiting to be dma to
   the soundcard.  it is drained by the dma machine and filled by this loop. */
static ssize_t i810_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

#ifdef DEBUG
	printk("i810_audio: i810_write called, count = %d\n", count);
#endif

	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (dmabuf->mapped)
		return -ENXIO;
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 0)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;

	while (count > 0) {
		spin_lock_irqsave(&state->card->lock, flags);
		if (dmabuf->count < 0) {
			/* buffer underrun, we are recovering from sleep_on_timeout,
			   resync hwptr and swptr */
			dmabuf->count = 0;
			dmabuf->swptr = dmabuf->hwptr;
		}
		swptr = dmabuf->swptr;
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count + cnt > dmabuf->dmasize)
			cnt = dmabuf->dmasize - dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			unsigned long tmo;
			/* buffer is full, start the dma machine and wait for data to be
			   played */
			start_dac(state);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret) ret = -EAGAIN;
				return ret;
			}
			/* Not strictly correct but works */
			tmo = (dmabuf->dmasize * HZ) / (dmabuf->rate * 2);
			tmo >>= sample_shift[dmabuf->fmt];
			/* There are two situations when sleep_on_timeout returns, one is when
			   the interrupt is serviced correctly and the process is waked up by
			   ISR ON TIME. Another is when timeout is expired, which means that
			   either interrupt is NOT serviced correctly (pending interrupt) or it
			   is TOO LATE for the process to be scheduled to run (scheduler latency)
			   which results in a (potential) buffer underrun. And worse, there is
			   NOTHING we can do to prevent it. */
			if (!interruptible_sleep_on_timeout(&dmabuf->wait, tmo)) {
#ifdef DEBUG
				printk(KERN_ERR "i810_audio: playback schedule timeout, "
				       "dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       dmabuf->dmasize, dmabuf->fragsize, dmabuf->count,
				       dmabuf->hwptr, dmabuf->swptr);
#endif
				/* a buffer underrun, we delay the recovery untill next time the
				   while loop begin and we REALLY have data to play */
			}
			if (signal_pending(current)) {
				if (!ret) ret = -ERESTARTSYS;
				return ret;
			}
			continue;
		}
		if (copy_from_user(dmabuf->rawbuf + swptr, buffer, cnt)) {
			if (!ret) ret = -EFAULT;
			return ret;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;

		spin_lock_irqsave(&state->card->lock, flags);
		dmabuf->swptr = swptr;
		dmabuf->count += cnt;
		dmabuf->endcleared = 0;
		spin_unlock_irqrestore(&state->card->lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac(state);
	}
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int i810_poll(struct file *file, struct poll_table_struct *wait)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned int mask = 0;

	if (file->f_mode & FMODE_WRITE) {
		if (!dmabuf->ready && prog_dmabuf(state, 0))
			return 0;
		poll_wait(file, &dmabuf->wait, wait);
	}
	if (file->f_mode & FMODE_READ) {
		if (!dmabuf->ready && prog_dmabuf(state, 1))
			return 0;
		poll_wait(file, &dmabuf->wait, wait);
	}
	spin_lock_irqsave(&state->card->lock, flags);
	i810_update_ptr(state);
	if (file->f_mode & FMODE_READ) {
		if (dmabuf->count >= (signed)dmabuf->fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (dmabuf->mapped) {
			if (dmabuf->count >= (signed)dmabuf->fragsize)
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)dmabuf->dmasize >= dmabuf->count + (signed)dmabuf->fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&state->card->lock, flags);

	return mask;
}

static int i810_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	int ret = -EINVAL;
	unsigned long size;

	lock_kernel();
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf(state, 0)) != 0)
			goto out;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf(state, 1)) != 0)
			goto out;
	} else 
		goto out;

	ret = -EINVAL;
	if (vma->vm_pgoff != 0)
		goto out;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << dmabuf->buforder))
		goto out;
	ret = -EAGAIN;
	if (remap_page_range(vma->vm_start, virt_to_phys(dmabuf->rawbuf),
			     size, vma->vm_page_prot))
		goto out;
	dmabuf->mapped = 1;
	ret = 0;
out:
	unlock_kernel();
	return ret;
}

static int i810_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	int val, mapped, ret;

	mapped = ((file->f_mode & FMODE_WRITE) && dmabuf->mapped) ||
		((file->f_mode & FMODE_READ) && dmabuf->mapped);
#ifdef DEBUG
	printk("i810_audio: i810_ioctl, command = %2d, arg = 0x%08x\n",
	       _IOC_NR(cmd), arg ? *(int *)arg : 0);
#endif

	switch (cmd) 
	{
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_RESET:
		/* FIXME: spin_lock ? */
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(state);
			synchronize_irq();
			dmabuf->ready = 0;
			resync_dma_ptrs(state);
			dmabuf->swptr = dmabuf->hwptr = 0;
			dmabuf->count = dmabuf->total_bytes = 0;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(state);
			synchronize_irq();
			resync_dma_ptrs(state);
			dmabuf->ready = 0;
			dmabuf->swptr = dmabuf->hwptr = 0;
			dmabuf->count = dmabuf->total_bytes = 0;
		}
		return 0;

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_dac(state, file->f_flags & O_NONBLOCK);
		return 0;

	case SNDCTL_DSP_SPEED: /* set smaple rate */
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val >= 0) {
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(state);
				dmabuf->ready = 0;
				spin_lock_irqsave(&state->card->lock, flags);
				i810_set_dac_rate(state, val);
				spin_unlock_irqrestore(&state->card->lock, flags);
			}
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
				dmabuf->ready = 0;
				spin_lock_irqsave(&state->card->lock, flags);
				i810_set_adc_rate(state, val);
				spin_unlock_irqrestore(&state->card->lock, flags);
			}
		}
		return put_user(dmabuf->rate, (int *)arg);

	case SNDCTL_DSP_STEREO: /* set stereo or mono channel */
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if(val==0)
			return -EINVAL;
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(state);
			dmabuf->ready = 0;
			dmabuf->fmt = I810_FMT_STEREO;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(state);
			dmabuf->ready = 0;
			dmabuf->fmt = I810_FMT_STEREO;
		}
		return 0;

	case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf(state, 0)))
				return val;
			return put_user(dmabuf->fragsize, (int *)arg);
		}
		if (file->f_mode & FMODE_READ) {
			if ((val = prog_dmabuf(state, 1)))
				return val;
			return put_user(dmabuf->fragsize, (int *)arg);
		}

	case SNDCTL_DSP_GETFMTS: /* Returns a mask of supported sample format*/
		return put_user(AFMT_S16_LE, (int *)arg);

	case SNDCTL_DSP_SETFMT: /* Select sample format */
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(state);
				dmabuf->ready = 0;
			}
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
				dmabuf->ready = 0;
			}
		}
		return put_user(AFMT_S16_LE, (int *)arg);

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != 0) {
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(state);
				dmabuf->ready = 0;
			}
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
				dmabuf->ready = 0;
			}
		}
		return put_user(2, (int *)arg);

	case SNDCTL_DSP_POST:
		/* FIXME: the same as RESET ?? */
		return 0;

	case SNDCTL_DSP_SUBDIVIDE:
		if (dmabuf->subdivision)
			return -EINVAL;
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		dmabuf->subdivision = val;
		return 0;

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, (int *)arg))
			return -EFAULT;

		dmabuf->ossfragshift = val & 0xffff;
		dmabuf->ossmaxfrags = (val >> 16) & 0xffff;
		if (dmabuf->ossfragshift < 4)
			dmabuf->ossfragshift = 4;
		if (dmabuf->ossfragshift > 15)
			dmabuf->ossfragshift = 15;
		if (dmabuf->ossmaxfrags < 4)
			dmabuf->ossmaxfrags = 4;

		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!dmabuf->enable && (val = prog_dmabuf(state, 0)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		i810_update_ptr(state);
		abinfo.fragsize = dmabuf->fragsize;
		abinfo.bytes = dmabuf->dmasize - dmabuf->count;
		abinfo.fragstotal = dmabuf->numfrag;
		abinfo.fragments = abinfo.bytes >> dmabuf->fragshift;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!dmabuf->enable && (val = prog_dmabuf(state, 1)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		i810_update_ptr(state);
		abinfo.fragsize = dmabuf->fragsize;
		abinfo.bytes = dmabuf->count;
		abinfo.fragstotal = dmabuf->numfrag;
		abinfo.fragments = abinfo.bytes >> dmabuf->fragshift;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETCAPS:
	    return put_user(DSP_CAP_REALTIME|DSP_CAP_TRIGGER|DSP_CAP_MMAP|DSP_CAP_BIND,
			    (int *)arg);

	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if (file->f_mode & FMODE_READ && dmabuf->enable)
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & FMODE_WRITE && dmabuf->enable)
			val |= PCM_ENABLE_OUTPUT;
		return put_user(val, (int *)arg);

	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!dmabuf->ready && (ret = prog_dmabuf(state, 1)))
					return ret;
				start_adc(state);
			} else
				stop_adc(state);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!dmabuf->ready && (ret = prog_dmabuf(state, 0)))
					return ret;
				start_dac(state);
			} else
				stop_dac(state);
		}
		return 0;

	case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		spin_lock_irqsave(&state->card->lock, flags);
		i810_update_ptr(state);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.blocks = dmabuf->count >> dmabuf->fragshift;
		cinfo.ptr = dmabuf->hwptr;
		if (dmabuf->mapped)
			dmabuf->count &= dmabuf->fragsize-1;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

	case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&state->card->lock, flags);
		i810_update_ptr(state);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.blocks = dmabuf->count >> dmabuf->fragshift;
		cinfo.ptr = dmabuf->hwptr;
		if (dmabuf->mapped)
			dmabuf->count &= dmabuf->fragsize-1;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

	case SNDCTL_DSP_SETDUPLEX:
		return -EINVAL;

	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&state->card->lock, flags);
		i810_update_ptr(state);
		val = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return put_user(val, (int *)arg);

	case SOUND_PCM_READ_RATE:
		return put_user(dmabuf->rate, (int *)arg);

	case SOUND_PCM_READ_CHANNELS:
		return put_user((dmabuf->fmt & I810_FMT_STEREO) ? 2 : 1,
				(int *)arg);

	case SOUND_PCM_READ_BITS:
		return put_user(AFMT_S16_LE, (int *)arg);

	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_WRITE_FILTER:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;
	}
	return -EINVAL;
}

static int i810_open(struct inode *inode, struct file *file)
{
	int i = 0;
	struct i810_card *card = devs;
	struct i810_state *state = NULL;
	struct dmabuf *dmabuf = NULL;

	/* find an avaiable virtual channel (instance of /dev/dsp) */
	while (card != NULL) {
		for (i = 0; i < NR_HW_CH; i++) {
			if (card->states[i] == NULL) {
				state = card->states[i] = (struct i810_state *)
					kmalloc(sizeof(struct i810_state), GFP_KERNEL);
				if (state == NULL)
					return -ENOMEM;
				memset(state, 0, sizeof(struct i810_state));
				dmabuf = &state->dmabuf;
				goto found_virt;
			}
		}
		card = card->next;
	}
	/* no more virtual channel avaiable */
	if (!state)
		return -ENODEV;

 found_virt:
	/* found a free virtual channel, allocate hardware channels */
	if(file->f_mode & FMODE_READ)
		dmabuf->channel = card->alloc_rec_pcm_channel(card);
	else
		dmabuf->channel = card->alloc_pcm_channel(card);
		
	if (dmabuf->channel == NULL) {
		kfree (card->states[i]);
		card->states[i] = NULL;;
		return -ENODEV;
	}

	/* initialize the virtual channel */
	state->virt = i;
	state->card = card;
	state->magic = I810_STATE_MAGIC;
	init_waitqueue_head(&dmabuf->wait);
	init_MUTEX(&state->open_sem);
	file->private_data = state;

	down(&state->open_sem);

	/* set default sample format. According to OSS Programmer's Guide  /dev/dsp
	   should be default to unsigned 8-bits, mono, with sample rate 8kHz and
	   /dev/dspW will accept 16-bits sample */
	if (file->f_mode & FMODE_WRITE) {
		dmabuf->fmt &= ~I810_FMT_MASK;
		dmabuf->fmt |= I810_FMT_16BIT;
		dmabuf->ossfragshift = 0;
		dmabuf->ossmaxfrags  = 0;
		dmabuf->subdivision  = 0;
		i810_set_dac_rate(state, 48000);
	}

	if (file->f_mode & FMODE_READ) {
		dmabuf->fmt &= ~I810_FMT_MASK;
		dmabuf->fmt |= I810_FMT_16BIT;
		dmabuf->ossfragshift = 0;
		dmabuf->ossmaxfrags  = 0;
		dmabuf->subdivision  = 0;
		i810_set_adc_rate(state, 48000);
	}

	state->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	up(&state->open_sem);

	return 0;
}

static int i810_release(struct inode *inode, struct file *file)
{
	struct i810_state *state = (struct i810_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;

	lock_kernel();
	if (file->f_mode & FMODE_WRITE) {
		i810_clear_tail(state);
		drain_dac(state, file->f_flags & O_NONBLOCK);
	}

	/* stop DMA state machine and free DMA buffers/channels */
	down(&state->open_sem);

	if (file->f_mode & FMODE_WRITE) {
		stop_dac(state);
		dealloc_dmabuf(state);
		state->card->free_pcm_channel(state->card, dmabuf->channel->num);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(state);
		dealloc_dmabuf(state);
		state->card->free_pcm_channel(state->card, dmabuf->channel->num);
	}

	/* we're covered by the open_sem */
	up(&state->open_sem);
	
	kfree(state->card->states[state->virt]);
	state->card->states[state->virt] = NULL;
	unlock_kernel();

	return 0;
}

static /*const*/ struct file_operations i810_audio_fops = {
	owner:		THIS_MODULE,
	llseek:		i810_llseek,
	read:		i810_read,
	write:		i810_write,
	poll:		i810_poll,
	ioctl:		i810_ioctl,
	mmap:		i810_mmap,
	open:		i810_open,
	release:	i810_release,
};

/* Write AC97 codec registers */

static u16 i810_ac97_get(struct ac97_codec *dev, u8 reg)
{
	struct i810_card *card = dev->private_data;
	int count = 100;

	while(count-- && (inb(card->iobase + CAS) & 1)) 
		udelay(1);
	return inw(card->ac97base + (reg&0x7f));
}

static void i810_ac97_set(struct ac97_codec *dev, u8 reg, u16 data)
{
	struct i810_card *card = dev->private_data;
	int count = 100;

	while(count-- && (inb(card->iobase + CAS) & 1)) 
		udelay(1);
	outw(data, card->ac97base + (reg&0x7f));
}


/* OSS /dev/mixer file operation methods */

static int i810_open_mixdev(struct inode *inode, struct file *file)
{
	int i;
	int minor = MINOR(inode->i_rdev);
	struct i810_card *card = devs;

	for (card = devs; card != NULL; card = card->next)
		for (i = 0; i < NR_AC97; i++)
			if (card->ac97_codec[i] != NULL &&
			    card->ac97_codec[i]->dev_mixer == minor)
				goto match;

	if (!card)
		return -ENODEV;

 match:
	file->private_data = card->ac97_codec[i];

	return 0;
}

static int i810_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct ac97_codec *codec = (struct ac97_codec *)file->private_data;

	return codec->mixer_ioctl(codec, cmd, arg);
}

static /*const*/ struct file_operations i810_mixer_fops = {
	owner:		THIS_MODULE,
	llseek:		i810_llseek,
	ioctl:		i810_ioctl_mixdev,
	open:		i810_open_mixdev,
};

/* AC97 codec initialisation. */
static int __init i810_ac97_init(struct i810_card *card)
{
	int num_ac97 = 0;
	int ready_2nd = 0;
	struct ac97_codec *codec;
	u16 eid;
	int i=0;
	u32 reg;

	reg = inl(card->iobase + GLOB_CNT);
	
	if((reg&2)==0)	/* Cold required */
		reg|=2;
	else
		reg|=4;	/* Warm */
		
	reg&=~8;	/* ACLink on */
	outl(reg , card->iobase + GLOB_CNT);
	
	while(i<10)
	{
		if((inl(card->iobase+GLOB_CNT)&4)==0)
			break;
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(HZ/20);
		i++;
	}
	if(i==10)
	{
		printk(KERN_ERR "i810_audio: AC'97 reset failed.\n");
		return 0;
	}

	current->state = TASK_UNINTERRUPTIBLE;
	schedule_timeout(HZ/5);
		
	inw(card->ac97base);

	for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {
		if ((codec = kmalloc(sizeof(struct ac97_codec), GFP_KERNEL)) == NULL)
			return -ENOMEM;
		memset(codec, 0, sizeof(struct ac97_codec));

		/* initialize some basic codec information, other fields will be filled
		   in ac97_probe_codec */
		codec->private_data = card;
		codec->id = num_ac97;

		codec->codec_read = i810_ac97_get;
		codec->codec_write = i810_ac97_set;
	
		if (ac97_probe_codec(codec) == 0)
			break;

		eid = i810_ac97_get(codec, AC97_EXTENDED_ID);
		
		if(eid==0xFFFFFF)
		{
			printk(KERN_WARNING "i810_audio: no codec attached ?\n");
			kfree(codec);
			break;
		}
		
		card->ac97_features = eid;
				
		/* Now check the codec for useful features to make up for
		   the dumbness of the 810 hardware engine */

		if(!(eid&0x0001))
			printk(KERN_WARNING "i810_audio: only 48Khz playback available.\n");
		else
		{
			/* Enable variable rate mode */
			i810_ac97_set(codec, AC97_EXTENDED_STATUS, 9);
			i810_ac97_set(codec,AC97_EXTENDED_STATUS,
				i810_ac97_get(codec, AC97_EXTENDED_STATUS)|0xE800);
			/* power up everything, modify this when implementing power saving */
			i810_ac97_set(codec, AC97_POWER_CONTROL,
				i810_ac97_get(codec, AC97_POWER_CONTROL) & ~0x7f00);
			/* wait for analog ready */
		        for (i=10;
			     i && ((i810_ac97_get(codec, AC97_POWER_CONTROL) & 0xf) != 0xf);
			     i--)
			{
				current->state = TASK_UNINTERRUPTIBLE;
				schedule_timeout(HZ/20);
			}

			if(!(i810_ac97_get(codec, AC97_EXTENDED_STATUS)&1))
			{
				printk(KERN_WARNING "i810_audio: Codec refused to allow VRA, using 48Khz only.\n");
				card->ac97_features&=~1;
			}
		}
   		
		if ((codec->dev_mixer = register_sound_mixer(&i810_mixer_fops, -1)) < 0) {
			printk(KERN_ERR "i810_audio: couldn't register mixer!\n");
			kfree(codec);
			break;
		}

		card->ac97_codec[num_ac97] = codec;

		/* if there is no secondary codec at all, don't probe any more */
		if (!ready_2nd)
			return num_ac97+1;
	}
	return num_ac97;
}

/* install the driver, we do not allocate hardware channel nor DMA buffer now, they are defered 
   untill "ACCESS" time (in prog_dmabuf called by open/read/write/ioctl/mmap) */
   
static int __init i810_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
	struct i810_card *card;

	if (!pci_dma_supported(pci_dev, I810_DMA_MASK)) {
		printk(KERN_ERR "intel810: architecture does not support"
		       " 32bit PCI busmaster DMA\n");
		return -ENODEV;
	}

	if (pci_enable_device(pci_dev))
		return -EIO;
	if ((card = kmalloc(sizeof(struct i810_card), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "i810_audio: out of memory\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(*card));

	card->iobase = pci_resource_start (pci_dev, 1);
	card->ac97base = pci_resource_start (pci_dev, 0);
	card->pci_dev = pci_dev;
	card->pci_id = pci_id->device;
	card->irq = pci_dev->irq;
	card->next = devs;
	card->magic = I810_CARD_MAGIC;
	spin_lock_init(&card->lock);
	devs = card;

	pci_set_master(pci_dev);

	printk(KERN_INFO "i810: %s found at IO 0x%04lx and 0x%04lx, IRQ %d\n",
	       card_names[pci_id->driver_data], card->iobase, card->ac97base, 
	       card->irq);

	card->alloc_pcm_channel = i810_alloc_pcm_channel;
	card->alloc_rec_pcm_channel = i810_alloc_rec_pcm_channel;
	card->free_pcm_channel = i810_free_pcm_channel;

	/* claim our iospace and irq */
	request_region(card->iobase, 64, card_names[pci_id->driver_data]);
	request_region(card->ac97base, 256, card_names[pci_id->driver_data]);

	if (request_irq(card->irq, &i810_interrupt, SA_SHIRQ,
			card_names[pci_id->driver_data], card)) {
		printk(KERN_ERR "i810_audio: unable to allocate irq %d\n", card->irq);
		release_region(card->iobase, 64);
		release_region(card->ac97base, 256);
		kfree(card);
		return -ENODEV;
	}
	/* register /dev/dsp */
	if ((card->dev_audio = register_sound_dsp(&i810_audio_fops, -1)) < 0) {
		printk(KERN_ERR "i810_audio: couldn't register DSP device!\n");
		release_region(card->iobase, 64);
		release_region(card->ac97base, 256);
		free_irq(card->irq, card);
		kfree(card);
		return -ENODEV;
	}


	/* initialize AC97 codec and register /dev/mixer */
	if (i810_ac97_init(card) <= 0) {
		unregister_sound_dsp(card->dev_audio);
		release_region(card->iobase, 64);
		release_region(card->ac97base, 256);
		free_irq(card->irq, card);
		kfree(card);
		return -ENODEV;
	}
	pci_dev->driver_data = card;
	pci_dev->dma_mask = I810_DMA_MASK;
	return 0;
}

static void __exit i810_remove(struct pci_dev *pci_dev)
{
	int i;
	struct i810_card *card = pci_dev->driver_data;
	/* free hardware resources */
	free_irq(card->irq, devs);
	release_region(card->iobase, 64);
	release_region(card->ac97base, 256);

	/* unregister audio devices */
	for (i = 0; i < NR_AC97; i++)
		if (devs->ac97_codec[i] != NULL) {
			unregister_sound_mixer(card->ac97_codec[i]->dev_mixer);
			kfree (card->ac97_codec[i]);
		}
	unregister_sound_dsp(card->dev_audio);
	kfree(card);
}


MODULE_AUTHOR("");
MODULE_DESCRIPTION("Intel 810 audio support");
MODULE_PARM(ftsodell, "i");
MODULE_PARM(clocking, "i");

#define I810_MODULE_NAME "intel810_audio"

static struct pci_driver i810_pci_driver = {
	name:		I810_MODULE_NAME,
	id_table:	i810_pci_tbl,
	probe:		i810_probe,
	remove:		i810_remove,
};

static int __init i810_init_module (void)
{
	if (!pci_present())   /* No PCI bus in this machine! */
		return -ENODEV;

	if(ftsodell==1)
		clocking=41194;
		
	printk(KERN_INFO "Intel 810 + AC97 Audio, version "
	       DRIVER_VERSION ", " __TIME__ " " __DATE__ "\n");

	if (!pci_register_driver(&i810_pci_driver)) {
		pci_unregister_driver(&i810_pci_driver);
                return -ENODEV;
	}
	return 0;
}

static void __exit i810_cleanup_module (void)
{
	pci_unregister_driver(&i810_pci_driver);
}

module_init(i810_init_module);
module_exit(i810_cleanup_module);
