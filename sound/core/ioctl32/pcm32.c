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

#include <sound/driver.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/compat.h>
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
	struct sndrv_mask masks[SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK + 1]; /* this must be identical */
	struct sndrv_mask mres[5];	/* reserved masks */
	struct sndrv_interval32 intervals[SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1];
	struct sndrv_interval ires[9];	/* reserved intervals */
	u32 rmask;
	u32 cmask;
	u32 info;
	u32 msbits;
	u32 rate_num;
	u32 rate_den;
	u32 fifo_size;
	unsigned char reserved[64];
} __attribute__((packed));

#define numberof(array) ARRAY_SIZE(array)

#define CVT_sndrv_pcm_hw_params()\
{\
	unsigned int i;\
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
} __attribute__((packed));

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
} __attribute__((packed));

#define CVT_sndrv_pcm_channel_info()\
{\
	COPY(channel);\
	COPY(offset);\
	COPY(first);\
	COPY(step);\
}

struct sndrv_pcm_status32 {
	s32 state;
	struct compat_timespec trigger_tstamp;
	struct compat_timespec tstamp;
	u32 appl_ptr;
	u32 hw_ptr;
	s32 delay;
	u32 avail;
	u32 avail_max;
	u32 overrange;
	s32 suspended_state;
	unsigned char reserved[60];
} __attribute__((packed));

#define CVT_sndrv_pcm_status()\
{\
	COPY(state);\
	COPY(trigger_tstamp.tv_sec);\
	COPY(trigger_tstamp.tv_nsec);\
	COPY(tstamp.tv_sec);\
	COPY(tstamp.tv_nsec);\
	COPY(appl_ptr);\
	COPY(hw_ptr);\
	COPY(delay);\
	COPY(avail);\
	COPY(avail_max);\
	COPY(overrange);\
	COPY(suspended_state);\
}

DEFINE_ALSA_IOCTL(pcm_uframes_str);
DEFINE_ALSA_IOCTL(pcm_sframes_str);
DEFINE_ALSA_IOCTL(pcm_sw_params);
DEFINE_ALSA_IOCTL(pcm_channel_info);
DEFINE_ALSA_IOCTL(pcm_status);

/* recalcuate the boundary within 32bit */
static void recalculate_boundary(struct file *file)
{
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;

	/* FIXME: need to check whether fop->ioctl is sane */
	if (! (pcm_file = file->private_data))
		return;
	if (! (substream = pcm_file->substream))
		return;
	if (! (runtime = substream->runtime))
		return;
	runtime->boundary = runtime->buffer_size;
	while (runtime->boundary * 2 <= 0x7fffffffUL - runtime->buffer_size)
		runtime->boundary *= 2;
}

static inline int _snd_ioctl32_pcm_hw_params(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file, unsigned int native_ctl)
{
	struct sndrv_pcm_hw_params32 *data32;
	struct sndrv_pcm_hw_params *data;
	mm_segment_t oldseg;
	int err;

	data32 = kmalloc(sizeof(*data32), GFP_KERNEL);
	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (data32 == NULL || data == NULL) {
		err = -ENOMEM;
		goto __end;
	}
	if (copy_from_user(data32, (void __user *)arg, sizeof(*data32))) {
		err = -EFAULT;
		goto __end;
	}
	memset(data, 0, sizeof(*data));
	convert_from_32(pcm_hw_params, data, data32);
	oldseg = get_fs();
	set_fs(KERNEL_DS);
	err = file->f_op->ioctl(file->f_dentry->d_inode, file, native_ctl, (unsigned long)data);
	set_fs(oldseg);
	if (err < 0)
		goto __end;
	err = 0;
	convert_to_32(pcm_hw_params, data32, data);
	if (copy_to_user((void __user *)arg, data32, sizeof(*data32)))
		err = -EFAULT;
	else
		recalculate_boundary(file);
      __end:
      	if (data)
      		kfree(data);
      	if (data32)
      		kfree(data32);
	return err;
}


/*
 */
struct sndrv_xferi32 {
	s32 result;
	u32 buf;
	u32 frames;
} __attribute__((packed));

static inline int _snd_ioctl32_xferi(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file, unsigned int native_ctl)
{
	struct sndrv_xferi32 data32;
	struct sndrv_xferi data;
	mm_segment_t oldseg;
	int err;

	if (copy_from_user(&data32, (void __user *)arg, sizeof(data32)))
		return -EFAULT;
	memset(&data, 0, sizeof(data));
	data.result = data32.result;
	data.buf = compat_ptr(data32.buf);
	data.frames = data32.frames;
	oldseg = get_fs();
	set_fs(KERNEL_DS);
	err = file->f_op->ioctl(file->f_dentry->d_inode, file, native_ctl, (unsigned long)&data);
	set_fs(oldseg);
	if (err < 0)
		return err;
	/* copy the result */
	data32.result = data.result;
	if (copy_to_user((void __user *)arg, &data32, sizeof(data32)))
		return -EFAULT;
	return 0;
}


/* snd_xfern needs remapping of bufs */
struct sndrv_xfern32 {
	s32 result;
	u32 bufs;  /* this is void **; */
	u32 frames;
} __attribute__((packed));

/*
 * xfern ioctl nees to copy (up to) 128 pointers on stack.
 * although we may pass the copied pointers through f_op->ioctl, but the ioctl
 * handler there expands again the same 128 pointers on stack, so it is better
 * to handle the function (calling pcm_readv/writev) directly in this handler.
 */
static inline int _snd_ioctl32_xfern(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file, unsigned int native_ctl)
{
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *substream;
	struct sndrv_xfern32 data32;
	struct sndrv_xfern32 __user *srcptr = (void __user *)arg;
	void __user **bufs = NULL;
	int err = 0, ch, i;
	u32 __user *bufptr;
	mm_segment_t oldseg;

	/* FIXME: need to check whether fop->ioctl is sane */

	pcm_file = file->private_data;
	substream = pcm_file->substream;
	snd_assert(substream != NULL && substream->runtime, return -ENXIO);

	/* check validty of the command */
	switch (native_ctl) {
	case SNDRV_PCM_IOCTL_WRITEN_FRAMES:
		if (substream->stream  != SNDRV_PCM_STREAM_PLAYBACK)
			return -EINVAL;
		if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN)
			return -EBADFD;
		break;
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
	bufptr = compat_ptr(data32.bufs);
	bufs = kmalloc(sizeof(void *) * 128, GFP_KERNEL);
	if (bufs == NULL)
		return -ENOMEM;
	for (i = 0; i < ch; i++) {
		u32 ptr;
		if (get_user(ptr, bufptr))
			return -EFAULT;
		bufs[ch] = compat_ptr(ptr);
		bufptr++;
	}
	oldseg = get_fs();
	set_fs(KERNEL_DS);
	switch (native_ctl) {
	case SNDRV_PCM_IOCTL_WRITEN_FRAMES:
		err = snd_pcm_lib_writev(substream, bufs, data32.frames);
		break;
	case SNDRV_PCM_IOCTL_READN_FRAMES:
		err = snd_pcm_lib_readv(substream, bufs, data32.frames);
		break;
	}
	set_fs(oldseg);
	if (err >= 0) {
		if (put_user(err, &srcptr->result))
			err = -EFAULT;
	}
	kfree(bufs);
	return 0;
}


struct sndrv_pcm_hw_params_old32 {
	u32 flags;
	u32 masks[SNDRV_PCM_HW_PARAM_SUBFORMAT -
			   SNDRV_PCM_HW_PARAM_ACCESS + 1];
	struct sndrv_interval32 intervals[SNDRV_PCM_HW_PARAM_TICK_TIME -
					SNDRV_PCM_HW_PARAM_SAMPLE_BITS + 1];
	u32 rmask;
	u32 cmask;
	u32 info;
	u32 msbits;
	u32 rate_num;
	u32 rate_den;
	u32 fifo_size;
	unsigned char reserved[64];
} __attribute__((packed));

#define __OLD_TO_NEW_MASK(x) ((x&7)|((x&0x07fffff8)<<5))
#define __NEW_TO_OLD_MASK(x) ((x&7)|((x&0xffffff00)>>5))

static void snd_pcm_hw_convert_from_old_params(snd_pcm_hw_params_t *params, struct sndrv_pcm_hw_params_old32 *oparams)
{
	unsigned int i;

	memset(params, 0, sizeof(*params));
	params->flags = oparams->flags;
	for (i = 0; i < ARRAY_SIZE(oparams->masks); i++)
		params->masks[i].bits[0] = oparams->masks[i];
	memcpy(params->intervals, oparams->intervals, sizeof(oparams->intervals));
	params->rmask = __OLD_TO_NEW_MASK(oparams->rmask);
	params->cmask = __OLD_TO_NEW_MASK(oparams->cmask);
	params->info = oparams->info;
	params->msbits = oparams->msbits;
	params->rate_num = oparams->rate_num;
	params->rate_den = oparams->rate_den;
	params->fifo_size = oparams->fifo_size;
}

static void snd_pcm_hw_convert_to_old_params(struct sndrv_pcm_hw_params_old32 *oparams, snd_pcm_hw_params_t *params)
{
	unsigned int i;

	memset(oparams, 0, sizeof(*oparams));
	oparams->flags = params->flags;
	for (i = 0; i < ARRAY_SIZE(oparams->masks); i++)
		oparams->masks[i] = params->masks[i].bits[0];
	memcpy(oparams->intervals, params->intervals, sizeof(oparams->intervals));
	oparams->rmask = __NEW_TO_OLD_MASK(params->rmask);
	oparams->cmask = __NEW_TO_OLD_MASK(params->cmask);
	oparams->info = params->info;
	oparams->msbits = params->msbits;
	oparams->rate_num = params->rate_num;
	oparams->rate_den = params->rate_den;
	oparams->fifo_size = params->fifo_size;
}

static inline int _snd_ioctl32_pcm_hw_params_old(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file, unsigned int native_ctl)
{
	struct sndrv_pcm_hw_params_old32 *data32;
	struct sndrv_pcm_hw_params *data;
	mm_segment_t oldseg;
	int err;

	data32 = kcalloc(1, sizeof(*data32), GFP_KERNEL);
	data = kcalloc(1, sizeof(*data), GFP_KERNEL);
	if (data32 == NULL || data == NULL) {
		err = -ENOMEM;
		goto __end;
	}
	if (copy_from_user(data32, (void __user *)arg, sizeof(*data32))) {
		err = -EFAULT;
		goto __end;
	}
	snd_pcm_hw_convert_from_old_params(data, data32);
	oldseg = get_fs();
	set_fs(KERNEL_DS);
	err = file->f_op->ioctl(file->f_dentry->d_inode, file, native_ctl, (unsigned long)data);
	set_fs(oldseg);
	if (err < 0)
		goto __end;
	snd_pcm_hw_convert_to_old_params(data32, data);
	err = 0;
	if (copy_to_user((void __user *)arg, data32, sizeof(*data32)))
		err = -EFAULT;
	else
		recalculate_boundary(file);
      __end:
      	if (data)
      		kfree(data);
      	if (data32)
      		kfree(data32);
	return err;
}

struct sndrv_pcm_mmap_status32 {
	s32 state;
	s32 pad1;
	u32 hw_ptr;
	struct compat_timespec tstamp;
	s32 suspended_state;
} __attribute__((packed));

struct sndrv_pcm_mmap_control32 {
	u32 appl_ptr;
	u32 avail_min;
} __attribute__((packed));

struct sndrv_pcm_sync_ptr32 {
	u32 flags;
	union {
		struct sndrv_pcm_mmap_status32 status;
		unsigned char reserved[64];
	} s;
	union {
		struct sndrv_pcm_mmap_control32 control;
		unsigned char reserved[64];
	} c;
} __attribute__((packed));

#define CVT_sndrv_pcm_sync_ptr()\
{\
	COPY(flags);\
	COPY(s.status.state);\
	COPY(s.status.pad1);\
	COPY(s.status.hw_ptr);\
	COPY(s.status.tstamp.tv_sec);\
	COPY(s.status.tstamp.tv_nsec);\
	COPY(s.status.suspended_state);\
	COPY(c.control.appl_ptr);\
	COPY(c.control.avail_min);\
}

DEFINE_ALSA_IOCTL_BIG(pcm_sync_ptr);

/*
 */

DEFINE_ALSA_IOCTL_ENTRY(pcm_hw_refine, pcm_hw_params, SNDRV_PCM_IOCTL_HW_REFINE);
DEFINE_ALSA_IOCTL_ENTRY(pcm_hw_params, pcm_hw_params, SNDRV_PCM_IOCTL_HW_PARAMS);
DEFINE_ALSA_IOCTL_ENTRY(pcm_sw_params, pcm_sw_params, SNDRV_PCM_IOCTL_SW_PARAMS);
DEFINE_ALSA_IOCTL_ENTRY(pcm_hw_refine_old, pcm_hw_params_old, SNDRV_PCM_IOCTL_HW_REFINE);
DEFINE_ALSA_IOCTL_ENTRY(pcm_hw_params_old, pcm_hw_params_old, SNDRV_PCM_IOCTL_HW_PARAMS);
DEFINE_ALSA_IOCTL_ENTRY(pcm_status, pcm_status, SNDRV_PCM_IOCTL_STATUS);
DEFINE_ALSA_IOCTL_ENTRY(pcm_delay, pcm_sframes_str, SNDRV_PCM_IOCTL_DELAY);
DEFINE_ALSA_IOCTL_ENTRY(pcm_channel_info, pcm_channel_info, SNDRV_PCM_IOCTL_CHANNEL_INFO);
DEFINE_ALSA_IOCTL_ENTRY(pcm_rewind, pcm_uframes_str, SNDRV_PCM_IOCTL_REWIND);
DEFINE_ALSA_IOCTL_ENTRY(pcm_readi, xferi, SNDRV_PCM_IOCTL_READI_FRAMES);
DEFINE_ALSA_IOCTL_ENTRY(pcm_writei, xferi, SNDRV_PCM_IOCTL_WRITEI_FRAMES);
DEFINE_ALSA_IOCTL_ENTRY(pcm_readn, xfern, SNDRV_PCM_IOCTL_READN_FRAMES);
DEFINE_ALSA_IOCTL_ENTRY(pcm_writen, xfern, SNDRV_PCM_IOCTL_WRITEN_FRAMES);
DEFINE_ALSA_IOCTL_ENTRY(pcm_sync_ptr, pcm_sync_ptr, SNDRV_PCM_IOCTL_SYNC_PTR);


/*
 * When PCM is used on 32bit mode, we need to disable
 * mmap of PCM status/control records because of the size
 * incompatibility.
 * 
 * Since INFO ioctl is always called at first, we mark the
 * mmap-disabling in this ioctl wrapper.
 */
static int snd_pcm_info_ioctl32(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *filp)
{
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *substream;
	if (! filp->f_op || ! filp->f_op->ioctl)
		return -ENOTTY;
	pcm_file = filp->private_data;
	if (! pcm_file)
		return -ENOTTY;
	substream = pcm_file->substream;
	if (! substream)
		return -ENOTTY;
	substream->no_mmap_ctrl = 1;
	return filp->f_op->ioctl(filp->f_dentry->d_inode, filp, cmd, arg);
}

/*
 */
#define AP(x) snd_ioctl32_##x

enum {
	SNDRV_PCM_IOCTL_HW_REFINE32 = _IOWR('A', 0x10, struct sndrv_pcm_hw_params32),
	SNDRV_PCM_IOCTL_HW_PARAMS32 = _IOWR('A', 0x11, struct sndrv_pcm_hw_params32),
	SNDRV_PCM_IOCTL_SW_PARAMS32 = _IOWR('A', 0x13, struct sndrv_pcm_sw_params32),
	SNDRV_PCM_IOCTL_STATUS32 = _IOR('A', 0x20, struct sndrv_pcm_status32),
	SNDRV_PCM_IOCTL_DELAY32 = _IOR('A', 0x21, s32),
	SNDRV_PCM_IOCTL_CHANNEL_INFO32 = _IOR('A', 0x32, struct sndrv_pcm_channel_info32),
	SNDRV_PCM_IOCTL_REWIND32 = _IOW('A', 0x46, u32),
	SNDRV_PCM_IOCTL_WRITEI_FRAMES32 = _IOW('A', 0x50, struct sndrv_xferi32),
	SNDRV_PCM_IOCTL_READI_FRAMES32 = _IOR('A', 0x51, struct sndrv_xferi32),
	SNDRV_PCM_IOCTL_WRITEN_FRAMES32 = _IOW('A', 0x52, struct sndrv_xfern32),
	SNDRV_PCM_IOCTL_READN_FRAMES32 = _IOR('A', 0x53, struct sndrv_xfern32),
	SNDRV_PCM_IOCTL_HW_REFINE_OLD32 = _IOWR('A', 0x10, struct sndrv_pcm_hw_params_old32),
	SNDRV_PCM_IOCTL_HW_PARAMS_OLD32 = _IOWR('A', 0x11, struct sndrv_pcm_hw_params_old32),
	SNDRV_PCM_IOCTL_SYNC_PTR32 = _IOWR('A', 0x23, struct sndrv_pcm_sync_ptr32),

};

struct ioctl32_mapper pcm_mappers[] = {
	MAP_COMPAT(SNDRV_PCM_IOCTL_PVERSION),
	/* MAP_COMPAT(SNDRV_PCM_IOCTL_INFO), */
	{ SNDRV_PCM_IOCTL_INFO, snd_pcm_info_ioctl32 },
	MAP_COMPAT(SNDRV_PCM_IOCTL_TSTAMP),
	{ SNDRV_PCM_IOCTL_HW_REFINE32, AP(pcm_hw_refine) },
	{ SNDRV_PCM_IOCTL_HW_PARAMS32, AP(pcm_hw_params) },
	{ SNDRV_PCM_IOCTL_HW_REFINE_OLD32, AP(pcm_hw_refine_old) },
	{ SNDRV_PCM_IOCTL_HW_PARAMS_OLD32, AP(pcm_hw_params_old) },
	MAP_COMPAT(SNDRV_PCM_IOCTL_HW_FREE),
	{ SNDRV_PCM_IOCTL_SW_PARAMS32, AP(pcm_sw_params) },
	{ SNDRV_PCM_IOCTL_STATUS32, AP(pcm_status) },
	{ SNDRV_PCM_IOCTL_DELAY32, AP(pcm_delay) },
	MAP_COMPAT(SNDRV_PCM_IOCTL_HWSYNC),
	{ SNDRV_PCM_IOCTL_SYNC_PTR32, AP(pcm_sync_ptr) },
	{ SNDRV_PCM_IOCTL_CHANNEL_INFO32, AP(pcm_channel_info) },
	MAP_COMPAT(SNDRV_PCM_IOCTL_PREPARE),
	MAP_COMPAT(SNDRV_PCM_IOCTL_RESET),
	MAP_COMPAT(SNDRV_PCM_IOCTL_START),
	MAP_COMPAT(SNDRV_PCM_IOCTL_DROP),
	MAP_COMPAT(SNDRV_PCM_IOCTL_DRAIN),
	MAP_COMPAT(SNDRV_PCM_IOCTL_PAUSE),
	{ SNDRV_PCM_IOCTL_REWIND32, AP(pcm_rewind) },
	MAP_COMPAT(SNDRV_PCM_IOCTL_RESUME),
	MAP_COMPAT(SNDRV_PCM_IOCTL_XRUN),
	{ SNDRV_PCM_IOCTL_WRITEI_FRAMES32, AP(pcm_writei) },
	{ SNDRV_PCM_IOCTL_READI_FRAMES32, AP(pcm_readi) },
	{ SNDRV_PCM_IOCTL_WRITEN_FRAMES32, AP(pcm_writen) },
	{ SNDRV_PCM_IOCTL_READN_FRAMES32, AP(pcm_readn) },
	MAP_COMPAT(SNDRV_PCM_IOCTL_LINK),
	MAP_COMPAT(SNDRV_PCM_IOCTL_UNLINK),

	{ 0 },
};
