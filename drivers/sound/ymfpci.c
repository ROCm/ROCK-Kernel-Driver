/*
 *  Copyright 1999 Jaroslav Kysela <perex@suse.cz>
 *  Copyright 2000 Alan Cox <alan@redhat.com>
 *
 *  Yamaha YMF7xx driver.
 *
 *  This code is a result of high-speed collision
 *  between ymfpci.c of ALSA and cs46xx.c of Linux.
 *  -- Pete Zaitcev <zaitcev@metabyte.com>; 2000/09/18
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
 * TODO:
 *  - Use P44Slot for 44.1 playback.
 *  - Capture and duplex
 *  - 96KHz playback for DVD - use pitch of 2.0.
 *  - uLaw for Sun apps.
 *  - Retain DMA buffer on close, do not wait the end of frame.
 *  - Cleanup
 *      ? merge ymf_pcm and state
 *      ? pcm interrupt no pointer
 *      ? underused structure members
 *      - Remove remaining P3 tags (debug messages).
 *  - Resolve XXX tagged questions.
 *  - Cannot play 5133Hz.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/malloc.h>
#include <linux/poll.h>
#include <linux/ac97_codec.h>
#include <linux/sound.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

#include "ymfpci.h"

#define snd_magic_cast(t, p, err)	((t *)(p))

/* Channels, such as play and record. I do only play a.t.m. XXX */
#define NR_HW_CH	1

static int ymf_playback_trigger(ymfpci_t *codec, ymfpci_pcm_t *ypcm, int cmd);
static int ymfpci_voice_alloc(ymfpci_t *codec, ymfpci_voice_type_t type,
    int pair, ymfpci_voice_t **rvoice);
static int ymfpci_voice_free(ymfpci_t *codec, ymfpci_voice_t *pvoice);
static int ymf_playback_prepare(ymfpci_t *codec, struct ymf_state *state);
static int ymf_state_alloc(ymfpci_t *unit, int nvirt);

static LIST_HEAD(ymf_devs);

/*
 *  constants
 */

static struct pci_device_id ymf_id_tbl[] __devinitdata = {
#define DEV(v, d, data) \
  { PCI_VENDOR_ID_##v, PCI_DEVICE_ID_##v##_##d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long)data }
	DEV (YAMAHA, 724,  "YMF724"),
	DEV (YAMAHA, 724F, "YMF724F"),
	DEV (YAMAHA, 740,  "YMF740"),
	DEV (YAMAHA, 740C, "YMF740C"),
	DEV (YAMAHA, 744,  "YMF744"),
	DEV (YAMAHA, 754,  "YMF754"),
#undef DEV
	{ }
};
MODULE_DEVICE_TABLE(pci, ymf_id_tbl);

/*
 * Mindlessly copied from cs46xx XXX
 */
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
 *  common I/O routines
 */

static inline u8 ymfpci_readb(ymfpci_t *codec, u32 offset)
{
	return readb(codec->reg_area_virt + offset);
}

static inline void ymfpci_writeb(ymfpci_t *codec, u32 offset, u8 val)
{
	writeb(val, codec->reg_area_virt + offset);
}

static inline u16 ymfpci_readw(ymfpci_t *codec, u32 offset)
{
	return readw(codec->reg_area_virt + offset);
}

static inline void ymfpci_writew(ymfpci_t *codec, u32 offset, u16 val)
{
	writew(val, codec->reg_area_virt + offset);
}

static inline u32 ymfpci_readl(ymfpci_t *codec, u32 offset)
{
	return readl(codec->reg_area_virt + offset);
}

static inline void ymfpci_writel(ymfpci_t *codec, u32 offset, u32 val)
{
	writel(val, codec->reg_area_virt + offset);
}

static int ymfpci_codec_ready(ymfpci_t *codec, int secondary, int sched)
{
	signed long end_time;
	u32 reg = secondary ? YDSXGR_SECSTATUSADR : YDSXGR_PRISTATUSADR;
	
	end_time = jiffies + 3 * (HZ / 4);
	do {
		if ((ymfpci_readw(codec, reg) & 0x8000) == 0)
			return 0;
		if (sched) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		}
	} while (end_time - (signed long)jiffies >= 0);
	printk("ymfpci_codec_ready: codec %i is not ready [0x%x]\n",
	    secondary, ymfpci_readw(codec, reg));
	return -EBUSY;
}

static void ymfpci_codec_write(struct ac97_codec *dev, u8 reg, u16 val)
{
	ymfpci_t *codec = dev->private_data;
	u32 cmd;

	/* XXX Do make use of dev->id */
	ymfpci_codec_ready(codec, 0, 0);
	cmd = ((YDSXG_AC97WRITECMD | reg) << 16) | val;
	ymfpci_writel(codec, YDSXGR_AC97CMDDATA, cmd);
}

static u16 ymfpci_codec_read(struct ac97_codec *dev, u8 reg)
{
	ymfpci_t *codec = dev->private_data;

	if (ymfpci_codec_ready(codec, 0, 0))
		return ~0;
	ymfpci_writew(codec, YDSXGR_AC97CMDADR, YDSXG_AC97READCMD | reg);
	if (ymfpci_codec_ready(codec, 0, 0))
		return ~0;
	if (codec->pci->device == PCI_DEVICE_ID_YAMAHA_744 && codec->rev < 2) {
		int i;
		for (i = 0; i < 600; i++)
			ymfpci_readw(codec, YDSXGR_PRISTATUSDATA);
	}
	return ymfpci_readw(codec, YDSXGR_PRISTATUSDATA);
}

/*
 *  Misc routines
 */

/*
 * Calculate the actual sampling rate relatetively to the base clock (48kHz).
 */
static u32 ymfpci_calc_delta(u32 rate)
{
	switch (rate) {
	case 8000:	return 0x02aaab00;
	case 11025:	return 0x03accd00;
	case 16000:	return 0x05555500;
	case 22050:	return 0x07599a00;
	case 32000:	return 0x0aaaab00;
	case 44100:	return 0x0eb33300;
	default:	return ((rate << 16) / 48000) << 12;
	}
}

static u32 def_rate[8] = {
	100, 2000, 8000, 11025, 16000, 22050, 32000, 48000
};

static u32 ymfpci_calc_lpfK(u32 rate)
{
	u32 i;
	static u32 val[8] = {
		0x00570000, 0x06AA0000, 0x18B20000, 0x20930000,
		0x2B9A0000, 0x35A10000, 0x3EAA0000, 0x40000000
	};
	
	if (rate == 44100)
		return 0x40000000;	/* FIXME: What's the right value? */
	for (i = 0; i < 8; i++)
		if (rate <= def_rate[i])
			return val[i];
	return val[0];
}

static u32 ymfpci_calc_lpfQ(u32 rate)
{
	u32 i;
	static u32 val[8] = {
		0x35280000, 0x34A70000, 0x32020000, 0x31770000,
		0x31390000, 0x31C90000, 0x33D00000, 0x40000000
	};
	
	if (rate == 44100)
		return 0x370A0000;
	for (i = 0; i < 8; i++)
		if (rate <= def_rate[i])
			return val[i];
	return val[0];
}

static u32 ymf_calc_lend(u32 rate)
{
	return (rate * YMF_SAMPF) / 48000;
}

/*
 * XXX Find if this function exists in the OSS framework.
 * XXX Make sure we do no panic when ADPCM is selected.
 */
static int ymf_pcm_format_width(int format)
{
	static int mask16 = AFMT_S16_LE|AFMT_S16_BE|AFMT_U16_LE|AFMT_U16_BE;

	if ((format & (format-1)) != 0) {
		printk(KERN_ERR "ymfpci: format 0x%x is not a power of 2\n", format);
		return 8;
	}

	if (format == AFMT_IMA_ADPCM) return 4;
	if ((format & mask16) != 0) return 16;
	return 8;
}

static void ymf_pcm_update_shift(struct ymf_pcm_format *f)
{
	f->shift = 0;
	if (f->voices == 2)
		f->shift++;
	if (ymf_pcm_format_width(f->format) == 16)
		f->shift++;
}

/*
 * Whole OSS-style DMA machinery is taken from cs46xx.
 */

/* Are you sure 32K is not too much? See if mpg123 skips on loaded systems. */
#define DMABUF_DEFAULTORDER (15-PAGE_SHIFT)
#define DMABUF_MINORDER 1

/* allocate DMA buffer, playback and recording buffer should be allocated seperately */
static int alloc_dmabuf(struct ymf_state *state)
{
	struct ymf_dmabuf *dmabuf = &state->dmabuf;
	void *rawbuf = NULL;
	int order;
	struct page * map,  * mapend;

	/* alloc as big a chunk as we can */
	for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER; order--)
		if((rawbuf = (void *)__get_free_pages(GFP_KERNEL|GFP_DMA, order)))
			break;

	if (!rawbuf)
		return -ENOMEM;

#if 0
	printk(KERN_DEBUG "ymfpci: allocated %ld (order = %d) bytes at %p\n",
	       PAGE_SIZE << order, order, rawbuf);
#endif

	dmabuf->ready  = dmabuf->mapped = 0;
	dmabuf->rawbuf = rawbuf;
	dmabuf->buforder = order;

	/* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
	mapend = virt_to_page(rawbuf + (PAGE_SIZE << order) - 1);
	for (map = virt_to_page(rawbuf); map <= mapend; map++)
		set_bit(PG_reserved, &map->flags);

	return 0;
}

/* free DMA buffer */
static void dealloc_dmabuf(struct ymf_state *state)
{
	struct ymf_dmabuf *dmabuf = &state->dmabuf;
	struct page *map, *mapend;

	if (dmabuf->rawbuf) {
		/* undo marking the pages as reserved */
		mapend = virt_to_page(dmabuf->rawbuf + (PAGE_SIZE << dmabuf->buforder) - 1);
		for (map = virt_to_page(dmabuf->rawbuf); map <= mapend; map++)
			clear_bit(PG_reserved, &map->flags);
		free_pages((unsigned long)dmabuf->rawbuf,dmabuf->buforder);
	}
	dmabuf->rawbuf = NULL;
	dmabuf->mapped = dmabuf->ready = 0;
}

static int prog_dmabuf(struct ymf_state *state, unsigned rec)
{
	struct ymf_dmabuf *dmabuf = &state->dmabuf;
	int w_16;
	unsigned bytepersec;
	unsigned bufsize;
	unsigned long flags;
	int redzone;
	int ret;

	w_16 = ymf_pcm_format_width(state->format.format) == 16;

	spin_lock_irqsave(&state->unit->reg_lock, flags);
	dmabuf->hwptr = dmabuf->swptr = 0;
	dmabuf->total_bytes = 0;
	dmabuf->count = dmabuf->error = 0;
	spin_unlock_irqrestore(&state->unit->reg_lock, flags);

	/* allocate DMA buffer if not allocated yet */
	if (!dmabuf->rawbuf)
		if ((ret = alloc_dmabuf(state)))
			return ret;

	bytepersec = state->format.rate << state->format.shift;

	/*
	 * Create fake fragment sizes and numbers for OSS ioctls.
	 */
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
	dmabuf->fragsamples = dmabuf->fragsize >> state->format.shift;
	dmabuf->dmasize = dmabuf->numfrag << dmabuf->fragshift;

	/*
	 * Import what Doom might have set with SNDCTL_DSD_SETFRAGMENT.
	 */
	if (dmabuf->ossmaxfrags >= 2 && dmabuf->ossmaxfrags < dmabuf->numfrag) {
		dmabuf->numfrag = dmabuf->ossmaxfrags;
		dmabuf->dmasize = dmabuf->numfrag << dmabuf->fragshift;

		redzone = ymf_calc_lend(state->format.rate);
		redzone <<= (state->format.shift + 1);
		if (dmabuf->dmasize < redzone*3) {
			/*
			 * The driver works correctly with minimum dmasize
			 * of redzone*2, but it produces stoppage and clicks.
			 * So, make it little larger for smoother sound.
			 * XXX Make dmasize a wholy divisible by fragsize.
			 */
//			printk(KERN_ERR "ymfpci: dmasize=%d < redzone=%d * 3\n",
//			    dmabuf->dmasize, redzone);
			dmabuf->dmasize = redzone*3;
		}
	}

	memset(dmabuf->rawbuf, w_16 ? 0 : 0x80, dmabuf->dmasize);

	/*
	 *	Now set up the ring 
	 */

	spin_lock_irqsave(&state->unit->reg_lock, flags);
	if (rec) {
		/* ymf_rec_setup(state); */
	} else {
		if ((ret = ymf_playback_prepare(state->unit, state)) != 0) {
			return ret;
		}
	}
	spin_unlock_irqrestore(&state->unit->reg_lock, flags);

	/* set the ready flag for the dma buffer (this comment is not stupid) */
	dmabuf->ready = 1;

#if 0
	printk("prog_dmabuf: rate %d format 0x%x,"
	    " numfrag %d fragsize %d dmasize %d\n",
	       state->format.rate, state->format.format, dmabuf->numfrag,
	       dmabuf->fragsize, dmabuf->dmasize);
#endif

	return 0;
}

static void ymf_start_dac(struct ymf_state *state)
{
	ymf_playback_trigger(state->unit, &state->ypcm, 1);
}

/*
 * Wait until output is drained.
 * This does not kill the hardware for the sake of ioctls.
 */
static void ymf_wait_dac(struct ymf_state *state)
{
	struct ymf_unit *unit = state->unit;
	ymfpci_pcm_t *ypcm = &state->ypcm;
	DECLARE_WAITQUEUE(waita, current);
	unsigned long flags;

	add_wait_queue(&state->dmabuf.wait, &waita);

	spin_lock_irqsave(&unit->reg_lock, flags);
	if (state->dmabuf.count != 0 && !state->ypcm.running) {
		ymf_playback_trigger(unit, ypcm, 1);
	}

#if 0
	if (file->f_flags & O_NONBLOCK) {
		/*
		 * XXX Our  mistake is to attach DMA buffer to state
		 * rather than to some per-device structure.
		 * Cannot skip waiting, can only make it shorter.
		 */
	}
#endif

	while (ypcm->running) {
		spin_unlock_irqrestore(&unit->reg_lock, flags);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
		spin_lock_irqsave(&unit->reg_lock, flags);
	}
	spin_unlock_irqrestore(&unit->reg_lock, flags);

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&state->dmabuf.wait, &waita);

	/*
	 * This function may take up to 4 seconds to reach this point
	 * (32K circular buffer, 8000 Hz). User notices.
	 */
}

/*
 *  Hardware start management
 */

static void ymfpci_hw_start(ymfpci_t *codec)
{
	unsigned long flags;

	spin_lock_irqsave(&codec->reg_lock, flags);
	if (codec->start_count++ == 0) {
		ymfpci_writel(codec, YDSXGR_MODE, 3);
		codec->active_bank = ymfpci_readl(codec, YDSXGR_CTRLSELECT) & 1;
	}
      	spin_unlock_irqrestore(&codec->reg_lock, flags);
}

static void ymfpci_hw_stop(ymfpci_t *codec)
{
	unsigned long flags;
	long timeout = 1000;

	spin_lock_irqsave(&codec->reg_lock, flags);
	if (--codec->start_count == 0) {
		ymfpci_writel(codec, YDSXGR_MODE, 0);
		while (timeout-- > 0) {
			if ((ymfpci_readl(codec, YDSXGR_STATUS) & 2) == 0)
				break;
		}
	}
      	spin_unlock_irqrestore(&codec->reg_lock, flags);
}

/*
 *  Playback voice management
 */

static int voice_alloc(ymfpci_t *codec, ymfpci_voice_type_t type, int pair, ymfpci_voice_t **rvoice)
{
	ymfpci_voice_t *voice, *voice2;
	int idx;
	
	*rvoice = NULL;
	for (idx = 0; idx < 64; idx += pair ? 2 : 1) {
		voice = &codec->voices[idx];
		voice2 = pair ? &codec->voices[idx+1] : NULL;
		if (voice->use || (voice2 && voice2->use))
			continue;
		voice->use = 1;
		if (voice2)
			voice2->use = 1;
		switch (type) {
		case YMFPCI_PCM:
			voice->pcm = 1;
			if (voice2)
				voice2->pcm = 1;
			break;
		case YMFPCI_SYNTH:
			voice->synth = 1;
			break;
		case YMFPCI_MIDI:
			voice->midi = 1;
			break;
		}
		ymfpci_hw_start(codec);
		if (voice2)
			ymfpci_hw_start(codec);
		*rvoice = voice;
		return 0;
	}
	return -ENOMEM;
}

static int ymfpci_voice_alloc(ymfpci_t *codec, ymfpci_voice_type_t type,
    int pair, ymfpci_voice_t **rvoice)
{
	unsigned long flags;
	int result;

	spin_lock_irqsave(&codec->voice_lock, flags);
	for (;;) {
		result = voice_alloc(codec, type, pair, rvoice);
		if (result == 0 || type != YMFPCI_PCM)
			break;
		/* TODO: synth/midi voice deallocation */
		break;
	}
	spin_unlock_irqrestore(&codec->voice_lock, flags);	
	return result;		
}

static int ymfpci_voice_free(ymfpci_t *codec, ymfpci_voice_t *pvoice)
{
	unsigned long flags;
	
	ymfpci_hw_stop(codec);
	spin_lock_irqsave(&codec->voice_lock, flags);
	pvoice->use = pvoice->pcm = pvoice->synth = pvoice->midi = 0;
	pvoice->ypcm = NULL;
	pvoice->interrupt = NULL;
	spin_unlock_irqrestore(&codec->voice_lock, flags);
	return 0;
}

/*
 *  PCM part
 */

static void ymf_pcm_interrupt(ymfpci_t *codec, ymfpci_voice_t *voice)
{
	ymfpci_pcm_t *ypcm;
	int redzone;
	int pos, delta, swptr;
	int played, distance;
	struct ymf_state *state;
	struct ymf_dmabuf *dmabuf;
	char silence;

	if ((ypcm = voice->ypcm) == NULL) {
		return;
	}
	if ((state = ypcm->state) == NULL) {
		ypcm->running = 0;	// lock it
		return;
	}
	dmabuf = &state->dmabuf;
	spin_lock(&codec->reg_lock);
	if (ypcm->running) {
/* P3 */ /** printk("ymfpci: %d, intr bank %d count %d start 0x%x:%x\n",
  voice->number, codec->active_bank, dmabuf->count,
  voice->bank[0].start, voice->bank[1].start); **/
		silence = (ymf_pcm_format_width(state->format.format) == 16) ?
		    0 : 0x80;
		/* We need actual left-hand-side redzone size here. */
		redzone = ymf_calc_lend(state->format.rate);
		redzone <<= (state->format.shift + 1);
		swptr = dmabuf->swptr;

		pos = voice->bank[codec->active_bank].start;
		pos <<= state->format.shift;
		if (pos < 0 || pos >= dmabuf->dmasize) {	/* ucode bug */
			printk(KERN_ERR
			    "ymfpci%d: %d: runaway: hwptr %d dmasize %d\n",
			    codec->dev_audio, voice->number,
			    dmabuf->hwptr, dmabuf->dmasize);
			pos = 0;
		}
		if (pos < dmabuf->hwptr) {
			delta = dmabuf->dmasize - dmabuf->hwptr;
			memset(dmabuf->rawbuf + dmabuf->hwptr, silence, delta);
			delta += pos;
			memset(dmabuf->rawbuf, silence, pos);
		} else {
			delta = pos - dmabuf->hwptr;
			memset(dmabuf->rawbuf + dmabuf->hwptr, silence, delta);
		}
		dmabuf->hwptr = pos;

		if (dmabuf->count == 0) {
			printk("ymfpci%d: %d: strain: hwptr %d\n",
			    codec->dev_audio, voice->number, dmabuf->hwptr);
			ymf_playback_trigger(codec, ypcm, 0);
		}

		if (swptr <= pos) {
			distance = pos - swptr;
		} else {
			distance = dmabuf->dmasize - (swptr - pos);
		}
		if (distance < redzone) {
			/*
			 * hwptr inside redzone => DMA ran out of samples.
			 */
			if (delta < dmabuf->count) {
				/*
				 * Lost interrupt or other screwage.
				 */
				printk("ymfpci%d: %d: lost: delta %d"
				    " hwptr %d swptr %d distance %d count %d\n",
				    codec->dev_audio, voice->number, delta,
				    dmabuf->hwptr, swptr, distance, dmabuf->count);
			} else {
				/*
				 * Normal end of DMA.
				 */
//				printk("ymfpci%d: %d: done: delta %d"
//				    " hwptr %d swptr %d distance %d count %d\n",
//				    codec->dev_audio, voice->number, delta,
//				    dmabuf->hwptr, swptr, distance, dmabuf->count);
			}
			played = dmabuf->count;
			if (ypcm->running) {
				ymf_playback_trigger(codec, ypcm, 0);
			}
		} else {
			/*
			 * hwptr is chipping away towards a remote swptr.
			 * Calculate other distance and apply it to count.
			 */
			if (swptr >= pos) {
				distance = swptr - pos;
			} else {
				distance = dmabuf->dmasize - (pos - swptr);
			}
			if (distance < dmabuf->count) {
				played = dmabuf->count - distance;
			} else {
				played = 0;
			}
		}

		dmabuf->total_bytes += played;
		dmabuf->count -= played;
		if (dmabuf->count < dmabuf->dmasize / 2) {
			wake_up(&dmabuf->wait);
		}
	}
	spin_unlock(&codec->reg_lock);
}

#if HAVE_RECORD
static void ymfpci_pcm_capture_interrupt(snd_pcm_subchn_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, );
	ymfpci_t *codec = ypcm->codec;
	u32 pos, delta;
	
	spin_lock(&codec->reg_lock);
	if (ypcm->running) {
		pos = codec->bank_capture[ypcm->capture_bank_number][codec->active_bank]->start << ypcm->shift_offset;
		if (pos < ypcm->last_pos)  // <--  dmabuf->hwptr
			delta = pos + (ypcm->buffer_size - ypcm->last_pos);
		else
			delta = pos - ypcm->last_pos;
		ypcm->frag_pos += delta;
		ypcm->last_pos = pos;
		while (ypcm->frag_pos >= ypcm->frag_size) {
			ypcm->frag_pos -= ypcm->frag_size;
			// printk("done - active_bank = 0x%x, start = 0x%x\n", codec->active_bank, voice->bank[codec->active_bank].start);
			spin_unlock(&codec->reg_lock);
			snd_pcm_transfer_done(substream);
			spin_lock(&codec->reg_lock);
		}
	}
	spin_unlock(&codec->reg_lock);
}
#endif

static int ymf_playback_trigger(ymfpci_t *codec, ymfpci_pcm_t *ypcm, int cmd)
{

	if (ypcm->voices[0] == NULL) {
		return -EINVAL;
	}
	if (cmd != 0) {
		codec->ctrl_playback[ypcm->voices[0]->number + 1] = virt_to_bus(ypcm->voices[0]->bank);
		if (ypcm->voices[1] != NULL)
			codec->ctrl_playback[ypcm->voices[1]->number + 1] = virt_to_bus(ypcm->voices[1]->bank);
		ypcm->running = 1;
	} else {
		codec->ctrl_playback[ypcm->voices[0]->number + 1] = 0;
		if (ypcm->voices[1] != NULL)
			codec->ctrl_playback[ypcm->voices[1]->number + 1] = 0;
		ypcm->running = 0;
	}
	return 0;
}

#if HAVE_RECORD
static int ymfpci_capture_trigger(void *private_data,	
				      snd_pcm_subchn_t * substream,
				      int cmd)
{
	unsigned long flags;
	ymfpci_t *codec = snd_magic_cast(ymfpci_t, private_data, -ENXIO);
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, substream->runtime->private_data, -ENXIO);
	int result = 0;
	u32 tmp;

	spin_lock_irqsave(&codec->reg_lock, flags);
	if (cmd == SND_PCM_TRIGGER_GO) {
		tmp = ymfpci_readl(codec, YDSXGR_MAPOFREC) | (1 << ypcm->capture_bank_number);
		ymfpci_writel(codec, YDSXGR_MAPOFREC, tmp);
		ypcm->running = 1;
	} else if (cmd == SND_PCM_TRIGGER_STOP) {
		tmp = ymfpci_readl(codec, YDSXGR_MAPOFREC) & ~(1 << ypcm->capture_bank_number);
		ymfpci_writel(codec, YDSXGR_MAPOFREC, tmp);
		ypcm->running = 0;
	} else {
		result = -EINVAL;
	}
	spin_unlock_irqrestore(&codec->reg_lock, flags);
	return result;
}
#endif

static int ymfpci_pcm_voice_alloc(ymfpci_pcm_t *ypcm, int voices)
{
	int err;

	if (ypcm->voices[1] != NULL && voices < 2) {
		ymfpci_voice_free(ypcm->codec, ypcm->voices[1]);
		ypcm->voices[1] = NULL;
	}
	if (voices == 1 && ypcm->voices[0] != NULL)
		return 0;		/* already allocated */
	if (voices == 2 && ypcm->voices[0] != NULL && ypcm->voices[1] != NULL)
		return 0;		/* already allocated */
	if (voices > 1) {
		if (ypcm->voices[0] != NULL && ypcm->voices[1] == NULL) {
			ymfpci_voice_free(ypcm->codec, ypcm->voices[0]);
			ypcm->voices[0] = NULL;
		}		
	}
	err = ymfpci_voice_alloc(ypcm->codec, YMFPCI_PCM, voices > 1, &ypcm->voices[0]);
	if (err < 0)
		return err;
	ypcm->voices[0]->ypcm = ypcm;
	ypcm->voices[0]->interrupt = ymf_pcm_interrupt;
	if (voices > 1) {
		ypcm->voices[1] = &ypcm->codec->voices[ypcm->voices[0]->number + 1];
		ypcm->voices[1]->ypcm = ypcm;
	}
	return 0;
}

static void ymf_pcm_init_voice(ymfpci_voice_t *voice, int stereo,
    int rate, int w_16, unsigned long addr, unsigned int end, int spdif)
{
	u32 format;
	u32 delta = ymfpci_calc_delta(rate);
	u32 lpfQ = ymfpci_calc_lpfQ(rate);
	u32 lpfK = ymfpci_calc_lpfK(rate);
	ymfpci_playback_bank_t *bank;
	int nbank;

	format = (stereo ? 0x00010000 : 0) | (w_16 ? 0 : 0x80000000);
	if (stereo)
		end >>= 1;
	if (w_16)
		end >>= 1;
/* P3 */ // printk("ymf_pcm_init_voice: %d: Rate %d Format 0x%08x Delta 0x%x End 0x%x\n",
//  voice->number, rate, format, delta, end);
	for (nbank = 0; nbank < 2; nbank++) {
		bank = &voice->bank[nbank];
		bank->format = format;
		bank->loop_default = 0;	/* 0-loops forever, otherwise count */
		bank->base = addr;
		bank->loop_start = 0;
		bank->loop_end = end;
		bank->loop_frac = 0;
		bank->eg_gain_end = 0x40000000;
		bank->lpfQ = lpfQ;
		bank->status = 0;
		bank->num_of_frames = 0;
		bank->loop_count = 0;
		bank->start = 0;
		bank->start_frac = 0;
		bank->delta =
		bank->delta_end = delta;
		bank->lpfK =
		bank->lpfK_end = lpfK;
		bank->eg_gain = 0x40000000;
		bank->lpfD1 =
		bank->lpfD2 = 0;

		bank->left_gain = 
		bank->right_gain =
		bank->left_gain_end =
		bank->right_gain_end =
		bank->eff1_gain =
		bank->eff2_gain =
		bank->eff3_gain =
		bank->eff1_gain_end =
		bank->eff2_gain_end =
		bank->eff3_gain_end = 0;

		if (!stereo) {
			if (!spdif) {
				bank->left_gain = 
				bank->right_gain =
				bank->left_gain_end =
				bank->right_gain_end = 0x40000000;
			} else {
				bank->eff2_gain =
				bank->eff2_gain_end =
				bank->eff3_gain =
				bank->eff3_gain_end = 0x40000000;
			}
		} else {
			if (!spdif) {
				if ((voice->number & 1) == 0) {
					bank->format |= 1;
					bank->left_gain =
					bank->left_gain_end = 0x40000000;
				} else {
					bank->right_gain =
					bank->right_gain_end = 0x40000000;
				}
			} else {
				if ((voice->number & 1) == 0) {
					bank->format |= 1;
					bank->eff2_gain =
					bank->eff2_gain_end = 0x40000000;
				} else {
					bank->eff3_gain =
					bank->eff3_gain_end = 0x40000000;
				}
			}
		}
	}
}

/*
 * XXX Use new cache coherent PCI DMA routines instead of virt_to_bus.
 */
static int ymf_playback_prepare(ymfpci_t *codec, struct ymf_state *state)
{
	ymfpci_pcm_t *ypcm = &state->ypcm;
	int err, nvoice;

	if ((err = ymfpci_pcm_voice_alloc(ypcm, state->format.voices)) < 0) {
		/* Cannot be unless we leak voices in ymf_release! */
		printk(KERN_ERR "ymfpci%d: cannot allocate voice!\n",
		    codec->dev_audio);
		return err;
	}

	for (nvoice = 0; nvoice < state->format.voices; nvoice++) {
		ymf_pcm_init_voice(ypcm->voices[nvoice],
		    state->format.voices == 2, state->format.rate,
		    ymf_pcm_format_width(state->format.format) == 16,
		    virt_to_bus(state->dmabuf.rawbuf), state->dmabuf.dmasize,
		    ypcm->spdif);
	}
	return 0;
}

#if 0  /* old */
static int ymfpci_capture_prepare(void *private_data,
				      snd_pcm_subchn_t * substream)
{
	ymfpci_t *codec = snd_magic_cast(ymfpci_t, private_data, -ENXIO);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, -ENXIO);
	ymfpci_capture_bank_t * bank;
	int nbank;
	u32 rate, format;

	ypcm->frag_size = snd_pcm_lib_transfer_fragment(substream);
	ypcm->buffer_size = snd_pcm_lib_transfer_size(substream);
	ypcm->frag_pos = 0;
	ypcm->last_pos = 0;
	ypcm->shift_offset = 0;
	rate = ((48000 * 4096) / runtime->format.rate) - 1;
	format = 0;
	if (runtime->format.voices == 2)
		format |= 2;
	if (snd_pcm_format_width(runtime->format.format) == 8)
		format |= 1;
	switch (ypcm->capture_bank_number) {
	case 0:
		ymfpci_writel(codec, YDSXGR_RECFORMAT, format);
		ymfpci_writel(codec, YDSXGR_RECSLOTSR, rate);
		break;
	case 1:
		ymfpci_writel(codec, YDSXGR_ADCFORMAT, format);
		ymfpci_writel(codec, YDSXGR_ADCSLOTSR, rate);
		break;
	}
	for (nbank = 0; nbank < 2; nbank++) {
		bank = codec->bank_capture[ypcm->capture_bank_number][nbank];
		bank->base = virt_to_bus(runtime->dma_area->buf);
		bank->loop_end = ypcm->buffer_size;
		bank->start = 0;
		bank->num_of_loops = 0;
	}
	if (runtime->digital.dig_valid)
		/*runtime->digital.type == SND_PCM_DIG_AES_IEC958*/
		ymfpci_writew(codec, YDSXGR_SPDIFOUTSTATUS, runtime->digital.dig_status[0] |
								(runtime->digital.dig_status[1] << 8));
	return 0;
}

static unsigned int ymfpci_playback_pointer(void *private_data,
						snd_pcm_subchn_t * substream)
{
	ymfpci_t *codec = snd_magic_cast(ymfpci_t, private_data, -ENXIO);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, -ENXIO);
	ymfpci_voice_t *voice = ypcm->voices[0];
	unsigned long flags;
	unsigned int result;

	spin_lock_irqsave(&codec->reg_lock, flags);
	if (ypcm->running && voice)
		result = voice->bank[codec->active_bank].start << ypcm->shift_offset;
	else
		result = 0;
	spin_unlock_irqrestore(&codec->reg_lock, flags);
	return result;
}

static unsigned int ymfpci_capture_pointer(void *private_data,
					       snd_pcm_subchn_t * substream)
{
	ymfpci_t *codec = snd_magic_cast(ymfpci_t, private_data, -ENXIO);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, -ENXIO);
	unsigned long flags;
	unsigned int result;

	spin_lock_irqsave(&codec->reg_lock, flags);
	if (ypcm->running)
		result = codec->bank_capture[ypcm->capture_bank_number][codec->active_bank]->start << ypcm->shift_offset;
	else
		result = 0;
	spin_unlock_irqrestore(&codec->reg_lock, flags);
	return result;
}
#endif /* old */

void ymf_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	ymfpci_t *codec = dev_id;
	u32 status, nvoice, mode;
	ymfpci_voice_t *voice;

	status = ymfpci_readl(codec, YDSXGR_STATUS);
	if (status & 0x80000000) {
		spin_lock(&codec->reg_lock);
		ymfpci_writel(codec, YDSXGR_STATUS, 0x80000000);
		mode = ymfpci_readl(codec, YDSXGR_MODE) | 2;
		ymfpci_writel(codec, YDSXGR_MODE, mode);
		spin_unlock(&codec->reg_lock);
		codec->active_bank = ymfpci_readl(codec, YDSXGR_CTRLSELECT) & 1;
		spin_lock(&codec->voice_lock);
		for (nvoice = 0; nvoice < 64; nvoice++) {
			voice = &codec->voices[nvoice];
			if (voice->interrupt)
				voice->interrupt(codec, voice);
		}
		spin_unlock(&codec->voice_lock);
	}

	status = ymfpci_readl(codec, YDSXGR_INTFLAG);
	if (status & 1) {
		/* timer handler */
		ymfpci_writel(codec, YDSXGR_INTFLAG, ~0);
	}
}

static void ymf_pcm_free_substream(ymfpci_pcm_t *ypcm)
{
	ymfpci_t *codec;

	if (ypcm) {
		codec = ypcm->codec;
		if (ypcm->voices[1])
			ymfpci_voice_free(codec, ypcm->voices[1]);
		if (ypcm->voices[0])
			ymfpci_voice_free(codec, ypcm->voices[0]);
	}
}

static int ymf_state_alloc(ymfpci_t *unit, int nvirt)
{
	ymfpci_pcm_t *ypcm;
	struct ymf_state *state;

	if ((state = kmalloc(sizeof(struct ymf_state), GFP_KERNEL)) == NULL) {
		goto out0;
	}
	memset(state, 0, sizeof(struct ymf_state));

	init_waitqueue_head(&state->dmabuf.wait);

	ypcm = &state->ypcm;
	ypcm->state = state;
	ypcm->codec = unit;
	ypcm->type = PLAYBACK_VOICE;

	state->unit = unit;
	state->virt = nvirt;

	state->format.format = AFMT_U8;
	state->format.rate = 8000;
	state->format.voices = 1;
	ymf_pcm_update_shift(&state->format);

	unit->states[nvirt] = state;
	return 0;

out0:
	return -ENOMEM;
}

#if HAVE_RECORD

static int ymfpci_capture_open(void *private_data,
				   snd_pcm_subchn_t * substream,
				   u32 capture_bank_number)
{
	ymfpci_t *codec = snd_magic_cast(ymfpci_t, private_data, -ENXIO);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm;
	int err;

	if ((err = snd_pcm_dma_alloc(substream, !capture_bank_number ? codec->dma2ptr : codec->dma3ptr, "YMFPCI - ADC")) < 0)
		return err;
	ypcm = snd_magic_kcalloc(ymfpci_pcm_t, 0, GFP_KERNEL);
	if (ypcm == NULL) {
		snd_pcm_dma_free(substream);
		return -ENOMEM;
	}
	ypcm->codec = codec;
	ypcm->type = capture_bank_number + CAPTURE_REC;
	ypcm->substream = substream;	
	ypcm->capture_bank_number = capture_bank_number;
	codec->capture_substream[capture_bank_number] = substream;
	runtime->hw = &ymfpci_capture;
	snd_pcm_set_mixer(substream, codec->mixer->device, codec->ac97->me_capture);
	runtime->private_data = ypcm;
	runtime->private_free = ymfpci_pcm_free_substream;
	ymfpci_hw_start(codec);
	return 0;
}

#endif  /* old */

/* AES/IEC958 channel status bits */
#define SND_PCM_AES0_PROFESSIONAL	(1<<0)	/* 0 = consumer, 1 = professional */
#define SND_PCM_AES0_NONAUDIO		(1<<1)	/* 0 = audio, 1 = non-audio */
#define SND_PCM_AES0_PRO_EMPHASIS	(7<<2)	/* mask - emphasis */
#define SND_PCM_AES0_PRO_EMPHASIS_NOTID	(0<<2)	/* emphasis not indicated */
#define SND_PCM_AES0_PRO_EMPHASIS_NONE	(1<<2)	/* none emphasis */
#define SND_PCM_AES0_PRO_EMPHASIS_5015	(3<<2)	/* 50/15us emphasis */
#define SND_PCM_AES0_PRO_EMPHASIS_CCITT	(7<<2)	/* CCITT J.17 emphasis */
#define SND_PCM_AES0_PRO_FREQ_UNLOCKED	(1<<5)	/* source sample frequency: 0 = locked, 1 = unlocked */
#define SND_PCM_AES0_PRO_FS		(3<<6)	/* mask - sample frequency */
#define SND_PCM_AES0_PRO_FS_NOTID	(0<<6)	/* fs not indicated */
#define SND_PCM_AES0_PRO_FS_44100	(1<<6)	/* 44.1kHz */
#define SND_PCM_AES0_PRO_FS_48000	(2<<6)	/* 48kHz */
#define SND_PCM_AES0_PRO_FS_32000	(3<<6)	/* 32kHz */
#define SND_PCM_AES0_CON_NOT_COPYRIGHT	(1<<2)	/* 0 = copyright, 1 = not copyright */
#define SND_PCM_AES0_CON_EMPHASIS	(7<<3)	/* mask - emphasis */
#define SND_PCM_AES0_CON_EMPHASIS_NONE	(0<<3)	/* none emphasis */
#define SND_PCM_AES0_CON_EMPHASIS_5015	(1<<3)	/* 50/15us emphasis */
#define SND_PCM_AES0_CON_MODE		(3<<6)	/* mask - mode */
#define SND_PCM_AES1_PRO_MODE		(15<<0)	/* mask - channel mode */
#define SND_PCM_AES1_PRO_MODE_NOTID	(0<<0)	/* not indicated */
#define SND_PCM_AES1_PRO_MODE_STEREOPHONIC (2<<0) /* stereophonic - ch A is left */
#define SND_PCM_AES1_PRO_MODE_SINGLE	(4<<0)	/* single channel */
#define SND_PCM_AES1_PRO_MODE_TWO	(8<<0)	/* two channels */
#define SND_PCM_AES1_PRO_MODE_PRIMARY	(12<<0)	/* primary/secondary */
#define SND_PCM_AES1_PRO_MODE_BYTE3	(15<<0)	/* vector to byte 3 */
#define SND_PCM_AES1_PRO_USERBITS	(15<<4)	/* mask - user bits */
#define SND_PCM_AES1_PRO_USERBITS_NOTID	(0<<4)	/* not indicated */
#define SND_PCM_AES1_PRO_USERBITS_192	(8<<4)	/* 192-bit structure */
#define SND_PCM_AES1_PRO_USERBITS_UDEF	(12<<4)	/* user defined application */
#define SND_PCM_AES1_CON_CATEGORY	0x7f
#define SND_PCM_AES1_CON_GENERAL	0x00
#define SND_PCM_AES1_CON_EXPERIMENTAL	0x40
#define SND_PCM_AES1_CON_SOLIDMEM_MASK	0x0f
#define SND_PCM_AES1_CON_SOLIDMEM_ID	0x08
#define SND_PCM_AES1_CON_BROADCAST1_MASK 0x07
#define SND_PCM_AES1_CON_BROADCAST1_ID	0x04
#define SND_PCM_AES1_CON_DIGDIGCONV_MASK 0x07
#define SND_PCM_AES1_CON_DIGDIGCONV_ID	0x02
#define SND_PCM_AES1_CON_ADC_COPYRIGHT_MASK 0x1f
#define SND_PCM_AES1_CON_ADC_COPYRIGHT_ID 0x06
#define SND_PCM_AES1_CON_ADC_MASK	0x1f
#define SND_PCM_AES1_CON_ADC_ID		0x16
#define SND_PCM_AES1_CON_BROADCAST2_MASK 0x0f
#define SND_PCM_AES1_CON_BROADCAST2_ID	0x0e
#define SND_PCM_AES1_CON_LASEROPT_MASK	0x07
#define SND_PCM_AES1_CON_LASEROPT_ID	0x01
#define SND_PCM_AES1_CON_MUSICAL_MASK	0x07
#define SND_PCM_AES1_CON_MUSICAL_ID	0x05
#define SND_PCM_AES1_CON_MAGNETIC_MASK	0x07
#define SND_PCM_AES1_CON_MAGNETIC_ID	0x03
#define SND_PCM_AES1_CON_IEC908_CD	(SND_PCM_AES1_CON_LASEROPT_ID|0x00)
#define SND_PCM_AES1_CON_NON_IEC908_CD	(SND_PCM_AES1_CON_LASEROPT_ID|0x08)
#define SND_PCM_AES1_CON_PCM_CODER	(SND_PCM_AES1_CON_DIGDIGCONV_ID|0x00)
#define SND_PCM_AES1_CON_SAMPLER	(SND_PCM_AES1_CON_DIGDIGCONV_ID|0x20)
#define SND_PCM_AES1_CON_MIXER		(SND_PCM_AES1_CON_DIGDIGCONV_ID|0x10)
#define SND_PCM_AES1_CON_RATE_CONVERTER	(SND_PCM_AES1_CON_DIGDIGCONV_ID|0x18)
#define SND_PCM_AES1_CON_SYNTHESIZER	(SND_PCM_AES1_CON_MUSICAL_ID|0x00)
#define SND_PCM_AES1_CON_MICROPHONE	(SND_PCM_AES1_CON_MUSICAL_ID|0x08)
#define SND_PCM_AES1_CON_DAT		(SND_PCM_AES1_CON_MAGNETIC_ID|0x00)
#define SND_PCM_AES1_CON_VCR		(SND_PCM_AES1_CON_MAGNETIC_ID|0x08)
#define SND_PCM_AES1_CON_ORIGINAL	(1<<7)	/* this bits depends on the category code */
#define SND_PCM_AES2_PRO_SBITS		(7<<0)	/* mask - sample bits */
#define SND_PCM_AES2_PRO_SBITS_20	(2<<0)	/* 20-bit - coordination */
#define SND_PCM_AES2_PRO_SBITS_24	(4<<0)	/* 24-bit - main audio */
#define SND_PCM_AES2_PRO_SBITS_UDEF	(6<<0)	/* user defined application */
#define SND_PCM_AES2_PRO_WORDLEN	(7<<3)	/* mask - source word length */
#define SND_PCM_AES2_PRO_WORDLEN_NOTID	(0<<3)	/* not indicated */
#define SND_PCM_AES2_PRO_WORDLEN_22_18	(2<<3)	/* 22-bit or 18-bit */
#define SND_PCM_AES2_PRO_WORDLEN_23_19	(4<<3)	/* 23-bit or 19-bit */
#define SND_PCM_AES2_PRO_WORDLEN_24_20	(5<<3)	/* 24-bit or 20-bit */
#define SND_PCM_AES2_PRO_WORDLEN_20_16	(6<<3)	/* 20-bit or 16-bit */
#define SND_PCM_AES2_CON_SOURCE		(15<<0)	/* mask - source number */
#define SND_PCM_AES2_CON_SOURCE_UNSPEC	(0<<0)	/* unspecified */
#define SND_PCM_AES2_CON_CHANNEL	(15<<4)	/* mask - channel number */
#define SND_PCM_AES2_CON_CHANNEL_UNSPEC	(0<<4)	/* unspecified */
#define SND_PCM_AES3_CON_FS		(15<<0)	/* mask - sample frequency */
#define SND_PCM_AES3_CON_FS_44100	(0<<0)	/* 44.1kHz */
#define SND_PCM_AES3_CON_FS_48000	(2<<0)	/* 48kHz */
#define SND_PCM_AES3_CON_FS_32000	(3<<0)	/* 32kHz */
#define SND_PCM_AES3_CON_CLOCK		(3<<4)	/* mask - clock accuracy */
#define SND_PCM_AES3_CON_CLOCK_1000PPM	(0<<4)	/* 1000 ppm */
#define SND_PCM_AES3_CON_CLOCK_50PPM	(1<<4)	/* 50 ppm */
#define SND_PCM_AES3_CON_CLOCK_VARIABLE	(2<<4)	/* variable pitch */

#if HAVE_RECORD  /* old */

static int ymfpci_capture_close(void *private_data,
				    snd_pcm_subchn_t * substream)
{
	ymfpci_t *codec = snd_magic_cast(ymfpci_t, private_data, -ENXIO);
	snd_pcm_runtime_t *runtime = substream->runtime;
	ymfpci_pcm_t *ypcm = snd_magic_cast(ymfpci_pcm_t, runtime->private_data, -ENXIO);

	if (ypcm != NULL) {
		codec->capture_substream[ypcm->capture_bank_number] = NULL;
		ymfpci_hw_stop(codec);
	}
	snd_pcm_dma_free(substream);
	return 0;
}
#endif

/*
 * User interface
 */

static loff_t ymf_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

/* in this loop, dmabuf.count signifies the amount of data that is waiting to be copied to
   the user's buffer.  it is filled by the dma machine and drained by this loop. */
static ssize_t ymf_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
#if HAVE_RECORD
	struct cs_state *state = (struct cs_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;

#ifdef DEBUG
	printk("cs461x: cs_read called, count = %d\n", count);
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
				printk(KERN_ERR "cs461x: recording schedule timeout, "
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
#else
	return -EINVAL;
#endif
}

static ssize_t ymf_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct ymf_state *state = (struct ymf_state *)file->private_data;
	struct ymf_dmabuf *dmabuf = &state->dmabuf;
	DECLARE_WAITQUEUE(waita, current);
	ssize_t ret;
	unsigned long flags;
	unsigned int swptr;
	int cnt;			/* This many to go in this revolution */
	int redzone;
	int delay;

/* P3 */ /* printk("ymf_write: count %d\n", count); */

	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (dmabuf->mapped)
		return -ENXIO;
	if (!dmabuf->ready && (ret = prog_dmabuf(state, 0)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	ret = 0;

	/*
	 * Alan's cs46xx works without a red zone - marvel of ingenuity.
	 * We are not so brilliant... Red zone does two things:
	 *  1. allows for safe start after a pause as we have no way
	 *     to know what the actual, relentlessly advancing, hwptr is.
	 *  2. makes computations in ymf_pcm_interrupt simpler.
	 */
	redzone = ymf_calc_lend(state->format.rate) << state->format.shift;
	redzone *= 3;	/* 2 redzone + 1 possible uncertainty reserve. */

	add_wait_queue(&dmabuf->wait, &waita);
	while (count > 0) {
		spin_lock_irqsave(&state->unit->reg_lock, flags);
		if (dmabuf->count < 0) {
			printk(KERN_ERR
			   "ymf_write: count %d, was legal in cs46xx\n",
			    dmabuf->count);
			dmabuf->count = 0;
		}
		if (dmabuf->count == 0) {
			swptr = dmabuf->hwptr;
			if (state->ypcm.running) {
				/*
				 * Add uncertainty reserve.
				 */
				cnt = ymf_calc_lend(state->format.rate);
				cnt <<= state->format.shift;
				if ((swptr += cnt) >= dmabuf->dmasize) {
					swptr -= dmabuf->dmasize;
				}
			}
			dmabuf->swptr = swptr;
		} else {
			/*
			 * XXX This is not right if dmabuf->count is small -
			 * about 2*x frame size or less. We cannot count on
			 * on appending and not causing an artefact.
			 * Should use a variation of the count==0 case above.
			 */
			swptr = dmabuf->swptr;
		}
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count + cnt > dmabuf->dmasize - redzone)
			cnt = (dmabuf->dmasize - redzone) - dmabuf->count;
		spin_unlock_irqrestore(&state->unit->reg_lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
/* P3 */ /* printk("ymf_write: full, count %d swptr %d\n",
  dmabuf->count, dmabuf->swptr); */
			/*
			 * buffer is full, start the dma machine and
			 * wait for data to be played
			 */
			spin_lock_irqsave(&state->unit->reg_lock, flags);
			if (!state->ypcm.running) {
				ymf_playback_trigger(state->unit, &state->ypcm, 1);
			}
			spin_unlock_irqrestore(&state->unit->reg_lock, flags);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret) ret = -EAGAIN;
				break;
			}
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			if (signal_pending(current)) {
				if (!ret) ret = -ERESTARTSYS;
				break;
			}
			continue;
		}
		if (copy_from_user(dmabuf->rawbuf + swptr, buffer, cnt)) {
			if (!ret) ret = -EFAULT;
			break;
		}

		if ((swptr += cnt) >= dmabuf->dmasize) {
			swptr -= dmabuf->dmasize;
		}

		spin_lock_irqsave(&state->unit->reg_lock, flags);
		dmabuf->swptr = swptr;
		dmabuf->count += cnt;

		/*
		 * Start here is a bad idea - may cause startup click
		 * in /bin/play when dmabuf is not full yet.
		 * However, some broken applications do not make
		 * any use of SNDCTL_DSP_SYNC (Doom is the worst).
		 * One frame is about 5.3ms, Doom write size is 46ms.
		 */
		delay = state->format.rate / 20;	/* 50ms */
		delay <<= state->format.shift;
		if (dmabuf->count >= delay && !state->ypcm.running) {
			ymf_playback_trigger(state->unit, &state->ypcm, 1);
		}

		spin_unlock_irqrestore(&state->unit->reg_lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dmabuf->wait, &waita);

/* P3 */ /* printk("ymf_write: dmabuf.count %d\n", dmabuf->count); */
	return ret;
}

static unsigned int ymf_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ymf_state *state = (struct ymf_state *)file->private_data;
	struct ymf_dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned int mask = 0;

	if (file->f_mode & (FMODE_WRITE | FMODE_READ))
		poll_wait(file, &dmabuf->wait, wait);

	spin_lock_irqsave(&state->unit->reg_lock, flags);
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
	spin_unlock_irqrestore(&state->unit->reg_lock, flags);

	return mask;
}

static int ymf_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ymf_state *state = (struct ymf_state *)file->private_data;
	struct ymf_dmabuf *dmabuf = &state->dmabuf;
	int ret;
	unsigned long size;
	

	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf(state, 0)) != 0)
			return ret;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf(state, 1)) != 0)
			return ret;
	} else 
		return -EINVAL;

	if (vma->vm_pgoff != 0)
		return -EINVAL;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << dmabuf->buforder))
		return -EINVAL;
	if (remap_page_range(vma->vm_start, virt_to_phys(dmabuf->rawbuf),
			     size, vma->vm_page_prot))
		return -EAGAIN;
	dmabuf->mapped = 1;

	return 0;
}

static int ymf_ioctl(struct inode *inode, struct file *file,
    unsigned int cmd, unsigned long arg)
{
	struct ymf_state *state = (struct ymf_state *)file->private_data;
	struct ymf_dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	int val;

	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			ymf_wait_dac(state);
			spin_lock_irqsave(&state->unit->reg_lock, flags);
			dmabuf->ready = 0;
			dmabuf->swptr = dmabuf->hwptr = 0;
			dmabuf->count = dmabuf->total_bytes = 0;
			spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		}
#if HAVE_RECORD
		if (file->f_mode & FMODE_READ) {
			stop_adc(state);
			synchronize_irq();
			dmabuf->ready = 0;
			dmabuf->swptr = dmabuf->hwptr = 0;
			dmabuf->count = dmabuf->total_bytes = 0;
		}
#endif
		return 0;

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE) {
			if (file->f_flags & O_NONBLOCK) {
				spin_lock_irqsave(&state->unit->reg_lock, flags);
				if (dmabuf->count != 0 && !state->ypcm.running) {
					ymf_start_dac(state);
				}
				spin_unlock_irqrestore(&state->unit->reg_lock, flags);
			} else {
				ymf_wait_dac(state);
			}
		}
		return 0;

	case SNDCTL_DSP_SPEED: /* set smaple rate */
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val >= 8000 && val <= 48000) {
			if (file->f_mode & FMODE_WRITE) {
				ymf_wait_dac(state);
			}
#if HAVE_RECORD
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
			}
#endif
			spin_lock_irqsave(&state->unit->reg_lock, flags);
			dmabuf->ready = 0;
			state->format.rate = val;
			ymf_pcm_update_shift(&state->format);
			spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		}
		return put_user(state->format.rate, (int *)arg);

	/*
	 * OSS manual does not mention SNDCTL_DSP_STEREO at all.
	 * All channels are mono and if you want stereo, you
	 * play into two channels with SNDCTL_DSP_CHANNELS.
	 * However, mpg123 uses it. I wonder, why Michael Hipp uses it.
	 */
	case SNDCTL_DSP_STEREO: /* set stereo or mono channel */
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (file->f_mode & FMODE_WRITE) {
			ymf_wait_dac(state); 
			spin_lock_irqsave(&state->unit->reg_lock, flags);
			dmabuf->ready = 0;
			state->format.voices = val ? 2 : 1;
			ymf_pcm_update_shift(&state->format);
			spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		}
#if HAVE_RECORD
		if (file->f_mode & FMODE_READ) {
			/* stop_adc(state); */
			dmabuf->ready = 0;
			if(val)
				dmabuf->fmt |= CS_FMT_STEREO;
			else
				dmabuf->fmt &= ~CS_FMT_STEREO;
		}
#endif
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
		return -EINVAL;

	case SNDCTL_DSP_GETFMTS: /* Returns a mask of supported sample format*/
		return put_user(AFMT_S16_LE|AFMT_U8, (int *)arg);

	case SNDCTL_DSP_SETFMT: /* Select sample format */
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val == AFMT_S16_LE || val == AFMT_U8) {
			if (file->f_mode & FMODE_WRITE) {
				ymf_wait_dac(state);
			}
#if HAVE_RECORD
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
			}
#endif
			spin_lock_irqsave(&state->unit->reg_lock, flags);
			dmabuf->ready = 0;
			state->format.format = val;
			ymf_pcm_update_shift(&state->format);
			spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		}
		return put_user(state->format.format, (int *)arg);

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, (int *)arg))
			return -EFAULT;
	/* P3 */ /* printk("ymfpci: ioctl SNDCTL_DSP_CHANNELS 0x%x\n", val); */
		if (val != 0) {
			if (file->f_mode & FMODE_WRITE) {
				ymf_wait_dac(state);
				if (val == 1 || val == 2) {
					spin_lock_irqsave(&state->unit->reg_lock, flags);
					dmabuf->ready = 0;
					state->format.voices = val;
					ymf_pcm_update_shift(&state->format);
					spin_unlock_irqrestore(&state->unit->reg_lock, flags);
				}
			}
#if HAVE_RECORD
			if (file->f_mode & FMODE_READ) {
				spin_lock_irqsave(&state->unit->reg_lock, flags);
				stop_adc(state);
				dmabuf->ready = 0;
				spin_unlock_irqrestore(&state->unit->reg_lock, flags);
			}
#endif
		}
		return put_user(state->format.voices, (int *)arg);

	case SNDCTL_DSP_POST:
		/*
		 * Quoting OSS PG:
		 *    The ioctl SNDCTL_DSP_POST is a lightweight version of
		 *    SNDCTL_DSP_SYNC. It just tells to the driver that there
		 *    is likely to be a pause in the output. This makes it
		 *    possible for the device to handle the pause more
		 *    intelligently. This ioctl doesn't block the application.
		 *
		 * The paragraph above is a clumsy way to say "flush ioctl".
		 * This ioctl is used by mpg123.
		 */
	/* P3 */ /* printk("ymfpci: ioctl SNDCTL_DSP_POST\n"); */
		spin_lock_irqsave(&state->unit->reg_lock, flags);
		if (dmabuf->count != 0 && !state->ypcm.running) {
			ymf_start_dac(state);
		}
		spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		return 0;

	case SNDCTL_DSP_SUBDIVIDE:
		if (dmabuf->subdivision)
			return -EINVAL;
		if (get_user(val, (int *)arg))
			return -EFAULT;
		if (val != 1 && val != 2)
			return -EINVAL;
		dmabuf->subdivision = val;
		return 0;

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, (int *)arg))
			return -EFAULT;
	/* P3: these frags are for Doom. Amasingly, it sets [2,2**11]. */
	/* P3 */ // printk("ymfpci: ioctl SNDCTL_DSP_SETFRAGMENT 0x%x\n", val);

		dmabuf->ossfragshift = val & 0xffff;
		dmabuf->ossmaxfrags = (val >> 16) & 0xffff;
		switch (dmabuf->ossmaxfrags) {
		case 1:
			dmabuf->ossfragshift = 12;
			return 0;
		default:
			/* Fragments must be 2K long */
			dmabuf->ossfragshift = 11;
			dmabuf->ossmaxfrags = 2;
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!dmabuf->ready && (val = prog_dmabuf(state, 0)) != 0)
			return val;
		spin_lock_irqsave(&state->unit->reg_lock, flags);
		/* cs_update_ptr(state); */  /* XXX Always up to date? */
		abinfo.fragsize = dmabuf->fragsize;
		abinfo.bytes = dmabuf->dmasize - dmabuf->count;
		abinfo.fragstotal = dmabuf->numfrag;
		abinfo.fragments = abinfo.bytes >> dmabuf->fragshift;
		spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

#if HAVE_RECORD
	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!dmabuf->ready && (val = prog_dmabuf(state, 1)) != 0)
			return val;
		spin_lock_irqsave(&state->card->lock, flags);
		cs_update_ptr(state);
		abinfo.fragsize = dmabuf->fragsize;
		abinfo.bytes = dmabuf->count;
		abinfo.fragstotal = dmabuf->numfrag;
		abinfo.fragments = abinfo.bytes >> dmabuf->fragshift;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
#endif

	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETCAPS:
		/* return put_user(DSP_CAP_REALTIME|DSP_CAP_TRIGGER|DSP_CAP_MMAP,
			    (int *)arg); */
		return put_user(0, (int *)arg);

#if 0 /* old */
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
				start_dac(state);  // sure?
			} else
				stop_dac(state);
		}
		return 0;

#endif

#if HAVE_RECORD
	case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		spin_lock_irqsave(&state->unit->reg_lock, flags);
		cs_update_ptr(state);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.blocks = dmabuf->count >> dmabuf->fragshift;
		cinfo.ptr = dmabuf->hwptr;
		if (dmabuf->mapped)
			dmabuf->count &= dmabuf->fragsize-1;
		spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));
#endif

	case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&state->unit->reg_lock, flags);
		/* cs_update_ptr(state); */  /* Always up to date */
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.blocks = dmabuf->count >> dmabuf->fragshift;
		cinfo.ptr = dmabuf->hwptr;
		if (dmabuf->mapped)
			dmabuf->count &= dmabuf->fragsize-1;
		spin_unlock_irqrestore(&state->unit->reg_lock, flags);
		return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

	case SNDCTL_DSP_SETDUPLEX:	/* XXX TODO */
		return -EINVAL;

#if 0 /* old */
	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&state->unit->reg_lock, flags);
		cs_update_ptr(state);
		val = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);
		return put_user(val, (int *)arg);
#endif

	case SOUND_PCM_READ_RATE:
		return put_user(state->format.rate, (int *)arg);

	case SOUND_PCM_READ_CHANNELS:
		return put_user(state->format.voices,	(int *)arg);

	case SOUND_PCM_READ_BITS:
		return put_user(AFMT_S16_LE, (int *)arg);

	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_WRITE_FILTER:
	case SOUND_PCM_READ_FILTER:
		return -ENOTTY;

	default:
		/*
		 * Some programs mix up audio devices and ioctls
		 * or perhaps they expect "universal" ioctls,
		 * for instance we get SNDCTL_TMR_CONTINUE here.
		 * XXX Is there sound_generic_ioctl() around?
		 */
	}
	return -ENOTTY;
}

static int ymf_open(struct inode *inode, struct file *file)
{
	struct list_head *list;
	ymfpci_t *unit;
	int minor;
	struct ymf_state *state;
	int nvirt;
	int err;

	/*
	 * This is how we do it currently: only one channel is used
	 * in every board, so that we could use several boards in one machine.
	 * We waste 63 out of 64 playback slots, but so what.
	 * OSS model is constructed for devices with single playback channel.
	 */
	minor = MINOR(inode->i_rdev);
	if ((minor & 0x0F) == 3) {	/* /dev/dspN */
		;
	} else {
		return -ENXIO;
	}
	nvirt = 0;			/* Such is the partitioning of minor */

	for (list = ymf_devs.next; list != &ymf_devs; list = list->next) {
		unit = list_entry(list, ymfpci_t, ymf_devs);
		if (((unit->dev_audio ^ minor) & ~0x0F) == 0)
			break;
	}
	if (list == &ymf_devs)
		return -ENODEV;

	down(&unit->open_sem);
	if (unit->states[nvirt] != NULL) {
		up(&unit->open_sem);
		return -EBUSY;
	}

	if ((err = ymf_state_alloc(unit, nvirt)) != 0) {
		up(&unit->open_sem);
		return err;
	}
	state = unit->states[nvirt];

	file->private_data = state;

	/*
	 * XXX This ymf_playback_prepare is totally unneeded here.
	 * The question is if we want to allow write to fail if
	 * prog_dmabuf fails... Say, no memory in DMA zone?
	 */
	if ((err = ymf_playback_prepare(unit, state)) != 0) {
		/* XXX This recovery is ugly as hell. */

		ymf_pcm_free_substream(&state->ypcm);

		unit->states[state->virt] = NULL;
		kfree(state);

		up(&unit->open_sem);
		return err;
	}

#if 0 /* test if interrupts work */
	ymfpci_writew(codec, YDSXGR_TIMERCOUNT, 0xfffe);	/* ~ 680ms */
	ymfpci_writeb(codec, YDSXGR_TIMERCTRL,
	    (YDSXGR_TIMERCTRL_TEN|YDSXGR_TIMERCTRL_TIEN));
#endif
	up(&unit->open_sem);
	/* XXX Is it correct to have MOD_INC_USE_COUNT outside of sem.? */

	MOD_INC_USE_COUNT;
	return 0;
}

static int ymf_release(struct inode *inode, struct file *file)
{
	struct ymf_state *state = (struct ymf_state *)file->private_data;
	ymfpci_t *codec = state->unit;

#if 0 /* test if interrupts work */
	ymfpci_writeb(codec, YDSXGR_TIMERCTRL, 0);
#endif

	if (state != codec->states[state->virt]) {
		printk(KERN_ERR "ymfpci%d.%d: state mismatch\n",
		    state->unit->dev_audio, state->virt);
		return -EIO;
	}

	down(&codec->open_sem);

	/*
	 * XXX Solve the case of O_NONBLOCK close - don't deallocate here.
	 * Deallocate when unloading the driver and we can wait.
	 */
	ymf_wait_dac(state);
	dealloc_dmabuf(state);
	ymf_pcm_free_substream(&state->ypcm);

	codec->states[state->virt] = NULL;
	kfree(state);

	up(&codec->open_sem);

	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * Mixer operations are based on cs46xx.
 */
static int ymf_open_mixdev(struct inode *inode, struct file *file)
{
	int i;
	int minor = MINOR(inode->i_rdev);
	struct list_head *list;
	ymfpci_t *unit;

	for (list = ymf_devs.next; list != &ymf_devs; list = list->next) {
		unit = list_entry(list, ymfpci_t, ymf_devs);
		for (i = 0; i < NR_AC97; i++) {
			if (unit->ac97_codec[i] != NULL &&
			    unit->ac97_codec[i]->dev_mixer == minor) {
				goto match;
			}
		}
	}
	return -ENODEV;

 match:
	file->private_data = unit->ac97_codec[i];

	MOD_INC_USE_COUNT;
	return 0;
}

static int ymf_ioctl_mixdev(struct inode *inode, struct file *file,
    unsigned int cmd, unsigned long arg)
{
	struct ac97_codec *codec = (struct ac97_codec *)file->private_data;

	return codec->mixer_ioctl(codec, cmd, arg);
}

static int ymf_release_mixdev(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static /*const*/ struct file_operations ymf_fops = {
	llseek:		ymf_llseek,
	read:		ymf_read,
	write:		ymf_write,
	poll:		ymf_poll,
	ioctl:		ymf_ioctl,
	mmap:		ymf_mmap,
	open:		ymf_open,
	release:	ymf_release,
};

static /*const*/ struct file_operations ymf_mixer_fops = {
	llseek:		ymf_llseek,
	ioctl:		ymf_ioctl_mixdev,
	open:		ymf_open_mixdev,
	release:	ymf_release_mixdev,
};

/*
 *  initialization routines
 */

static void ymfpci_aclink_reset(struct pci_dev * pci)
{
	u8 cmd;

	pci_read_config_byte(pci, PCIR_DSXGCTRL, &cmd);
	if (cmd & 0x03) {
		pci_write_config_byte(pci, PCIR_DSXGCTRL, cmd & 0xfc);
		pci_write_config_byte(pci, PCIR_DSXGCTRL, cmd | 0x03);
		pci_write_config_byte(pci, PCIR_DSXGCTRL, cmd & 0xfc);
	}
}

static void ymfpci_enable_dsp(ymfpci_t *codec)
{
	ymfpci_writel(codec, YDSXGR_CONFIG, 0x00000001);
}

static void ymfpci_disable_dsp(ymfpci_t *codec)
{
	u32 val;
	int timeout = 1000;

	val = ymfpci_readl(codec, YDSXGR_CONFIG);
	if (val)
		ymfpci_writel(codec, YDSXGR_CONFIG, 0x00000000);
	while (timeout-- > 0) {
		val = ymfpci_readl(codec, YDSXGR_STATUS);
		if ((val & 0x00000002) == 0)
			break;
	}
}

#include "ymfpci_image.h"

static void ymfpci_download_image(ymfpci_t *codec)
{
	int i, ver_1e;
	u16 ctrl;

	ymfpci_writel(codec, YDSXGR_NATIVEDACOUTVOL, 0x00000000);
	ymfpci_disable_dsp(codec);
	ymfpci_writel(codec, YDSXGR_MODE, 0x00010000);
	ymfpci_writel(codec, YDSXGR_MODE, 0x00000000);
	ymfpci_writel(codec, YDSXGR_MAPOFREC, 0x00000000);
	ymfpci_writel(codec, YDSXGR_MAPOFEFFECT, 0x00000000);
	ymfpci_writel(codec, YDSXGR_PLAYCTRLBASE, 0x00000000);
	ymfpci_writel(codec, YDSXGR_RECCTRLBASE, 0x00000000);
	ymfpci_writel(codec, YDSXGR_EFFCTRLBASE, 0x00000000);
	ctrl = ymfpci_readw(codec, YDSXGR_GLOBALCTRL);
	ymfpci_writew(codec, YDSXGR_GLOBALCTRL, ctrl & ~0x0007);

	/* setup DSP instruction code */
	for (i = 0; i < YDSXG_DSPLENGTH; i++)
		ymfpci_writel(codec, YDSXGR_DSPINSTRAM + i, DspInst[i >> 2]);

	switch (codec->pci->device) {
	case PCI_DEVICE_ID_YAMAHA_724F:
	case PCI_DEVICE_ID_YAMAHA_740C:
	case PCI_DEVICE_ID_YAMAHA_744:
	case PCI_DEVICE_ID_YAMAHA_754:
		ver_1e = 1;
		break;
	default:
		ver_1e = 0;
	}

	if (ver_1e) {
		/* setup control instruction code */
		for (i = 0; i < YDSXG_CTRLLENGTH; i++)
			ymfpci_writel(codec, YDSXGR_CTRLINSTRAM + i, CntrlInst1E[i >> 2]);
	} else {
		for (i = 0; i < YDSXG_CTRLLENGTH; i++)
			ymfpci_writel(codec, YDSXGR_CTRLINSTRAM + i, CntrlInst[i >> 2]);
	}

	ymfpci_enable_dsp(codec);
}

static int ymfpci_memalloc(ymfpci_t *codec)
{
	long size, playback_ctrl_size;
	int voice, bank;
	u8 *ptr;

	playback_ctrl_size = 4 + 4 * YDSXG_PLAYBACK_VOICES;
	codec->bank_size_playback = ymfpci_readl(codec, YDSXGR_PLAYCTRLSIZE) << 2;
	codec->bank_size_capture = ymfpci_readl(codec, YDSXGR_RECCTRLSIZE) << 2;
	codec->bank_size_effect = ymfpci_readl(codec, YDSXGR_EFFCTRLSIZE) << 2;
	codec->work_size = YDSXG_DEFAULT_WORK_SIZE;

	size = ((playback_ctrl_size + 0x00ff) & ~0x00ff) +
	    ((codec->bank_size_playback * 2 * YDSXG_PLAYBACK_VOICES + 0xff) & ~0xff) +
	    ((codec->bank_size_capture * 2 * YDSXG_CAPTURE_VOICES + 0xff) & ~0xff) +
	    ((codec->bank_size_effect * 2 * YDSXG_EFFECT_VOICES + 0xff) & ~0xff) +
	    codec->work_size;

	ptr = (u8 *)kmalloc(size + 0x00ff, GFP_KERNEL);
	if (ptr == NULL)
		return -ENOMEM;

	codec->work_ptr = ptr;
	ptr += 0x00ff;
	(long)ptr &= ~0x00ff;

	codec->bank_base_playback = ptr;
	codec->ctrl_playback = (u32 *)ptr;
	codec->ctrl_playback[0] = YDSXG_PLAYBACK_VOICES;
	ptr += (playback_ctrl_size + 0x00ff) & ~0x00ff;
	for (voice = 0; voice < YDSXG_PLAYBACK_VOICES; voice++) {
		for (bank = 0; bank < 2; bank++) {
			codec->bank_playback[voice][bank] = (ymfpci_playback_bank_t *)ptr;
			ptr += codec->bank_size_playback;
		}
		codec->voices[voice].number = voice;
		codec->voices[voice].bank = codec->bank_playback[voice][0];
	}
	ptr += (codec->bank_size_playback + 0x00ff) & ~0x00ff;
	codec->bank_base_capture = ptr;
	for (voice = 0; voice < YDSXG_CAPTURE_VOICES; voice++)
		for (bank = 0; bank < 2; bank++) {
			codec->bank_capture[voice][bank] = (ymfpci_capture_bank_t *)ptr;
			ptr += codec->bank_size_capture;
		}
	ptr += (codec->bank_size_capture + 0x00ff) & ~0x00ff;
	codec->bank_base_effect = ptr;
	for (voice = 0; voice < YDSXG_EFFECT_VOICES; voice++)
		for (bank = 0; bank < 2; bank++) {
			codec->bank_effect[voice][bank] = (ymfpci_effect_bank_t *)ptr;
			ptr += codec->bank_size_effect;
		}
	ptr += (codec->bank_size_effect + 0x00ff) & ~0x00ff;
	codec->work_base = ptr;

	ymfpci_writel(codec, YDSXGR_PLAYCTRLBASE, virt_to_bus(codec->bank_base_playback));
	ymfpci_writel(codec, YDSXGR_RECCTRLBASE, virt_to_bus(codec->bank_base_capture));
	ymfpci_writel(codec, YDSXGR_EFFCTRLBASE, virt_to_bus(codec->bank_base_effect));
	ymfpci_writel(codec, YDSXGR_WORKBASE, virt_to_bus(codec->work_base));
	ymfpci_writel(codec, YDSXGR_WORKSIZE, codec->work_size >> 2);

	/* S/PDIF output initialization */
	ymfpci_writew(codec, YDSXGR_SPDIFOUTCTRL, 0);
	ymfpci_writew(codec, YDSXGR_SPDIFOUTSTATUS,
		SND_PCM_AES0_CON_EMPHASIS_NONE |
		(SND_PCM_AES1_CON_ORIGINAL << 8) |
		(SND_PCM_AES1_CON_PCM_CODER << 8));

	/* S/PDIF input initialization */
	ymfpci_writew(codec, YDSXGR_SPDIFINCTRL, 0);

	/* move this volume setup to mixer */
	ymfpci_writel(codec, YDSXGR_NATIVEDACOUTVOL, 0x3fff3fff);
	ymfpci_writel(codec, YDSXGR_BUF441OUTVOL, 0x3fff3fff);
	ymfpci_writel(codec, YDSXGR_NATIVEADCINVOL, 0x3fff3fff);
	ymfpci_writel(codec, YDSXGR_NATIVEDACINVOL, 0x3fff3fff);

	return 0;
}

static void ymfpci_memfree(ymfpci_t *codec)
{
	ymfpci_writel(codec, YDSXGR_PLAYCTRLBASE, 0);
	ymfpci_writel(codec, YDSXGR_RECCTRLBASE, 0);
	ymfpci_writel(codec, YDSXGR_EFFCTRLBASE, 0);
	ymfpci_writel(codec, YDSXGR_WORKBASE, 0);
	ymfpci_writel(codec, YDSXGR_WORKSIZE, 0);
	kfree(codec->work_ptr);
}

static int ymf_ac97_init(ymfpci_t *card, int num_ac97)
{
	struct ac97_codec *codec;
	u16 eid;

	if ((codec = kmalloc(sizeof(struct ac97_codec), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	memset(codec, 0, sizeof(struct ac97_codec));

	/* initialize some basic codec information, other fields will be filled
	   in ac97_probe_codec */
	codec->private_data = card;
	codec->id = num_ac97;

	codec->codec_read = ymfpci_codec_read;
	codec->codec_write = ymfpci_codec_write;

	if (ac97_probe_codec(codec) == 0) {
		printk("ymfpci: ac97_probe_codec failed\n");
		goto out_kfree;
	}

	eid = ymfpci_codec_read(codec, AC97_EXTENDED_ID);
	if (eid==0xFFFFFF) {
		printk(KERN_WARNING "ymfpci: no codec attached ?\n");
		goto out_kfree;
	}

	card->ac97_features = eid;

	if ((codec->dev_mixer = register_sound_mixer(&ymf_mixer_fops, -1)) < 0) {
		printk(KERN_ERR "ymfpci: couldn't register mixer!\n");
		goto out_kfree;
	}

	card->ac97_codec[num_ac97] = codec;

	return 0;
 out_kfree:
	kfree(codec);
	return -ENODEV;
}

static int __devinit ymf_probe_one(struct pci_dev *pcidev, const struct pci_device_id *ent)
{
	u16 ctrl;
	ymfpci_t *codec;

	int err;

	if (pci_enable_device(pcidev) < 0) {
		printk(KERN_ERR "ymfpci: pci_enable_device failed\n");
		return -ENODEV;
	}

	if ((codec = kmalloc(sizeof(ymfpci_t), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "ymfpci: no core\n");
		return -ENOMEM;
	}
	memset(codec, 0, sizeof(*codec));

	spin_lock_init(&codec->reg_lock);
	spin_lock_init(&codec->voice_lock);
	init_MUTEX(&codec->open_sem);
	codec->pci = pcidev;

	pci_read_config_byte(pcidev, PCI_REVISION_ID, &codec->rev);
	codec->reg_area_virt = ioremap(pci_resource_start(pcidev, 0), 0x8000);

	printk(KERN_INFO "ymfpci: %s at 0x%lx IRQ %d\n",
	    (char *)ent->driver_data, pci_resource_start(pcidev, 0), pcidev->irq);

	ymfpci_aclink_reset(pcidev);
	if (ymfpci_codec_ready(codec, 0, 1) < 0)
		goto out_unmap;

	ymfpci_download_image(codec);

	udelay(100); /* seems we need some delay after downloading image.. */

	if (ymfpci_memalloc(codec) < 0)
		goto out_disable_dsp;

	/* ymfpci_proc_init(card, codec); */

	if (request_irq(pcidev->irq, ymf_interrupt, SA_SHIRQ, "ymfpci", codec) != 0) {
		printk(KERN_ERR "ymfpci%d: unable to request IRQ %d\n",
		       codec->dev_audio, pcidev->irq);
		goto out_memfree;
	}

	/* register /dev/dsp */
	if ((codec->dev_audio = register_sound_dsp(&ymf_fops, -1)) < 0) {
		printk(KERN_ERR "ymfpci%d: unable to register dsp\n",
		    codec->dev_audio);
		goto out_free_irq;
	}

	/*
	 * Poke just the primary for the moment.
	 */
	if ((err = ymf_ac97_init(codec, 0)) != 0)
		goto out_unregister_sound_dsp;

	/* put it into driver list */
	list_add_tail(&codec->ymf_devs, &ymf_devs);
	pci_set_drvdata(pcidev, codec);

	return 0;

 out_unregister_sound_dsp:
	unregister_sound_dsp(codec->dev_audio);
 out_free_irq:
	free_irq(pcidev->irq, codec);
 out_memfree:
	ymfpci_memfree(codec);
 out_disable_dsp:
	ymfpci_disable_dsp(codec);
	ctrl = ymfpci_readw(codec, YDSXGR_GLOBALCTRL);
	ymfpci_writew(codec, YDSXGR_GLOBALCTRL, ctrl & ~0x0007);
	ymfpci_writel(codec, YDSXGR_STATUS, ~0);
 out_unmap:
	iounmap(codec->reg_area_virt);
	kfree(codec);
	return -ENODEV;
}

static void __devexit ymf_remove_one(struct pci_dev *pcidev)
{
	__u16 ctrl;
	ymfpci_t *codec = pci_get_drvdata(pcidev);

	/* remove from list of devices */
	list_del(&codec->ymf_devs);

	unregister_sound_mixer(codec->ac97_codec[0]->dev_mixer);
	kfree(codec->ac97_codec[0]);
	unregister_sound_dsp(codec->dev_audio);
	free_irq(pcidev->irq, codec);
	ymfpci_memfree(codec);
	ymfpci_writel(codec, YDSXGR_STATUS, ~0);
	ymfpci_disable_dsp(codec);
	ctrl = ymfpci_readw(codec, YDSXGR_GLOBALCTRL);
	ymfpci_writew(codec, YDSXGR_GLOBALCTRL, ctrl & ~0x0007);
	iounmap(codec->reg_area_virt);
	kfree(codec);
}

MODULE_AUTHOR("Jaroslav Kysela");
MODULE_DESCRIPTION("Yamaha YMF7xx PCI Audio");

static struct pci_driver ymfpci_driver = {
	name:		"ymfpci",
	id_table:	ymf_id_tbl,
	probe:		ymf_probe_one,
	remove:         ymf_remove_one,
};

static int __init ymf_init_module(void)
{
	return pci_module_init(&ymfpci_driver);
}

static void __exit ymf_cleanup_module (void)
{
	pci_unregister_driver(&ymfpci_driver);
}

module_init(ymf_init_module);
module_exit(ymf_cleanup_module);
