/*
 *	Crystal SoundFusion CS46xx driver
 *
 *	Copyright 1998-2000 Cirrus Logic Corporation <audio@crystal.cirrus.com>
 *	Copyright 1999-2000 Jaroslav Kysela <perex@suse.cz>
 *	Copyright 2000 Alan Cox <alan@redhat.com>
 *
 *	The core of this code is taken from the ALSA project driver by 
 *	Jaroslav. Please send Jaroslav the credit for the driver and 
 *	report bugs in this port to <alan@redhat.com>
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
 *	Current maintainers:
 *		Cirrus Logic Corporation, Thomas Woller (tw)
 *			<twoller@crystal.cirrus.com>
 *		Nils Faerber (nf)
 *			<nils@kernelconcepts.de>
 *		Thanks to David Pollard for testing.
 *
 *	Changes:
 *	20000909-nf	Changed cs_read, cs_write and drain_dac
 *	20001025-tw	Separate Playback/Capture structs and buffers.
 *			Added Scatter/Gather support for Playback.
 *			Added Capture.
 *	20001027-nf	Port to kernel 2.4.0-test9, some clean-ups
 *			Start of powermanagement support (CS46XX_PM).
 *	20001128-tw	Add module parm for default buffer order.
 *			added DMA_GFP flag to kmalloc dma buffer allocs.
 *			backfill silence to eliminate stuttering on
 *			underruns.
 *	20001201-tw	add resyncing of swptr on underruns.
 *	20001205-tw-nf	fixed GETOSPACE ioctl() after open()
 *
 *
 *	Status:
 *	Playback/Capture supported from 8k-48k.
 *	16Bit Signed LE & 8Bit Unsigned, with Mono or Stereo supported.
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
#ifdef CS46XX_PM
#include <linux/pm.h>
#endif
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/ac97_codec.h>
#include <linux/wrapper.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>

#include "cs461x.h"


/* MIDI buffer sizes */
#define CS_MIDIINBUF  500
#define CS_MIDIOUTBUF 500

#define ADC_RUNNING	1
#define DAC_RUNNING	2

#define CS_FMT_16BIT	1		/* These are fixed in fact */
#define CS_FMT_STEREO	2
#define CS_FMT_MASK	3

#define CS_TYPE_ADC	1
#define CS_TYPE_DAC	2
/*
 *	CS461x definitions
 */
 
#define CS461X_BA0_SIZE		0x2000
#define CS461X_BA1_DATA0_SIZE	0x3000
#define CS461X_BA1_DATA1_SIZE	0x3800
#define CS461X_BA1_PRG_SIZE	0x7000
#define CS461X_BA1_REG_SIZE	0x0100

#define GOF_PER_SEC	200

#define CSDEBUG_INTERFACE 1
#define CSDEBUG 1
/*
 * Turn on/off debugging compilation by using 1/0 respectively for CSDEBUG
 *
 *
 * CSDEBUG is usual mode is set to 1, then use the
 * cs_debuglevel and cs_debugmask to turn on or off debugging.
 * Debug level of 1 has been defined to be kernel errors and info
 * that should be printed on any released driver.
 */
#if CSDEBUG
#define CS_DBGOUT(mask,level,x) if((cs_debuglevel >= (level)) && ((mask) & cs_debugmask)) {x;} 
#else
#define CS_DBGOUT(mask,level,x) 
#endif
/*
 * cs_debugmask areas
 */
#define CS_INIT	 	0x00000001		/* initialization and probe functions */
#define CS_ERROR 	0x00000002		/* tmp debugging bit placeholder */
#define CS_INTERRUPT	0x00000004		/* interrupt handler (separate from all other) */
#define CS_FUNCTION 	0x00000008		/* enter/leave functions */
#define CS_WAVE_WRITE 	0x00000010		/* write information for wave */
#define CS_WAVE_READ 	0x00000020		/* read information for wave */
#define CS_MIDI_WRITE 	0x00000040		/* write information for midi */
#define CS_MIDI_READ 	0x00000080		/* read information for midi */
#define CS_MPU401_WRITE 0x00000100		/* write information for mpu401 */
#define CS_MPU401_READ 	0x00000200		/* read information for mpu401 */
#define CS_OPEN		0x00000400		/* all open functions in the driver */
#define CS_RELEASE	0x00000800		/* all release functions in the driver */
#define CS_PARMS	0x00001000		/* functional and operational parameters */
#define CS_IOCTL	0x00002000		/* ioctl (non-mixer) */
#define CS_TMP		0x10000000		/* tmp debug mask bit */

#if CSDEBUG
static unsigned long cs_debuglevel=1;			/* levels range from 1-9 */
static unsigned long cs_debugmask=CS_INIT | CS_ERROR;	/* use CS_DBGOUT with various mask values */
#endif
#define DMABUF_DEFAULTORDER 3
static unsigned long defaultorder=DMABUF_DEFAULTORDER;
#if MODULE
MODULE_PARM(defaultorder, "i");
MODULE_PARM(cs_debuglevel, "i");
MODULE_PARM(cs_debugmask, "i");
#endif

static int external_amp = 0;
static int thinkpad = 0;


/* An instance of the 4610 channel */

struct cs_channel 
{
	int used;
	int num;
	void *state;
};

#define DRIVER_VERSION "1.10"

/* magic numbers to protect our data structures */
#define CS_CARD_MAGIC		0x43525553 /* "CRUS" */
#define CS_STATE_MAGIC		0x4c4f4749 /* "LOGI" */
#define NR_HW_CH		3

/* maxinum number of AC97 codecs connected, AC97 2.0 defined 4 */
#define NR_AC97		2

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

/* "software" or virtual channel, an instance of opened /dev/dsp */
struct cs_state {
	unsigned int magic;
	struct cs_card *card;	/* Card info */

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
		struct cs_channel *channel;
		int pringbuf;		/* Software ring slot */
		void *pbuf;		/* 4K hardware DMA buffer */

		/* OSS buffer management stuff */
		void *rawbuf;
		dma_addr_t dma_handle;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;
		unsigned divisor;
		unsigned type;
		void *tmpbuff;			/* tmp buffer for sample conversions */
		dma_addr_t dma_handle_tmpbuff;
		unsigned buforder_tmpbuff;	/* Log base 2 of size in bytes.. */

		/* our buffer acts like a circular ring */
		unsigned hwptr;		/* where dma last started, updated by update_ptr */
		unsigned swptr;		/* where driver last clear/filled, updated by read/write */
		int count;		/* bytes to be comsumed or been generated by dma machine */
		unsigned total_bytes;	/* total bytes dmaed by hardware */
		unsigned blocks;	/* total blocks */

		unsigned error;		/* number of over/underruns */
		unsigned underrun;	/* underrun pending before next write has occurred */
		wait_queue_head_t wait;	/* put process on wait queue when no more space in buffer */

		/* redundant, but makes calculations easier */
		unsigned fragsize;
		unsigned dmasize;
		unsigned fragsamples;

		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned endcleared:1;
		unsigned SGok:1;
		unsigned update_flag;
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;
	} dmabuf;
};


struct cs_card {
	struct cs_channel channel[2];
	unsigned int magic;

	/* We keep cs461x cards in a linked list */
	struct cs_card *next;

	/* The cs461x has a certain amount of cross channel interaction
	   so we use a single per card lock */
	spinlock_t lock;

	/* PCI device stuff */
	struct pci_dev * pci_dev;

	unsigned int pctl, cctl;	/* Hardware DMA flag sets */

	/* soundcore stuff */
	int dev_audio;
	int dev_midi;

	/* structures for abstraction of hardware facilities, codecs, banks and channels*/
	struct ac97_codec *ac97_codec[NR_AC97];
	struct cs_state *states[2];

	u16 ac97_features;
	
	int amplifier;			/* Amplifier control */
	void (*amplifier_ctrl)(struct cs_card *, int);
	
	int active;			/* Active clocking */
	void (*active_ctrl)(struct cs_card *, int);
	
	/* hardware resources */
	unsigned long ba0_addr;
	unsigned long ba1_addr;
	u32 irq;
	
	/* mappings */
	void *ba0;
	union
	{
		struct
		{
			u8 *data0;
			u8 *data1;
			u8 *pmem;
			u8 *reg;
		} name;
		u8 *idx[4];
	} ba1;
	
	/* Function support */
	struct cs_channel *(*alloc_pcm_channel)(struct cs_card *);
	struct cs_channel *(*alloc_rec_pcm_channel)(struct cs_card *);
	void (*free_pcm_channel)(struct cs_card *, int chan);

	/* /dev/midi stuff */
	struct {
		unsigned ird, iwr, icnt;
		unsigned ord, owr, ocnt;
		wait_queue_head_t open_wait;
		wait_queue_head_t iwait;
		wait_queue_head_t owait;
		spinlock_t lock;
		unsigned char ibuf[CS_MIDIINBUF];
		unsigned char obuf[CS_MIDIOUTBUF];
		mode_t open_mode;
		struct semaphore open_sem;
	} midi;
};

static struct cs_card *devs = NULL;

static int cs_open_mixdev(struct inode *inode, struct file *file);
static int cs_release_mixdev(struct inode *inode, struct file *file);
static int cs_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd,
				unsigned long arg);
static loff_t cs_llseek(struct file *file, loff_t offset, int origin);

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

#if CSDEBUG

/* DEBUG ROUTINES */

#define SOUND_MIXER_CS_GETDBGLEVEL 	_SIOWR('M',120, int)
#define SOUND_MIXER_CS_SETDBGLEVEL 	_SIOWR('M',121, int)
#define SOUND_MIXER_CS_GETDBGMASK 	_SIOWR('M',122, int)
#define SOUND_MIXER_CS_SETDBGMASK 	_SIOWR('M',123, int)

#define SNDCTL_DSP_CS_GETDBGLEVEL 	_SIOWR('P', 50, int)
#define SNDCTL_DSP_CS_SETDBGLEVEL 	_SIOWR('P', 51, int)
#define SNDCTL_DSP_CS_GETDBGMASK 	_SIOWR('P', 52, int)
#define SNDCTL_DSP_CS_SETDBGMASK 	_SIOWR('P', 53, int)

static void printioctl(unsigned int x)
{
    unsigned int i;
    unsigned char vidx;
	/* these values are incorrect for the ac97 driver, fix.
         * Index of mixtable1[] member is Device ID 
         * and must be <= SOUND_MIXER_NRDEVICES.
         * Value of array member is index into s->mix.vol[]
         */
        static const unsigned char mixtable1[SOUND_MIXER_NRDEVICES] = {
                [SOUND_MIXER_PCM]     = 1,   /* voice */
                [SOUND_MIXER_LINE1]   = 2,   /* AUX */
                [SOUND_MIXER_CD]      = 3,   /* CD */
                [SOUND_MIXER_LINE]    = 4,   /* Line */
                [SOUND_MIXER_SYNTH]   = 5,   /* FM */
                [SOUND_MIXER_MIC]     = 6,   /* Mic */
                [SOUND_MIXER_SPEAKER] = 7,   /* Speaker */
                [SOUND_MIXER_RECLEV]  = 8,   /* Recording level */
                [SOUND_MIXER_VOLUME]  = 9    /* Master Volume */
        };
        
    switch(x) 
    {
	case SOUND_MIXER_CS_GETDBGMASK:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_CS_GETDBGMASK: ") );
		break;
	case SOUND_MIXER_CS_GETDBGLEVEL:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_CS_GETDBGLEVEL: ") );
		break;
	case SOUND_MIXER_CS_SETDBGMASK:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_CS_SETDBGMASK: ") );
		break;
	case SOUND_MIXER_CS_SETDBGLEVEL:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_CS_SETDBGLEVEL: ") );
		break;
        case OSS_GETVERSION:
		CS_DBGOUT(CS_IOCTL, 4, printk("OSS_GETVERSION: ") );
		break;
        case SNDCTL_DSP_SYNC:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SYNC: ") );
		break;
        case SNDCTL_DSP_SETDUPLEX:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SETDUPLEX: ") );
		break;
        case SNDCTL_DSP_GETCAPS:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETCAPS: ") );
		break;
        case SNDCTL_DSP_RESET:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_RESET: ") );
		break;
        case SNDCTL_DSP_SPEED:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SPEED: ") );
		break;
        case SNDCTL_DSP_STEREO:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_STEREO: ") );
		break;
        case SNDCTL_DSP_CHANNELS:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_CHANNELS: ") );
		break;
        case SNDCTL_DSP_GETFMTS: 
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETFMTS: ") );
		break;
        case SNDCTL_DSP_SETFMT: 
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SETFMT: ") );
		break;
        case SNDCTL_DSP_POST:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_POST: ") );
		break;
        case SNDCTL_DSP_GETTRIGGER:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETTRIGGER: ") );
		break;
        case SNDCTL_DSP_SETTRIGGER:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SETTRIGGER: ") );
		break;
        case SNDCTL_DSP_GETOSPACE:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETOSPACE: ") );
		break;
        case SNDCTL_DSP_GETISPACE:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETISPACE: ") );
		break;
        case SNDCTL_DSP_NONBLOCK:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_NONBLOCK: ") );
		break;
        case SNDCTL_DSP_GETODELAY:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETODELAY: ") );
		break;
        case SNDCTL_DSP_GETIPTR:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETIPTR: ") );
		break;
        case SNDCTL_DSP_GETOPTR:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETOPTR: ") );
		break;
        case SNDCTL_DSP_GETBLKSIZE:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_GETBLKSIZE: ") );
		break;
        case SNDCTL_DSP_SETFRAGMENT:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SETFRAGMENT: ") );
		break;
        case SNDCTL_DSP_SUBDIVIDE:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SUBDIVIDE: ") );
		break;
        case SOUND_PCM_READ_RATE:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_PCM_READ_RATE: ") );
		break;
        case SOUND_PCM_READ_CHANNELS:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_PCM_READ_CHANNELS: ") );
		break;
        case SOUND_PCM_READ_BITS:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_PCM_READ_BITS: ") );
		break;
        case SOUND_PCM_WRITE_FILTER:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_PCM_WRITE_FILTER: ") );
		break;
        case SNDCTL_DSP_SETSYNCRO:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_SETSYNCRO: ") );
		break;
        case SOUND_PCM_READ_FILTER:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_PCM_READ_FILTER: ") );
		break;
        case SNDCTL_DSP_CS_GETDBGMASK:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_CS_GETDBGMASK: ") );
		break;
        case SNDCTL_DSP_CS_GETDBGLEVEL:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_CS_GETDBGLEVEL: ") );
		break;
        case SNDCTL_DSP_CS_SETDBGMASK:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_CS_SETDBGMASK: ") );
		break;
        case SNDCTL_DSP_CS_SETDBGLEVEL:
		CS_DBGOUT(CS_IOCTL, 4, printk("SNDCTL_DSP_CS_SETDBGLEVEL: ") );
		break;

        case SOUND_MIXER_PRIVATE1:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_PRIVATE1: ") );
		break;
        case SOUND_MIXER_PRIVATE2:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_PRIVATE2: ") );
		break;
        case SOUND_MIXER_PRIVATE3:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_PRIVATE3: ") );
		break;
        case SOUND_MIXER_PRIVATE4:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_PRIVATE4: ") );
		break;
        case SOUND_MIXER_PRIVATE5:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_PRIVATE5: ") );
		break;
        case SOUND_MIXER_INFO:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_INFO: ") );
		break;
        case SOUND_OLD_MIXER_INFO:
		CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_OLD_MIXER_INFO: ") );
		break;

	default:
		switch (_IOC_NR(x)) 
		{
			case SOUND_MIXER_VOLUME:
				CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_VOLUME: ") );
				break;
			case SOUND_MIXER_SPEAKER:
				CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_SPEAKER: ") );
				break;
			case SOUND_MIXER_RECLEV:
				CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_RECLEV: ") );
				break;
			case SOUND_MIXER_MIC:
				CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_MIC: ") );
				break;
			case SOUND_MIXER_SYNTH:
				CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_SYNTH: ") );
				break;
			case SOUND_MIXER_RECSRC: 
				CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_RECSRC: ") );
				break;
			case SOUND_MIXER_DEVMASK:
				CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_DEVMASK: ") );
				break;
			case SOUND_MIXER_RECMASK:
				CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_RECMASK: ") );
				break;
			case SOUND_MIXER_STEREODEVS: 
				CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_STEREODEVS: ") );
				break;
			case SOUND_MIXER_CAPS:
				CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_CAPS:") );
				break;
			default:
				i = _IOC_NR(x);
				if (i >= SOUND_MIXER_NRDEVICES || !(vidx = mixtable1[i]))
				{
					CS_DBGOUT(CS_IOCTL, 4, printk("UNKNOWN IOCTL: 0x%.8x NR=%d ",x,i) );
				}
				else
				{
					CS_DBGOUT(CS_IOCTL, 4, printk("SOUND_MIXER_IOCTL AC9x: 0x%.8x NR=%d ",
							x,i) );
				}
				break;
		}
    }
    CS_DBGOUT(CS_IOCTL, 4, printk("command = 0x%x IOC_NR=%d\n",x, _IOC_NR(x)) );
}
#endif

/*
 *  common I/O routines
 */

static void cs461x_poke(struct cs_card *codec, unsigned long reg, unsigned int val)
{
	writel(val, codec->ba1.idx[(reg >> 16) & 3]+(reg&0xffff));
}

static unsigned int cs461x_peek(struct cs_card *codec, unsigned long reg)
{
	return readl(codec->ba1.idx[(reg >> 16) & 3]+(reg&0xffff));
}

static void cs461x_pokeBA0(struct cs_card *codec, unsigned long reg, unsigned int val)
{
	writel(val, codec->ba0+reg);
}

static unsigned int cs461x_peekBA0(struct cs_card *codec, unsigned long reg)
{
	return readl(codec->ba0+reg);
}


static u16 cs_ac97_get(struct ac97_codec *dev, u8 reg);
static void cs_ac97_set(struct ac97_codec *dev, u8 reg, u16 data);

static struct cs_channel *cs_alloc_pcm_channel(struct cs_card *card)
{
	if(card->channel[1].used==1)
		return NULL;
	card->channel[1].used=1;
	card->channel[1].num=1;
	return &card->channel[1];
}

static struct cs_channel *cs_alloc_rec_pcm_channel(struct cs_card *card)
{
	if(card->channel[0].used==1)
		return NULL;
	card->channel[0].used=1;
	card->channel[0].num=0;
	return &card->channel[0];
}

static void cs_free_pcm_channel(struct cs_card *card, int channel)
{
	card->channel[channel].state = NULL;
	card->channel[channel].used=0;
}

/*
 * setup a divisor value to help with conversion from
 * 16bit Stereo, down to 8bit stereo/mono or 16bit mono.
 * assign a divisor of 1 if using 16bit Stereo as that is
 * the only format that the static image will capture.
 */
static void cs_set_divisor(struct dmabuf *dmabuf)
{
	if(dmabuf->type == CS_TYPE_DAC)
		dmabuf->divisor = 1;
	else if( !(dmabuf->fmt & CS_FMT_STEREO) && 
	    (dmabuf->fmt & CS_FMT_16BIT))
		dmabuf->divisor = 2;
	else if( (dmabuf->fmt & CS_FMT_STEREO) && 
	    !(dmabuf->fmt & CS_FMT_16BIT))
		dmabuf->divisor = 2;
	else if( !(dmabuf->fmt & CS_FMT_STEREO) && 
	    !(dmabuf->fmt & CS_FMT_16BIT))
		dmabuf->divisor = 4;
	else
		dmabuf->divisor = 1;

	CS_DBGOUT(CS_PARMS | CS_FUNCTION, 8, printk(
		"cs46xx: cs_set_divisor()- %s %d\n",
			(dmabuf->type == CS_TYPE_ADC) ? "ADC" : "DAC", 
			dmabuf->divisor) );
}

/* set playback sample rate */
static unsigned int cs_set_dac_rate(struct cs_state * state, unsigned int rate)
{	
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int tmp1, tmp2;
	unsigned int phiIncr;
	unsigned int correctionPerGOF, correctionPerSec;
	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: cs_set_dac_rate()+ %d\n",rate) );

	/*
	 *  Compute the values used to drive the actual sample rate conversion.
	 *  The following formulas are being computed, using inline assembly
	 *  since we need to use 64 bit arithmetic to compute the values:
	 *
	 *  phiIncr = floor((Fs,in * 2^26) / Fs,out)
	 *  correctionPerGOF = floor((Fs,in * 2^26 - Fs,out * phiIncr) /
         *                                   GOF_PER_SEC)
         *  ulCorrectionPerSec = Fs,in * 2^26 - Fs,out * phiIncr -M
         *                       GOF_PER_SEC * correctionPerGOF
	 *
	 *  i.e.
	 *
	 *  phiIncr:other = dividend:remainder((Fs,in * 2^26) / Fs,out)
	 *  correctionPerGOF:correctionPerSec =
	 *      dividend:remainder(ulOther / GOF_PER_SEC)
	 */
	tmp1 = rate << 16;
	phiIncr = tmp1 / 48000;
	tmp1 -= phiIncr * 48000;
	tmp1 <<= 10;
	phiIncr <<= 10;
	tmp2 = tmp1 / 48000;
	phiIncr += tmp2;
	tmp1 -= tmp2 * 48000;
	correctionPerGOF = tmp1 / GOF_PER_SEC;
	tmp1 -= correctionPerGOF * GOF_PER_SEC;
	correctionPerSec = tmp1;

	/*
	 *  Fill in the SampleRateConverter control block.
	 */
	 
	spin_lock_irq(&state->card->lock);
	cs461x_poke(state->card, BA1_PSRC,
	  ((correctionPerSec << 16) & 0xFFFF0000) | (correctionPerGOF & 0xFFFF));
	cs461x_poke(state->card, BA1_PPI, phiIncr);
	spin_unlock_irq(&state->card->lock);
	dmabuf->rate = rate;
	
	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: cs_set_dac_rate()- %d\n",rate) );
	return rate;
}

/* set recording sample rate */
static unsigned int cs_set_adc_rate(struct cs_state * state, unsigned int rate)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct cs_card *card = state->card;
	unsigned int phiIncr, coeffIncr, tmp1, tmp2;
	unsigned int correctionPerGOF, correctionPerSec, initialDelay;
	unsigned int frameGroupLength, cnt;
	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: cs_set_adc_rate()+ %d\n",rate) );

	/*
	 *  We can only decimate by up to a factor of 1/9th the hardware rate.
	 *  Correct the value if an attempt is made to stray outside that limit.
	 */
	if ((rate * 9) < 48000)
		rate = 48000 / 9;

	/*
	 *  We can not capture at at rate greater than the Input Rate (48000).
	 *  Return an error if an attempt is made to stray outside that limit.
	 */
	if (rate > 48000)
		rate = 48000;

	/*
	 *  Compute the values used to drive the actual sample rate conversion.
	 *  The following formulas are being computed, using inline assembly
	 *  since we need to use 64 bit arithmetic to compute the values:
	 *
	 *     coeffIncr = -floor((Fs,out * 2^23) / Fs,in)
	 *     phiIncr = floor((Fs,in * 2^26) / Fs,out)
	 *     correctionPerGOF = floor((Fs,in * 2^26 - Fs,out * phiIncr) /
	 *                                GOF_PER_SEC)
	 *     correctionPerSec = Fs,in * 2^26 - Fs,out * phiIncr -
	 *                          GOF_PER_SEC * correctionPerGOF
	 *     initialDelay = ceil((24 * Fs,in) / Fs,out)
	 *
	 * i.e.
	 *
	 *     coeffIncr = neg(dividend((Fs,out * 2^23) / Fs,in))
	 *     phiIncr:ulOther = dividend:remainder((Fs,in * 2^26) / Fs,out)
	 *     correctionPerGOF:correctionPerSec =
	 * 	    dividend:remainder(ulOther / GOF_PER_SEC)
	 *     initialDelay = dividend(((24 * Fs,in) + Fs,out - 1) / Fs,out)
	 */

	tmp1 = rate << 16;
	coeffIncr = tmp1 / 48000;
	tmp1 -= coeffIncr * 48000;
	tmp1 <<= 7;
	coeffIncr <<= 7;
	coeffIncr += tmp1 / 48000;
	coeffIncr ^= 0xFFFFFFFF;
	coeffIncr++;
	tmp1 = 48000 << 16;
	phiIncr = tmp1 / rate;
	tmp1 -= phiIncr * rate;
	tmp1 <<= 10;
	phiIncr <<= 10;
	tmp2 = tmp1 / rate;
	phiIncr += tmp2;
	tmp1 -= tmp2 * rate;
	correctionPerGOF = tmp1 / GOF_PER_SEC;
	tmp1 -= correctionPerGOF * GOF_PER_SEC;
	correctionPerSec = tmp1;
	initialDelay = ((48000 * 24) + rate - 1) / rate;

	/*
	 *  Fill in the VariDecimate control block.
	 */
	spin_lock_irq(&card->lock);
	cs461x_poke(card, BA1_CSRC,
		((correctionPerSec << 16) & 0xFFFF0000) | (correctionPerGOF & 0xFFFF));
	cs461x_poke(card, BA1_CCI, coeffIncr);
	cs461x_poke(card, BA1_CD,
		(((BA1_VARIDEC_BUF_1 + (initialDelay << 2)) << 16) & 0xFFFF0000) | 0x80);
	cs461x_poke(card, BA1_CPI, phiIncr);
	spin_unlock_irq(&card->lock);

	/*
	 *  Figure out the frame group length for the write back task.  Basically,
	 *  this is just the factors of 24000 (2^6*3*5^3) that are not present in
	 *  the output sample rate.
	 */
	frameGroupLength = 1;
	for (cnt = 2; cnt <= 64; cnt *= 2) {
		if (((rate / cnt) * cnt) != rate)
			frameGroupLength *= 2;
	}
	if (((rate / 3) * 3) != rate) {
		frameGroupLength *= 3;
	}
	for (cnt = 5; cnt <= 125; cnt *= 5) {
		if (((rate / cnt) * cnt) != rate) 
			frameGroupLength *= 5;
        }

	/*
	 * Fill in the WriteBack control block.
	 */
	spin_lock_irq(&card->lock);
	cs461x_poke(card, BA1_CFG1, frameGroupLength);
	cs461x_poke(card, BA1_CFG2, (0x00800000 | frameGroupLength));
	cs461x_poke(card, BA1_CCST, 0x0000FFFF);
	cs461x_poke(card, BA1_CSPB, ((65536 * rate) / 24000));
	cs461x_poke(card, (BA1_CSPB + 4), 0x0000FFFF);
	spin_unlock_irq(&card->lock);
	dmabuf->rate = rate;
	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: cs_set_adc_rate()- %d\n",rate) );
	return rate;
}

/* prepare channel attributes for playback */ 
static void cs_play_setup(struct cs_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct cs_card *card = state->card;
        unsigned int tmp, Count, playFormat;

	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: cs_play_setup()+\n") );
        cs461x_poke(card, BA1_PVOL, 0x80008000);
        if(!dmabuf->SGok)
               cs461x_poke(card, BA1_PBA, virt_to_bus(dmabuf->pbuf));
    
        Count = 4;                                                          
        playFormat=cs461x_peek(card, BA1_PFIE);                             
        if ((dmabuf->fmt & CS_FMT_STEREO)) {                                
                playFormat &= ~DMA_RQ_C2_AC_MONO_TO_STEREO;                 
                Count *= 2;                                                 
        }                                                                   
        else                                                                
                playFormat |= DMA_RQ_C2_AC_MONO_TO_STEREO;                  
                                                                            
        if ((dmabuf->fmt & CS_FMT_16BIT)) {                                 
                playFormat &= ~(DMA_RQ_C2_AC_8_TO_16_BIT                    
                           | DMA_RQ_C2_AC_SIGNED_CONVERT);                  
                Count *= 2;                                                 
        }                                                                   
        else                                                                
                playFormat |= (DMA_RQ_C2_AC_8_TO_16_BIT                     
                           | DMA_RQ_C2_AC_SIGNED_CONVERT);                  
                                                                            
        cs461x_poke(card, BA1_PFIE, playFormat);                            
                                                                            
        tmp = cs461x_peek(card, BA1_PDTC);                                  
        tmp &= 0xfffffe00;                                                  
        cs461x_poke(card, BA1_PDTC, tmp | --Count);                         

	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: cs_play_setup()-\n") );

}

struct InitStruct
{
    u32 long off;
    u32 long val;
} InitArray[] = { {0x00000040, 0x3fc0000f},
                  {0x0000004c, 0x04800000},

                  {0x000000b3, 0x00000780},
                  {0x000000b7, 0x00000000},
                  {0x000000bc, 0x07800000},

                  {0x000000cd, 0x00800000},
                };

/*
 * "SetCaptureSPValues()" -- Initialize record task values before each
 * 	capture startup.  
 */
void SetCaptureSPValues(struct cs_card *card)
{
	unsigned i, offset;
	CS_DBGOUT(CS_FUNCTION, 8, printk("cs46xx: SetCaptureSPValues()+\n") );
	for(i=0; i<sizeof(InitArray)/sizeof(struct InitStruct); i++)
	{
		offset = InitArray[i].off*4; /* 8bit to 32bit offset value */
		cs461x_poke(card, offset, InitArray[i].val );
	}
	CS_DBGOUT(CS_FUNCTION, 8, printk("cs46xx: SetCaptureSPValues()-\n") );
}

/* prepare channel attributes for recording */
static void cs_rec_setup(struct cs_state *state)
{
	struct cs_card *card = state->card;
	struct dmabuf *dmabuf = &state->dmabuf;
	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: cs_rec_setup()+\n") );

	SetCaptureSPValues(card);

	/*
	 * set the attenuation to 0dB 
	 */
	cs461x_poke(card, BA1_CVOL, 0x80008000);

	/*
	 * set the physical address of the capture buffer into the SP
	 */
	cs461x_poke(card, BA1_CBA, virt_to_bus(dmabuf->rawbuf));

	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: cs_rec_setup()-\n") );
}


/* get current playback/recording dma buffer pointer (byte offset from LBA),
   called with spinlock held! */
   
extern __inline__ unsigned cs_get_dma_addr(struct cs_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	u32 offset;
	
	if ( (!(dmabuf->enable & DAC_RUNNING)) &&
	     (!(dmabuf->enable & ADC_RUNNING) ) )
	{
		CS_DBGOUT(CS_ERROR, 2, printk(
			"cs46xx: ERROR cs_get_dma_addr(): not enabled \n") );
		return 0;
	}
		
	/*
	 * ganularity is byte boundry, good part.
	 */
	if(dmabuf->enable & DAC_RUNNING)
	{
		offset = cs461x_peek(state->card, BA1_PBA);                                  
	}
	else /* ADC_RUNNING must be set */
	{
		offset = cs461x_peek(state->card, BA1_CBA);                                  
	}
	CS_DBGOUT(CS_PARMS | CS_FUNCTION, 9, 
		printk("cs46xx: cs_get_dma_addr() %d\n",offset) );
	offset = (u32)bus_to_virt((unsigned long)offset) - (u32)dmabuf->rawbuf;
	CS_DBGOUT(CS_PARMS | CS_FUNCTION, 8, 
		printk("cs46xx: cs_get_dma_addr()- %d\n",offset) );
	return offset;
}

static void resync_dma_ptrs(struct cs_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	int offset;
	
	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: resync_dma_ptrs()+ \n") );
	offset = 0;
	dmabuf->hwptr=dmabuf->swptr = 0;
	dmabuf->pringbuf = 0;
	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: resync_dma_ptrs()- \n") );
}
	
/* Stop recording (lock held) */
extern __inline__ void __stop_adc(struct cs_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct cs_card *card = state->card;
	unsigned int tmp;
	
	dmabuf->enable &= ~ADC_RUNNING;
	
	tmp = cs461x_peek(card, BA1_CCTL);
	tmp &= 0xFFFF0000;
	cs461x_poke(card, BA1_CCTL, tmp );
}

static void stop_adc(struct cs_state *state)
{
	unsigned long flags;

	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: stop_adc()+ \n") );
	spin_lock_irqsave(&state->card->lock, flags);
	__stop_adc(state);
	spin_unlock_irqrestore(&state->card->lock, flags);
	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: stop_adc()- \n") );
}

static void start_adc(struct cs_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct cs_card *card = state->card;
	unsigned long flags;
	unsigned int tmp;

	spin_lock_irqsave(&card->lock, flags);
	if (!(dmabuf->enable & ADC_RUNNING) && 
	     ((dmabuf->mapped || dmabuf->count < (signed)dmabuf->dmasize) 
	       && dmabuf->ready)) 
	{
		dmabuf->enable |= ADC_RUNNING;
		cs_set_divisor(dmabuf);
		tmp = cs461x_peek(card, BA1_CCTL);
		tmp &= 0xFFFF0000;
		tmp |= card->cctl;
		CS_DBGOUT(CS_FUNCTION, 2, printk(
			"cs46xx: start_adc() poke 0x%x \n",tmp) );
		cs461x_poke(card, BA1_CCTL, tmp);
	}
	spin_unlock_irqrestore(&card->lock, flags);
}

/* stop playback (lock held) */
extern __inline__ void __stop_dac(struct cs_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct cs_card *card = state->card;
	unsigned int tmp;

	dmabuf->enable &= ~DAC_RUNNING;
	
	tmp=cs461x_peek(card, BA1_PCTL);
	tmp&=0xFFFF;
	cs461x_poke(card, BA1_PCTL, tmp);
}

static void stop_dac(struct cs_state *state)
{
	unsigned long flags;

	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: stop_dac()+ \n") );
	spin_lock_irqsave(&state->card->lock, flags);
	__stop_dac(state);
	spin_unlock_irqrestore(&state->card->lock, flags);
	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: stop_dac()- \n") );
}	

static void start_dac(struct cs_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct cs_card *card = state->card;
	unsigned long flags;
	int tmp;

	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: start_dac()+ \n") );
	spin_lock_irqsave(&card->lock, flags);
	if (!(dmabuf->enable & DAC_RUNNING) && 
	    ((dmabuf->mapped || dmabuf->count > 0) && dmabuf->ready)) {
		dmabuf->enable |= DAC_RUNNING;
		tmp = cs461x_peek(card, BA1_PCTL);
		tmp &= 0xFFFF;
		tmp |= card->pctl;
		CS_DBGOUT(CS_PARMS, 6, printk(
		    "cs46xx: start_dac() poke card=0x%.08x tmp=0x%.08x addr=0x%.08x \n",
		    (unsigned)card, (unsigned)tmp, 
		    (unsigned)card->ba1.idx[(BA1_PCTL >> 16) & 3]+(BA1_PCTL&0xffff) ) );
		cs461x_poke(card, BA1_PCTL, tmp);
	}
	spin_unlock_irqrestore(&card->lock, flags);
	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: start_dac()- \n") );
}

#define DMABUF_MINORDER 1

/*
 * allocate DMA buffer, playback and recording buffers are separate.
 */
static int alloc_dmabuf(struct cs_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	void *rawbuf = NULL;
	void *tmpbuff = NULL;
	int order;
	struct page *page, *pend;

	/* alloc as big a chunk as we can */
	for (order = defaultorder; order >= DMABUF_MINORDER; order--)
		if((rawbuf = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA, order)))
			break;

	if (!rawbuf)
		return -ENOMEM;

	dmabuf->buforder = order;
	dmabuf->rawbuf = rawbuf;

	/* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
	pend = virt_to_page(rawbuf + (PAGE_SIZE << order) - 1);
	for (page = virt_to_page(rawbuf); page <= pend; page++)
		mem_map_reserve(page);

	CS_DBGOUT(CS_PARMS, 9, printk("cs461x: allocated %ld (order = %d) bytes at %p\n",
	       PAGE_SIZE << order, order, rawbuf) );
/*
 * now the temp buffer for 16/8 conversions
 */
	for (order = defaultorder; order >= DMABUF_MINORDER; order--)
		if((tmpbuff = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA, order)))
			break;
	if (!tmpbuff)
		return -ENOMEM;
	CS_DBGOUT(CS_PARMS, 9, printk("cs461x: allocated %ld (order = %d) bytes at %p\n",
	       PAGE_SIZE << order, order, tmpbuff) );

	dmabuf->tmpbuff = tmpbuff;
	dmabuf->buforder_tmpbuff = order;
	
	/* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
	pend = virt_to_page(tmpbuff + (PAGE_SIZE << order) - 1);
	for (page = virt_to_page(tmpbuff); page <= pend; page++)
		mem_map_reserve(page);

	CS_DBGOUT(CS_PARMS, 9, printk("cs461x: allocated %ld (order = %d) bytes at %p\n",
	       PAGE_SIZE << order, order, tmpbuff) );

	dmabuf->ready  = dmabuf->mapped = 0;
	dmabuf->SGok = 0;
	return 0;
}

/* free DMA buffer */
static void dealloc_dmabuf(struct cs_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct page *page, *pend;

	if (dmabuf->rawbuf) {
		pend = virt_to_page(dmabuf->rawbuf + (PAGE_SIZE << dmabuf->buforder) - 1);
		for (page = virt_to_page(dmabuf->rawbuf); page <= pend; page++)
			mem_map_unreserve(page);
		pci_free_consistent(state->card->pci_dev, PAGE_SIZE << dmabuf->buforder,
				    dmabuf->rawbuf, dmabuf->dma_handle);
	}
	dmabuf->rawbuf = NULL;

	if (dmabuf->tmpbuff) {
		/* undo marking the pages as reserved */
		pend = virt_to_page(dmabuf->tmpbuff + (PAGE_SIZE << dmabuf->buforder_tmpbuff) - 1);
		for (page = virt_to_page(dmabuf->tmpbuff); page <= pend; page++)
			mem_map_unreserve(page);
		pci_free_consistent(state->card->pci_dev, PAGE_SIZE << dmabuf->buforder_tmpbuff,
				    dmabuf->tmpbuff, dmabuf->dma_handle_tmpbuff);
	}

	dmabuf->rawbuf = NULL;
	dmabuf->tmpbuff = NULL;
	dmabuf->mapped = dmabuf->ready = 0;
	dmabuf->SGok = 0;
}

static int prog_dmabuf(struct cs_state *state)
{
        struct dmabuf *dmabuf = &state->dmabuf;
        unsigned long flags;
        unsigned long allocated_pages, allocated_bytes;                     
        unsigned long tmp1, tmp2, fmt=0;                                           
        unsigned long *ptmp = (unsigned long *) dmabuf->pbuf;               
        unsigned long SGarray[9], nSGpages=0;                               
        int ret;

	CS_DBGOUT(CS_FUNCTION, 4, printk("cs46xx: prog_dmabuf()+ \n"));
/*
 * check for CAPTURE and use only non-sg for initial release
 */
	if(dmabuf->type == CS_TYPE_ADC)
	{
		CS_DBGOUT(CS_FUNCTION, 4, printk("cs46xx: prog_dmabuf() ADC\n"));
		/* 
		 * add in non-sg support for capture.
		 */
		spin_lock_irqsave(&state->card->lock, flags);
	/* add code to reset the rawbuf memory. TRW */
		resync_dma_ptrs(state);
		dmabuf->total_bytes = dmabuf->blocks = 0;
		dmabuf->count = dmabuf->error = dmabuf->underrun = 0;

		dmabuf->SGok = 0;                                                   

		spin_unlock_irqrestore(&state->card->lock, flags);

		/* allocate DMA buffer if not allocated yet */
		if (!dmabuf->rawbuf || !dmabuf->tmpbuff)
			if ((ret = alloc_dmabuf(state)))
				return ret; 
	/*
	 * static image only supports 16Bit signed, stereo - hard code fmt
	 */
		fmt = CS_FMT_16BIT | CS_FMT_STEREO;

		dmabuf->numfrag = 2;                                        
		dmabuf->fragsize = 2048;                                    
		dmabuf->fragsamples = 2048 >> sample_shift[fmt];    
		dmabuf->dmasize = 4096;                                     
		dmabuf->fragshift = 11;                                     

		memset(dmabuf->rawbuf, (fmt & CS_FMT_16BIT) ? 0 : 0x80,
		       dmabuf->dmasize);
        	memset(dmabuf->tmpbuff, (fmt & CS_FMT_16BIT) ? 0 : 0x80, 
			PAGE_SIZE<<dmabuf->buforder_tmpbuff);      

		/*
		 *      Now set up the ring
		 */

		spin_lock_irqsave(&state->card->lock, flags);
		cs_rec_setup(state);
		spin_unlock_irqrestore(&state->card->lock, flags);

		/* set the ready flag for the dma buffer */
		dmabuf->ready = 1;

		CS_DBGOUT(CS_PARMS, 4, printk(
			"cs461x: prog_dmabuf(): CAPTURE rate=%d fmt=0x%x numfrag=%d "
			"fragsize=%d dmasize=%d\n",
			    dmabuf->rate, dmabuf->fmt, dmabuf->numfrag,
			    dmabuf->fragsize, dmabuf->dmasize) );

		CS_DBGOUT(CS_FUNCTION, 4, printk("cs46xx: prog_dmabuf()- 0 \n"));
		return 0;
	}
	else if (dmabuf->type == CS_TYPE_DAC)
	{
	/*
	 * Must be DAC
	 */
		CS_DBGOUT(CS_FUNCTION, 4, printk("cs46xx: prog_dmabuf() DAC\n"));
		spin_lock_irqsave(&state->card->lock, flags);
		resync_dma_ptrs(state);
		dmabuf->total_bytes = dmabuf->blocks = 0;
		dmabuf->count = dmabuf->error = dmabuf->underrun = 0;

		dmabuf->SGok = 0;                                                   

		spin_unlock_irqrestore(&state->card->lock, flags);

		/* allocate DMA buffer if not allocated yet */
		if (!dmabuf->rawbuf)
			if ((ret = alloc_dmabuf(state)))
				return ret;

		allocated_pages = 1 << dmabuf->buforder;                            
		allocated_bytes = allocated_pages*PAGE_SIZE;                        
										    
		if(allocated_pages < 2)                                             
		{
			CS_DBGOUT(CS_FUNCTION, 4, printk(
			    "cs46xx: prog_dmabuf() Error: allocated_pages too small (%d)\n",
				(unsigned)allocated_pages));
			return -ENOMEM;
		}
										    
		/* Use all the pages allocated, fragsize 4k. */
		/* Use 'pbuf' for S/G page map table. */
		dmabuf->SGok = 1;           /* Use S/G. */

		nSGpages = allocated_bytes/4096;    /* S/G pages always 4k. */
										    
		     /* Set up S/G variables. */
		*ptmp = virt_to_bus(dmabuf->rawbuf);                                
		*(ptmp+1) = 0x00000008;                                             
		for(tmp1= 1; tmp1 < nSGpages; tmp1++) {                             
			*(ptmp+2*tmp1) = virt_to_bus( (dmabuf->rawbuf)+4096*tmp1);  
			if( tmp1 == nSGpages-1)                                     
				tmp2 = 0xbfff0000;
			else                                                        
				tmp2 = 0x80000000+8*(tmp1+1);                       
			*(ptmp+2*tmp1+1) = tmp2;                                    
		}                                                                   
		SGarray[0] = 0x82c0200d;                                            
		SGarray[1] = 0xffff0000;                                            
		SGarray[2] = *ptmp;                                                 
		SGarray[3] = 0x00010600;                                            
		SGarray[4] = *(ptmp+2);                                             
		SGarray[5] = 0x80000010;                                            
		SGarray[6] = *ptmp;                                                 
		SGarray[7] = *(ptmp+2);                                             
		SGarray[8] = (virt_to_bus(dmabuf->pbuf) & 0xffff000) | 0x10;        

		if (dmabuf->SGok) {                                                 
			dmabuf->numfrag = nSGpages;                                 
			dmabuf->fragsize = 4096;                                    
			dmabuf->fragsamples = 4096 >> sample_shift[dmabuf->fmt];    
			dmabuf->fragshift = 12;                                     
			dmabuf->dmasize = dmabuf->numfrag*4096;                     
		}                                                                   
		else {                                                              
			SGarray[0] = 0xf2c0000f;                                    
			SGarray[1] = 0x00000200;                                    
			SGarray[2] = 0;                                             
			SGarray[3] = 0x00010600;                                    
			SGarray[4]=SGarray[5]=SGarray[6]=SGarray[7]=SGarray[8] = 0; 
			dmabuf->numfrag = 2;                                        
			dmabuf->fragsize = 2048;                                    
			dmabuf->fragsamples = 2048 >> sample_shift[dmabuf->fmt];    
			dmabuf->dmasize = 4096;                                     
			dmabuf->fragshift = 11;                                     
		}
		for(tmp1 = 0; tmp1 < sizeof(SGarray)/4; tmp1++)                     
			cs461x_poke( state->card, BA1_PDTC+tmp1*4, SGarray[tmp1]);  

		memset(dmabuf->rawbuf, (dmabuf->fmt & CS_FMT_16BIT) ? 0 : 0x80,
		       dmabuf->dmasize);

		/*
		 *      Now set up the ring
		 */

		spin_lock_irqsave(&state->card->lock, flags);
		cs_play_setup(state);
		spin_unlock_irqrestore(&state->card->lock, flags);

		/* set the ready flag for the dma buffer */
		dmabuf->ready = 1;

		CS_DBGOUT(CS_PARMS, 4, printk(
			"cs461x: prog_dmabuf(): PLAYBACK rate=%d fmt=0x%x numfrag=%d "
			"fragsize=%d dmasize=%d\n",
			    dmabuf->rate, dmabuf->fmt, dmabuf->numfrag,
			    dmabuf->fragsize, dmabuf->dmasize) );

		CS_DBGOUT(CS_FUNCTION, 4, printk("cs46xx: prog_dmabuf()- \n"));
		return 0;
	}
	else
	{
		CS_DBGOUT(CS_FUNCTION, 4, printk("cs46xx: prog_dmabuf()- Invalid Type %d\n",
			dmabuf->type));
	}
	return 1;
}

static void cs_clear_tail(struct cs_state *state)
{
}

static int drain_dac(struct cs_state *state, int nonblock)
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
		tmo += (2048*HZ)/dmabuf->rate;
		
		if (!schedule_timeout(tmo ? tmo : 1) && tmo){
			printk(KERN_ERR "cs461x: drain_dac, dma timeout? %d\n", count);
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
static void cs_update_ptr(void)
{
	struct cs_card *card=devs;
	struct cs_state *state;
	struct dmabuf *dmabuf;
	unsigned hwptr;
	int diff;

	/* error handling and process wake up for ADC */
	state = card->states[0];
	if(state)
	{
		dmabuf = &state->dmabuf;
		if (dmabuf->enable & ADC_RUNNING) {
			/* update hardware pointer */
			hwptr = cs_get_dma_addr(state);

			diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;
			CS_DBGOUT(CS_PARMS, 9, printk(
				"cs46xx: cs_update_ptr()+ ADC hwptr=%d diff=%d\n", 
				hwptr,diff) );
			dmabuf->hwptr = hwptr;
			dmabuf->total_bytes += diff;
			dmabuf->count += diff;
			if (dmabuf->count > dmabuf->dmasize)
				dmabuf->count = dmabuf->dmasize;

			if(dmabuf->mapped)
			{
				if (dmabuf->count >= (signed)dmabuf->fragsize)
					wake_up(&dmabuf->wait);
			} else 
			{
				if (dmabuf->count > 0)
					wake_up(&dmabuf->wait);
			}
		}
	}

/*
 * Now the DAC
 */
	state = card->states[1];
	if(state)
	{
		dmabuf = &state->dmabuf;
		/* error handling and process wake up for DAC */
		if (dmabuf->enable & DAC_RUNNING) {
			/* update hardware pointer */
			hwptr = cs_get_dma_addr(state);

			diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;
			CS_DBGOUT(CS_PARMS, 9, printk(
				"cs46xx: cs_update_ptr()+ DAC hwptr=%d diff=%d\n", 
				hwptr,diff) );
			dmabuf->hwptr = hwptr;
			dmabuf->total_bytes += diff;
			if (dmabuf->mapped) {
				dmabuf->count += diff;
				if (dmabuf->count >= (signed)dmabuf->fragsize)
					wake_up(&dmabuf->wait);
				/*
				 * other drivers use fragsize, but don't see any sense
				 * in that, since dmasize is the buffer asked for
				 * via mmap.
				 */
				if( dmabuf->count > dmabuf->dmasize)
					dmabuf->count &= dmabuf->dmasize-1;
			} else {
				dmabuf->count -= diff;
				/*
				 * backfill with silence and clear out the last 
				 * "diff" number of bytes.
				 */
				if(hwptr >= diff)
				{
					memset(dmabuf->rawbuf + hwptr - diff, 
						(dmabuf->fmt & CS_FMT_16BIT) ? 0 : 0x80, diff);
				}
				else
				{
					memset(dmabuf->rawbuf, 
						(dmabuf->fmt & CS_FMT_16BIT) ? 0 : 0x80,
						(unsigned)hwptr);
					memset((void *)((unsigned)dmabuf->rawbuf + 
							dmabuf->dmasize + hwptr - diff),
						(dmabuf->fmt & CS_FMT_16BIT) ? 0 : 0x80, 
						diff - hwptr); 
				}

				if (dmabuf->count < 0 || dmabuf->count > dmabuf->dmasize) {
					CS_DBGOUT(CS_ERROR, 2, printk(
					  "cs46xx: ERROR DAC count<0 or count > dmasize (%d)\n",
					  	dmabuf->count));
					/* 
					* buffer underrun or buffer overrun, reset the
					* count of bytes written back to 0.
					*/
					if(dmabuf->count < 0)
						dmabuf->underrun=1;
					dmabuf->count = 0;
					dmabuf->error++;
				}
				if (dmabuf->count < (signed)dmabuf->dmasize/2)
					wake_up(&dmabuf->wait);
			}
		}
	}
}


/* hold spinlock for the following! */
static void cs_handle_midi(struct cs_card *card)
{
        unsigned char ch;
        int wake;
        unsigned temp1;

        wake = 0;
        while (!(cs461x_peekBA0(card,  BA0_MIDSR) & MIDSR_RBE)) {
                ch = cs461x_peekBA0(card, BA0_MIDRP);
                if (card->midi.icnt < CS_MIDIINBUF) {
                        card->midi.ibuf[card->midi.iwr] = ch;
                        card->midi.iwr = (card->midi.iwr + 1) % CS_MIDIINBUF;
                        card->midi.icnt++;
                }
                wake = 1;
        }
        if (wake)
                wake_up(&card->midi.iwait);
        wake = 0;
        while (!(cs461x_peekBA0(card,  BA0_MIDSR) & MIDSR_TBF) && card->midi.ocnt > 0) {
                temp1 = ( card->midi.obuf[card->midi.ord] ) & 0x000000ff;
                cs461x_pokeBA0(card, BA0_MIDWP,temp1);
                card->midi.ord = (card->midi.ord + 1) % CS_MIDIOUTBUF;
                card->midi.ocnt--;
                if (card->midi.ocnt < CS_MIDIOUTBUF-16)
                        wake = 1;
        }
        if (wake)
                wake_up(&card->midi.owait);
}

static void cs_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct cs_card *card = (struct cs_card *)dev_id;
	/* Single channel card */
	struct cs_state *recstate = card->channel[0].state;
	struct cs_state *playstate = card->channel[1].state;
	u32 status;

	CS_DBGOUT(CS_INTERRUPT, 4, printk("cs46xx: cs_interrupt()+ \n"));

	spin_lock(&card->lock);

	status = cs461x_peekBA0(card, BA0_HISR);
	
	if ((status & 0x7fffffff) == 0)
	{
		cs461x_pokeBA0(card, BA0_HICR, HICR_CHGM|HICR_IEV);
		spin_unlock(&card->lock);
		return;
	}
	
	/*
	 * check for playback or capture interrupt only
	 */
	if( ((status & HISR_VC0) && playstate && playstate->dmabuf.ready) || 
	    (((status & HISR_VC1) && recstate && recstate->dmabuf.ready)) )
	{
		CS_DBGOUT(CS_INTERRUPT, 8, printk(
			"cs46xx: cs_interrupt() interrupt bit(s) set (0x%x)\n",status));
		cs_update_ptr();
	}

        if( status & HISR_MIDI )
                cs_handle_midi(card);
	
 	/* clear 'em */
	cs461x_pokeBA0(card, BA0_HICR, HICR_CHGM|HICR_IEV);
	spin_unlock(&card->lock);
	CS_DBGOUT(CS_INTERRUPT, 4, printk("cs46xx: cs_interrupt()- \n"));
}


/**********************************************************************/

static ssize_t cs_midi_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
        struct cs_card *card = (struct cs_card *)file->private_data;
        ssize_t ret;
        unsigned long flags;
        unsigned ptr;
        int cnt;

        if (ppos != &file->f_pos)
                return -ESPIPE;
        if (!access_ok(VERIFY_WRITE, buffer, count))
                return -EFAULT;
        ret = 0;
        while (count > 0) {
                spin_lock_irqsave(&card->lock, flags);
                ptr = card->midi.ird;
                cnt = CS_MIDIINBUF - ptr;
                if (card->midi.icnt < cnt)
                        cnt = card->midi.icnt;
                spin_unlock_irqrestore(&card->lock, flags);
                if (cnt > count)
                        cnt = count;
                if (cnt <= 0) {
                        if (file->f_flags & O_NONBLOCK)
                                return ret ? ret : -EAGAIN;
                        interruptible_sleep_on(&card->midi.iwait);
                        if (signal_pending(current))
                                return ret ? ret : -ERESTARTSYS;
                        continue;
                }
                if (copy_to_user(buffer, card->midi.ibuf + ptr, cnt))
                        return ret ? ret : -EFAULT;
                ptr = (ptr + cnt) % CS_MIDIINBUF;
                spin_lock_irqsave(&card->lock, flags);
                card->midi.ird = ptr;
                card->midi.icnt -= cnt;
                spin_unlock_irqrestore(&card->lock, flags);
                count -= cnt;
                buffer += cnt;
                ret += cnt;
        }
        return ret;
}


static ssize_t cs_midi_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
        struct cs_card *card = (struct cs_card *)file->private_data;
        ssize_t ret;
        unsigned long flags;
        unsigned ptr;
        int cnt;

        if (ppos != &file->f_pos)
                return -ESPIPE;
        if (!access_ok(VERIFY_READ, buffer, count))
                return -EFAULT;
        ret = 0;
        while (count > 0) {
                spin_lock_irqsave(&card->lock, flags);
                ptr = card->midi.owr;
                cnt = CS_MIDIOUTBUF - ptr;
                if (card->midi.ocnt + cnt > CS_MIDIOUTBUF)
                        cnt = CS_MIDIOUTBUF - card->midi.ocnt;
                if (cnt <= 0)
                        cs_handle_midi(card);
                spin_unlock_irqrestore(&card->lock, flags);
                if (cnt > count)
                        cnt = count;
                if (cnt <= 0) {
                        if (file->f_flags & O_NONBLOCK)
                                return ret ? ret : -EAGAIN;
                        interruptible_sleep_on(&card->midi.owait);
                        if (signal_pending(current))
                                return ret ? ret : -ERESTARTSYS;
                        continue;
                }
                if (copy_from_user(card->midi.obuf + ptr, buffer, cnt))
                        return ret ? ret : -EFAULT;
                ptr = (ptr + cnt) % CS_MIDIOUTBUF;
                spin_lock_irqsave(&card->lock, flags);
                card->midi.owr = ptr;
                card->midi.ocnt += cnt;
                spin_unlock_irqrestore(&card->lock, flags);
                count -= cnt;
                buffer += cnt;
                ret += cnt;
                spin_lock_irqsave(&card->lock, flags);
                cs_handle_midi(card);
                spin_unlock_irqrestore(&card->lock, flags);
        }
        return ret;
}


static unsigned int cs_midi_poll(struct file *file, struct poll_table_struct *wait)
{
        struct cs_card *card = (struct cs_card *)file->private_data;
        unsigned long flags;
        unsigned int mask = 0;

        if (file->f_flags & FMODE_WRITE)
                poll_wait(file, &card->midi.owait, wait);
        if (file->f_flags & FMODE_READ)
                poll_wait(file, &card->midi.iwait, wait);
        spin_lock_irqsave(&card->lock, flags);
        if (file->f_flags & FMODE_READ) {
                if (card->midi.icnt > 0)
                        mask |= POLLIN | POLLRDNORM;
        }
        if (file->f_flags & FMODE_WRITE) {
                if (card->midi.ocnt < CS_MIDIOUTBUF)
                        mask |= POLLOUT | POLLWRNORM;
        }
        spin_unlock_irqrestore(&card->lock, flags);
        return mask;
}


static int cs_midi_open(struct inode *inode, struct file *file)
{
        int minor = MINOR(inode->i_rdev);
        struct cs_card *card = devs;
        unsigned long flags;
        while (card && card->dev_midi != minor)
                card = card->next;
        if (!card)
                return -ENODEV;
        file->private_data = card;
        /* wait for device to become free */
        down(&card->midi.open_sem);
        while (card->midi.open_mode & file->f_mode) {
                if (file->f_flags & O_NONBLOCK) {
                        up(&card->midi.open_sem);
                        return -EBUSY;
                }
                up(&card->midi.open_sem);
                interruptible_sleep_on(&card->midi.open_wait);
                if (signal_pending(current))
                        return -ERESTARTSYS;
                down(&card->midi.open_sem);
        }
        spin_lock_irqsave(&card->midi.lock, flags);
        if (!(card->midi.open_mode & (FMODE_READ | FMODE_WRITE))) {
                card->midi.ird = card->midi.iwr = card->midi.icnt = 0;
                card->midi.ord = card->midi.owr = card->midi.ocnt = 0;
                card->midi.ird = card->midi.iwr = card->midi.icnt = 0;
                cs461x_pokeBA0(card, BA0_MIDCR, 0x0000000f);            /* Enable xmit, rcv. */
                cs461x_pokeBA0(card, BA0_HICR, HICR_IEV | HICR_CHGM);   /* Enable interrupts */
        }
        if (file->f_mode & FMODE_READ) {
                card->midi.ird = card->midi.iwr = card->midi.icnt = 0;
        }
        if (file->f_mode & FMODE_WRITE) {
                card->midi.ord = card->midi.owr = card->midi.ocnt = 0;
        }
        spin_unlock_irqrestore(&card->midi.lock, flags);
        card->midi.open_mode |= (file->f_mode & (FMODE_READ | FMODE_WRITE));
        up(&card->midi.open_sem);
        MOD_INC_USE_COUNT;
        return 0;
}


static int cs_midi_release(struct inode *inode, struct file *file)
{
        struct cs_card *card = (struct cs_card *)file->private_data;
        DECLARE_WAITQUEUE(wait, current);
        unsigned long flags;
        unsigned count, tmo;

        if (file->f_mode & FMODE_WRITE) {
                current->state = TASK_INTERRUPTIBLE;
                add_wait_queue(&card->midi.owait, &wait);
                for (;;) {
                        spin_lock_irqsave(&card->midi.lock, flags);
                        count = card->midi.ocnt;
                        spin_unlock_irqrestore(&card->midi.lock, flags);
                        if (count <= 0)
                                break;
                        if (signal_pending(current))
                                break;
                        if (file->f_flags & O_NONBLOCK) {
                                remove_wait_queue(&card->midi.owait, &wait);
                                current->state = TASK_RUNNING;
                                return -EBUSY;
                        }                      
                        tmo = (count * HZ) / 3100;
                        if (!schedule_timeout(tmo ? : 1) && tmo)
                                printk(KERN_DEBUG "cs46xx: midi timed out??\n");
                }
                remove_wait_queue(&card->midi.owait, &wait);
                current->state = TASK_RUNNING;
        }
        down(&card->midi.open_sem);
        card->midi.open_mode &= (~(file->f_mode & (FMODE_READ | FMODE_WRITE)));
        up(&card->midi.open_sem);
        wake_up(&card->midi.open_wait);
        MOD_DEC_USE_COUNT;
        return 0;
}

/*
 *   Midi file operations struct.
 */
static /*const*/ struct file_operations cs_midi_fops = {
        llseek:		cs_llseek,
        read:		cs_midi_read,
        write:		cs_midi_write,
        poll:		cs_midi_poll,
        open:		cs_midi_open,
        release:	cs_midi_release,
};

static loff_t cs_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

/*
 *
 * CopySamples copies 16-bit stereo signed samples from the source to the
 * destination, possibly converting down to unsigned 8-bit and/or mono.
 * count specifies the number of output bytes to write.
 *
 *  Arguments:
 *
 *  dst             - Pointer to a destination buffer.
 *  src             - Pointer to a source buffer
 *  count           - The number of bytes to copy into the destination buffer.
 *  fmt             - CS_FMT_16BIT and/or CS_FMT_STEREO bits
 *  dmabuf          - pointer to the dma buffer structure
 *
 * NOTES: only call this routine if the output desired is not 16 Signed Stereo
 * 	
 *
 */
static void CopySamples(char *dst, char *src, int count, unsigned fmt, 
		struct dmabuf *dmabuf)
{

    s32 s32AudioSample;
    s16 *psSrc=(s16 *)src;
    s16 *psDst=(s16 *)dst;
    u8 *pucDst=(u8 *)dst;

    CS_DBGOUT(CS_FUNCTION, 2, printk(KERN_INFO "cs4281: CopySamples()+ ") );
    CS_DBGOUT(CS_WAVE_READ, 8, printk(KERN_INFO
	" dst=0x%x src=0x%x count=%d fmt=0x%x\n",
	(unsigned)dst,(unsigned)src,(unsigned)count,(unsigned)fmt) );

    /*
     * See if the data should be output as 8-bit unsigned stereo.
     */
    if((fmt & CS_FMT_STEREO) && !(fmt & CS_FMT_16BIT))
    {
        /*
         * Convert each 16-bit signed stereo sample to 8-bit unsigned 
	 * stereo using rounding.
         */
        psSrc = (s16 *)src;
	count = count/2;
        while(count--)
        {
            *(pucDst++) = (u8)(((s16)(*psSrc++) + (s16)0x8000) >> 8);
        }
    }
    /*
     * See if the data should be output at 8-bit unsigned mono.
     */
    else if(!(fmt & CS_FMT_STEREO) && !(fmt & CS_FMT_16BIT))
    {
        /*
         * Convert each 16-bit signed stereo sample to 8-bit unsigned 
	 * mono using averaging and rounding.
         */
        psSrc = (s16 *)src;
	count = count/2;
        while(count--)
        {
	    s32AudioSample = ((*psSrc)+(*(psSrc + 1)))/2 + (s32)0x80;
	    if(s32AudioSample > 0x7fff)
	    	s32AudioSample = 0x7fff;
            *(pucDst++) = (u8)(((s16)s32AudioSample + (s16)0x8000) >> 8);
	    psSrc += 2;
        }
    }
    /*
     * See if the data should be output at 16-bit signed mono.
     */
    else if(!(fmt & CS_FMT_STEREO) && (fmt & CS_FMT_16BIT))
    {
        /*
         * Convert each 16-bit signed stereo sample to 16-bit signed 
	 * mono using averaging.
         */
        psSrc = (s16 *)src;
	count = count/2;
        while(count--)
        {
            *(psDst++) = (s16)((*psSrc)+(*(psSrc + 1)))/2;
	    psSrc += 2;
        }
    }
}

/*
 * cs_copy_to_user()
 * replacement for the standard copy_to_user, to allow for a conversion from
 * 16 bit to 8 bit and from stereo to mono, if the record conversion is active.  
 * The current CS46xx/CS4280 static image only records in 16bit unsigned Stereo, 
 * so we convert from any of the other format combinations.
 */
static unsigned cs_copy_to_user(
	struct cs_state *s, 
	void *dest, 
	void *hwsrc, 
	unsigned cnt, 
	unsigned *copied)
{
	struct dmabuf *dmabuf = &s->dmabuf;
	void *src = hwsrc;  /* default to the standard destination buffer addr */

	CS_DBGOUT(CS_FUNCTION, 6, printk(KERN_INFO 
		"cs_copy_to_user()+ fmt=0x%x cnt=%d dest=0x%.8x\n",
		dmabuf->fmt,(unsigned)cnt,(unsigned)dest) );

	if(cnt > dmabuf->dmasize)
	{
		cnt = dmabuf->dmasize;
	}
	if(!cnt)
	{
		*copied = 0;
		return 0;
	}
	if(dmabuf->divisor != 1)
	{
		if(!dmabuf->tmpbuff)
		{
			*copied = cnt/dmabuf->divisor;
			return 0;
		}

		CopySamples((char *)dmabuf->tmpbuff, (char *)hwsrc, cnt, 
			dmabuf->fmt, dmabuf);
		src = dmabuf->tmpbuff;
		cnt = cnt/dmabuf->divisor;
	}
        if (copy_to_user(dest, src, cnt))
	{
		CS_DBGOUT(CS_FUNCTION, 2, printk(KERN_ERR 
			"cs4281: cs_copy_to_user()- fault dest=0x%x src=0x%x cnt=%d\n",
				(unsigned)dest,(unsigned)src,cnt) );
		*copied = 0;
		return -EFAULT;
	}
	*copied = cnt;
	CS_DBGOUT(CS_FUNCTION, 2, printk(KERN_INFO 
		"cs4281: cs_copy_to_user()- copied bytes is %d \n",cnt) );
	return 0;
}

/* in this loop, dmabuf.count signifies the amount of data that is waiting to be copied to
   the user's buffer.  it is filled by the dma machine and drained by this loop. */
static ssize_t cs_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct cs_card *card=devs;
	struct cs_state *state;
	DECLARE_WAITQUEUE(wait, current);
	struct dmabuf *dmabuf;
	ssize_t ret = 0;
	unsigned long flags;
	unsigned swptr;
	int cnt;
	unsigned copied=0;

	CS_DBGOUT(CS_WAVE_READ, 4, printk("cs461x: cs_read()+ %d\n",count) );
	state = (struct cs_state *)card->states[0];
	if(!state)
		return -ENODEV;
	dmabuf = &state->dmabuf;

	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (dmabuf->mapped)
		return -ENXIO;
	if (!dmabuf->ready && (ret = prog_dmabuf(state)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;

	add_wait_queue(&state->dmabuf.wait, &wait);
	while (count > 0) {
		spin_lock_irqsave(&state->card->lock, flags);
		swptr = dmabuf->swptr;
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count < cnt)
			cnt = dmabuf->count;
		if (cnt <= 0)
			__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (cnt > (count * dmabuf->divisor))
			cnt = count * dmabuf->divisor;
		if (cnt <= 0) {
			/* buffer is empty, start the dma machine and wait for data to be
			   recorded */
			start_adc(state);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret) ret = -EAGAIN;
				break;
 			}
			schedule();
			if (signal_pending(current)) {
				ret = ret ? ret : -ERESTARTSYS;
				break;
			}
			continue;
		}

		CS_DBGOUT(CS_WAVE_READ, 2, printk(KERN_INFO 
			"_read() copy_to cnt=%d count=%d ", cnt,count) );
		CS_DBGOUT(CS_WAVE_READ, 8, printk(KERN_INFO 
			" .dmasize=%d .count=%d buffer=0x%.8x ret=%d\n",
			dmabuf->dmasize,dmabuf->count,(unsigned)buffer,ret) );

                if (cs_copy_to_user(state, buffer, 
			(void *)((unsigned)dmabuf->rawbuf + swptr), cnt, &copied))
		{
			if (!ret) ret = -EFAULT;
			break;
		}
                swptr = (swptr + cnt) % dmabuf->dmasize;
                spin_lock_irqsave(&card->lock, flags);
                dmabuf->swptr = swptr;
                dmabuf->count -= cnt;
                spin_unlock_irqrestore(&card->lock, flags);
                count -= copied;
                buffer += copied;
                ret += copied;
                start_adc(state);
	}
	remove_wait_queue(&state->dmabuf.wait, &wait);
	set_current_state(TASK_RUNNING);
	CS_DBGOUT(CS_WAVE_READ, 4, printk("cs461x: cs_read()- %d\n",ret) );
	return ret;
}

/* in this loop, dmabuf.count signifies the amount of data that is waiting to be dma to
   the soundcard.  it is drained by the dma machine and filled by this loop. */
static ssize_t cs_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct cs_card *card=devs;
	struct cs_state *state;
	DECLARE_WAITQUEUE(wait, current);
	struct dmabuf *dmabuf;
	ssize_t ret = 0;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	CS_DBGOUT(CS_WAVE_WRITE | CS_FUNCTION, 4,
		printk("cs461x: cs_write called, count = %d\n", count) );
	state = (struct cs_state *)card->states[1];
	if(!state)
		return -ENODEV;
	dmabuf = &state->dmabuf;

	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (dmabuf->mapped)
		return -ENXIO;
	if (!dmabuf->ready && (ret = prog_dmabuf(state)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	add_wait_queue(&state->dmabuf.wait, &wait);
	while (count > 0) {
		spin_lock_irqsave(&state->card->lock, flags);
		if (dmabuf->count < 0) {
			/* buffer underrun, we are recovering from sleep_on_timeout,
			   resync hwptr and swptr */
			dmabuf->count = 0;
			dmabuf->swptr = dmabuf->hwptr;
		}
		if (dmabuf->underrun)
		{
			dmabuf->underrun = 0;
			dmabuf->hwptr = cs_get_dma_addr(state);
			dmabuf->swptr = dmabuf->hwptr;
		}
		swptr = dmabuf->swptr;
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count + cnt > dmabuf->dmasize)
			cnt = dmabuf->dmasize - dmabuf->count;
		if (cnt <= 0)
			__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			/* buffer is full, start the dma machine and wait for data to be
			   played */
			start_dac(state);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret) ret = -EAGAIN;
				break;
 			}
			schedule();
 			if (signal_pending(current)) {
				ret = ret ? ret : -ERESTARTSYS;
				break;
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
		if(dmabuf->count > dmabuf->dmasize)
		{
			CS_DBGOUT(CS_WAVE_WRITE | CS_ERROR, 2, printk(
			    "cs46xx: cs_write() d->count > dmasize - resetting\n"));
			dmabuf->count = dmabuf->dmasize;
		}
		dmabuf->endcleared = 0;
		spin_unlock_irqrestore(&state->card->lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac(state);
	}
	remove_wait_queue(&state->dmabuf.wait, &wait);
	set_current_state(TASK_RUNNING);

	CS_DBGOUT(CS_WAVE_WRITE | CS_FUNCTION, 2, 
		printk("cs46xx: cs_write()- ret=0x%x\n", ret) );
	return ret;
}

static unsigned int cs_poll(struct file *file, struct poll_table_struct *wait)
{
	struct cs_card *card = (struct cs_card *)file->private_data;
	struct dmabuf *dmabuf;
	struct cs_state *state;

	unsigned long flags;
	unsigned int mask = 0;

	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: cs_poll()+ \n"));
	if (!(file->f_mode & (FMODE_WRITE | FMODE_READ)))
	{
		return -EINVAL;
	}
	if (file->f_mode & FMODE_WRITE)
	{
		state = card->states[1];
		if(state)
		{
			dmabuf = &state->dmabuf;
			poll_wait(file, &dmabuf->wait, wait);
		}
	}
	if (file->f_mode & FMODE_READ)
	{
		state = card->states[0];
		if(state)
		{
			dmabuf = &state->dmabuf;
			poll_wait(file, &dmabuf->wait, wait);
		}
	}

	spin_lock_irqsave(&card->lock, flags);
	cs_update_ptr();
	if (file->f_mode & FMODE_READ) {
		state = card->states[0];
		if(state)
		{
			dmabuf = &state->dmabuf;
			if (dmabuf->count >= (signed)dmabuf->fragsize)
				mask |= POLLIN | POLLRDNORM;
		}
	}
	if (file->f_mode & FMODE_WRITE) {
		state = card->states[1];
		if(state)
		{
			dmabuf = &state->dmabuf;
			if (dmabuf->mapped) {
				if (dmabuf->count >= (signed)dmabuf->fragsize)
				    mask |= POLLOUT | POLLWRNORM;
			} else {
				if ((signed)dmabuf->dmasize >= dmabuf->count 
					+ (signed)dmabuf->fragsize)
				    mask |= POLLOUT | POLLWRNORM;
			}
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);

	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: cs_poll()- \n"));
	return mask;
}

/*
 *	We let users mmap the ring buffer. Its not the real DMA buffer but
 *	that side of the code is hidden in the IRQ handling. We do a software
 *	emulation of DMA from a 64K or so buffer into a 2K FIFO. 
 *	(the hardware probably deserves a moan here but Crystal send me nice
 *	toys ;)).
 */
 
static int cs_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct cs_card *card=devs;
	struct cs_state *state;
	struct dmabuf *dmabuf;
	int ret;
	unsigned long size;
	
	CS_DBGOUT(CS_FUNCTION | CS_PARMS, 2, printk("cs46xx: cs_mmap()+ file=0x%x %s %s\n", 
		(unsigned)file, vma->vm_flags & VM_WRITE ? "VM_WRITE" : "",
		vma->vm_flags & VM_READ ? "VM_READ" : "") );

	if (vma->vm_flags & VM_WRITE) {
		state = card->states[1];
		if(state)
		{
			CS_DBGOUT(CS_OPEN, 2, printk(
			  "cs46xx: cs_mmap() VM_WRITE - state TRUE prog_dmabuf DAC\n") );
			if ((ret = prog_dmabuf(state)) != 0)
				return ret;
		}
	} else if (vma->vm_flags & VM_READ) {
		state = card->states[0];
		if(state)
		{
			CS_DBGOUT(CS_OPEN, 2, printk(
			  "cs46xx: cs_mmap() VM_READ - state TRUE prog_dmabuf ADC\n") );
			if ((ret = prog_dmabuf(state)) != 0)
				return ret;
		}
	} else {
		CS_DBGOUT(CS_ERROR, 2, printk(
		  "cs46xx: cs_mmap() return -EINVAL\n") );
		return -EINVAL;
	}

/*
 * For now ONLY support playback, but seems like the only way to use
 * mmap() is to open an FD with RDWR, just read or just write access
 * does not function, get an error back from the kernel.
 * Also, QuakeIII opens with RDWR!  So, there must be something
 * to needing read/write access mapping.  So, allow read/write but 
 * use the DAC only.
 */
	state = card->states[1];  
	if(!(unsigned)state)
		return -EINVAL;

	dmabuf = &state->dmabuf;
	if (vma->vm_pgoff != 0)
		return -EINVAL;
	size = vma->vm_end - vma->vm_start;

	CS_DBGOUT(CS_PARMS, 2, printk("cs46xx: cs_mmap(): size=%d\n",(unsigned)size) );

	if (size > (PAGE_SIZE << dmabuf->buforder))
		return -EINVAL;
	if (remap_page_range(vma->vm_start, virt_to_phys(dmabuf->rawbuf),
			     size, vma->vm_page_prot))
		return -EAGAIN;
	dmabuf->mapped = 1;

	CS_DBGOUT(CS_FUNCTION, 2, printk("cs46xx: cs_mmap()-\n") );
	return 0;
}

static int cs_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct cs_card *card = (struct cs_card *)file->private_data;
	struct cs_state *state;
	struct dmabuf *dmabuf=0;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	int val, valsave, mapped, ret;

	state = (struct cs_state *)card->states[0];
	if(state)
	{
		dmabuf = &state->dmabuf;
		mapped = (file->f_mode & FMODE_READ) && dmabuf->mapped;
	}
	state = (struct cs_state *)card->states[1];
	if(state)
	{
		dmabuf = &state->dmabuf;
		mapped |= (file->f_mode & FMODE_WRITE) && dmabuf->mapped;
	}
		
#if CSDEBUG
	printioctl(cmd);
#endif

	switch (cmd) 
	{
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_RESET:
		/* FIXME: spin_lock ? */
		if (file->f_mode & FMODE_WRITE) {
			state = (struct cs_state *)card->states[1];
			if(state)
			{
				dmabuf = &state->dmabuf;
				stop_dac(state);
				synchronize_irq();
				dmabuf->ready = 0;
				resync_dma_ptrs(state);
				dmabuf->swptr = dmabuf->hwptr = 0;
				dmabuf->count = dmabuf->total_bytes = 0;
				dmabuf->blocks = 0;
				dmabuf->SGok = 0;
			}
		}
		if (file->f_mode & FMODE_READ) {
			state = (struct cs_state *)card->states[0];
			if(state)
			{
				dmabuf = &state->dmabuf;
				stop_adc(state);
				synchronize_irq();
				resync_dma_ptrs(state);
				dmabuf->ready = 0;
				dmabuf->swptr = dmabuf->hwptr = 0;
				dmabuf->count = dmabuf->total_bytes = 0;
				dmabuf->blocks = 0;
				dmabuf->SGok = 0;
			}
		}
		CS_DBGOUT(CS_IOCTL, 2, printk("cs46xx: DSP_RESET()-\n") );
		return 0;

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_dac(state, file->f_flags & O_NONBLOCK);
		return 0;

	case SNDCTL_DSP_SPEED: /* set sample rate */
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val >= 0) {
			if (file->f_mode & FMODE_READ) {
				state = (struct cs_state *)card->states[0];
				if(state)
				{
					dmabuf = &state->dmabuf;
					stop_adc(state);
					dmabuf->ready = 0;
					dmabuf->SGok = 0;
					cs_set_adc_rate(state, val);
					cs_set_divisor(dmabuf);
				}
			}
			if (file->f_mode & FMODE_WRITE) {
				state = (struct cs_state *)card->states[1];
				if(state)
				{
					dmabuf = &state->dmabuf;
					stop_dac(state);
					dmabuf->ready = 0;
					dmabuf->SGok = 0;
					cs_set_dac_rate(state, val);
					cs_set_divisor(dmabuf);
				}
			}
			CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(
			    "cs46xx: cs_ioctl() DSP_SPEED %s %s %d\n",
				file->f_mode & FMODE_WRITE ? "DAC" : "",
				file->f_mode & FMODE_READ ? "ADC" : "",
				dmabuf->rate ) );
			return put_user(dmabuf->rate, (int *)arg);
		}
		return put_user(0, (int *)arg);

	case SNDCTL_DSP_STEREO: /* set stereo or mono channel */
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_WRITE) {
			state = (struct cs_state *)card->states[1];
			if(state)
			{
				dmabuf = &state->dmabuf;
				stop_dac(state);
				dmabuf->ready = 0;
				dmabuf->SGok = 0;
				if(val)
					dmabuf->fmt |= CS_FMT_STEREO;
				else
					dmabuf->fmt &= ~CS_FMT_STEREO;
				cs_set_divisor(dmabuf);
				CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(
				    "cs46xx: DSP_STEREO() DAC %s\n",
				    (dmabuf->fmt & CS_FMT_STEREO) ?
					"STEREO":"MONO") );
			}
		}
		if (file->f_mode & FMODE_READ) {
			state = (struct cs_state *)card->states[0];
			if(state)
			{
				dmabuf = &state->dmabuf;
				stop_adc(state);
				dmabuf->ready = 0;
				dmabuf->SGok = 0;
				if(val)
					dmabuf->fmt |= CS_FMT_STEREO;
				else
					dmabuf->fmt &= ~CS_FMT_STEREO;
				cs_set_divisor(dmabuf);
				CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(
				    "cs46xx: DSP_STEREO() ADC %s\n",
				    (dmabuf->fmt & CS_FMT_STEREO) ?
					"STEREO":"MONO") );
			}
		}
		return 0;

	case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			state = (struct cs_state *)card->states[1];
			if(state)
			{
				dmabuf = &state->dmabuf;
				if ((val = prog_dmabuf(state)))
					return val;
				return put_user(dmabuf->fragsize, (int *)arg);
			}
		}
		if (file->f_mode & FMODE_READ) {
			state = (struct cs_state *)card->states[0];
			if(state)
			{
				dmabuf = &state->dmabuf;
				if ((val = prog_dmabuf(state)))
					return val;
				return put_user(dmabuf->fragsize/dmabuf->divisor, 
						(int *)arg);
			}
		}
		return put_user(0, (int *)arg);

	case SNDCTL_DSP_GETFMTS: /* Returns a mask of supported sample format*/
		return put_user(AFMT_S16_LE | AFMT_U8, (int *)arg);

	case SNDCTL_DSP_SETFMT: /* Select sample format */
		if (get_user(val, (int *)arg))
			return -EFAULT;
		CS_DBGOUT(CS_IOCTL | CS_PARMS, 4, printk(
		    "cs46xx: cs_ioctl() DSP_SETFMT %s %s %s %s\n",
			file->f_mode & FMODE_WRITE ? "DAC" : "",
			file->f_mode & FMODE_READ ? "ADC" : "",
			val == AFMT_S16_LE ? "16Bit Signed" : "",
			val == AFMT_U8 ? "8Bit Unsigned" : "") );
		valsave = val;
		if (val != AFMT_QUERY) {
			if(val==AFMT_S16_LE || val==AFMT_U8)
			{
				if (file->f_mode & FMODE_WRITE) {
					state = (struct cs_state *)card->states[1];
					if(state)
					{
						dmabuf = &state->dmabuf;
						stop_dac(state);
						dmabuf->ready = 0;
						dmabuf->SGok = 0;
						if(val==AFMT_S16_LE)
							dmabuf->fmt |= CS_FMT_16BIT;
						else
							dmabuf->fmt &= ~CS_FMT_16BIT;
						cs_set_divisor(dmabuf);
						if((ret = prog_dmabuf(state)))
							return ret;
					}
				}
				if (file->f_mode & FMODE_READ) {
					val = valsave;
					state = (struct cs_state *)card->states[0];
					if(state)
					{
						dmabuf = &state->dmabuf;
						stop_adc(state);
						dmabuf->ready = 0;
						dmabuf->SGok = 0;
						if(val==AFMT_S16_LE)
							dmabuf->fmt |= CS_FMT_16BIT;
						else
							dmabuf->fmt &= ~CS_FMT_16BIT;
						cs_set_divisor(dmabuf);
						if((ret = prog_dmabuf(state)))
							return ret;
					}
				}
			}
			else
			{
				CS_DBGOUT(CS_IOCTL | CS_ERROR, 2, printk(
				    "cs46xx: DSP_SETFMT() Unsupported format (0x%x)\n",
					valsave) );
			}
		}
		else
		{
			if(file->f_mode & FMODE_WRITE)
			{
				state = (struct cs_state *)card->states[1];
				if(state)
					dmabuf = &state->dmabuf;
			}
			else if(file->f_mode & FMODE_READ)
			{
				state = (struct cs_state *)card->states[0];
				if(state)
					dmabuf = &state->dmabuf;
			}
		}
		if(dmabuf)
		{
			if(dmabuf->fmt & CS_FMT_16BIT)
				return put_user(AFMT_S16_LE, (int *)arg);
			else
				return put_user(AFMT_U8, (int *)arg);
		}
		return put_user(0, (int *)arg);

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != 0) {
			if (file->f_mode & FMODE_WRITE) {
				state = (struct cs_state *)card->states[1];
				if(state)
				{
					dmabuf = &state->dmabuf;
					stop_dac(state);
					dmabuf->ready = 0;
					dmabuf->SGok = 0;
					if(val>1)
						dmabuf->fmt |= CS_FMT_STEREO;
					else
						dmabuf->fmt &= ~CS_FMT_STEREO;
					cs_set_divisor(dmabuf);
					if (prog_dmabuf(state))
						return 0;
				}
			}
			if (file->f_mode & FMODE_READ) {
				state = (struct cs_state *)card->states[0];
				if(state)
				{
					dmabuf = &state->dmabuf;
					stop_adc(state);
					dmabuf->ready = 0;
					dmabuf->SGok = 0;
					if(val>1)
						dmabuf->fmt |= CS_FMT_STEREO;
					else
						dmabuf->fmt &= ~CS_FMT_STEREO;
					cs_set_divisor(dmabuf);
					if (prog_dmabuf(state))
						return 0;
				}
			}
		}
		return put_user((dmabuf->fmt & CS_FMT_STEREO) ? 2 : 1,
				(int *)arg);

	case SNDCTL_DSP_POST:
		/*
		 * There will be a longer than normal pause in the data.
		 * so... do nothing, because there is nothing that we can do.
		 */
		return 0;

	case SNDCTL_DSP_SUBDIVIDE:
		if (file->f_mode & FMODE_WRITE) {
			state = (struct cs_state *)card->states[1];
			if(state)
			{
				dmabuf = &state->dmabuf;
				if (dmabuf->subdivision)
					return -EINVAL;
				if (get_user(val, (int *)arg))
					return -EFAULT;
				if (val != 1 && val != 2)
					return -EINVAL;
				dmabuf->subdivision = val;
			}
		}
		if (file->f_mode & FMODE_READ) {
			state = (struct cs_state *)card->states[0];
			if(state)
			{
				dmabuf = &state->dmabuf;
				if (dmabuf->subdivision)
					return -EINVAL;
				if (get_user(val, (int *)arg))
					return -EFAULT;
				if (val != 1 && val != 2)
					return -EINVAL;
				dmabuf->subdivision = val;
			}
		}
		return 0;

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, (int *)arg))
			return -EFAULT;

		if (file->f_mode & FMODE_WRITE) {
			state = (struct cs_state *)card->states[1];
			if(state)
			{
				dmabuf = &state->dmabuf;
				dmabuf->ossfragshift = val & 0xffff;
				dmabuf->ossmaxfrags = (val >> 16) & 0xffff;
			}
		}
		if (file->f_mode & FMODE_READ) {
			state = (struct cs_state *)card->states[0];
			if(state)
			{
				dmabuf = &state->dmabuf;
				dmabuf->ossfragshift = val & 0xffff;
				dmabuf->ossmaxfrags = (val >> 16) & 0xffff;
			}
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		state = (struct cs_state *)card->states[1];
		if(state)
		{
			dmabuf = &state->dmabuf;
			spin_lock_irqsave(&state->card->lock, flags);
			cs_update_ptr();
			abinfo.fragsize = dmabuf->fragsize;
			abinfo.fragstotal = dmabuf->numfrag;
		/*
		 * for mmap we always have total space available
		 */
			if (dmabuf->mapped)
				abinfo.bytes = dmabuf->dmasize;
			else
				abinfo.bytes = dmabuf->dmasize - dmabuf->count;

			abinfo.fragments = abinfo.bytes >> dmabuf->fragshift;
			spin_unlock_irqrestore(&state->card->lock, flags);
			return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
		}
		return -ENODEV;

	case SNDCTL_DSP_GETISPACE:
		state = (struct cs_state *)card->states[0];
		if(state)
		{
			dmabuf = &state->dmabuf;
			spin_lock_irqsave(&state->card->lock, flags);
			cs_update_ptr();
			abinfo.fragsize = dmabuf->fragsize/dmabuf->divisor;
			abinfo.bytes = dmabuf->count/dmabuf->divisor;
			abinfo.fragstotal = dmabuf->numfrag;
			abinfo.fragments = abinfo.bytes >> dmabuf->fragshift;
			spin_unlock_irqrestore(&state->card->lock, flags);
			return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
		}
		return -ENODEV;

	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_REALTIME|DSP_CAP_TRIGGER|DSP_CAP_MMAP,
			    (int *)arg);

	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		CS_DBGOUT(CS_IOCTL, 2, printk("cs46xx: DSP_GETTRIGGER()+\n") );
		if (file->f_mode & FMODE_WRITE)
		{
			state = (struct cs_state *)card->states[1];
			if(state)
			{
				dmabuf = &state->dmabuf;
				if(dmabuf->enable & DAC_RUNNING)
					val |= PCM_ENABLE_INPUT;
			}
		}
		if (file->f_mode & FMODE_READ)
		{
			if(state)
			{
				state = (struct cs_state *)card->states[0];
				dmabuf = &state->dmabuf;
				if(dmabuf->enable & ADC_RUNNING)
					val |= PCM_ENABLE_OUTPUT;
			}
		}
		CS_DBGOUT(CS_IOCTL, 2, printk("cs46xx: DSP_GETTRIGGER()- val=0x%x\n",val) );
		return put_user(val, (int *)arg);

	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			state = (struct cs_state *)card->states[0];
			if(state)
			{
				dmabuf = &state->dmabuf;
				if (val & PCM_ENABLE_INPUT) {
					if (!dmabuf->ready && (ret = prog_dmabuf(state)))
						return ret;
					start_adc(state);
				} else
					stop_adc(state);
			}
		}
		if (file->f_mode & FMODE_WRITE) {
			state = (struct cs_state *)card->states[1];
			if(state)
			{
				dmabuf = &state->dmabuf;
				if (val & PCM_ENABLE_OUTPUT) {
					if (!dmabuf->ready && (ret = prog_dmabuf(state)))
						return ret;
					start_dac(state);
				} else
					stop_dac(state);
			}
		}
		return 0;

	case SNDCTL_DSP_GETIPTR:
		state = (struct cs_state *)card->states[0];
		if(state)
		{
			dmabuf = &state->dmabuf;
			spin_lock_irqsave(&state->card->lock, flags);
			cs_update_ptr();
			cinfo.bytes = dmabuf->total_bytes/dmabuf->divisor;
			cinfo.blocks = dmabuf->count/dmabuf->divisor >> dmabuf->fragshift;
			cinfo.ptr = dmabuf->hwptr/dmabuf->divisor;
			spin_unlock_irqrestore(&state->card->lock, flags);
			return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));
		}
		return -ENODEV;

	case SNDCTL_DSP_GETOPTR:
		state = (struct cs_state *)card->states[1];
		if(state)
		{
			dmabuf = &state->dmabuf;
			spin_lock_irqsave(&state->card->lock, flags);
			cs_update_ptr();
			cinfo.bytes = dmabuf->total_bytes;
			if (dmabuf->mapped)
			{
				cinfo.blocks = (cinfo.bytes >> dmabuf->fragshift) 
							- dmabuf->blocks;
				CS_DBGOUT(CS_PARMS, 8, 
					printk("total_bytes=%d blocks=%d dmabuf->blocks=%d\n", 
					cinfo.bytes,cinfo.blocks,dmabuf->blocks) );
				dmabuf->blocks = cinfo.bytes >> dmabuf->fragshift;
			}
			else
			{
				cinfo.blocks = dmabuf->count >> dmabuf->fragshift;
			}
			cinfo.ptr = dmabuf->hwptr;

			CS_DBGOUT(CS_PARMS, 4, printk(
			    "cs46xx: GETOPTR bytes=%d blocks=%d ptr=%d\n",
				cinfo.bytes,cinfo.blocks,cinfo.ptr) );
			spin_unlock_irqrestore(&state->card->lock, flags);
			return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));
		}
		return -ENODEV;

	case SNDCTL_DSP_SETDUPLEX:
		return -EINVAL;

	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		state = (struct cs_state *)card->states[1];
		if(state)
		{
			dmabuf = &state->dmabuf;
			spin_lock_irqsave(&state->card->lock, flags);
			cs_update_ptr();
			val = dmabuf->count;
			spin_unlock_irqrestore(&state->card->lock, flags);
		}
		else
			val = 0;
		return put_user(val, (int *)arg);

	case SOUND_PCM_READ_RATE:
		state = (struct cs_state *)card->states[0];
		if(state)
		{
			dmabuf = &state->dmabuf;
			return put_user(dmabuf->rate, (int *)arg);
		}
		return put_user(0, (int *)arg);
		

	case SOUND_PCM_READ_CHANNELS:
		state = (struct cs_state *)card->states[0];
		if(state)
		{
			dmabuf = &state->dmabuf;
			return put_user((dmabuf->fmt & CS_FMT_STEREO) ? 2 : 1,
				(int *)arg);
		}
		return put_user(0, (int *)arg);

	case SOUND_PCM_READ_BITS:
		state = (struct cs_state *)card->states[0];
		if(state)
		{
			dmabuf = &state->dmabuf;
			return put_user(AFMT_S16_LE | AFMT_U8, (int *)arg);
		}
		return put_user(0, (int *)arg);

	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_WRITE_FILTER:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;
	}
	return -EINVAL;
}


/*
 *	AMP control - null AMP
 */
 
static void amp_none(struct cs_card *card, int change)
{	
}

/*
 *	Crystal EAPD mode
 */
 
static void amp_voyetra(struct cs_card *card, int change)
{
	/* Manage the EAPD bit on the Crystal 4297 
	   and the Analog AD1885 */
	   
	int old=card->amplifier;
	
	card->amplifier+=change;
	if(card->amplifier && !old)
	{
		/* Turn the EAPD amp on */
		cs_ac97_set(card->ac97_codec[0],  AC97_POWER_CONTROL, 
			cs_ac97_get(card->ac97_codec[0], AC97_POWER_CONTROL) |
				0x8000);
	}
	else if(old && !card->amplifier)
	{
		/* Turn the EAPD amp off */
		cs_ac97_set(card->ac97_codec[0],  AC97_POWER_CONTROL, 
			cs_ac97_get(card->ac97_codec[0], AC97_POWER_CONTROL) &
				~0x8000);
	}
}



/*
 *	Untested
 */
 
static void amp_voyetra_4294(struct cs_card *card, int change)
{
	struct ac97_codec *c=card->ac97_codec[0];
	
	card->amplifier+=change;

	if(card->amplifier)
	{
		/* Switch the GPIO pins 7 and 8 to open drain */
		cs_ac97_set(c, 0x4C, cs_ac97_get(c, 0x4C) & 0xFE7F);
		cs_ac97_set(c, 0x4E, cs_ac97_get(c, 0x4E) | 0x0180);
		/* Now wake the AMP (this might be backwards) */
		cs_ac97_set(c, 0x54, cs_ac97_get(c, 0x54) & ~0x0180);
	}
	else
	{
		cs_ac97_set(c, 0x54, cs_ac97_get(c, 0x54) | 0x0180);
	}
}

/*
 *	Handle the CLKRUN on a thinkpad. We must disable CLKRUN support
 *	whenever we need to beat on the chip.
 *
 *	The original idea and code for this hack comes from David Kaiser at
 *	Linuxcare. Perhaps one day Crystal will document their chips well
 *	enough to make them useful.
 */
 
static void clkrun_hack(struct cs_card *card, int change)
{
	struct pci_dev *acpi_dev;
	u16 control;
	u8 pp;
	unsigned long port;
	int old=card->amplifier;
	
	card->amplifier+=change;
	
	acpi_dev = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_3, NULL);
	if(acpi_dev == NULL)
		return;		/* Not a thinkpad thats for sure */

	/* Find the control port */		
	pci_read_config_byte(acpi_dev, 0x41, &pp);
	port=pp<<8;

	/* Read ACPI port */	
	control=inw(port+0x10);

	/* Flip CLKRUN off while running */
	if(!card->amplifier && old)
		outw(control|0x2000, port+0x10);
	else if(card->amplifier && !old)
		outw(control&~0x2000, port+0x10);
}

	
static int cs_open(struct inode *inode, struct file *file)
{
	struct cs_card *card = devs;
	struct cs_state *state = NULL;
	struct dmabuf *dmabuf = NULL;
	int ret=0;

	CS_DBGOUT(CS_OPEN | CS_FUNCTION, 2, printk("cs46xx: cs_open()+ file=0x%x %s %s\n",
		(unsigned)file, file->f_mode & FMODE_WRITE ? "FMODE_WRITE" : "",
		file->f_mode & FMODE_READ ? "FMODE_READ" : "") );

	/*
	 * hardcode state[0] for capture, [1] for playback
	 */
	if(file->f_mode & FMODE_READ)
	{
		CS_DBGOUT(CS_WAVE_READ, 2, printk("cs46xx: cs_open() FMODE_READ\n") );
		if (card->states[0] == NULL) {
			state = card->states[0] = (struct cs_state *)
				kmalloc(sizeof(struct cs_state), GFP_KERNEL);
			if (state == NULL)
				return -ENOMEM;
			memset(state, 0, sizeof(struct cs_state));
			dmabuf = &state->dmabuf;
			dmabuf->pbuf = (void *)get_free_page(GFP_KERNEL | GFP_DMA);
			if(dmabuf->pbuf==NULL)
			{
				kfree(state);
				card->states[0]=NULL;
				return -ENOMEM;
			}
		}
		else
		{
			state = card->states[0];
			if(state->open_mode & FMODE_READ)
				return -EBUSY;
		}
		dmabuf->channel = card->alloc_rec_pcm_channel(card);
			
		if (dmabuf->channel == NULL) {
			kfree (card->states[0]);
			card->states[0] = NULL;;
			return -ENODEV;
		}

		/* Now turn on external AMP if needed */
		state->card = card;
		state->card->active_ctrl(state->card,1);
		state->card->amplifier_ctrl(state->card,1);
		
		dmabuf->channel->state = state;
		/* initialize the virtual channel */
		state->virt = 0;
		state->magic = CS_STATE_MAGIC;
		init_waitqueue_head(&dmabuf->wait);
		init_MUTEX(&state->open_sem);
		file->private_data = card;

		down(&state->open_sem);

		/* set default sample format. According to OSS Programmer's Guide  /dev/dsp
		   should be default to unsigned 8-bits, mono, with sample rate 8kHz and
		   /dev/dspW will accept 16-bits sample */

		/* Default input is 8bit mono */
		dmabuf->fmt &= ~CS_FMT_MASK;
		dmabuf->type = CS_TYPE_ADC;
		dmabuf->ossfragshift = 0;
		dmabuf->ossmaxfrags  = 0;
		dmabuf->subdivision  = 0;
		cs_set_adc_rate(state, 8000);
		cs_set_divisor(dmabuf);

		state->open_mode |= FMODE_READ;
		up(&state->open_sem);
		MOD_INC_USE_COUNT;
	}
	if(file->f_mode & FMODE_WRITE)
	{
		CS_DBGOUT(CS_OPEN, 2, printk("cs46xx: cs_open() FMODE_WRITE\n") );
		if (card->states[1] == NULL) {
			state = card->states[1] = (struct cs_state *)
				kmalloc(sizeof(struct cs_state), GFP_KERNEL);
			if (state == NULL)
				return -ENOMEM;
			memset(state, 0, sizeof(struct cs_state));
			dmabuf = &state->dmabuf;
			dmabuf->pbuf = (void *)get_free_page(GFP_KERNEL | GFP_DMA);
			if(dmabuf->pbuf==NULL)
			{
				kfree(state);
				card->states[1]=NULL;
				return -ENOMEM;
			}
		}
		else
		{
			state = card->states[1];
			if(state->open_mode & FMODE_WRITE)
				return -EBUSY;
		}
		dmabuf->channel = card->alloc_pcm_channel(card);
			
		if (dmabuf->channel == NULL) {
			kfree (card->states[1]);
			card->states[1] = NULL;;
			return -ENODEV;
		}

		/* Now turn on external AMP if needed */
		state->card = card;
		state->card->active_ctrl(state->card,1);
		state->card->amplifier_ctrl(state->card,1);
		
		dmabuf->channel->state = state;
		/* initialize the virtual channel */
		state->virt = 1;
		state->magic = CS_STATE_MAGIC;
		init_waitqueue_head(&dmabuf->wait);
		init_MUTEX(&state->open_sem);
		file->private_data = card;

		down(&state->open_sem);

		/* set default sample format. According to OSS Programmer's Guide  /dev/dsp
		   should be default to unsigned 8-bits, mono, with sample rate 8kHz and
		   /dev/dspW will accept 16-bits sample */

		/* Default output is 8bit mono. */
		dmabuf->fmt &= ~CS_FMT_MASK;
		dmabuf->type = CS_TYPE_DAC;
		dmabuf->ossfragshift = 0;
		dmabuf->ossmaxfrags  = 0;
		dmabuf->subdivision  = 0;
		cs_set_dac_rate(state, 8000);
		cs_set_divisor(dmabuf);

		state->open_mode |= FMODE_WRITE;
		up(&state->open_sem);
		MOD_INC_USE_COUNT;
		if((ret = prog_dmabuf(state)))
			return ret;
	}

	CS_DBGOUT(CS_OPEN | CS_FUNCTION, 2, printk("cs46xx: cs_open()- 0\n") );
	return 0;
}

static int cs_release(struct inode *inode, struct file *file)
{
	struct cs_card *card = (struct cs_card *)file->private_data;
	struct dmabuf *dmabuf;
	struct cs_state *state;
	CS_DBGOUT(CS_RELEASE | CS_FUNCTION, 2, printk("cs46xx: cs_release()+ file=0x%x %s %s\n",
		(unsigned)file, file->f_mode & FMODE_WRITE ? "FMODE_WRITE" : "",
		file->f_mode & FMODE_READ ? "FMODE_READ" : "") );

	if (!(file->f_mode & (FMODE_WRITE | FMODE_READ)))
	{
		return -EINVAL;
	}
	state = card->states[1];
	if(state)
	{
		if ( (state->open_mode & FMODE_WRITE) & (file->f_mode & FMODE_WRITE) )
		{
			CS_DBGOUT(CS_RELEASE, 2, printk("cs46xx: cs_release() FMODE_WRITE\n") );
			dmabuf = &state->dmabuf;
			cs_clear_tail(state);
			drain_dac(state, file->f_flags & O_NONBLOCK);
			/* stop DMA state machine and free DMA buffers/channels */
			down(&state->open_sem);
			stop_dac(state);
			dealloc_dmabuf(state);
			state->card->free_pcm_channel(state->card, dmabuf->channel->num);
			free_page((unsigned long)state->dmabuf.pbuf);

			/* we're covered by the open_sem */
			up(&state->open_sem);
			state->card->states[state->virt] = NULL;
			state->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);

			/* Now turn off external AMP if needed */
			state->card->amplifier_ctrl(state->card, -1);
			state->card->active_ctrl(state->card, -1);

			kfree(state);
		}
		MOD_DEC_USE_COUNT;
	}

	state = card->states[0];
	if(state)
	{
		if ( (state->open_mode & FMODE_READ) & (file->f_mode & FMODE_READ) )
		{
			CS_DBGOUT(CS_RELEASE, 2, printk("cs46xx: cs_release() FMODE_READ\n") );
			dmabuf = &state->dmabuf;
			down(&state->open_sem);
			stop_adc(state);
			dealloc_dmabuf(state);
			state->card->free_pcm_channel(state->card, dmabuf->channel->num);
			free_page((unsigned long)state->dmabuf.pbuf);

			/* we're covered by the open_sem */
			up(&state->open_sem);
			state->card->states[state->virt] = NULL;
			state->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);

			/* Now turn off external AMP if needed */
			state->card->amplifier_ctrl(state->card, -1);
			state->card->active_ctrl(state->card, -1);

			kfree(state);
		}
		MOD_DEC_USE_COUNT;
	}

	CS_DBGOUT(CS_FUNCTION | CS_RELEASE, 2, printk("cs46xx: cs_release()- 0\n") );
	return 0;
}

static /*const*/ struct file_operations cs461x_fops = {
	llseek:		cs_llseek,
	read:		cs_read,
	write:		cs_write,
	poll:		cs_poll,
	ioctl:		cs_ioctl,
	mmap:		cs_mmap,
	open:		cs_open,
	release:	cs_release,
};

/* Write AC97 codec registers */


static u16 cs_ac97_get(struct ac97_codec *dev, u8 reg)
{
	struct cs_card *card = dev->private_data;
	int count;
	
	/*
	 *  1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
	 *  2. Write ACCDA = Command Data Register = 470h    for data to write to AC97 
	 *  3. Write ACCTL = Control Register = 460h for initiating the write
	 *  4. Read ACCTL = 460h, DCV should be reset by now and 460h = 17h
	 *  5. if DCV not cleared, break and return error
	 *  6. Read ACSTS = Status Register = 464h, check VSTS bit
	 */


	cs461x_peekBA0(card, BA0_ACSDA);

	/*
	 *  Setup the AC97 control registers on the CS461x to send the
	 *  appropriate command to the AC97 to perform the read.
	 *  ACCAD = Command Address Register = 46Ch
	 *  ACCDA = Command Data Register = 470h
	 *  ACCTL = Control Register = 460h
	 *  set DCV - will clear when process completed
	 *  set CRW - Read command
	 *  set VFRM - valid frame enabled
	 *  set ESYN - ASYNC generation enabled
	 *  set RSTN - ARST# inactive, AC97 codec not reset
	 */

	cs461x_pokeBA0(card, BA0_ACCAD, reg);
	cs461x_pokeBA0(card, BA0_ACCDA, 0);
	cs461x_pokeBA0(card, BA0_ACCTL, ACCTL_DCV | ACCTL_CRW |
					     ACCTL_VFRM | ACCTL_ESYN |
					     ACCTL_RSTN);


	/*
	 *  Wait for the read to occur.
	 */
	for (count = 0; count < 500; count++) {
		/*
		 *  First, we want to wait for a short time.
	 	 */
		udelay(10);
		/*
		 *  Now, check to see if the read has completed.
		 *  ACCTL = 460h, DCV should be reset by now and 460h = 17h
		 */
		if (!(cs461x_peekBA0(card, BA0_ACCTL) & ACCTL_DCV))
			break;
	}

	/*
	 *  Make sure the read completed.
	 */
	if (cs461x_peekBA0(card, BA0_ACCTL) & ACCTL_DCV) {
		printk(KERN_WARNING "cs461x: AC'97 read problem (ACCTL_DCV), reg = 0x%x\n", reg);
		return 0xffff;
	}

	/*
	 *  Wait for the valid status bit to go active.
	 */
	for (count = 0; count < 100; count++) {
		/*
		 *  Read the AC97 status register.
		 *  ACSTS = Status Register = 464h
		 *  VSTS - Valid Status
		 */
		if (cs461x_peekBA0(card, BA0_ACSTS) & ACSTS_VSTS)
			break;
		udelay(10);
	}
	
	/*
	 *  Make sure we got valid status.
	 */
	if (!(cs461x_peekBA0(card, BA0_ACSTS) & ACSTS_VSTS)) {
		printk(KERN_WARNING "cs461x: AC'97 read problem (ACSTS_VSTS), reg = 0x%x\n", reg);
		return 0xffff;
	}

	/*
	 *  Read the data returned from the AC97 register.
	 *  ACSDA = Status Data Register = 474h
	 */
#if 0
	printk("e) reg = 0x%x, val = 0x%x, BA0_ACCAD = 0x%x\n", reg,
			cs461x_peekBA0(card, BA0_ACSDA),
			cs461x_peekBA0(card, BA0_ACCAD));
#endif
	return cs461x_peekBA0(card, BA0_ACSDA);
}

static void cs_ac97_set(struct ac97_codec *dev, u8 reg, u16 val)
{
	struct cs_card *card = dev->private_data;
	int count;
	int val2 = 0;
	
	if(reg == AC97_CD_VOL)
	{
		val2 = cs_ac97_get(dev, AC97_CD_VOL);
	}
	
	/*
	 *  1. Write ACCAD = Command Address Register = 46Ch for AC97 register address
	 *  2. Write ACCDA = Command Data Register = 470h    for data to write to AC97
	 *  3. Write ACCTL = Control Register = 460h for initiating the write
	 *  4. Read ACCTL = 460h, DCV should be reset by now and 460h = 07h
	 *  5. if DCV not cleared, break and return error
	 */

	/*
	 *  Setup the AC97 control registers on the CS461x to send the
	 *  appropriate command to the AC97 to perform the read.
	 *  ACCAD = Command Address Register = 46Ch
	 *  ACCDA = Command Data Register = 470h
	 *  ACCTL = Control Register = 460h
	 *  set DCV - will clear when process completed
	 *  reset CRW - Write command
	 *  set VFRM - valid frame enabled
	 *  set ESYN - ASYNC generation enabled
	 *  set RSTN - ARST# inactive, AC97 codec not reset
         */
	cs461x_pokeBA0(card, BA0_ACCAD, reg);
	cs461x_pokeBA0(card, BA0_ACCDA, val);
	cs461x_pokeBA0(card, BA0_ACCTL, ACCTL_DCV | ACCTL_VFRM |
				             ACCTL_ESYN | ACCTL_RSTN);
	for (count = 0; count < 1000; count++) {
		/*
		 *  First, we want to wait for a short time.
		 */
		udelay(10);
		/*
		 *  Now, check to see if the write has completed.
		 *  ACCTL = 460h, DCV should be reset by now and 460h = 07h
		 */
		if (!(cs461x_peekBA0(card, BA0_ACCTL) & ACCTL_DCV))
			break;
	}
	/*
	 *  Make sure the write completed.
	 */
	if (cs461x_peekBA0(card, BA0_ACCTL) & ACCTL_DCV)
		printk(KERN_WARNING "cs461x: AC'97 write problem, reg = 0x%x, val = 0x%x\n", reg, val);

	/*
	 *	Adjust power if the mixer is selected/deselected according
	 *	to the CD.
	 *
	 *	IF the CD is a valid input source (mixer or direct) AND
	 *		the CD is not muted THEN power is needed
	 *
	 *	We do two things. When record select changes the input to
	 *	add/remove the CD we adjust the power count if the CD is
	 *	unmuted.
	 *
	 *	When the CD mute changes we adjust the power level if the
	 *	CD was a valid input.
	 *
	 *      We also check for CD volume != 0, as the CD mute isn't
	 *      normally tweaked from userspace.
	 */
	 
	/* CD mute change ? */
	
	if(reg==AC97_CD_VOL)
	{
		/* Mute bit change ? */
		if((val2^val)&0x8000 || ((val2 == 0x1f1f || val == 0x1f1f) && val2 != val))
		{
			/* This is a hack but its cleaner than the alternatives.
			   Right now card->ac97_codec[0] might be NULL as we are
			   still doing codec setup. This does an early assignment
			   to avoid the problem if it occurs */
			   
			if(card->ac97_codec[0]==NULL)
				card->ac97_codec[0]=dev;
				
			/* Mute on */
			if(val&0x8000 || val == 0x1f1f)
				card->amplifier_ctrl(card, -1);
			else /* Mute off power on */
				card->amplifier_ctrl(card, 1);
		}
	}
}


/* OSS /dev/mixer file operation methods */

static int cs_open_mixdev(struct inode *inode, struct file *file)
{
	int i;
	int minor = MINOR(inode->i_rdev);
	struct cs_card *card = devs;

	for (card = devs; card != NULL; card = card->next)
		for (i = 0; i < NR_AC97; i++)
			if (card->ac97_codec[i] != NULL &&
			    card->ac97_codec[i]->dev_mixer == minor)
				goto match;

	if (!card)
		return -ENODEV;

 match:
	file->private_data = card->ac97_codec[i];

	card->active_ctrl(card,1);
	MOD_INC_USE_COUNT;
	return 0;
}

static int cs_release_mixdev(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct cs_card *card = devs;
	int i;
	
	for (card = devs; card != NULL; card = card->next)
		for (i = 0; i < NR_AC97; i++)
			if (card->ac97_codec[i] != NULL &&
			    card->ac97_codec[i]->dev_mixer == minor)
				goto match;

	if (!card)
		return -ENODEV;
match:
	card->active_ctrl(card, -1);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int cs_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct ac97_codec *codec = (struct ac97_codec *)file->private_data;

#if CSDEBUG_INTERFACE
        int val;

	if( 	(cmd == SOUND_MIXER_CS_GETDBGMASK) || 
		(cmd == SOUND_MIXER_CS_SETDBGMASK) ||
		(cmd == SOUND_MIXER_CS_GETDBGLEVEL) ||
		(cmd == SOUND_MIXER_CS_SETDBGLEVEL) )
	{
	    switch(cmd)
	    {

		case SOUND_MIXER_CS_GETDBGMASK:
			return put_user(cs_debugmask, (unsigned long *)arg);
		
		case SOUND_MIXER_CS_GETDBGLEVEL:
			return put_user(cs_debuglevel, (unsigned long *)arg);

		case SOUND_MIXER_CS_SETDBGMASK:
			if (get_user(val, (unsigned long *)arg))
				return -EFAULT;
			cs_debugmask = val;
			return 0;

		case SOUND_MIXER_CS_SETDBGLEVEL:
			if (get_user(val, (unsigned long *)arg))
				return -EFAULT;
			cs_debuglevel = val;
			return 0;
		default:
			CS_DBGOUT(CS_ERROR, 1, printk(KERN_INFO 
				"cs4281: mixer_ioctl(): ERROR unknown debug cmd\n") );
			return 0;
	    }
	}
#endif
	return codec->mixer_ioctl(codec, cmd, arg);
}

static /*const*/ struct file_operations cs_mixer_fops = {
	llseek:		cs_llseek,
	ioctl:		cs_ioctl_mixdev,
	open:		cs_open_mixdev,
	release:	cs_release_mixdev,
};

/* AC97 codec initialisation. */
static int __init cs_ac97_init(struct cs_card *card)
{
	int num_ac97 = 0;
	int ready_2nd = 0;
	struct ac97_codec *codec;
	u16 eid;

	for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {
		if ((codec = kmalloc(sizeof(struct ac97_codec), GFP_KERNEL)) == NULL)
			return -ENOMEM;
		memset(codec, 0, sizeof(struct ac97_codec));

		/* initialize some basic codec information, other fields will be filled
		   in ac97_probe_codec */
		codec->private_data = card;
		codec->id = num_ac97;

		codec->codec_read = cs_ac97_get;
		codec->codec_write = cs_ac97_set;
	
		if (ac97_probe_codec(codec) == 0)
			break;

		eid = cs_ac97_get(codec, AC97_EXTENDED_ID);
		
		if(eid==0xFFFFFF)
		{
			printk(KERN_WARNING "cs461x: no codec attached ?\n");
			kfree(codec);
			break;
		}
		
		card->ac97_features = eid;
			
		if ((codec->dev_mixer = register_sound_mixer(&cs_mixer_fops, -1)) < 0) {
			printk(KERN_ERR "cs461x: couldn't register mixer!\n");
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

/*
 * load the static image into the DSP
 */
#include "cs461x_image.h"
static void cs461x_download_image(struct cs_card *card)
{
    unsigned i, j, temp1, temp2, offset, count;
    unsigned char *pBA1 = ioremap(card->ba1_addr, 0x40000);
    for( i=0; i < CLEAR__COUNT; i++)
    {
        offset = ClrStat[i].BA1__DestByteOffset;
        count  = ClrStat[i].BA1__SourceSize;
        for(  temp1 = offset; temp1<(offset+count); temp1+=4 );
              writel(0, pBA1+temp1);
    }

    for(i=0; i<FILL__COUNT; i++)
    {
        temp2 = FillStat[i].Offset;
        for(j=0; j<(FillStat[i].Size)/4; j++)
        {
            temp1 = (FillStat[i]).pFill[j];
            writel(temp1, pBA1+temp2+j*4);
        }
    }
    iounmap(pBA1);
}


/*
 *  Chip reset
 */

static void cs461x_reset(struct cs_card *card)
{
	int idx;

	/*
	 *  Write the reset bit of the SP control register.
	 */
	cs461x_poke(card, BA1_SPCR, SPCR_RSTSP);

	/*
	 *  Write the control register.
	 */
	cs461x_poke(card, BA1_SPCR, SPCR_DRQEN);

	/*
	 *  Clear the trap registers.
	 */
	for (idx = 0; idx < 8; idx++) {
		cs461x_poke(card, BA1_DREG, DREG_REGID_TRAP_SELECT + idx);
		cs461x_poke(card, BA1_TWPR, 0xFFFF);
	}
	cs461x_poke(card, BA1_DREG, 0);

	/*
	 *  Set the frame timer to reflect the number of cycles per frame.
	 */
	cs461x_poke(card, BA1_FRMT, 0xadf);
}

static void cs461x_clear_serial_FIFOs(struct cs_card *card)
{
	int idx, loop, powerdown = 0;
	unsigned int tmp;

	/*
	 *  See if the devices are powered down.  If so, we must power them up first
	 *  or they will not respond.
	 */
	if (!((tmp = cs461x_peekBA0(card, BA0_CLKCR1)) & CLKCR1_SWCE)) {
		cs461x_pokeBA0(card, BA0_CLKCR1, tmp | CLKCR1_SWCE);
		powerdown = 1;
	}

	/*
	 *  We want to clear out the serial port FIFOs so we don't end up playing
	 *  whatever random garbage happens to be in them.  We fill the sample FIFOS
	 *  with zero (silence).
         */
	cs461x_pokeBA0(card, BA0_SERBWP, 0);

	/*
	 *  Fill all 256 sample FIFO locations.
	 */
	for (idx = 0; idx < 256; idx++) {
		/*
		 *  Make sure the previous FIFO write operation has completed.
		 */
		for (loop = 0; loop < 5; loop++) {
			udelay(50);
			if (!(cs461x_peekBA0(card, BA0_SERBST) & SERBST_WBSY))
				break;
		}
		if (cs461x_peekBA0(card, BA0_SERBST) & SERBST_WBSY) {
			if (powerdown)
				cs461x_pokeBA0(card, BA0_CLKCR1, tmp);
		}
		/*
		 *  Write the serial port FIFO index.
		 */
		cs461x_pokeBA0(card, BA0_SERBAD, idx);
		/*
		 *  Tell the serial port to load the new value into the FIFO location.
		 */
		cs461x_pokeBA0(card, BA0_SERBCM, SERBCM_WRC);
	}
	/*
	 *  Now, if we powered up the devices, then power them back down again.
	 *  This is kinda ugly, but should never happen.
	 */
	if (powerdown)
		cs461x_pokeBA0(card, BA0_CLKCR1, tmp);
}

static void cs461x_powerup_dac(struct cs_card *card)
{
	int count;
	unsigned int tmp;

	/*
	 *  Power on the DACs on the AC97 card.  We turn off the DAC
	 *  powerdown bit and write the new value of the power control
	 *  register.
	 */
	tmp = cs_ac97_get(card->ac97_codec[0], AC97_POWER_CONTROL);
	if (tmp & 2)	/* already */
		return;
	cs_ac97_set(card->ac97_codec[0], AC97_POWER_CONTROL, tmp & 0xfdff);

	/*
	 *  Now, we wait until we sample a DAC ready state.
	 */
	for (count = 0; count < 32; count++) {
		/*
		 *  First, lets wait a short while to let things settle out a
		 *  bit, and to prevent retrying the read too quickly.
		 */
		udelay(50);

		/*
		 *  Read the current state of the power control register.
		 */
		if (cs_ac97_get(card->ac97_codec[0], AC97_POWER_CONTROL) & 2)
			break;
	}
	
	/*
	 *  Check the status..
	 */
	if (!(cs_ac97_get(card->ac97_codec[0], AC97_POWER_CONTROL) & 2))
		printk(KERN_WARNING "cs461x: powerup DAC failed\n");
}

static void cs461x_powerup_adc(struct cs_card *card)
{
	int count;
	unsigned int tmp;

	/*
	 *  Power on the ADCs on the AC97 card.  We turn off the DAC
	 *  powerdown bit and write the new value of the power control
	 *  register.
	 */
	tmp = cs_ac97_get(card->ac97_codec[0], AC97_POWER_CONTROL);
	if (tmp & 1)	/* already */
		return;
	cs_ac97_set(card->ac97_codec[0], AC97_POWER_CONTROL, tmp & 0xfeff);

	/*
	 *  Now, we wait until we sample a ADC ready state.
	 */
	for (count = 0; count < 32; count++) {
		/*
		 *  First, lets wait a short while to let things settle out a
		 *  bit, and to prevent retrying the read too quickly.
		 */
		udelay(50);

		/*
		 *  Read the current state of the power control register.
		 */
		if (cs_ac97_get(card->ac97_codec[0], AC97_POWER_CONTROL) & 1)
			break;
	}

	/*
	 *  Check the status..
	 */
	if (!(cs_ac97_get(card->ac97_codec[0], AC97_POWER_CONTROL) & 1))
		printk(KERN_WARNING "cs461x: powerup ADC failed\n");
}

static void cs461x_proc_start(struct cs_card *card)
{
	int cnt;

	/*
	 *  Set the frame timer to reflect the number of cycles per frame.
	 */
	cs461x_poke(card, BA1_FRMT, 0xadf);
	/*
	 *  Turn on the run, run at frame, and DMA enable bits in the local copy of
	 *  the SP control register.
	 */
	cs461x_poke(card, BA1_SPCR, SPCR_RUN | SPCR_RUNFR | SPCR_DRQEN);
	/*
	 *  Wait until the run at frame bit resets itself in the SP control
	 *  register.
	 */
	for (cnt = 0; cnt < 25; cnt++) {
		udelay(50);
		if (!(cs461x_peek(card, BA1_SPCR) & SPCR_RUNFR))
			break;
	}

	if (cs461x_peek(card, BA1_SPCR) & SPCR_RUNFR)
		printk(KERN_WARNING "cs461x: SPCR_RUNFR never reset\n");
}

static void cs461x_proc_stop(struct cs_card *card)
{
	/*
	 *  Turn off the run, run at frame, and DMA enable bits in the local copy of
	 *  the SP control register.
	 */
	cs461x_poke(card, BA1_SPCR, 0);
}



static int cs_hardware_init(struct cs_card *card)
{
	unsigned long end_time;
	unsigned int tmp;
	
	/* 
	 *  First, blast the clock control register to zero so that the PLL starts
         *  out in a known state, and blast the master serial port control register
         *  to zero so that the serial ports also start out in a known state.
         */
        cs461x_pokeBA0(card, BA0_CLKCR1, 0);
        cs461x_pokeBA0(card, BA0_SERMC1, 0);

	/*
	 *  If we are in AC97 mode, then we must set the part to a host controlled
         *  AC-link.  Otherwise, we won't be able to bring up the link.
         */        
        cs461x_pokeBA0(card, BA0_SERACC, SERACC_HSP | SERACC_CODEC_TYPE_1_03);	/* 1.03 card */
        /* cs461x_pokeBA0(card, BA0_SERACC, SERACC_HSP | SERACC_CODEC_TYPE_2_0); */ /* 2.00 card */

        /*
         *  Drive the ARST# pin low for a minimum of 1uS (as defined in the AC97
         *  spec) and then drive it high.  This is done for non AC97 modes since
         *  there might be logic external to the CS461x that uses the ARST# line
         *  for a reset.
         */
        cs461x_pokeBA0(card, BA0_ACCTL, 0);
        udelay(50);
        cs461x_pokeBA0(card, BA0_ACCTL, ACCTL_RSTN);

	/*
	 *  The first thing we do here is to enable sync generation.  As soon
	 *  as we start receiving bit clock, we'll start producing the SYNC
	 *  signal.
	 */
	cs461x_pokeBA0(card, BA0_ACCTL, ACCTL_ESYN | ACCTL_RSTN);

	/*
	 *  Now wait for a short while to allow the AC97 part to start
	 *  generating bit clock (so we don't try to start the PLL without an
	 *  input clock).
	 */
	mdelay(5);		/* 1 should be enough ?? (and pigs might fly) */

	/*
	 *  Set the serial port timing configuration, so that
	 *  the clock control circuit gets its clock from the correct place.
	 */
	cs461x_pokeBA0(card, BA0_SERMC1, SERMC1_PTC_AC97);

	/*
	 *  Write the selected clock control setup to the hardware.  Do not turn on
	 *  SWCE yet (if requested), so that the devices clocked by the output of
	 *  PLL are not clocked until the PLL is stable.
	 */
	cs461x_pokeBA0(card, BA0_PLLCC, PLLCC_LPF_1050_2780_KHZ | PLLCC_CDR_73_104_MHZ);
	cs461x_pokeBA0(card, BA0_PLLM, 0x3a);
	cs461x_pokeBA0(card, BA0_CLKCR2, CLKCR2_PDIVS_8);

	/*
	 *  Power up the PLL.
	 */
	cs461x_pokeBA0(card, BA0_CLKCR1, CLKCR1_PLLP);

	/*
         *  Wait until the PLL has stabilized.
	 */
	mdelay(5);		/* Again 1 should be enough ?? */

	/*
	 *  Turn on clocking of the core so that we can setup the serial ports.
	 */
	tmp = cs461x_peekBA0(card, BA0_CLKCR1) | CLKCR1_SWCE;
	cs461x_pokeBA0(card, BA0_CLKCR1, tmp);

	/*
	 *  Fill the serial port FIFOs with silence.
	 */
	cs461x_clear_serial_FIFOs(card);

	/*
	 *  Set the serial port FIFO pointer to the first sample in the FIFO.
	 */
	/* cs461x_pokeBA0(card, BA0_SERBSP, 0); */

	/*
	 *  Write the serial port configuration to the part.  The master
	 *  enable bit is not set until all other values have been written.
	 */
	cs461x_pokeBA0(card, BA0_SERC1, SERC1_SO1F_AC97 | SERC1_SO1EN);
	cs461x_pokeBA0(card, BA0_SERC2, SERC2_SI1F_AC97 | SERC1_SO1EN);
	cs461x_pokeBA0(card, BA0_SERMC1, SERMC1_PTC_AC97 | SERMC1_MSPE);


	mdelay(5);		/* Shouldnt be needed ?? */
	
	/*
	 * Wait for the card ready signal from the AC97 card.
	 */
	end_time = jiffies + 3 * (HZ >> 2);
	do {
		/*
		 *  Read the AC97 status register to see if we've seen a CODEC READY
		 *  signal from the AC97 card.
		 */
		if (cs461x_peekBA0(card, BA0_ACSTS) & ACSTS_CRDY)
			break;
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(1);
	} while (time_before(jiffies, end_time));

	/*
	 *  Make sure CODEC is READY.
	 */
	if (!(cs461x_peekBA0(card, BA0_ACSTS) & ACSTS_CRDY)) {
		printk(KERN_WARNING "cs461x: create - never read card ready from AC'97\n");
		printk(KERN_WARNING "cs461x: it is probably not a bug, try using the CS4232 driver\n");
		return -EIO;
	}

	/*
	 *  Assert the vaid frame signal so that we can start sending commands
	 *  to the AC97 card.
	 */
	cs461x_pokeBA0(card, BA0_ACCTL, ACCTL_VFRM | ACCTL_ESYN | ACCTL_RSTN);

	/*
	 *  Wait until we've sampled input slots 3 and 4 as valid, meaning that
	 *  the card is pumping ADC data across the AC-link.
	 */
	end_time = jiffies + 3 * (HZ >> 2);
	do {
		/*
		 *  Read the input slot valid register and see if input slots 3 and
		 *  4 are valid yet.
		 */
		if ((cs461x_peekBA0(card, BA0_ACISV) & (ACISV_ISV3 | ACISV_ISV4)) == (ACISV_ISV3 | ACISV_ISV4))
			break;
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(1);
	} while (time_before(jiffies, end_time));

	/*
	 *  Make sure input slots 3 and 4 are valid.  If not, then return
	 *  an error.
	 */
	if ((cs461x_peekBA0(card, BA0_ACISV) & (ACISV_ISV3 | ACISV_ISV4)) != (ACISV_ISV3 | ACISV_ISV4)) {
		printk(KERN_WARNING "cs461x: create - never read ISV3 & ISV4 from AC'97\n");
		return -EIO;
	}

	/*
	 *  Now, assert valid frame and the slot 3 and 4 valid bits.  This will
	 *  commense the transfer of digital audio data to the AC97 card.
	 */
	cs461x_pokeBA0(card, BA0_ACOSV, ACOSV_SLV3 | ACOSV_SLV4);

	/*
	 *  Power down the DAC and ADC.  We will power them up (if) when we need
	 *  them.
	 */
	/* cs461x_pokeBA0(card, BA0_AC97_POWERDOWN, 0x300); */

	/*
	 *  Turn off the Processor by turning off the software clock enable flag in 
	 *  the clock control register.
	 */
	/* tmp = cs461x_peekBA0(card, BA0_CLKCR1) & ~CLKCR1_SWCE; */
	/* cs461x_pokeBA0(card, BA0_CLKCR1, tmp); */

	/*
         *  Reset the processor.
         */
	cs461x_reset(card);

	/*
         *  Download the image to the processor.
	 */
	
	cs461x_download_image(card);

	/*
         *  Stop playback DMA.
	 */
	tmp = cs461x_peek(card, BA1_PCTL);
	card->pctl = tmp & 0xffff0000;
	cs461x_poke(card, BA1_PCTL, tmp & 0x0000ffff);

	/*
         *  Stop capture DMA.
	 */
	tmp = cs461x_peek(card, BA1_CCTL);
	card->cctl = tmp & 0x0000ffff;
	cs461x_poke(card, BA1_CCTL, tmp & 0xffff0000);

	/* initialize AC97 codec and register /dev/mixer */
	if (cs_ac97_init(card) <= 0)
		return -EIO;
		
	mdelay(5);		/* Do we need this ?? */
	
	cs461x_powerup_adc(card);
	cs461x_powerup_dac(card);

	cs461x_proc_start(card);

	/*
	 *  Enable interrupts on the part.
	 */
	cs461x_pokeBA0(card, BA0_HICR, HICR_IEV | HICR_CHGM);

	tmp = cs461x_peek(card, BA1_PFIE);
	tmp &= ~0x0000f03f;
	cs461x_poke(card, BA1_PFIE, tmp);	/* playback interrupt enable */

	tmp = cs461x_peek(card, BA1_CIE);
	tmp &= ~0x0000003f;
	tmp |=  0x00000001;
	cs461x_poke(card, BA1_CIE, tmp);	/* capture interrupt enable */	
	return 0;
}

/* install the driver, we do not allocate hardware channel nor DMA buffer now, they are defered 
   untill "ACCESS" time (in prog_dmabuf called by open/read/write/ioctl/mmap) */
   
   
/*
 *	Card subid table
 */
 
struct cs_card_type
{
	u16 vendor;
	u16 id;
	char *name;
	void (*amp)(struct cs_card *, int);
	void (*active)(struct cs_card *, int);
};

static struct cs_card_type __initdata cards[]={
	{0x1489, 0x7001, "Genius Soundmaker 128 value", amp_none, NULL},
	{0x5053, 0x3357, "Voyetra", amp_voyetra, NULL},
	{0x1071, 0x6003, "Mitac MI6020/21", amp_voyetra, NULL},
	/* Not sure if the 570 needs the clkrun hack */
	{PCI_VENDOR_ID_IBM, 0x0132, "Thinkpad 570", amp_none, clkrun_hack},
	{PCI_VENDOR_ID_IBM, 0x0153, "Thinkpad 600X/A20/T20", amp_none, clkrun_hack},
	{PCI_VENDOR_ID_IBM, 0x1010, "Thinkpad 600E (unsupported)", NULL, NULL},
	{0, 0, "Card without SSID set", NULL, NULL },
	{0, 0, NULL, NULL, NULL}
};

#ifdef CS46XX_PM
static int cs46xx_pm_callback(struct pm_dev *dev, pm_request_t rqst, void *data)
{
struct cs_state *state = (struct cs_state *) dev->data;

	if (state) {
		switch(rqst) {
			case PM_RESUME:
				printk( KERN_DEBUG "cs46xx: PM resume request\n");
				cs_hardware_init(state->card);
				break;
			case PM_SUSPEND:
				printk( KERN_DEBUG "cs46xx: PM suspend request\n");
				stop_dac(state);
				resync_dma_ptrs(state);
				break;
		}
	}

return 0;
}
#endif

static int __init cs_install(struct pci_dev *pci_dev)
{
	struct cs_card *card;
	struct cs_card_type *cp = &cards[0];
#ifdef CS46XX_PM
	struct pm_dev *pmdev;
#endif
	u16 ss_card, ss_vendor;
	
	
	pci_read_config_word(pci_dev, PCI_SUBSYSTEM_VENDOR_ID, &ss_vendor);
	pci_read_config_word(pci_dev, PCI_SUBSYSTEM_ID, &ss_card);

	if ((card = kmalloc(sizeof(struct cs_card), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "cs461x: out of memory\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(*card));

	card->ba0_addr = pci_dev->resource[0].start&PCI_BASE_ADDRESS_MEM_MASK;
	card->ba1_addr = pci_dev->resource[1].start&PCI_BASE_ADDRESS_MEM_MASK;
	card->pci_dev = pci_dev;
	card->irq = pci_dev->irq;
	card->magic = CS_CARD_MAGIC;
	spin_lock_init(&card->lock);

	pci_set_master(pci_dev);

	printk(KERN_INFO "cs461x: Card found at 0x%08lx and 0x%08lx, IRQ %d\n",
	       card->ba0_addr, card->ba1_addr, card->irq);

	card->alloc_pcm_channel = cs_alloc_pcm_channel;
	card->alloc_rec_pcm_channel = cs_alloc_rec_pcm_channel;
	card->free_pcm_channel = cs_free_pcm_channel;
	card->amplifier_ctrl = amp_none;
	card->active_ctrl = amp_none;
	
	while (cp->name)
	{
		if(cp->vendor == ss_vendor && cp->id == ss_card)
		{
			card->amplifier_ctrl = cp->amp;
			if(cp->active)
				card->active_ctrl = cp->active;
			break;
		}
		cp++;
	}
	if (cp->name==NULL)
	{
		printk(KERN_INFO "cs461x: Unknown card (%04X:%04X) at 0x%08lx/0x%08lx, IRQ %d\n",
			ss_vendor, ss_card, card->ba0_addr, card->ba1_addr,  card->irq);
	}
	else
	{
		printk(KERN_INFO "cs461x: %s at 0x%08lx/0x%08lx, IRQ %d\n",
			cp->name, card->ba0_addr, card->ba1_addr, card->irq);
	}
	
	if (card->amplifier_ctrl==NULL)
	{
		card->amplifier_ctrl = amp_none;
		card->active_ctrl = clkrun_hack;
	}		
		       
	if (external_amp == 1)
	{
		printk(KERN_INFO "cs461x: Crystal EAPD support forced on.\n");
		card->amplifier_ctrl = amp_voyetra;
	}

	if (thinkpad == 1)
	{
		card->active_ctrl = clkrun_hack;
		printk(KERN_INFO "cs461x: Activating CLKRUN hack for Thinkpad.\n");
	}
	
	card->active_ctrl(card, 1);
	
	/* claim our iospace and irq */
	
	card->ba0 = ioremap(card->ba0_addr, CS461X_BA0_SIZE);
	card->ba1.name.data0 = ioremap(card->ba1_addr + BA1_SP_DMEM0, CS461X_BA1_DATA0_SIZE);
	card->ba1.name.data1 = ioremap(card->ba1_addr + BA1_SP_DMEM1, CS461X_BA1_DATA1_SIZE);
	card->ba1.name.pmem = ioremap(card->ba1_addr + BA1_SP_PMEM, CS461X_BA1_PRG_SIZE);
	card->ba1.name.reg = ioremap(card->ba1_addr + BA1_SP_REG, CS461X_BA1_REG_SIZE);
	
	CS_DBGOUT(CS_INIT, 4, printk("card->ba0=0x%.08x\n",(unsigned)card->ba0) );
	CS_DBGOUT(CS_INIT, 4, printk("card->ba1=0x%.08x 0x%.08x 0x%.08x 0x%.08x\n",
		(unsigned)card->ba1.name.data0,
		(unsigned)card->ba1.name.data1,
		(unsigned)card->ba1.name.pmem,
		(unsigned)card->ba1.name.reg) );

	if(card->ba0 == 0 || card->ba1.name.data0 == 0 ||
		card->ba1.name.data1 == 0 || card->ba1.name.pmem == 0 ||
		card->ba1.name.reg == 0)
		goto fail2;
		
	if (request_irq(card->irq, &cs_interrupt, SA_SHIRQ, "cs461x", card)) {
		printk(KERN_ERR "cs461x: unable to allocate irq %d\n", card->irq);
		goto fail2;
	}
	/* register /dev/dsp */
	if ((card->dev_audio = register_sound_dsp(&cs461x_fops, -1)) < 0) {
		printk(KERN_ERR "cs461x: unable to register dsp\n");
		goto fail;
	}

        /* register /dev/midi */
        if((card->dev_midi = register_sound_midi(&cs_midi_fops, -1)) < 0)
                printk(KERN_ERR "cs461x: unable to register midi\n");
                
        if (cs_hardware_init(card)<0)
        {
                unregister_sound_dsp(card->dev_audio);
                if(card->dev_midi)
                        unregister_sound_midi(card->dev_midi);
                goto fail;
        }
        init_waitqueue_head(&card->midi.open_wait);
        init_MUTEX(&card->midi.open_sem);
        init_waitqueue_head(&card->midi.iwait);
        init_waitqueue_head(&card->midi.owait);
        card->next = devs;
        devs = card;
        cs461x_pokeBA0(card, BA0_MIDCR, MIDCR_MRST);   
        cs461x_pokeBA0(card, BA0_MIDCR, 0);   
        
        card->active_ctrl(card, -1);
#ifdef CS46XX_PM
        pmdev = pm_register(PM_PCI_DEV, PM_PCI_ID(pci_dev), cs46xx_pm_callback);
        if (pmdev)
        	pmdev->data = card;
#endif
        return 0;

fail:
	free_irq(card->irq, card);
fail2:
	if(card->ba0)
		iounmap(card->ba0);
	if(card->ba1.name.data0)
		iounmap(card->ba1.name.data0);
	if(card->ba1.name.data1)
		iounmap(card->ba1.name.data1);
	if(card->ba1.name.pmem)
		iounmap(card->ba1.name.pmem);
	if(card->ba1.name.reg)
		iounmap(card->ba1.name.reg);
	kfree(card);
	return -ENODEV;

}

static void cs_remove(struct cs_card *card)
{
	int i;
	unsigned int tmp;
	
	card->active_ctrl(card,1);
	
	tmp = cs461x_peek(card, BA1_PFIE);
	tmp &= ~0x0000f03f;
	tmp |=  0x00000010;
	cs461x_poke(card, BA1_PFIE, tmp);	/* playback interrupt disable */

	tmp = cs461x_peek(card, BA1_CIE);
	tmp &= ~0x0000003f;
	tmp |=  0x00000011;
	cs461x_poke(card, BA1_CIE, tmp);	/* capture interrupt disable */

	/*
         *  Stop playback DMA.
	 */
	tmp = cs461x_peek(card, BA1_PCTL);
	cs461x_poke(card, BA1_PCTL, tmp & 0x0000ffff);

	/*
         *  Stop capture DMA.
	 */
	tmp = cs461x_peek(card, BA1_CCTL);
	cs461x_poke(card, BA1_CCTL, tmp & 0xffff0000);

	/*
         *  Reset the processor.
         */
	cs461x_reset(card);

	cs461x_proc_stop(card);

	/*
	 *  Power down the DAC and ADC.  We will power them up (if) when we need
	 *  them.
	 */
	cs_ac97_set(card->ac97_codec[0], AC97_POWER_CONTROL, 0x300);

	/*
	 *  Power down the PLL.
	 */
	cs461x_pokeBA0(card, BA0_CLKCR1, 0);

	/*
	 *  Turn off the Processor by turning off the software clock enable flag in 
	 *  the clock control register.
	 */
	tmp = cs461x_peekBA0(card, BA0_CLKCR1) & ~CLKCR1_SWCE;
	cs461x_pokeBA0(card, BA0_CLKCR1, tmp);

	card->active_ctrl(card,-1);

	/* free hardware resources */
	free_irq(card->irq, card);
	iounmap(card->ba0);
	iounmap(card->ba1.name.data0);
	iounmap(card->ba1.name.data1);
	iounmap(card->ba1.name.pmem);
	iounmap(card->ba1.name.reg);
	
	/* unregister audio devices */
	for (i = 0; i < NR_AC97; i++)
		if (card->ac97_codec[i] != NULL) {
			unregister_sound_mixer(card->ac97_codec[i]->dev_mixer);
			kfree (card->ac97_codec[i]);
		}
	unregister_sound_dsp(card->dev_audio);
        if(card->dev_midi)
                unregister_sound_midi(card->dev_midi);
	kfree(card);
}

MODULE_AUTHOR("Alan Cox <alan@redhat.com>, Jaroslav Kysela, <audio@crystal.cirrus.com>");
MODULE_DESCRIPTION("Crystal SoundFusion Audio Support");

int __init cs_probe(void)
{
	struct pci_dev *pcidev = NULL;
	int foundone=0;
	
	if (!pci_present())   /* No PCI bus in this machine! */
		return -ENODEV;
		
	printk(KERN_INFO "Crystal 4280/461x + AC97 Audio, version "
	       DRIVER_VERSION ", " __TIME__ " " __DATE__ "\n");

	while( (pcidev = pci_find_device(PCI_VENDOR_ID_CIRRUS, 0x6001 , pcidev))!=NULL ) {
		if (cs_install(pcidev)==0)
			foundone++;
	}
	while( (pcidev = pci_find_device(PCI_VENDOR_ID_CIRRUS, 0x6003 , pcidev))!=NULL ) {
		if (cs_install(pcidev)==0)
			foundone++;
	}
	while( (pcidev = pci_find_device(PCI_VENDOR_ID_CIRRUS, 0x6004 , pcidev))!=NULL ) {
		if (cs_install(pcidev)==0)
			foundone++;
	}

	printk(KERN_INFO "cs461x: Found %d audio device(s).\n",
		foundone);
	return foundone;
}

int __init init_module(void)
{
	if(cs_probe()==0)
		printk(KERN_ERR "cs461x: No devices found.\n");
	return 0;
}

void __exit cleanup_module (void)
{
	struct cs_card *next;
#ifdef CS46XX_PM
	pm_unregister_all(cs46xx_pm_callback);
#endif
	while(devs)
	{
		next=devs->next;
		cs_remove(devs);
		devs=next;
	}
}

MODULE_PARM(external_amp, "i");
MODULE_PARM(thinkpad, "i");
