/*
 *   US-428 AUDIO

 *   Copyright (c) 2002-2003 by Karsten Wiese
 
 *   based on

 *   (Tentative) USB Audio Driver for ALSA
 *
 *   Main and PCM part
 *
 *   Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 *
 *   Many codes borrowed from audio.c by 
 *	    Alan Cox (alan@lxorguk.ukuu.org.uk)
 *	    Thomas Sailer (sailer@ife.ee.ethz.ch)
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
 */


#include <sound/driver.h>
#include <linux/interrupt.h>
#include <linux/usb.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "usx2y.h"
#include "usbusx2y.h"


struct snd_usX2Y_substream {
	usX2Ydev_t	*usX2Y;
	snd_pcm_substream_t *pcm_substream;

	unsigned char		endpoint;		
	unsigned int		datapipe;  		/* the data i/o pipe */
	unsigned int		maxpacksize;		/* max packet size in bytes */

	char			prepared,
				running,
				stalled;

	int			hwptr;			/* free frame position in the buffer (only for playback) */
	int			hwptr_done;		/* processed frame position in the buffer */
	int			transfer_done;		/* processed frames since last period update */

	struct urb		*urb[NRURBS];	/* data urb table */
	int			next_urb_complete;
	struct urb		*completed_urb;
	char			*tmpbuf;			/* temporary buffer for playback */
	volatile int		submitted_urbs;
	wait_queue_head_t	wait_queue;
};






static int usX2Y_urb_capt_retire(snd_usX2Y_substream_t *subs)
{
	struct urb	*urb = subs->completed_urb;
	snd_pcm_runtime_t *runtime = subs->pcm_substream->runtime;
	unsigned char	*cp;
	int 		i, len, lens = 0, hwptr_done = subs->hwptr_done;
	usX2Ydev_t	*usX2Y = subs->usX2Y;

	for (i = 0; i < NRPACKS; i++) {
		cp = (unsigned char*)urb->transfer_buffer + urb->iso_frame_desc[i].offset;
		if (urb->iso_frame_desc[i].status) { /* active? hmm, skip this */
			snd_printdd("activ frame status %i\n", urb->iso_frame_desc[i].status);
			return urb->iso_frame_desc[i].status;
		}
		len = urb->iso_frame_desc[i].actual_length / usX2Y->stride;
		if (! len) {
			snd_printk("0 == len ERROR!\n");
			continue;
		}

		/* copy a data chunk */
		if ((hwptr_done + len) > runtime->buffer_size) {
			int cnt = runtime->buffer_size - hwptr_done;
			int blen = cnt * usX2Y->stride;
			memcpy(runtime->dma_area + hwptr_done * usX2Y->stride, cp, blen);
			memcpy(runtime->dma_area, cp + blen, len * usX2Y->stride - blen);
		} else {
			memcpy(runtime->dma_area + hwptr_done * usX2Y->stride, cp, len * usX2Y->stride);
		}
		lens += len;
		if ((hwptr_done += len) >= runtime->buffer_size)
			hwptr_done -= runtime->buffer_size;
	}

	subs->hwptr_done = hwptr_done;
	subs->transfer_done += lens;
	/* update the pointer, call callback if necessary */
	if (subs->transfer_done >= runtime->period_size) {
		subs->transfer_done -= runtime->period_size;
		snd_pcm_period_elapsed(subs->pcm_substream);
	}
	return 0;
}
/*
 * prepare urb for playback data pipe
 *
 * we copy the data directly from the pcm buffer.
 * the current position to be copied is held in hwptr field.
 * since a urb can handle only a single linear buffer, if the total
 * transferred area overflows the buffer boundary, we cannot send
 * it directly from the buffer.  thus the data is once copied to
 * a temporary buffer and urb points to that.
 */
static int usX2Y_urb_play_prepare(snd_usX2Y_substream_t *subs,
				  struct urb *cap_urb,
				  struct urb *urb)
{
	int count, counts, pack;
	usX2Ydev_t* usX2Y = subs->usX2Y;
	snd_pcm_runtime_t *runtime = subs->pcm_substream->runtime;

	count = 0;
	for (pack = 0; pack < NRPACKS; pack++) {
		/* calculate the size of a packet */
		counts = cap_urb->iso_frame_desc[pack].actual_length / usX2Y->stride;
		count += counts;
		if (counts < 43 || counts > 50) {
			snd_printk("should not be here with counts=%i\n", counts);
			return -EPIPE;
		}

		/* set up descriptor */
		urb->iso_frame_desc[pack].offset = pack ? urb->iso_frame_desc[pack - 1].offset + urb->iso_frame_desc[pack - 1].length : 0;
		urb->iso_frame_desc[pack].length = counts * usX2Y->stride;
	}
	if (subs->hwptr + count > runtime->buffer_size) {
		/* err, the transferred area goes over buffer boundary.
		 * copy the data to the temp buffer.
		 */
		int len;
		len = runtime->buffer_size - subs->hwptr;
		urb->transfer_buffer = subs->tmpbuf;
		memcpy(subs->tmpbuf, runtime->dma_area + subs->hwptr * usX2Y->stride, len * usX2Y->stride);
		memcpy(subs->tmpbuf + len * usX2Y->stride, runtime->dma_area, (count - len) * usX2Y->stride);
		subs->hwptr += count;
		subs->hwptr -= runtime->buffer_size;
	} else {
		/* set the buffer pointer */
		urb->transfer_buffer = runtime->dma_area + subs->hwptr * usX2Y->stride;
		if ((subs->hwptr += count) >= runtime->buffer_size)
			subs->hwptr -= runtime->buffer_size;			
	}
	urb->transfer_buffer_length = count * usX2Y->stride;
	return 0;
}

/*
 * process after playback data complete
 *
 * update the current position and call callback if a period is processed.
 */
inline static int usX2Y_urb_play_retire(snd_usX2Y_substream_t *subs, struct urb *urb)
{
	snd_pcm_runtime_t *runtime = subs->pcm_substream->runtime;
	int		len = (urb->iso_frame_desc[0].actual_length
#if NRPACKS > 1
			       + urb->iso_frame_desc[1].actual_length
#endif
		               ) / subs->usX2Y->stride;

	subs->transfer_done += len;
	subs->hwptr_done +=  len;
	if (subs->hwptr_done >= runtime->buffer_size)
		subs->hwptr_done -= runtime->buffer_size;
	if (subs->transfer_done >= runtime->period_size) {
		subs->transfer_done -= runtime->period_size;
		snd_pcm_period_elapsed(subs->pcm_substream);
	}
	return 0;
}

inline static int usX2Y_urb_submit(snd_usX2Y_substream_t *subs, struct urb *urb, int frame)
{
	int err;
	if (!urb)
		return -ENODEV;
	urb->start_frame = (frame + NRURBS*NRPACKS) & (1024 - 1);
	urb->hcpriv = NULL;
	urb->dev = subs->usX2Y->chip.dev; /* we need to set this at each time */
	if ((err = usb_submit_urb(urb, GFP_ATOMIC)) < 0) {
		snd_printk("%i\n", err);
		return err;
	} else {
		subs->submitted_urbs++;
		if (subs->next_urb_complete < 0) 
			subs->next_urb_complete = 0;
	}
	return 0;
}


static inline int frame_distance(int from, int to)
{
	int distance = to - from;
	if (distance < -512)
		distance += 1024;
	else
		if (distance > 511)
			distance -= 1024;
	return distance;
}


static void usX2Y_subs_set_next_urb_complete(snd_usX2Y_substream_t *subs)
{
	int next_urb_complete = subs->next_urb_complete + 1;
	int distance;
	if (next_urb_complete >= NRURBS)
		next_urb_complete = 0;
	distance = frame_distance(subs->completed_urb->start_frame,
				  subs->urb[next_urb_complete]->start_frame);
	if (1 == distance) {
		subs->next_urb_complete = next_urb_complete;
	} else {
		snd_printdd("distance %i not set_nuc %i %i %i \n", distance, subs->endpoint, next_urb_complete, subs->urb[next_urb_complete]->status);
		subs->next_urb_complete = -1;
	}
}


static inline void usX2Y_usbframe_complete(snd_usX2Y_substream_t *capsubs, snd_usX2Y_substream_t *playbacksubs, int frame)
{
	{
		struct urb *urb;
		if ((urb = playbacksubs->completed_urb)) {
			if (playbacksubs->prepared)
				usX2Y_urb_play_retire(playbacksubs, urb);
			usX2Y_subs_set_next_urb_complete(playbacksubs);
		}
		if (playbacksubs->running) {
			if (NULL == urb)
				urb = playbacksubs->urb[playbacksubs->next_urb_complete + 1];
			if (urb && 0 == usX2Y_urb_play_prepare(playbacksubs,
							       capsubs->completed_urb,
							       urb)) {
				if (usX2Y_urb_submit(playbacksubs, urb, frame) < 0)
					return;
			} else
				snd_pcm_stop(playbacksubs->pcm_substream, SNDRV_PCM_STATE_XRUN);
		}
		playbacksubs->completed_urb = NULL;
	}
	if (capsubs->running)
		usX2Y_urb_capt_retire(capsubs);
	usX2Y_subs_set_next_urb_complete(capsubs);
	if (capsubs->prepared)
		usX2Y_urb_submit(capsubs, capsubs->completed_urb, frame);
	capsubs->completed_urb = NULL;
}


static void usX2Y_clients_stop(snd_usX2Y_substream_t *subs)
{
	usX2Ydev_t *usX2Y = subs->usX2Y;
	int i;
	for (i = 0; i < 4; i++) {
		snd_usX2Y_substream_t *substream = usX2Y->substream[i];
		if (substream && substream->running)
			snd_pcm_stop(substream->pcm_substream, SNDRV_PCM_STATE_XRUN);
	}
}


static void i_usX2Y_urb_complete(struct urb *urb, struct pt_regs *regs)
{
	snd_usX2Y_substream_t *subs = (snd_usX2Y_substream_t*)urb->context;

	subs->submitted_urbs--;
	if (urb->status) {
		snd_printk("ep=%i stalled with status=%i\n", subs->endpoint, urb->status);
		subs->stalled = 1;
		usX2Y_clients_stop(subs);
		urb->status = 0;
		return;
	}
	if (urb == subs->urb[subs->next_urb_complete]) {
		subs->completed_urb = urb;
	} else {
		snd_printk("Sequence Error!(ep=%i;nuc=%i,frame=%i)\n",
			   subs->endpoint, subs->next_urb_complete, urb->start_frame);
		subs->stalled = 1;
		usX2Y_clients_stop(subs);
		return;
	}
	if (waitqueue_active(&subs->wait_queue))
		wake_up(&subs->wait_queue);
	{
		snd_usX2Y_substream_t *capsubs = subs->usX2Y->substream[SNDRV_PCM_STREAM_CAPTURE],
			*playbacksubs = subs->usX2Y->substream[SNDRV_PCM_STREAM_PLAYBACK];
		if (capsubs->completed_urb &&
		    (playbacksubs->completed_urb ||
		     !playbacksubs->prepared ||
		     (playbacksubs->prepared && (playbacksubs->next_urb_complete < 0 ||	// not started yet
						 frame_distance(capsubs->completed_urb->start_frame,
								playbacksubs->urb[playbacksubs->next_urb_complete]->start_frame)
						 > 0 ||					// other expected later
						 playbacksubs->stalled))))
			usX2Y_usbframe_complete(capsubs, playbacksubs, urb->start_frame);
	}
}


static int usX2Y_urbs_capt_start(snd_usX2Y_substream_t *subs)
{
	int i, err;

	for (i = 0; i < NRURBS; i++) {
		unsigned long pack;
		struct urb *urb = subs->urb[i];
		urb->dev = subs->usX2Y->chip.dev;
		urb->transfer_flags = URB_ISO_ASAP;
		for (pack = 0; pack < NRPACKS; pack++) {
			urb->iso_frame_desc[pack].offset = subs->maxpacksize * pack;
			urb->iso_frame_desc[pack].length = subs->maxpacksize;
		}
		urb->transfer_buffer_length = subs->maxpacksize * NRPACKS; 
		if ((err = usb_submit_urb(urb, GFP_ATOMIC)) < 0) {
			snd_printk (KERN_ERR "cannot submit datapipe for urb %d, err = %d\n", i, err);
			return -EPIPE;
		} else {
			subs->submitted_urbs++;
		}
		urb->transfer_flags = 0;
	}
	subs->stalled = 0;
	subs->next_urb_complete = 0;
	subs->prepared = 1;
	return 0;
}

/* 
 *  wait until all urbs are processed.
 */
static int usX2Y_urbs_wait_clear(snd_usX2Y_substream_t *subs)
{
	int timeout = HZ;

	do {
		if (0 == subs->submitted_urbs)
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		snd_printdd("snd_usX2Y_urbs_wait_clear waiting\n");
		schedule_timeout(1);
	} while (--timeout > 0);
	if (subs->submitted_urbs)
		snd_printk(KERN_ERR "timeout: still %d active urbs..\n", subs->submitted_urbs);
	return 0;
}
/*
 * return the current pcm pointer.  just return the hwptr_done value.
 */
static snd_pcm_uframes_t snd_usX2Y_pcm_pointer(snd_pcm_substream_t *substream)
{
	snd_usX2Y_substream_t *subs = (snd_usX2Y_substream_t *)substream->runtime->private_data;
	return subs->hwptr_done;
}
/*
 * start/stop substream
 */
static int snd_usX2Y_pcm_trigger(snd_pcm_substream_t *substream, int cmd)
{
	snd_usX2Y_substream_t *subs = (snd_usX2Y_substream_t *)substream->runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_printdd("snd_usX2Y_pcm_trigger(START)\n");
		if (subs->usX2Y->substream[SNDRV_PCM_STREAM_CAPTURE]->stalled)
			return -EPIPE;
		else
			subs->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_printdd("snd_usX2Y_pcm_trigger(STOP)\n");
		subs->running = 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}



static void usX2Y_urb_release(struct urb** urb, int free_tb)
{
	if (*urb) {
		if (free_tb)
			kfree((*urb)->transfer_buffer);
		usb_free_urb(*urb);
		*urb = NULL;
	}
}
/*
 * release a substream
 */
static void usX2Y_urbs_release(snd_usX2Y_substream_t *subs)
{
	int i;
	snd_printdd("snd_usX2Y_urbs_release() %i\n", subs->endpoint);
	usX2Y_urbs_wait_clear(subs);
	for (i = 0; i < NRURBS; i++)
		usX2Y_urb_release(subs->urb + i, subs != subs->usX2Y->substream[SNDRV_PCM_STREAM_PLAYBACK]);

	if (subs->tmpbuf) {
		kfree(subs->tmpbuf);
		subs->tmpbuf = NULL;
	}
}

static void usX2Y_substream_prepare(snd_usX2Y_substream_t *subs)
{
	snd_printdd("usX2Y_substream_prepare() ep=%i urb0=%p urb1=%p\n", subs->endpoint, subs->urb[0], subs->urb[1]);
	/* reset the pointer */
	subs->hwptr = 0;
	subs->hwptr_done = 0;
	subs->transfer_done = 0;
}


/*
 * initialize a substream's urbs
 */
static int usX2Y_urbs_allocate(snd_usX2Y_substream_t *subs)
{
	int i;
	int is_playback = subs == subs->usX2Y->substream[SNDRV_PCM_STREAM_PLAYBACK];
	struct usb_device *dev = subs->usX2Y->chip.dev;

	snd_assert(!subs->prepared, return 0);

	if (is_playback) {	/* allocate a temporary buffer for playback */
		subs->datapipe = usb_sndisocpipe(dev, subs->endpoint);
		subs->maxpacksize = dev->epmaxpacketout[subs->endpoint];
		if (NULL == subs->tmpbuf) {
			subs->tmpbuf = kcalloc(NRPACKS, subs->maxpacksize, GFP_KERNEL);
			if (NULL == subs->tmpbuf) {
				snd_printk(KERN_ERR "cannot malloc tmpbuf\n");
				return -ENOMEM;
			}
		}
	} else {
		subs->datapipe = usb_rcvisocpipe(dev, subs->endpoint);
		subs->maxpacksize = dev->epmaxpacketin[subs->endpoint];
	}

	/* allocate and initialize data urbs */
	for (i = 0; i < NRURBS; i++) {
		struct urb** purb = subs->urb + i;
		if (*purb)
			continue;
		*purb = usb_alloc_urb(NRPACKS, GFP_KERNEL);
		if (NULL == *purb) {
			usX2Y_urbs_release(subs);
			return -ENOMEM;
		}
		if (!is_playback && !(*purb)->transfer_buffer) {
			/* allocate a capture buffer per urb */
			(*purb)->transfer_buffer = kmalloc(subs->maxpacksize*NRPACKS, GFP_KERNEL);
			if (NULL == (*purb)->transfer_buffer) {
				usX2Y_urbs_release(subs);
				return -ENOMEM;
			}
		}
		(*purb)->dev = dev;
		(*purb)->pipe = subs->datapipe;
		(*purb)->number_of_packets = NRPACKS;
		(*purb)->context = subs;
		(*purb)->interval = 1;
		(*purb)->complete = snd_usb_complete_callback(i_usX2Y_urb_complete);
	}
	return 0;
}

static void i_usX2Y_04Int(struct urb* urb, struct pt_regs *regs)
{
	usX2Ydev_t*	usX2Y = urb->context;
	
	if (urb->status) {
		snd_printk("snd_usX2Y_04Int() urb->status=%i\n", urb->status);
		return;
	}
	if (0 == --usX2Y->US04->len)
		wake_up(&usX2Y->In04WaitQueue);
}
/*
 * allocate a buffer, setup samplerate
 *
 * so far we use a physically linear buffer although packetize transfer
 * doesn't need a continuous area.
 * if sg buffer is supported on the later version of alsa, we'll follow
 * that.
 */
static struct s_c2
{
	char c1, c2;
}
	SetRate44100[] =
{
	{ 0x14, 0x08},	// this line sets 44100, well actually a little less
	{ 0x18, 0x40},	// only tascam / frontier design knows the further lines .......
	{ 0x18, 0x42},
	{ 0x18, 0x45},
	{ 0x18, 0x46},
	{ 0x18, 0x48},
	{ 0x18, 0x4A},
	{ 0x18, 0x4C},
	{ 0x18, 0x4E},
	{ 0x18, 0x50},
	{ 0x18, 0x52},
	{ 0x18, 0x54},
	{ 0x18, 0x56},
	{ 0x18, 0x58},
	{ 0x18, 0x5A},
	{ 0x18, 0x5C},
	{ 0x18, 0x5E},
	{ 0x18, 0x60},
	{ 0x18, 0x62},
	{ 0x18, 0x64},
	{ 0x18, 0x66},
	{ 0x18, 0x68},
	{ 0x18, 0x6A},
	{ 0x18, 0x6C},
	{ 0x18, 0x6E},
	{ 0x18, 0x70},
	{ 0x18, 0x72},
	{ 0x18, 0x74},
	{ 0x18, 0x76},
	{ 0x18, 0x78},
	{ 0x18, 0x7A},
	{ 0x18, 0x7C},
	{ 0x18, 0x7E}
};
static struct s_c2 SetRate48000[] =
{
	{ 0x14, 0x09},	// this line sets 48000, well actually a little less
	{ 0x18, 0x40},	// only tascam / frontier design knows the further lines .......
	{ 0x18, 0x42},
	{ 0x18, 0x45},
	{ 0x18, 0x46},
	{ 0x18, 0x48},
	{ 0x18, 0x4A},
	{ 0x18, 0x4C},
	{ 0x18, 0x4E},
	{ 0x18, 0x50},
	{ 0x18, 0x52},
	{ 0x18, 0x54},
	{ 0x18, 0x56},
	{ 0x18, 0x58},
	{ 0x18, 0x5A},
	{ 0x18, 0x5C},
	{ 0x18, 0x5E},
	{ 0x18, 0x60},
	{ 0x18, 0x62},
	{ 0x18, 0x64},
	{ 0x18, 0x66},
	{ 0x18, 0x68},
	{ 0x18, 0x6A},
	{ 0x18, 0x6C},
	{ 0x18, 0x6E},
	{ 0x18, 0x70},
	{ 0x18, 0x73},
	{ 0x18, 0x74},
	{ 0x18, 0x76},
	{ 0x18, 0x78},
	{ 0x18, 0x7A},
	{ 0x18, 0x7C},
	{ 0x18, 0x7E}
};
#define NOOF_SETRATE_URBS ARRAY_SIZE(SetRate48000)

static int usX2Y_rate_set(usX2Ydev_t *usX2Y, int rate)
{
	int			err = 0, i;
	snd_usX2Y_urbSeq_t	*us = NULL;
	int			*usbdata = NULL;
	DECLARE_WAITQUEUE(wait, current);
	struct s_c2		*ra = rate == 48000 ? SetRate48000 : SetRate44100;

	if (usX2Y->rate != rate) {
		do {
			us = kmalloc(sizeof(*us) + sizeof(struct urb*) * NOOF_SETRATE_URBS, GFP_KERNEL);
			if (NULL == us) {
				err = -ENOMEM;
				break;
			}
			memset(us, 0, sizeof(*us) + sizeof(struct urb*) * NOOF_SETRATE_URBS); 
			usbdata = kmalloc(sizeof(int)*NOOF_SETRATE_URBS, GFP_KERNEL);
			if (NULL == usbdata) {
				err = -ENOMEM;
				break;
			}
			for (i = 0; i < NOOF_SETRATE_URBS; ++i) {
				if (NULL == (us->urb[i] = usb_alloc_urb(0, GFP_KERNEL))) {
					err = -ENOMEM;
					break;
				}
				((char*)(usbdata + i))[0] = ra[i].c1;
				((char*)(usbdata + i))[1] = ra[i].c2;
				usb_fill_bulk_urb(us->urb[i], usX2Y->chip.dev, usb_sndbulkpipe(usX2Y->chip.dev, 4),
						  usbdata + i, 2, i_usX2Y_04Int, usX2Y);
#ifdef OLD_USB
				us->urb[i]->transfer_flags = USB_QUEUE_BULK;
#endif
			}
			if (err)
				break;

			add_wait_queue(&usX2Y->In04WaitQueue, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
			us->submitted =	0;
			us->len =	NOOF_SETRATE_URBS;
			usX2Y->US04 =	us;
		
			do {
				signed long	timeout = schedule_timeout(HZ/2);
                	
				if (signal_pending(current)) {
					err = -ERESTARTSYS;
					break;
				}
				if (0 == timeout) {
					err = -ENODEV;
					break;
				}
				usX2Y->rate = rate;
				usX2Y->refframes = rate == 48000 ? 47 : 44;
			} while (0);
		
			remove_wait_queue(&usX2Y->In04WaitQueue, &wait);
		} while (0);

		if (us) {
			us->submitted =	2*NOOF_SETRATE_URBS;
			for (i = 0; i < NOOF_SETRATE_URBS; ++i) {
				usb_kill_urb(us->urb[i]);
				usb_free_urb(us->urb[i]);
			}
			usX2Y->US04 = NULL;
			kfree(usbdata);
			kfree(us);
		}
	}

	return err;
}


static int usX2Y_format_set(usX2Ydev_t *usX2Y, snd_pcm_format_t format)
{
	int alternate, err;
	struct list_head* p;
	if (format == SNDRV_PCM_FORMAT_S24_3LE) {
		alternate = 2;
		usX2Y->stride = 6;
	} else {
		alternate = 1;
		usX2Y->stride = 4;
	}
	list_for_each(p, &usX2Y->chip.midi_list) {
		snd_usbmidi_input_stop(p);
	}
	usb_kill_urb(usX2Y->In04urb);
	if ((err = usb_set_interface(usX2Y->chip.dev, 0, alternate))) {
		snd_printk("usb_set_interface error \n");
		return err;
	}
	usX2Y->In04urb->dev = usX2Y->chip.dev;
	err = usb_submit_urb(usX2Y->In04urb, GFP_KERNEL);
	list_for_each(p, &usX2Y->chip.midi_list) {
		snd_usbmidi_input_start(p);
	}
	usX2Y->format = format;
	usX2Y->rate = 0;
	return err;
}


static int snd_usX2Y_pcm_hw_params(snd_pcm_substream_t *substream,
				   snd_pcm_hw_params_t *hw_params)
{
	int			err = 0;
	unsigned int		rate = params_rate(hw_params);
	snd_pcm_format_t	format = params_format(hw_params);
	snd_printdd("snd_usX2Y_hw_params(%p, %p)\n", substream, hw_params);

	{	// all pcm substreams off one usX2Y have to operate at the same rate & format
		snd_card_t *card = substream->pstr->pcm->card;
		struct list_head *list;
		list_for_each(list, &card->devices) {
			snd_device_t *dev;
			snd_pcm_t *pcm;
			int s;
			dev = snd_device(list);
			if (dev->type != SNDRV_DEV_PCM)
				continue;
			pcm = dev->device_data;
			for (s = 0; s < 2; ++s) {
				snd_pcm_substream_t *test_substream;
				test_substream = pcm->streams[s].substream;
				if (test_substream && test_substream != substream  &&
				    test_substream->runtime &&
				    ((test_substream->runtime->format &&
				      test_substream->runtime->format != format) ||
				     (test_substream->runtime->rate &&
				      test_substream->runtime->rate != rate)))
					return -EINVAL;
			}
		}
	}
	if (0 > (err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params)))) {
		snd_printk("snd_pcm_lib_malloc_pages(%p, %i) returned %i\n", substream, params_buffer_bytes(hw_params), err);
		return err;
	}
	return 0;
}

/*
 * free the buffer
 */
static int snd_usX2Y_pcm_hw_free(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_usX2Y_substream_t *subs = (snd_usX2Y_substream_t *)runtime->private_data;
	snd_printdd("snd_usX2Y_hw_free(%p)\n", substream);

	if (SNDRV_PCM_STREAM_PLAYBACK == substream->stream) {
		snd_usX2Y_substream_t *cap_subs = subs->usX2Y->substream[SNDRV_PCM_STREAM_CAPTURE];
		subs->prepared = 0;
		usX2Y_urbs_release(subs);
		if (!cap_subs->pcm_substream ||
		    !cap_subs->pcm_substream->runtime ||
		    !cap_subs->pcm_substream->runtime->status ||
		    cap_subs->pcm_substream->runtime->status->state < SNDRV_PCM_STATE_PREPARED) {
			cap_subs->prepared = 0;
			usX2Y_urbs_release(cap_subs);
		}
	} else {
		snd_usX2Y_substream_t *playback_subs = subs->usX2Y->substream[SNDRV_PCM_STREAM_PLAYBACK];
		if (!playback_subs->prepared) {
			subs->prepared = 0;
			usX2Y_urbs_release(subs);
		}
	}

	return snd_pcm_lib_free_pages(substream);
}
/*
 * prepare callback
 *
 * set format and initialize urbs
 */
static int snd_usX2Y_pcm_prepare(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_usX2Y_substream_t *subs = (snd_usX2Y_substream_t *)runtime->private_data;
	snd_usX2Y_substream_t *capsubs = subs->usX2Y->substream[SNDRV_PCM_STREAM_CAPTURE];
	int err = 0;
	snd_printdd("snd_usX2Y_pcm_prepare(%p)\n", substream);

// Start hardware streams
// SyncStream first....
	if (! capsubs->prepared) {
		if (subs->usX2Y->format != runtime->format)
			if ((err = usX2Y_format_set(subs->usX2Y, runtime->format)) < 0)
				return err;
		if (subs->usX2Y->rate != runtime->rate)
			if ((err = usX2Y_rate_set(subs->usX2Y, runtime->rate)) < 0)
				return err;
		snd_printdd("starting capture pipe for playpipe\n");
		usX2Y_urbs_allocate(capsubs);
		capsubs->completed_urb = NULL;
		{
			DECLARE_WAITQUEUE(wait, current);
			add_wait_queue(&capsubs->wait_queue, &wait);
			if (0 <= (err = usX2Y_urbs_capt_start(capsubs))) {
				signed long timeout;
				set_current_state(TASK_INTERRUPTIBLE);
				timeout = schedule_timeout(HZ/4);
				if (signal_pending(current))
					err = -ERESTARTSYS;
				else {
					snd_printdd("%li\n", HZ/4 - timeout);
					if (0 == timeout)
						err = -EPIPE;
				}
			}
			remove_wait_queue(&capsubs->wait_queue, &wait);
			if (0 > err)
				return err;
		}
	}

	if (subs != capsubs) {
		int u;
		if (!subs->prepared) {
			if ((err = usX2Y_urbs_allocate(subs)) < 0)
				return err;
			subs->prepared = 1;
		}
		while (subs->submitted_urbs)
			for (u = 0; u < NRURBS; u++) {
				snd_printdd("%i\n", subs->urb[u]->status);
				while(subs->urb[u]->status  ||  NULL != subs->urb[u]->hcpriv) {
					signed long timeout;
					snd_printdd("ep=%i waiting for urb=%p status=%i hcpriv=%p\n",
						    subs->endpoint, subs->urb[u],
						    subs->urb[u]->status, subs->urb[u]->hcpriv);
					set_current_state(TASK_INTERRUPTIBLE);
					timeout = schedule_timeout(HZ/10);
					if (signal_pending(current)) {
						return -ERESTARTSYS;
					}
				}
			}
		subs->completed_urb = NULL;
		subs->next_urb_complete = -1;
		subs->stalled = 0;
	}

	usX2Y_substream_prepare(subs);
	return err;
}

static snd_pcm_hardware_t snd_usX2Y_2c =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =                 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_3LE,
	.rates =                   SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
	.rate_min =                44100,
	.rate_max =                48000,
	.channels_min =            2,
	.channels_max =            2,
	.buffer_bytes_max =	(2*128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =              0
};



static int snd_usX2Y_pcm_open(snd_pcm_substream_t *substream)
{
	snd_usX2Y_substream_t	*subs = ((snd_usX2Y_substream_t **)
					 snd_pcm_substream_chip(substream))[substream->stream];
	snd_pcm_runtime_t	*runtime = substream->runtime;

	runtime->hw = snd_usX2Y_2c;
	runtime->private_data = subs;
	subs->pcm_substream = substream;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 1000, 200000);
	return 0;
}



static int snd_usX2Y_pcm_close(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_usX2Y_substream_t *subs = (snd_usX2Y_substream_t *)runtime->private_data;
	int err = 0;

	subs->pcm_substream = NULL;

	return err;
}


static snd_pcm_ops_t snd_usX2Y_pcm_ops = 
{
	.open =		snd_usX2Y_pcm_open,
	.close =	snd_usX2Y_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_usX2Y_pcm_hw_params,
	.hw_free =	snd_usX2Y_pcm_hw_free,
	.prepare =	snd_usX2Y_pcm_prepare,
	.trigger =	snd_usX2Y_pcm_trigger,
	.pointer =	snd_usX2Y_pcm_pointer,
};


/*
 * free a usb stream instance
 */
static void usX2Y_audio_stream_free(snd_usX2Y_substream_t **usX2Y_substream)
{
	if (NULL != usX2Y_substream[SNDRV_PCM_STREAM_PLAYBACK]) {
		kfree(usX2Y_substream[SNDRV_PCM_STREAM_PLAYBACK]);
		usX2Y_substream[SNDRV_PCM_STREAM_PLAYBACK] = NULL;
	}
	kfree(usX2Y_substream[SNDRV_PCM_STREAM_CAPTURE]);
	usX2Y_substream[SNDRV_PCM_STREAM_CAPTURE] = NULL;
}

static void snd_usX2Y_pcm_private_free(snd_pcm_t *pcm)
{
	snd_usX2Y_substream_t **usX2Y_stream = pcm->private_data;
	if (usX2Y_stream) {
		snd_pcm_lib_preallocate_free_for_all(pcm);
		usX2Y_audio_stream_free(usX2Y_stream);
	}
}

static int usX2Y_audio_stream_new(snd_card_t *card, int playback_endpoint, int capture_endpoint)
{
	snd_pcm_t *pcm;
	int err, i;
	snd_usX2Y_substream_t **usX2Y_substream =
		usX2Y(card)->substream + 2 * usX2Y(card)->chip.pcm_devs;

	for (i = playback_endpoint ? SNDRV_PCM_STREAM_PLAYBACK : SNDRV_PCM_STREAM_CAPTURE;
	     i <= SNDRV_PCM_STREAM_CAPTURE; ++i) {
		usX2Y_substream[i] = kcalloc(1, sizeof(snd_usX2Y_substream_t), GFP_KERNEL);
		if (NULL == usX2Y_substream[i]) {
			snd_printk(KERN_ERR "cannot malloc\n");
			return -ENOMEM;
		}
		init_waitqueue_head(&usX2Y_substream[i]->wait_queue);
		usX2Y_substream[i]->usX2Y = usX2Y(card);
	}

	if (playback_endpoint)
		usX2Y_substream[SNDRV_PCM_STREAM_PLAYBACK]->endpoint = playback_endpoint;
	usX2Y_substream[SNDRV_PCM_STREAM_CAPTURE]->endpoint = capture_endpoint;

	err = snd_pcm_new(card, NAME_ALLCAPS" Audio", usX2Y(card)->chip.pcm_devs,
			  playback_endpoint ? 1 : 0, 1,
			  &pcm);
	if (err < 0) {
		usX2Y_audio_stream_free(usX2Y_substream);
		return err;
	}

	if (playback_endpoint)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_usX2Y_pcm_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_usX2Y_pcm_ops);

	pcm->private_data = usX2Y_substream;
	pcm->private_free = snd_usX2Y_pcm_private_free;
	pcm->info_flags = 0;

	sprintf(pcm->name, NAME_ALLCAPS" Audio #%d", usX2Y(card)->chip.pcm_devs);

	if ((playback_endpoint &&
	     0 > (err = snd_pcm_lib_preallocate_pages(pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream,
						     SNDRV_DMA_TYPE_CONTINUOUS,
						     snd_dma_continuous_data(GFP_KERNEL),
						     64*1024, 128*1024))) ||
	    0 > (err = snd_pcm_lib_preallocate_pages(pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream,
	    					     SNDRV_DMA_TYPE_CONTINUOUS,
	    					     snd_dma_continuous_data(GFP_KERNEL),
						     64*1024, 128*1024))) {
		snd_usX2Y_pcm_private_free(pcm);
		return err;
	}
	usX2Y(card)->chip.pcm_devs++;

	return 0;
}

/*
 * free the chip instance
 *
 * here we have to do not much, since pcm and controls are already freed
 *
 */
static int snd_usX2Y_device_dev_free(snd_device_t *device)
{
	return 0;
}


/*
 * create a chip instance and set its names.
 */
int usX2Y_audio_create(snd_card_t* card)
{
	int err = 0;
	static snd_device_ops_t ops = {
		.dev_free = snd_usX2Y_device_dev_free,
	};
	
	INIT_LIST_HEAD(&usX2Y(card)->chip.pcm_list);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, usX2Y(card), &ops)) < 0) {
//		snd_usX2Y_audio_free(usX2Y(card));
		return err;
	}

	if (0 > (err = usX2Y_audio_stream_new(card, 0xA, 0x8)))
		return err;
	if (usX2Y(card)->chip.dev->descriptor.idProduct == USB_ID_US428)
	     if (0 > (err = usX2Y_audio_stream_new(card, 0, 0xA)))
		     return err;
	if (usX2Y(card)->chip.dev->descriptor.idProduct != USB_ID_US122)
		err = usX2Y_rate_set(usX2Y(card), 44100);	// Lets us428 recognize output-volume settings, disturbs us122.
	return err;
}
