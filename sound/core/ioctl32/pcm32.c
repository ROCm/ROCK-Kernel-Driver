/*
 *   32bit -> 64bit ioctl wrapper for PCM API
 *   Copyright (c) by Takashi Iwai <tiwai@suse.de>
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

#define __NO_VERSION__
#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include "ioctl32.h"


/* wrapper for sndrv_pcm_[us]frames */
struct sndrv_pcm_sframes_str {
	sndrv_pcm_sframes_t val;
};
struct sndrv_pcm_sframes_str32 {
	s32 val;
};
struct sndrv_pcm_uframes_str {
	sndrv_pcm_uframes_t val;
};
struct sndrv_pcm_uframes_str32 {
	u32 val;
};

#define CVT_sndrv_pcm_sframes_str() { COPY(val); }
#define CVT_sndrv_pcm_uframes_str() { COPY(val); }


struct sndrv_interval32 {
	u32 min, max;
	unsigned int openmin:1,
		     openmax:1,
		     integer:1,
		     empty:1;
};

struct sndrv_pcm_hw_params32 {
	u32 flags;
	u32 masks[SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK + 1];
	struct sndrv_interval32 intervals[SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1];
	u32 rmask;
	u32 cmask;
	u32 info;
	u32 msbits;
	u32 rate_num;
	u32 rate_den;
	u32 fifo_size;
	unsigned char reserved[64];
};

#define numberof(array)  (sizeof(array)/sizeof(array[0]))

#define CVT_sndrv_pcm_hw_params()\
{\
	int i;\
	COPY(flags);\
	for (i = 0; i < numberof(dst->masks); i++)\
		COPY(masks[i]);\
	for (i = 0; i < numberof(dst->intervals); i++) {\
		COPY(intervals[i].min);\
		COPY(intervals[i].max);\
		COPY(intervals[i].openmin);\
		COPY(intervals[i].openmax);\
		COPY(intervals[i].integer);\
		COPY(intervals[i].empty);\
	}\
	COPY(rmask);\
	COPY(cmask);\
	COPY(info);\
	COPY(msbits);\
	COPY(rate_num);\
	COPY(rate_den);\
	COPY(fifo_size);\
}

struct sndrv_pcm_sw_params32 {
	s32 tstamp_mode;
	u32 period_step;
	u32 sleep_min;
	u32 avail_min;
	u32 xfer_align;
	u32 start_threshold;
	u32 stop_threshold;
	u32 silence_threshold;
	u32 silence_size;
	u32 boundary;
	unsigned char reserved[64];
};

#define CVT_sndrv_pcm_sw_params()\
{\
	COPY(tstamp_mode);\
	COPY(period_step);\
	COPY(sleep_min);\
	COPY(avail_min);\
	COPY(xfer_align);\
	COPY(start_threshold);\
	COPY(stop_threshold);\
	COPY(silence_threshold);\
	COPY(silence_size);\
	COPY(boundary);\
}

struct sndrv_pcm_channel_info32 {
	u32 channel;
	u32 offset;
	u32 first;
	u32 step;
};

#define CVT_sndrv_pcm_channel_info()\
{\
	COPY(channel);\
	COPY(offset);\
	COPY(first);\
	COPY(step);\
}

struct timeval32 {
	s32 tv_sec;
	s32 tv_usec;
};

struct sndrv_pcm_status32 {
	s32 state;
	struct timeval32 trigger_tstamp;
	struct timeval32 tstamp;
	u32 appl_ptr;
	u32 hw_ptr;
	s32 delay;
	u32 avail;
	u32 avail_max;
	u32 overrange;
	s32 suspended_state;
	unsigned char reserved[60];
};

#define CVT_sndrv_pcm_status()\
{\
	COPY(state);\
	COPY(trigger_tstamp.tv_sec);\
	COPY(trigger_tstamp.tv_usec);\
	COPY(tstamp.tv_sec);\
	COPY(tstamp.tv_usec);\
	COPY(appl_ptr);\
	COPY(hw_ptr);\
	COPY(delay);\
	COPY(avail);\
	COPY(avail_max);\
	COPY(overrange);\
	COPY(suspended_state);\
}

struct sndrv_xferi32 {
	s32 result;
	u32 buf;
	u32 frames;
};

#define CVT_sndrv_xferi()\
{\
	COPY(result);\
	CPTR(buf);\
	COPY(frames);\
}

DEFINE_ALSA_IOCTL(pcm_uframes_str);
DEFINE_ALSA_IOCTL(pcm_sframes_str);
DEFINE_ALSA_IOCTL(pcm_hw_params);
DEFINE_ALSA_IOCTL(pcm_sw_params);
DEFINE_ALSA_IOCTL(pcm_channel_info);
DEFINE_ALSA_IOCTL(pcm_status);
DEFINE_ALSA_IOCTL(xferi);

/* snd_xfern needs remapping of bufs */
struct sndrv_xfern32 {
	s32 result;
	u32 bufs;  /* this is void **; */
	u32 frames;
};

/*
 * xfern ioctl nees to copy (up to) 128 pointers on stack.
 * although we may pass the copied pointers through f_op->ioctl, but the ioctl
 * handler there expands again the same 128 pointers on stack, so it is better
 * to handle the function (calling pcm_readv/writev) directly in this handler.
 */
static int snd_ioctl32_xfern(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file)
{
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *substream;
	struct sndrv_xfern32 data32, *srcptr = (struct sndrv_xfern32*)arg;
	void *bufs[128];
	int err = 0, ch, i;
	u32 *bufptr;

	/* FIXME: need to check whether fop->ioctl is sane */

	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, return -ENXIO);
	substream = pcm_file->substream;
	snd_assert(substream != NULL && substream->runtime, return -ENXIO);

	/* check validty of the command */
	switch (cmd) {
	case SNDRV_PCM_IOCTL_WRITEN_FRAMES:
		if (substream->stream  != SNDRV_PCM_STREAM_PLAYBACK)
			return -EINVAL;
		if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN)
			return -EBADFD;
	case SNDRV_PCM_IOCTL_READN_FRAMES:
		if (substream->stream  != SNDRV_PCM_STREAM_CAPTURE)
			return -EINVAL;
		break;
	}
	if ((ch = substream->runtime->channels) > 128)
		return -EINVAL;
	if (get_user(data32.frames, &srcptr->frames))
		return -EFAULT;
	__get_user(data32.bufs, &srcptr->bufs);
	bufptr = (u32*)TO_PTR(data32.bufs);
	for (i = 0; i < ch; i++) {
		u32 ptr;
		if (get_user(ptr, bufptr))
			return -EFAULT;
		bufs[ch] = (void*)TO_PTR(ptr);
		bufptr++;
	}
	switch (cmd) {
	case SNDRV_PCM_IOCTL_WRITEN_FRAMES:
		err = snd_pcm_lib_writev(substream, bufs, data32.frames);
		break;
	case SNDRV_PCM_IOCTL_READN_FRAMES:
		err = snd_pcm_lib_readv(substream, bufs, data32.frames);
		break;
	}
	
	if (err < 0)
		return err;
	if (put_user(err, &srcptr->result))
		return -EFAULT;
	return err < 0 ? err : 0;
}


#define AP(x) snd_ioctl32_##x

struct ioctl32_mapper pcm_mappers[] = {
	{ SNDRV_PCM_IOCTL_PVERSION, NULL },
	{ SNDRV_PCM_IOCTL_INFO, NULL },
	{ SNDRV_PCM_IOCTL_HW_REFINE, AP(pcm_hw_params) },
	{ SNDRV_PCM_IOCTL_HW_PARAMS, AP(pcm_hw_params) },
	{ SNDRV_PCM_IOCTL_HW_FREE, NULL },
	{ SNDRV_PCM_IOCTL_SW_PARAMS, AP(pcm_sw_params) },
	{ SNDRV_PCM_IOCTL_STATUS, AP(pcm_status) },
	{ SNDRV_PCM_IOCTL_DELAY, AP(pcm_sframes_str) },
	{ SNDRV_PCM_IOCTL_CHANNEL_INFO, AP(pcm_channel_info) },
	{ SNDRV_PCM_IOCTL_PREPARE, NULL },
	{ SNDRV_PCM_IOCTL_RESET, NULL },
	{ SNDRV_PCM_IOCTL_START, NULL },
	{ SNDRV_PCM_IOCTL_DROP, NULL },
	{ SNDRV_PCM_IOCTL_DRAIN, NULL },
	{ SNDRV_PCM_IOCTL_PAUSE, NULL },
	{ SNDRV_PCM_IOCTL_REWIND, AP(pcm_uframes_str) },
	{ SNDRV_PCM_IOCTL_RESUME, NULL },
	{ SNDRV_PCM_IOCTL_XRUN, NULL },
	{ SNDRV_PCM_IOCTL_WRITEI_FRAMES, AP(xferi) },
	{ SNDRV_PCM_IOCTL_READI_FRAMES, AP(xferi) },
	{ SNDRV_PCM_IOCTL_WRITEN_FRAMES, AP(xfern) },
	{ SNDRV_PCM_IOCTL_READN_FRAMES, AP(xfern) },
	{ SNDRV_PCM_IOCTL_LINK, NULL },
	{ SNDRV_PCM_IOCTL_UNLINK, NULL },

	{ SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE, NULL },
	{ SNDRV_CTL_IOCTL_PCM_INFO, NULL },
	{ SNDRV_CTL_IOCTL_PCM_PREFER_SUBDEVICE, NULL },

	{ 0 },
};
