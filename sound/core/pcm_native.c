/*
 *  Digital Audio (PCM) abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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
#include <linux/version.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/uio.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/timer.h>
#include <sound/minors.h>

/*
 *  Compatibility
 */

struct sndrv_pcm_hw_params_old {
	unsigned int flags;
	unsigned int masks[SNDRV_PCM_HW_PARAM_SUBFORMAT -
			   SNDRV_PCM_HW_PARAM_ACCESS + 1];
	struct sndrv_interval intervals[SNDRV_PCM_HW_PARAM_TICK_TIME -
					SNDRV_PCM_HW_PARAM_SAMPLE_BITS + 1];
	unsigned int rmask;
	unsigned int cmask;
	unsigned int info;
	unsigned int msbits;
	unsigned int rate_num;
	unsigned int rate_den;
	sndrv_pcm_uframes_t fifo_size;
	unsigned char reserved[64];
};

#define SNDRV_PCM_IOCTL_HW_REFINE_OLD _IOWR('A', 0x10, struct sndrv_pcm_hw_params_old)
#define SNDRV_PCM_IOCTL_HW_PARAMS_OLD _IOWR('A', 0x11, struct sndrv_pcm_hw_params_old)

static int snd_pcm_hw_refine_old_user(snd_pcm_substream_t * substream, struct sndrv_pcm_hw_params_old * _oparams);
static int snd_pcm_hw_params_old_user(snd_pcm_substream_t * substream, struct sndrv_pcm_hw_params_old * _oparams);

/*
 *
 */

static rwlock_t pcm_link_lock = RW_LOCK_UNLOCKED;

static inline mm_segment_t snd_enter_user(void)
{
	mm_segment_t fs = get_fs();
	set_fs(get_ds());
	return fs;
}

static inline void snd_leave_user(mm_segment_t fs)
{
	set_fs(fs);
}



int snd_pcm_info(snd_pcm_substream_t * substream, snd_pcm_info_t *info)
{
	snd_pcm_runtime_t * runtime;
	snd_pcm_t *pcm = substream->pcm;
	snd_pcm_str_t *pstr = substream->pstr;

	snd_assert(substream != NULL, return -ENXIO);
	memset(info, 0, sizeof(*info));
	info->card = pcm->card->number;
	info->device = pcm->device;
	info->stream = substream->stream;
	info->subdevice = substream->number;
	strncpy(info->id, pcm->id, sizeof(info->id)-1);
	strncpy(info->name, pcm->name, sizeof(info->name)-1);
	info->dev_class = pcm->dev_class;
	info->dev_subclass = pcm->dev_subclass;
	info->subdevices_count = pstr->substream_count;
	info->subdevices_avail = pstr->substream_count - pstr->substream_opened;
	strncpy(info->subname, substream->name, sizeof(info->subname)-1);
	runtime = substream->runtime;
	/* AB: FIXME!!! This is definitely nonsense */
	if (runtime) {
		info->sync = runtime->sync;
		substream->ops->ioctl(substream, SNDRV_PCM_IOCTL1_INFO, info);
	}
	return 0;
}

int snd_pcm_info_user(snd_pcm_substream_t * substream, snd_pcm_info_t * _info)
{
	snd_pcm_info_t info;
	int err = snd_pcm_info(substream, &info);
	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return err;
}

#undef RULES_DEBUG

#ifdef RULES_DEBUG
#define HW_PARAM(v) [SNDRV_PCM_HW_PARAM_##v] = #v
char *snd_pcm_hw_param_names[] = {
	HW_PARAM(ACCESS),
	HW_PARAM(FORMAT),
	HW_PARAM(SUBFORMAT),
	HW_PARAM(SAMPLE_BITS),
	HW_PARAM(FRAME_BITS),
	HW_PARAM(CHANNELS),
	HW_PARAM(RATE),
	HW_PARAM(PERIOD_TIME),
	HW_PARAM(PERIOD_SIZE),
	HW_PARAM(PERIOD_BYTES),
	HW_PARAM(PERIODS),
	HW_PARAM(BUFFER_TIME),
	HW_PARAM(BUFFER_SIZE),
	HW_PARAM(BUFFER_BYTES),
	HW_PARAM(TICK_TIME),
};
#endif

int snd_pcm_hw_refine(snd_pcm_substream_t *substream, 
		      snd_pcm_hw_params_t *params)
{
	unsigned int k;
	snd_pcm_hardware_t *hw;
	snd_interval_t *i = NULL;
	snd_mask_t *m = NULL;
	snd_pcm_hw_constraints_t *constrs = &substream->runtime->hw_constraints;
	unsigned int rstamps[constrs->rules_num];
	unsigned int vstamps[SNDRV_PCM_HW_PARAM_LAST_INTERVAL + 1];
	unsigned int stamp = 2;
	int changed, again;

	params->info = 0;
	params->fifo_size = 0;
	if (params->rmask & (1 << SNDRV_PCM_HW_PARAM_SAMPLE_BITS))
		params->msbits = 0;
	if (params->rmask & (1 << SNDRV_PCM_HW_PARAM_RATE)) {
		params->rate_num = 0;
		params->rate_den = 0;
	}

	for (k = SNDRV_PCM_HW_PARAM_FIRST_MASK; k <= SNDRV_PCM_HW_PARAM_LAST_MASK; k++) {
		m = hw_param_mask(params, k);
		if (snd_mask_empty(m))
			return -EINVAL;
		if (!(params->rmask & (1 << k)))
			continue;
#ifdef RULES_DEBUG
		printk("%s = ", snd_pcm_hw_param_names[k]);
		printk("%04x%04x%04x%04x -> ", m->bits[3], m->bits[2], m->bits[1], m->bits[0]);
#endif
		changed = snd_mask_refine(m, constrs_mask(constrs, k));
#ifdef RULES_DEBUG
		printk("%04x%04x%04x%04x\n", m->bits[3], m->bits[2], m->bits[1], m->bits[0]);
#endif
		if (changed)
			params->cmask |= 1 << k;
		if (changed < 0)
			return changed;
	}

	for (k = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; k <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; k++) {
		i = hw_param_interval(params, k);
		if (snd_interval_empty(i))
			return -EINVAL;
		if (!(params->rmask & (1 << k)))
			continue;
#ifdef RULES_DEBUG
		printk("%s = ", snd_pcm_hw_param_names[k]);
		if (i->empty)
			printk("empty");
		else
			printk("%c%u %u%c", 
			       i->openmin ? '(' : '[', i->min,
			       i->max, i->openmax ? ')' : ']');
		printk(" -> ");
#endif
		changed = snd_interval_refine(i, constrs_interval(constrs, k));
#ifdef RULES_DEBUG
		if (i->empty)
			printk("empty\n");
		else 
			printk("%c%u %u%c\n", 
			       i->openmin ? '(' : '[', i->min,
			       i->max, i->openmax ? ')' : ']');
#endif
		if (changed)
			params->cmask |= 1 << k;
		if (changed < 0)
			return changed;
	}

	for (k = 0; k < constrs->rules_num; k++)
		rstamps[k] = 0;
	for (k = 0; k <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; k++) 
		vstamps[k] = (params->rmask & (1 << k)) ? 1 : 0;
	do {
		again = 0;
		for (k = 0; k < constrs->rules_num; k++) {
			snd_pcm_hw_rule_t *r = &constrs->rules[k];
			unsigned int d;
			int doit = 0;
			if (r->cond && !(r->cond & params->flags))
				continue;
			for (d = 0; r->deps[d] >= 0; d++) {
				if (vstamps[r->deps[d]] > rstamps[k]) {
					doit = 1;
					break;
				}
			}
			if (!doit)
				continue;
#ifdef RULES_DEBUG
			printk("Rule %d [%p]: ", k, r->func);
			if (r->var >= 0) {
				printk("%s = ", snd_pcm_hw_param_names[r->var]);
				if (hw_is_mask(r->var)) {
					m = hw_param_mask(params, r->var);
					printk("%x", *m->bits);
				} else {
					i = hw_param_interval(params, r->var);
					if (i->empty)
						printk("empty");
					else
						printk("%c%u %u%c", 
						       i->openmin ? '(' : '[', i->min,
						       i->max, i->openmax ? ')' : ']');
				}
			}
#endif
			changed = r->func(params, r);
#ifdef RULES_DEBUG
			if (r->var >= 0) {
				printk(" -> ");
				if (hw_is_mask(r->var))
					printk("%x", *m->bits);
				else {
					if (i->empty)
						printk("empty");
					else
						printk("%c%u %u%c", 
						       i->openmin ? '(' : '[', i->min,
						       i->max, i->openmax ? ')' : ']');
				}
			}
			printk("\n");
#endif
			rstamps[k] = stamp;
			if (changed && r->var >= 0) {
				params->cmask |= (1 << r->var);
				vstamps[r->var] = stamp;
				again = 1;
			}
			if (changed < 0)
				return changed;
			stamp++;
		}
	} while (again);
	if (!params->msbits) {
		i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
		if (snd_interval_single(i))
			params->msbits = snd_interval_value(i);
	}

	if (!params->rate_den) {
		i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
		if (snd_interval_single(i)) {
			params->rate_num = snd_interval_value(i);
			params->rate_den = 1;
		}
	}

	hw = &substream->runtime->hw;
	if (!params->info)
		params->info = hw->info;
	if (!params->fifo_size)
		params->fifo_size = hw->fifo_size;
	params->rmask = 0;
	return 0;
}

static int snd_pcm_hw_refine_user(snd_pcm_substream_t * substream, snd_pcm_hw_params_t * _params)
{
	snd_pcm_hw_params_t params;
	int err;
	if (copy_from_user(&params, _params, sizeof(params)))
		return -EFAULT;
	err = snd_pcm_hw_refine(substream, &params);
	if (copy_to_user(_params, &params, sizeof(params)))
		return -EFAULT;
	return err;
}

static int snd_pcm_hw_params(snd_pcm_substream_t *substream,
			     snd_pcm_hw_params_t *params)
{
	snd_pcm_runtime_t *runtime;
	int err;
	unsigned int bits;
	snd_pcm_uframes_t frames;

	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -ENXIO);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_SETUP:
	case SNDRV_PCM_STATE_PREPARED:
		break;
	default:
		return -EBADFD;
	}
#if defined(CONFIG_SND_PCM_OSS) || defined(CONFIG_SND_PCM_OSS_MODULE)
	if (!substream->oss.oss)
#endif
		if (atomic_read(&runtime->mmap_count))
			return -EBADFD;

	params->rmask = ~0U;
	err = snd_pcm_hw_refine(substream, params);
	if (err < 0)
		goto _error;

	err = snd_pcm_hw_params_choose(substream, params);
	if (err < 0)
		goto _error;

	if (substream->ops->hw_params != NULL) {
		err = substream->ops->hw_params(substream, params);
		if (err < 0)
			goto _error;
	}

	runtime->access = params_access(params);
	runtime->format = params_format(params);
	runtime->subformat = params_subformat(params);
	runtime->channels = params_channels(params);
	runtime->rate = params_rate(params);
	runtime->period_size = params_period_size(params);
	runtime->periods = params_periods(params);
	runtime->buffer_size = params_buffer_size(params);
	runtime->tick_time = params_tick_time(params);
	runtime->info = params->info;
	runtime->rate_num = params->rate_num;
	runtime->rate_den = params->rate_den;

	bits = snd_pcm_format_physical_width(runtime->format);
	runtime->sample_bits = bits;
	bits *= runtime->channels;
	runtime->frame_bits = bits;
	frames = 1;
	while (bits % 8 != 0) {
		bits *= 2;
		frames *= 2;
	}
	runtime->byte_align = bits / 8;
	runtime->min_align = frames;

	/* Default sw params */
	runtime->tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
	runtime->period_step = 1;
	runtime->sleep_min = 0;
	runtime->control->avail_min = runtime->period_size;
	runtime->xfer_align = runtime->period_size;
	runtime->start_threshold = 1;
	runtime->stop_threshold = runtime->buffer_size;
	runtime->silence_threshold = 0;
	runtime->silence_size = 0;
	runtime->boundary = runtime->buffer_size;
	while (runtime->boundary * 2 <= LONG_MAX - runtime->buffer_size)
		runtime->boundary *= 2;

	snd_pcm_timer_resolution_change(substream);
	runtime->status->state = SNDRV_PCM_STATE_SETUP;
	return 0;
 _error:
	/* hardware might be unuseable from this time,
	   so we force application to retry to set
	   the correct hardware parameter settings */
	runtime->status->state = SNDRV_PCM_STATE_OPEN;
	if (substream->ops->hw_free != NULL)
		substream->ops->hw_free(substream);
	return err;
}

static int snd_pcm_hw_params_user(snd_pcm_substream_t * substream, snd_pcm_hw_params_t * _params)
{
	snd_pcm_hw_params_t params;
	int err;
	if (copy_from_user(&params, _params, sizeof(params)))
		return -EFAULT;
	err = snd_pcm_hw_params(substream, &params);
	if (copy_to_user(_params, &params, sizeof(params)))
		return -EFAULT;
	return err;
}

static int snd_pcm_hw_free(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime;
	int result;

	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -ENXIO);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_SETUP:
	case SNDRV_PCM_STATE_PREPARED:
		break;
	default:
		return -EBADFD;
	}
	if (atomic_read(&runtime->mmap_count))
		return -EBADFD;
	if (substream->ops->hw_free == NULL) {
		runtime->status->state = SNDRV_PCM_STATE_OPEN;
		return 0;
	}
	result = substream->ops->hw_free(substream);
	runtime->status->state = SNDRV_PCM_STATE_OPEN;
	return result;
}

static int snd_pcm_sw_params(snd_pcm_substream_t * substream, snd_pcm_sw_params_t *params)
{
	snd_pcm_runtime_t *runtime;
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -ENXIO);
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;

	if (params->tstamp_mode > SNDRV_PCM_TSTAMP_LAST)
		return -EINVAL;
	if (params->avail_min == 0)
		return -EINVAL;
	if (params->xfer_align == 0 ||
	    params->xfer_align % runtime->min_align != 0)
		return -EINVAL;
	if ((params->silence_threshold != 0 || params->silence_size < runtime->boundary) &&
	    (params->silence_threshold + params->silence_size > runtime->buffer_size))
		return -EINVAL;
	spin_lock_irq(&runtime->lock);
	runtime->tstamp_mode = params->tstamp_mode;
	runtime->sleep_min = params->sleep_min;
	runtime->period_step = params->period_step;
	runtime->control->avail_min = params->avail_min;
	runtime->start_threshold = params->start_threshold;
	runtime->stop_threshold = params->stop_threshold;
	runtime->silence_threshold = params->silence_threshold;
	runtime->silence_size = params->silence_size;
	runtime->xfer_align = params->xfer_align;
        params->boundary = runtime->boundary;
	if (snd_pcm_running(substream)) {
		if (runtime->sleep_min)
			snd_pcm_tick_prepare(substream);
		else
			snd_pcm_tick_set(substream, 0);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
		    runtime->silence_size > 0)
			snd_pcm_playback_silence(substream, ULONG_MAX);
		wake_up(&runtime->sleep);
	}
	spin_unlock_irq(&runtime->lock);
	return 0;
}

static int snd_pcm_sw_params_user(snd_pcm_substream_t * substream, snd_pcm_sw_params_t * _params)
{
	snd_pcm_sw_params_t params;
	int err;
	if (copy_from_user(&params, _params, sizeof(params)))
		return -EFAULT;
	err = snd_pcm_sw_params(substream, &params);
	if (copy_to_user(_params, &params, sizeof(params)))
		return -EFAULT;
	return err;
}

int snd_pcm_status(snd_pcm_substream_t *substream,
		   snd_pcm_status_t *status)
{
	snd_pcm_runtime_t *runtime = substream->runtime;

	spin_lock_irq(&runtime->lock);
	status->state = runtime->status->state;
	status->suspended_state = runtime->status->suspended_state;
	if (status->state == SNDRV_PCM_STATE_OPEN)
		goto _end;
	status->trigger_tstamp = runtime->trigger_tstamp;
	if (snd_pcm_running(substream)) {
		snd_pcm_update_hw_ptr(substream);
		if (runtime->tstamp_mode & SNDRV_PCM_TSTAMP_MMAP)
			status->tstamp = runtime->status->tstamp;
		else
			snd_timestamp_now(&status->tstamp, runtime->tstamp_timespec);
	} else
		snd_timestamp_now(&status->tstamp, runtime->tstamp_timespec);
	status->appl_ptr = runtime->control->appl_ptr;
	status->hw_ptr = runtime->status->hw_ptr;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		status->avail = snd_pcm_playback_avail(runtime);
		if (runtime->status->state == SNDRV_PCM_STATE_RUNNING ||
		    runtime->status->state == SNDRV_PCM_STATE_DRAINING)
			status->delay = runtime->buffer_size - status->avail;
		else
			status->delay = 0;
	} else {
		status->avail = snd_pcm_capture_avail(runtime);
		if (runtime->status->state == SNDRV_PCM_STATE_RUNNING)
			status->delay = status->avail;
		else
			status->delay = 0;
	}
	status->avail_max = runtime->avail_max;
	status->overrange = runtime->overrange;
	runtime->avail_max = 0;
	runtime->overrange = 0;
 _end:
	spin_unlock_irq(&runtime->lock);	
	return 0;
}

static int snd_pcm_status_user(snd_pcm_substream_t * substream, snd_pcm_status_t * _status)
{
	snd_pcm_status_t status;
	snd_pcm_runtime_t *runtime;
	int res;
	
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	memset(&status, 0, sizeof(status));
	res = snd_pcm_status(substream, &status);
	if (res < 0)
		return res;
	if (copy_to_user(_status, &status, sizeof(status)))
		return -EFAULT;
	return 0;
}

static int snd_pcm_channel_info(snd_pcm_substream_t * substream, snd_pcm_channel_info_t * _info)
{
	snd_pcm_channel_info_t info;
	snd_pcm_runtime_t *runtime;
	int res;
	unsigned int channel;
	
	snd_assert(substream != NULL, return -ENXIO);
	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;
	channel = info.channel;
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	if (channel >= runtime->channels)
		return -EINVAL;
	memset(&info, 0, sizeof(info));
	info.channel = channel;
	res = substream->ops->ioctl(substream, SNDRV_PCM_IOCTL1_CHANNEL_INFO, &info);
	if (res < 0)
		return res;
	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static void snd_pcm_trigger_tstamp(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (runtime->trigger_master == NULL)
		return;
	if (runtime->trigger_master == substream) {
		snd_timestamp_now(&runtime->trigger_tstamp, runtime->tstamp_timespec);
	} else {
		snd_pcm_trigger_tstamp(runtime->trigger_master);
		runtime->trigger_tstamp = runtime->trigger_master->runtime->trigger_tstamp;
	}
	runtime->trigger_master = NULL;
}

#define _SND_PCM_ACTION(aname, substream, state, res, check_master) { \
	snd_pcm_substream_t *s; \
	res = 0; \
	read_lock(&pcm_link_lock); \
	s = substream; \
	do { \
		if (s != substream) \
			spin_lock(&s->runtime->lock); \
		res = snd_pcm_pre_##aname(s, state); \
		if (res < 0) \
			break; \
		s = s->link_next; \
	} while (s != substream); \
	if (res < 0) { \
		/* Clean all spin_lock */ \
		while (s != substream) { \
			spin_unlock(&s->runtime->lock); \
			s = s->link_prev; \
		} \
		goto _end; \
	} \
	s = substream; \
	do { \
		snd_pcm_runtime_t *runtime = s->runtime; \
		int err; \
		if (check_master && runtime->trigger_master != s) \
			goto _done; \
		err = snd_pcm_do_##aname(s, state); \
		if (err < 0) { \
			if (res == 0) \
				res = err; \
		} else { \
			_done: \
			snd_pcm_post_##aname(s, state); \
		} \
		if (s != substream) \
			spin_unlock(&runtime->lock); \
		s = s->link_next; \
	} while (s != substream); \
 _end: \
	read_unlock(&pcm_link_lock); \
}

#define SND_PCM_ACTION(aname, substream, state) { \
	int res; \
	_SND_PCM_ACTION(aname, substream, state, res, 1); \
	return res; \
}

static inline int snd_pcm_pre_start(snd_pcm_substream_t *substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (runtime->status->state != SNDRV_PCM_STATE_PREPARED)
		return -EBADFD;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    !snd_pcm_playback_data(substream))
		return -EPIPE;
	runtime->trigger_master = substream;
	return 0;
}

static inline int snd_pcm_do_start(snd_pcm_substream_t *substream, int state)
{
        return substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_START);
}

static inline void snd_pcm_post_start(snd_pcm_substream_t *substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_trigger_tstamp(substream);
	runtime->status->state = SNDRV_PCM_STATE_RUNNING;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    runtime->silence_size > 0)
		snd_pcm_playback_silence(substream, ULONG_MAX);
	if (runtime->sleep_min)
		snd_pcm_tick_prepare(substream);
	if (substream->timer)
		snd_timer_notify(substream->timer, SNDRV_TIMER_EVENT_MSTART, &runtime->trigger_tstamp);
}

/**
 * snd_pcm_sart
 */
int snd_pcm_start(snd_pcm_substream_t *substream)
{
	SND_PCM_ACTION(start, substream, 0);
}

static inline int snd_pcm_pre_stop(snd_pcm_substream_t *substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (substream->runtime->status->state != SNDRV_PCM_STATE_RUNNING &&
	    substream->runtime->status->state != SNDRV_PCM_STATE_DRAINING)
		return -EBADFD;
	runtime->trigger_master = substream;
	return 0;
}

static inline int snd_pcm_do_stop(snd_pcm_substream_t *substream, int state)
{
	return substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_STOP);
}

static inline void snd_pcm_post_stop(snd_pcm_substream_t *substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_trigger_tstamp(substream);
	if (substream->timer)
		snd_timer_notify(substream->timer, SNDRV_TIMER_EVENT_MSTOP, &runtime->trigger_tstamp);
	runtime->status->state = state;
	snd_pcm_tick_set(substream, 0);
	wake_up(&runtime->sleep);
}

/**
 * snd_pcm_stop
 */
int snd_pcm_stop(snd_pcm_substream_t *substream, int state)
{
	SND_PCM_ACTION(stop, substream, state);
}

static inline int snd_pcm_pre_pause(snd_pcm_substream_t *substream, int push)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (!(runtime->info & SNDRV_PCM_INFO_PAUSE))
		return -ENOSYS;
	if (push) {
		if (runtime->status->state != SNDRV_PCM_STATE_RUNNING)
			return -EBADFD;
	} else if (runtime->status->state != SNDRV_PCM_STATE_PAUSED)
		return -EBADFD;
	runtime->trigger_master = substream;
	return 0;
}

static inline int snd_pcm_do_pause(snd_pcm_substream_t *substream, int push)
{
	return substream->ops->trigger(substream,
				       push ? SNDRV_PCM_TRIGGER_PAUSE_PUSH :
					      SNDRV_PCM_TRIGGER_PAUSE_RELEASE);
}

static inline void snd_pcm_post_pause(snd_pcm_substream_t *substream, int push)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_trigger_tstamp(substream);
	if (push) {
		runtime->status->state = SNDRV_PCM_STATE_PAUSED;
		if (substream->timer)
			snd_timer_notify(substream->timer, SNDRV_TIMER_EVENT_MPAUSE, &runtime->trigger_tstamp);
		snd_pcm_tick_set(substream, 0);
		wake_up(&runtime->sleep);
	} else {
		runtime->status->state = SNDRV_PCM_STATE_RUNNING;
		if (runtime->sleep_min)
			snd_pcm_tick_prepare(substream);
		if (substream->timer)
			snd_timer_notify(substream->timer, SNDRV_TIMER_EVENT_MCONTINUE, &runtime->trigger_tstamp);
	}
}

static int snd_pcm_pause(snd_pcm_substream_t *substream, int push)
{
	SND_PCM_ACTION(pause, substream, push);
}

#ifdef CONFIG_PM
/* suspend */

static inline int snd_pcm_pre_suspend(snd_pcm_substream_t *substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_SUSPENDED)
		return -EBUSY;
	runtime->status->suspended_state = runtime->status->state;
	runtime->trigger_master = substream;
	return 0;
}

static inline int snd_pcm_do_suspend(snd_pcm_substream_t *substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (runtime->status->suspended_state != SNDRV_PCM_STATE_RUNNING)
		return 0;
	return substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_SUSPEND);
}

static inline void snd_pcm_post_suspend(snd_pcm_substream_t *substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_trigger_tstamp(substream);
	if (substream->timer)
		snd_timer_notify(substream->timer, SNDRV_TIMER_EVENT_MPAUSE, &runtime->trigger_tstamp);
	runtime->status->state = SNDRV_PCM_STATE_SUSPENDED;
	snd_pcm_tick_set(substream, 0);
	wake_up(&runtime->sleep);
}

/**
 * snd_pcm_suspend
 */
int snd_pcm_suspend(snd_pcm_substream_t *substream)
{
	SND_PCM_ACTION(suspend, substream, 0);
}

/**
 * snd_pcm_suspend_all
 */
int snd_pcm_suspend_all(snd_pcm_t *pcm)
{
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;
	int stream, err;

	for (stream = 0; stream < 2; stream++) {
		for (substream = pcm->streams[stream].substream; substream; substream = substream->next) {
			/* FIXME: the open/close code should lock this as well */
			if ((runtime = substream->runtime) == NULL)
				continue;
			spin_lock(&runtime->lock);
			if (runtime->status->state == SNDRV_PCM_STATE_SUSPENDED) {
				spin_unlock(&runtime->lock);
				continue;
			}
			if ((err = snd_pcm_suspend(substream)) < 0) {
				spin_unlock(&runtime->lock);
				return err;
			}
			spin_unlock(&runtime->lock);
		}
	}
	return 0;
}

/* resume */

static inline int snd_pcm_pre_resume(snd_pcm_substream_t *substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (!(runtime->info & SNDRV_PCM_INFO_RESUME))
		return -ENOSYS;
	runtime->trigger_master = substream;
	return 0;
}

static inline int snd_pcm_do_resume(snd_pcm_substream_t *substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	if (runtime->status->suspended_state != SNDRV_PCM_STATE_RUNNING)
		return 0;
	return substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_RESUME);
}

static inline void snd_pcm_post_resume(snd_pcm_substream_t *substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_trigger_tstamp(substream);
	if (substream->timer)
		snd_timer_notify(substream->timer, SNDRV_TIMER_EVENT_MCONTINUE, &runtime->trigger_tstamp);
	runtime->status->state = runtime->status->suspended_state;
	if (runtime->sleep_min)
		snd_pcm_tick_prepare(substream);
}

static int snd_pcm_resume(snd_pcm_substream_t *substream)
{
	snd_card_t *card = substream->pcm->card;
	int res;

	snd_power_lock(card);
	while (snd_power_get_state(card) != SNDRV_CTL_POWER_D0) {
		if (substream->ffile->f_flags & O_NONBLOCK) {
			res = -EAGAIN;
			goto _power_unlock;
		}
		snd_power_wait(card);
	}

	_SND_PCM_ACTION(resume, substream, 0, res, 1);

       _power_unlock:
	snd_power_unlock(card);
	return res;
}

#else

static int snd_pcm_resume(snd_pcm_substream_t *substream)
{
	return -ENOSYS;
}

#endif /* CONFIG_PM */

static int snd_pcm_xrun(snd_pcm_substream_t *substream)
{
	snd_card_t *card = substream->pcm->card;
	snd_pcm_runtime_t *runtime = substream->runtime;
	int result;

	snd_power_lock(card);
	spin_lock_irq(&runtime->lock);
       _xrun_recovery:
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_XRUN:
		result = 0;	/* already there */
		break;
	case SNDRV_PCM_STATE_RUNNING:
		result = snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
		break;
	case SNDRV_PCM_STATE_SUSPENDED:
		while (snd_power_get_state(card) != SNDRV_CTL_POWER_D0) {
			if (substream->ffile->f_flags & O_NONBLOCK) {
				result = -EAGAIN;
				goto _end;
			}
			spin_unlock_irq(&runtime->lock);
			snd_power_wait(card);
			spin_lock_irq(&runtime->lock);
		}
		goto _xrun_recovery;
	default:
		result = -EBADFD;
	}
       _end:
	spin_unlock_irq(&runtime->lock);
	snd_power_unlock(card);
	return result;
}

static inline int snd_pcm_pre_reset(snd_pcm_substream_t * substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
	case SNDRV_PCM_STATE_SUSPENDED:
		return 0;
	default:
		return -EBADFD;
	}
}

static inline int snd_pcm_do_reset(snd_pcm_substream_t * substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err = substream->ops->ioctl(substream, SNDRV_PCM_IOCTL1_RESET, 0);
	if (err < 0)
		return err;
	// snd_assert(runtime->status->hw_ptr < runtime->buffer_size, );
	runtime->hw_ptr_base = 0;
	runtime->hw_ptr_interrupt = runtime->status->hw_ptr - runtime->status->hw_ptr % runtime->period_size;
	runtime->silenced_start = runtime->status->hw_ptr;
	runtime->silenced_size = 0;
	return 0;
}

static inline void snd_pcm_post_reset(snd_pcm_substream_t * substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	runtime->control->appl_ptr = runtime->status->hw_ptr;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    runtime->silence_size > 0)
		snd_pcm_playback_silence(substream, ULONG_MAX);
}

static int snd_pcm_reset(snd_pcm_substream_t *substream)
{
	int res;

	spin_lock_irq(&substream->runtime->lock);
	_SND_PCM_ACTION(reset, substream, 0, res, 0);
	spin_unlock_irq(&substream->runtime->lock);
	return res;
}

static inline int snd_pcm_pre_prepare(snd_pcm_substream_t * substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_OPEN:
		return -EBADFD;
	case SNDRV_PCM_STATE_RUNNING:
		return -EBUSY;
	default:
		return 0;
	}
}

static inline int snd_pcm_do_prepare(snd_pcm_substream_t * substream, int state)
{
	int err;
	err = substream->ops->prepare(substream);
	if (err < 0)
		return err;
	return snd_pcm_do_reset(substream, 0);
}

static inline void snd_pcm_post_prepare(snd_pcm_substream_t * substream, int state)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	runtime->control->appl_ptr = runtime->status->hw_ptr;
	runtime->status->state = SNDRV_PCM_STATE_PREPARED;
}

/**
 * snd_pcm_prepare
 */
int snd_pcm_prepare(snd_pcm_substream_t *substream)
{
	int res;
	snd_card_t *card = substream->pcm->card;

	snd_power_lock(card);
	while (snd_power_get_state(card) != SNDRV_CTL_POWER_D0) {
		if (substream->ffile->f_flags & O_NONBLOCK) {
			res = -EAGAIN;
			goto _power_unlock;
		}
		snd_power_wait(card);
	}

	spin_lock_irq(&substream->runtime->lock);
	_SND_PCM_ACTION(prepare, substream, 0, res, 0);
	spin_unlock_irq(&substream->runtime->lock);

       _power_unlock:
	snd_power_unlock(card);
	return res;
}

static void snd_pcm_change_state(snd_pcm_substream_t *substream, int state)
{
	snd_pcm_substream_t *s;
	read_lock(&pcm_link_lock);
	s = substream->link_next;
	while (s != substream) {
		spin_lock(&s->runtime->lock);
		s = s->link_next;
	}
	s = substream;
	do {
		snd_pcm_runtime_t *runtime = s->runtime;
		runtime->status->state = state;
		if (s != substream)
			spin_unlock(&runtime->lock);
		s = s->link_next;
	} while (s != substream);
	read_unlock(&pcm_link_lock);
}

static int snd_pcm_playback_drop(snd_pcm_substream_t *substream);

static int snd_pcm_playback_drain(snd_pcm_substream_t * substream)
{
	snd_card_t *card;
	snd_pcm_runtime_t *runtime;
	int err, result = 0;
	wait_queue_t wait;
	enum { READY, EXPIRED, SUSPENDED, SIGNALED } state = READY;
	snd_pcm_uframes_t stop_threshold;

	snd_assert(substream != NULL, return -ENXIO);
	snd_assert(substream->stream == SNDRV_PCM_STREAM_PLAYBACK, return -EINVAL);
	runtime = substream->runtime;
	card = substream->pcm->card;

	snd_power_lock(card);
	spin_lock_irq(&runtime->lock);

	/* stop_threshold fixup to avoid endless loop when */
	/* stop_threshold > buffer_size */
	stop_threshold = runtime->stop_threshold;
	if (runtime->stop_threshold > runtime->buffer_size)
		runtime->stop_threshold = runtime->buffer_size;

	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PAUSED:
		snd_pcm_pause(substream, 0);
		/* Fall through */
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_DRAINING:
		break;
	case SNDRV_PCM_STATE_SUSPENDED:
		while (snd_power_get_state(card) != SNDRV_CTL_POWER_D0) {
			if (substream->ffile->f_flags & O_NONBLOCK) {
				result = -EAGAIN;
				goto _end;
			}
			spin_unlock_irq(&runtime->lock);
			snd_power_wait(card);
			spin_lock_irq(&runtime->lock);
		}
		goto _xrun_recovery;
	case SNDRV_PCM_STATE_OPEN:
		result = -EBADFD;
		goto _end;
	case SNDRV_PCM_STATE_PREPARED:
		if (!snd_pcm_playback_empty(substream)) {
			err = snd_pcm_start(substream);
			if (err < 0) {
				result = err;
				goto _end;
			}
			break;
		}
		/* Fall through */
	case SNDRV_PCM_STATE_XRUN:
	       _xrun_recovery:
		snd_pcm_change_state(substream, SNDRV_PCM_STATE_SETUP);
		/* Fall through */
	case SNDRV_PCM_STATE_SETUP:
		goto _end;
	default: 
		break; 
	}

	if (runtime->status->state == SNDRV_PCM_STATE_RUNNING) {
		if (snd_pcm_playback_empty(substream)) {
			snd_pcm_stop(substream, SNDRV_PCM_STATE_SETUP);
			goto _end;
		}
		snd_pcm_change_state(substream, SNDRV_PCM_STATE_DRAINING);
	}

	if (substream->ffile->f_flags & O_NONBLOCK) {
		result = -EAGAIN;
		goto _end;
	}

	init_waitqueue_entry(&wait, current);
	add_wait_queue(&runtime->sleep, &wait);
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current)) {
			state = SIGNALED;
			break;
		}
		spin_unlock_irq(&runtime->lock);
		if (schedule_timeout(10 * HZ) == 0) {
			spin_lock_irq(&runtime->lock);
			state = runtime->status->state == SNDRV_PCM_STATE_SUSPENDED ? SUSPENDED : EXPIRED;
			break;
		}
		spin_lock_irq(&runtime->lock);
		if (runtime->status->state != SNDRV_PCM_STATE_DRAINING) {
			state = READY;
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&runtime->sleep, &wait);

	switch (state) {
	case SIGNALED:
		result = -ERESTARTSYS;
		goto _end;
	case SUSPENDED:
		result = -ESTRPIPE;
		goto _end;
	case EXPIRED:
		snd_printd("playback drain error (DMA or IRQ trouble?)\n");
		result = -EIO;
		goto _end;
	default:
		break;
	}

      _end:
	runtime->stop_threshold = stop_threshold;
	spin_unlock_irq(&runtime->lock);
	snd_power_unlock(card);
	if (state == EXPIRED)
		snd_pcm_playback_drop(substream);

	return result;
}

static int snd_pcm_playback_drop(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_t *card = substream->pcm->card;
	int res = 0;
	snd_power_lock(card);
	spin_lock_irq(&runtime->lock);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_OPEN:
		res = -EBADFD;
		break;
	case SNDRV_PCM_STATE_SETUP:
		break;
	case SNDRV_PCM_STATE_PAUSED:
		snd_pcm_pause(substream, 0);
		/* Fall through */
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_DRAINING:
		if (snd_pcm_update_hw_ptr(substream) >= 0) {
			snd_pcm_stop(substream, SNDRV_PCM_STATE_SETUP);
			break;
		}
		/* Fall through */
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_XRUN:
	       _xrun_recovery:
		snd_pcm_change_state(substream, SNDRV_PCM_STATE_SETUP);
		break;
	case SNDRV_PCM_STATE_SUSPENDED:
		while (snd_power_get_state(card) != SNDRV_CTL_POWER_D0) {
			if (substream->ffile->f_flags & O_NONBLOCK) {
				res = -EAGAIN;
				goto _end;
			}
			spin_unlock_irq(&runtime->lock);
			snd_power_wait(card);
			spin_lock_irq(&runtime->lock);
		}
		goto _xrun_recovery;
	default:
		break; 
	}
	runtime->control->appl_ptr = runtime->status->hw_ptr;
       _end:
	spin_unlock_irq(&runtime->lock);
	snd_power_unlock(card);
	return res;
}

static int snd_pcm_capture_drain(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_t *card = substream->pcm->card;
	int res = 0;
	snd_power_lock(card);
	spin_lock_irq(&runtime->lock);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_OPEN:
		res = -EBADFD;
		break;
	case SNDRV_PCM_STATE_PREPARED:
		snd_pcm_change_state(substream, SNDRV_PCM_STATE_SETUP);
		break;
	case SNDRV_PCM_STATE_SETUP:
	case SNDRV_PCM_STATE_DRAINING:
		break;
	case SNDRV_PCM_STATE_PAUSED:
		snd_pcm_pause(substream, 0);
		/* Fall through */
	case SNDRV_PCM_STATE_RUNNING:
		if (snd_pcm_update_hw_ptr(substream) >= 0) {
			snd_pcm_stop(substream, 
				     snd_pcm_capture_avail(runtime) > 0 ?
				     SNDRV_PCM_STATE_DRAINING : SNDRV_PCM_STATE_SETUP);
			break;
		}
		/* Fall through */
	case SNDRV_PCM_STATE_XRUN:
	       _xrun_recovery:
		snd_pcm_change_state(substream, 
				     snd_pcm_capture_avail(runtime) > 0 ?
				     SNDRV_PCM_STATE_DRAINING : SNDRV_PCM_STATE_SETUP);
		break;
	case SNDRV_PCM_STATE_SUSPENDED:
		while (snd_power_get_state(card) != SNDRV_CTL_POWER_D0) {
			if (substream->ffile->f_flags & O_NONBLOCK) {
				res = -EAGAIN;
				goto _end;
			}
			spin_unlock_irq(&runtime->lock);
			snd_power_wait(card);
			spin_lock_irq(&runtime->lock);
		}
		goto _xrun_recovery;
	default: 
		break; 
	}
       _end:
	spin_unlock_irq(&runtime->lock);
	snd_power_unlock(card);
	return res;
}

static int snd_pcm_capture_drop(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_card_t *card = substream->pcm->card;
	int res = 0;
	snd_power_lock(card);
	spin_lock_irq(&runtime->lock);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_OPEN:
		res = -EBADFD;
		break;
	case SNDRV_PCM_STATE_SETUP:
		break;
	case SNDRV_PCM_STATE_PAUSED:
		snd_pcm_pause(substream, 0);
		/* Fall through */
	case SNDRV_PCM_STATE_RUNNING:
		snd_pcm_stop(substream, SNDRV_PCM_STATE_SETUP);
		break;
	case SNDRV_PCM_STATE_SUSPENDED:
		while (snd_power_get_state(card) != SNDRV_CTL_POWER_D0) {
			if (substream->ffile->f_flags & O_NONBLOCK) {
				res = -EAGAIN;
				goto _end;
			}
			spin_unlock_irq(&runtime->lock);
			snd_power_wait(card);
			spin_lock_irq(&runtime->lock);
		}
		/* Fall through */
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_DRAINING:
	case SNDRV_PCM_STATE_XRUN:
		snd_pcm_change_state(substream, SNDRV_PCM_STATE_SETUP);
		break;
	default: 
		break; 
	}
	runtime->control->appl_ptr = runtime->status->hw_ptr;
       _end: 
	spin_unlock_irq(&runtime->lock);
	snd_power_unlock(card);
	return res;
}

/* WARNING: Don't forget to fput back the file */
static struct file *snd_pcm_file_fd(int fd)
{
	struct file *file;
	struct inode *inode;
	unsigned short minor;
	file = fget(fd);
	if (!file)
		return 0;
	inode = file->f_dentry->d_inode;
	if (!S_ISCHR(inode->i_mode) ||
	    major(inode->i_rdev) != CONFIG_SND_MAJOR) {
		fput(file);
		return 0;
	}
	minor = minor(inode->i_rdev);
	if (minor >= 256 || 
	    minor % SNDRV_MINOR_DEVICES < SNDRV_MINOR_PCM_PLAYBACK) {
		fput(file);
		return 0;
	}
	return file;
}

static int snd_pcm_link(snd_pcm_substream_t *substream, int fd)
{
	int res = 0;
	struct file *file;
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *s, *substream1;
	if (substream->runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	file = snd_pcm_file_fd(fd);
	if (!file)
		return -EBADFD;
	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, return -ENXIO);
	substream1 = pcm_file->substream;
	write_lock_irq(&pcm_link_lock);
	if (substream->runtime->status->state != substream1->runtime->status->state) {
		res = -EBADFD;
		goto _end;
	}
	s = substream;
	do {
		if (s == substream1) {
			res = -EALREADY;
			goto _end;
		}
		s = s->link_next;
	} while (s != substream);
	substream1->link_prev->link_next = substream->link_next;
	substream->link_next->link_prev = substream1->link_prev;
	substream->link_next = substream1;
	substream1->link_prev = substream;
 _end:
	write_unlock_irq(&pcm_link_lock);
	fput(file);
	return res;
}

static int snd_pcm_unlink(snd_pcm_substream_t *substream)
{
	write_lock_irq(&pcm_link_lock);
	substream->link_prev->link_next = substream->link_next;
	substream->link_next->link_prev = substream->link_prev;
	substream->link_prev = substream;
	substream->link_next = substream;
	write_unlock_irq(&pcm_link_lock);
	return 0;
}


static int snd_pcm_hw_rule_mul(snd_pcm_hw_params_t *params,
			       snd_pcm_hw_rule_t *rule)
{
	snd_interval_t t;
	snd_interval_mul(hw_param_interval_c(params, rule->deps[0]),
		     hw_param_interval_c(params, rule->deps[1]), &t);
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static int snd_pcm_hw_rule_div(snd_pcm_hw_params_t *params,
			       snd_pcm_hw_rule_t *rule)
{
	snd_interval_t t;
	snd_interval_div(hw_param_interval_c(params, rule->deps[0]),
		     hw_param_interval_c(params, rule->deps[1]), &t);
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static int snd_pcm_hw_rule_muldivk(snd_pcm_hw_params_t *params,
				   snd_pcm_hw_rule_t *rule)
{
	snd_interval_t t;
	snd_interval_muldivk(hw_param_interval_c(params, rule->deps[0]),
			 hw_param_interval_c(params, rule->deps[1]),
			 (unsigned long) rule->private, &t);
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static int snd_pcm_hw_rule_mulkdiv(snd_pcm_hw_params_t *params,
				   snd_pcm_hw_rule_t *rule)
{
	snd_interval_t t;
	snd_interval_mulkdiv(hw_param_interval_c(params, rule->deps[0]),
			 (unsigned long) rule->private,
			 hw_param_interval_c(params, rule->deps[1]), &t);
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

static int snd_pcm_hw_rule_format(snd_pcm_hw_params_t *params,
				  snd_pcm_hw_rule_t *rule)
{
	unsigned int k;
	snd_interval_t *i = hw_param_interval(params, rule->deps[0]);
	snd_mask_t m;
	snd_mask_t *mask = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	snd_mask_any(&m);
	for (k = 0; k <= SNDRV_PCM_FORMAT_LAST; ++k) {
		int bits;
		if (! snd_mask_test(mask, k))
			continue;
		bits = snd_pcm_format_physical_width(k);
		snd_assert(bits > 0, continue);
		if ((unsigned)bits < i->min || (unsigned)bits > i->max)
			snd_mask_reset(&m, k);
	}
	return snd_mask_refine(mask, &m);
}

static int snd_pcm_hw_rule_sample_bits(snd_pcm_hw_params_t *params,
				       snd_pcm_hw_rule_t *rule)
{
	snd_interval_t t;
	unsigned int k;
	t.min = UINT_MAX;
	t.max = 0;
	t.openmin = 0;
	t.openmax = 0;
	for (k = 0; k <= SNDRV_PCM_FORMAT_LAST; ++k) {
		int bits;
		if (! snd_mask_test(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT), k))
			continue;
		bits = snd_pcm_format_physical_width(k);
		snd_assert(bits > 0, continue);
		if (t.min > (unsigned)bits)
			t.min = bits;
		if (t.max < (unsigned)bits)
			t.max = bits;
	}
	t.integer = 1;
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}

#if SNDRV_PCM_RATE_5512 != 1 << 0 || SNDRV_PCM_RATE_192000 != 1 << 12
#error "Change this table"
#endif

static unsigned int rates[] = { 5512, 8000, 11025, 16000, 22050, 32000, 44100,
                                 48000, 64000, 88200, 96000, 176400, 192000 };

#define RATES (sizeof(rates) / sizeof(rates[0]))

static int snd_pcm_hw_rule_rate(snd_pcm_hw_params_t *params,
				snd_pcm_hw_rule_t *rule)
{
	snd_pcm_hardware_t *hw = rule->private;
	return snd_interval_list(hw_param_interval(params, rule->var), RATES, rates, hw->rates);
}		

static int snd_pcm_hw_rule_buffer_bytes_max(snd_pcm_hw_params_t *params,
					    snd_pcm_hw_rule_t *rule)
{
	snd_interval_t t;
	snd_pcm_substream_t *substream = rule->private;
	t.min = 0;
	t.max = substream->buffer_bytes_max;
	t.openmin = 0;
	t.openmax = 0;
	t.integer = 1;
	return snd_interval_refine(hw_param_interval(params, rule->var), &t);
}		

int snd_pcm_hw_constraints_init(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_hw_constraints_t *constrs = &runtime->hw_constraints;
	int k, err;

	for (k = SNDRV_PCM_HW_PARAM_FIRST_MASK; k <= SNDRV_PCM_HW_PARAM_LAST_MASK; k++) {
		snd_mask_any(constrs_mask(constrs, k));
	}

	for (k = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; k <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; k++) {
		snd_interval_any(constrs_interval(constrs, k));
	}

	snd_interval_setinteger(constrs_interval(constrs, SNDRV_PCM_HW_PARAM_CHANNELS));
	snd_interval_setinteger(constrs_interval(constrs, SNDRV_PCM_HW_PARAM_BUFFER_SIZE));
	snd_interval_setinteger(constrs_interval(constrs, SNDRV_PCM_HW_PARAM_BUFFER_BYTES));
	snd_interval_setinteger(constrs_interval(constrs, SNDRV_PCM_HW_PARAM_SAMPLE_BITS));
	snd_interval_setinteger(constrs_interval(constrs, SNDRV_PCM_HW_PARAM_FRAME_BITS));

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FORMAT,
				   snd_pcm_hw_rule_format, 0,
				   SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 
				  snd_pcm_hw_rule_sample_bits, 0,
				  SNDRV_PCM_HW_PARAM_FORMAT, 
				  SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 
				  snd_pcm_hw_rule_div, 0,
				  SNDRV_PCM_HW_PARAM_FRAME_BITS, SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FRAME_BITS, 
				  snd_pcm_hw_rule_mul, 0,
				  SNDRV_PCM_HW_PARAM_SAMPLE_BITS, SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FRAME_BITS, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 8,
				  SNDRV_PCM_HW_PARAM_PERIOD_BYTES, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_FRAME_BITS, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 8,
				  SNDRV_PCM_HW_PARAM_BUFFER_BYTES, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS, 
				  snd_pcm_hw_rule_div, 0,
				  SNDRV_PCM_HW_PARAM_FRAME_BITS, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_PERIOD_SIZE, SNDRV_PCM_HW_PARAM_PERIOD_TIME, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE, SNDRV_PCM_HW_PARAM_BUFFER_TIME, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIODS, 
				  snd_pcm_hw_rule_div, 0,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 
				  snd_pcm_hw_rule_div, 0,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE, SNDRV_PCM_HW_PARAM_PERIODS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 8,
				  SNDRV_PCM_HW_PARAM_PERIOD_BYTES, SNDRV_PCM_HW_PARAM_FRAME_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 
				  snd_pcm_hw_rule_muldivk, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_PERIOD_TIME, SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 
				  snd_pcm_hw_rule_mul, 0,
				  SNDRV_PCM_HW_PARAM_PERIOD_SIZE, SNDRV_PCM_HW_PARAM_PERIODS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 8,
				  SNDRV_PCM_HW_PARAM_BUFFER_BYTES, SNDRV_PCM_HW_PARAM_FRAME_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 
				  snd_pcm_hw_rule_muldivk, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_BUFFER_TIME, SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 
				  snd_pcm_hw_rule_muldivk, (void*) 8,
				  SNDRV_PCM_HW_PARAM_PERIOD_SIZE, SNDRV_PCM_HW_PARAM_FRAME_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 
				  snd_pcm_hw_rule_muldivk, (void*) 8,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE, SNDRV_PCM_HW_PARAM_FRAME_BITS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_PERIOD_SIZE, SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_TIME, 
				  snd_pcm_hw_rule_mulkdiv, (void*) 1000000,
				  SNDRV_PCM_HW_PARAM_BUFFER_SIZE, SNDRV_PCM_HW_PARAM_RATE, -1);
	if (err < 0)
		return err;
	return 0;
}

int snd_pcm_hw_constraints_complete(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_hardware_t *hw = &runtime->hw;
	int err;
	unsigned int mask = 0;

        if (hw->info & SNDRV_PCM_INFO_INTERLEAVED)
		mask |= 1 << SNDRV_PCM_ACCESS_RW_INTERLEAVED;
        if (hw->info & SNDRV_PCM_INFO_NONINTERLEAVED)
		mask |= 1 << SNDRV_PCM_ACCESS_RW_NONINTERLEAVED;
	if (hw->info & SNDRV_PCM_INFO_MMAP) {
		if (hw->info & SNDRV_PCM_INFO_INTERLEAVED)
			mask |= 1 << SNDRV_PCM_ACCESS_MMAP_INTERLEAVED;
		if (hw->info & SNDRV_PCM_INFO_NONINTERLEAVED)
			mask |= 1 << SNDRV_PCM_ACCESS_MMAP_NONINTERLEAVED;
		if (hw->info & SNDRV_PCM_INFO_COMPLEX)
			mask |= 1 << SNDRV_PCM_ACCESS_MMAP_COMPLEX;
	}
	err = snd_pcm_hw_constraint_mask(runtime, SNDRV_PCM_HW_PARAM_ACCESS, mask);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_mask64(runtime, SNDRV_PCM_HW_PARAM_FORMAT, hw->formats);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_mask(runtime, SNDRV_PCM_HW_PARAM_SUBFORMAT, 1 << SNDRV_PCM_SUBFORMAT_STD);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_CHANNELS,
					   hw->channels_min, hw->channels_max);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_RATE,
					   hw->rate_min, hw->rate_max);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
					   hw->period_bytes_min, hw->period_bytes_max);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIODS,
					   hw->periods_min, hw->periods_max);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					   hw->period_bytes_min, hw->buffer_bytes_max);
	snd_assert(err >= 0, return -EINVAL);

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 
				  snd_pcm_hw_rule_buffer_bytes_max, substream,
				  SNDRV_PCM_HW_PARAM_BUFFER_BYTES, -1);
	if (err < 0)
		return err;

	/* FIXME: remove */
	if (runtime->dma_bytes) {
		err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 0, runtime->dma_bytes);
		snd_assert(err >= 0, return -EINVAL);
	}

	if (!(hw->rates & (SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_CONTINUOUS))) {
		err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, 
					  snd_pcm_hw_rule_rate, hw,
					  SNDRV_PCM_HW_PARAM_RATE, -1);
		if (err < 0)
			return err;
	}

	/* FIXME: this belong to lowlevel */
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_TICK_TIME,
				     1000000 / HZ, 1000000 / HZ);
	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);

	return 0;
}

static void snd_pcm_add_file(snd_pcm_str_t *str,
			     snd_pcm_file_t *pcm_file)
{
	pcm_file->next = str->files;
	str->files = pcm_file;
}

static void snd_pcm_remove_file(snd_pcm_str_t *str,
				snd_pcm_file_t *pcm_file)
{
	snd_pcm_file_t * pcm_file1;
	if (str->files == pcm_file) {
		str->files = pcm_file->next;
	} else {
		pcm_file1 = str->files;
		while (pcm_file1 && pcm_file1->next != pcm_file)
			pcm_file1 = pcm_file1->next;
		if (pcm_file1 != NULL)
			pcm_file1->next = pcm_file->next;
	}
}

static int snd_pcm_release_file(snd_pcm_file_t * pcm_file)
{
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;
	snd_pcm_str_t * str;

	snd_assert(pcm_file != NULL, return -ENXIO);
	substream = pcm_file->substream;
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	str = substream->pstr;
	snd_pcm_unlink(substream);
	if (substream->open_flag) {
		if (substream->ops->hw_free != NULL)
			substream->ops->hw_free(substream);
		substream->ops->close(substream);
		substream->open_flag = 0;
	}
	substream->ffile = NULL;
	snd_pcm_remove_file(str, pcm_file);
	snd_pcm_release_substream(substream);
	snd_magic_kfree(pcm_file);
	return 0;
}

static int snd_pcm_open_file(struct file *file,
			     snd_pcm_t *pcm,
			     int stream,
			     snd_pcm_file_t **rpcm_file)
{
	int err = 0;
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *substream;
	snd_pcm_str_t *str;

	snd_assert(rpcm_file != NULL, return -EINVAL);
	*rpcm_file = NULL;

	pcm_file = snd_magic_kcalloc(snd_pcm_file_t, 0, GFP_KERNEL);
	if (pcm_file == NULL) {
		return -ENOMEM;
	}

	if ((err = snd_pcm_open_substream(pcm, stream, &substream)) < 0) {
		snd_magic_kfree(pcm_file);
		return err;
	}

	str = substream->pstr;
	substream->file = pcm_file;

	pcm_file->substream = substream;

	snd_pcm_add_file(str, pcm_file);

	err = snd_pcm_hw_constraints_init(substream);
	if (err < 0) {
		snd_printd("snd_pcm_hw_constraints_init failed\n");
		snd_pcm_release_file(pcm_file);
		return err;
	}

	if ((err = substream->ops->open(substream)) < 0) {
		snd_pcm_release_file(pcm_file);
		return err;
	}
	substream->open_flag = 1;

	err = snd_pcm_hw_constraints_complete(substream);
	if (err < 0) {
		snd_printd("snd_pcm_hw_constraints_complete failed\n");
		substream->ops->close(substream);
		snd_pcm_release_file(pcm_file);
		return err;
	}

	substream->ffile = file;

	file->private_data = pcm_file;
	*rpcm_file = pcm_file;
	return 0;
}

int snd_pcm_open(struct inode *inode, struct file *file)
{
	int cardnum = SNDRV_MINOR_CARD(minor(inode->i_rdev));
	int device = SNDRV_MINOR_DEVICE(minor(inode->i_rdev));
	int err;
	snd_pcm_t *pcm;
	snd_pcm_file_t *pcm_file;
	wait_queue_t wait;

	snd_runtime_check(device >= SNDRV_MINOR_PCM_PLAYBACK && device < SNDRV_MINOR_DEVICES, return -ENXIO);
	pcm = snd_pcm_devices[(cardnum * SNDRV_PCM_DEVICES) + (device % SNDRV_MINOR_PCMS)];
	if (pcm == NULL) {
		err = -ENODEV;
		goto __error1;
	}
	err = snd_card_file_add(pcm->card, file);
	if (err < 0)
		goto __error1;
	if (!try_module_get(pcm->card->module)) {
		err = -EFAULT;
		goto __error2;
	}
	init_waitqueue_entry(&wait, current);
	add_wait_queue(&pcm->open_wait, &wait);
	while (1) {
		down(&pcm->open_mutex);
		err = snd_pcm_open_file(file, pcm, device >= SNDRV_MINOR_PCM_CAPTURE ? SNDRV_PCM_STREAM_CAPTURE : SNDRV_PCM_STREAM_PLAYBACK, &pcm_file);
		if (err >= 0)
			break;
		up(&pcm->open_mutex);
		if (err == -EAGAIN) {
			if (file->f_flags & O_NONBLOCK) {
				err = -EBUSY;
				break;
			}
		} else
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&pcm->open_wait, &wait);
	if (err < 0)
		goto __error;
	up(&pcm->open_mutex);
	return err;

      __error:
	module_put(pcm->card->module);
      __error2:
      	snd_card_file_remove(pcm->card, file);
      __error1:
      	return err;
}

int snd_pcm_release(struct inode *inode, struct file *file)
{
	snd_pcm_t *pcm;
	snd_pcm_substream_t *substream;
	snd_pcm_file_t *pcm_file;

	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, return -ENXIO);
	substream = pcm_file->substream;
	snd_assert(substream != NULL, return -ENXIO);
	snd_assert(!atomic_read(&substream->runtime->mmap_count), );
	pcm = substream->pcm;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_pcm_playback_drop(substream);
	else
		snd_pcm_capture_drop(substream);
	fasync_helper(-1, file, 0, &substream->runtime->fasync);
	down(&pcm->open_mutex);
	snd_pcm_release_file(pcm_file);
	up(&pcm->open_mutex);
	wake_up(&pcm->open_wait);
	module_put(pcm->card->module);
	snd_card_file_remove(pcm->card, file);
	return 0;
}

snd_pcm_sframes_t snd_pcm_playback_rewind(snd_pcm_substream_t *substream, snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_sframes_t appl_ptr;
	snd_pcm_sframes_t ret;
	snd_pcm_sframes_t hw_avail;

	if (frames == 0)
		return 0;

	spin_lock_irq(&runtime->lock);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
		break;
	case SNDRV_PCM_STATE_DRAINING:
	case SNDRV_PCM_STATE_RUNNING:
		if (snd_pcm_update_hw_ptr(substream) >= 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_XRUN:
		ret = -EPIPE;
		goto __end;
	default:
		ret = -EBADFD;
		goto __end;
	}

	hw_avail = snd_pcm_playback_hw_avail(runtime);
	if (hw_avail <= 0) {
		ret = 0;
		goto __end;
	}
	if (frames > (snd_pcm_uframes_t)hw_avail)
		frames = hw_avail;
	else
		frames -= frames % runtime->xfer_align;
	appl_ptr = runtime->control->appl_ptr - frames;
	if (appl_ptr < 0)
		appl_ptr += runtime->boundary;
	runtime->control->appl_ptr = appl_ptr;
	if (runtime->status->state == SNDRV_PCM_STATE_RUNNING &&
	    runtime->sleep_min)
		snd_pcm_tick_prepare(substream);
	ret = frames;
 __end:
	spin_unlock_irq(&runtime->lock);
	return ret;
}

snd_pcm_sframes_t snd_pcm_capture_rewind(snd_pcm_substream_t *substream, snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_sframes_t appl_ptr;
	snd_pcm_sframes_t ret;
	snd_pcm_sframes_t hw_avail;

	if (frames == 0)
		return 0;

	spin_lock_irq(&runtime->lock);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_DRAINING:
		break;
	case SNDRV_PCM_STATE_RUNNING:
		if (snd_pcm_update_hw_ptr(substream) >= 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_XRUN:
		ret = -EPIPE;
		goto __end;
	default:
		ret = -EBADFD;
		goto __end;
	}

	hw_avail = snd_pcm_capture_hw_avail(runtime);
	if (hw_avail <= 0) {
		ret = 0;
		goto __end;
	}
	if (frames > (snd_pcm_uframes_t)hw_avail)
		frames = hw_avail;
	else
		frames -= frames % runtime->xfer_align;
	appl_ptr = runtime->control->appl_ptr - frames;
	if (appl_ptr < 0)
		appl_ptr += runtime->boundary;
	runtime->control->appl_ptr = appl_ptr;
	if (runtime->status->state == SNDRV_PCM_STATE_RUNNING &&
	    runtime->sleep_min)
		snd_pcm_tick_prepare(substream);
	ret = frames;
 __end:
	spin_unlock_irq(&runtime->lock);
	return ret;
}

snd_pcm_sframes_t snd_pcm_playback_forward(snd_pcm_substream_t *substream, snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_sframes_t appl_ptr;
	snd_pcm_sframes_t ret;
	snd_pcm_sframes_t avail;

	if (frames == 0)
		return 0;

	spin_lock_irq(&runtime->lock);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
		break;
	case SNDRV_PCM_STATE_DRAINING:
	case SNDRV_PCM_STATE_RUNNING:
		if (snd_pcm_update_hw_ptr(substream) >= 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_XRUN:
		ret = -EPIPE;
		goto __end;
	default:
		ret = -EBADFD;
		goto __end;
	}

	avail = snd_pcm_playback_avail(runtime);
	if (avail <= 0) {
		ret = 0;
		goto __end;
	}
	if (frames > (snd_pcm_uframes_t)avail)
		frames = avail;
	else
		frames -= frames % runtime->xfer_align;
	appl_ptr = runtime->control->appl_ptr + frames;
	if (appl_ptr >= (snd_pcm_sframes_t)runtime->boundary)
		appl_ptr -= runtime->boundary;
	runtime->control->appl_ptr = appl_ptr;
	if (runtime->status->state == SNDRV_PCM_STATE_RUNNING &&
	    runtime->sleep_min)
		snd_pcm_tick_prepare(substream);
	ret = frames;
 __end:
	spin_unlock_irq(&runtime->lock);
	return ret;
}

snd_pcm_sframes_t snd_pcm_capture_forward(snd_pcm_substream_t *substream, snd_pcm_uframes_t frames)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_pcm_sframes_t appl_ptr;
	snd_pcm_sframes_t ret;
	snd_pcm_sframes_t avail;

	if (frames == 0)
		return 0;

	spin_lock_irq(&runtime->lock);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_DRAINING:
	case SNDRV_PCM_STATE_PAUSED:
		break;
	case SNDRV_PCM_STATE_RUNNING:
		if (snd_pcm_update_hw_ptr(substream) >= 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_XRUN:
		ret = -EPIPE;
		goto __end;
	default:
		ret = -EBADFD;
		goto __end;
	}

	avail = snd_pcm_capture_avail(runtime);
	if (avail <= 0) {
		ret = 0;
		goto __end;
	}
	if (frames > (snd_pcm_uframes_t)avail)
		frames = avail;
	else
		frames -= frames % runtime->xfer_align;
	appl_ptr = runtime->control->appl_ptr + frames;
	if (appl_ptr >= (snd_pcm_sframes_t)runtime->boundary)
		appl_ptr -= runtime->boundary;
	runtime->control->appl_ptr = appl_ptr;
	if (runtime->status->state == SNDRV_PCM_STATE_RUNNING &&
	    runtime->sleep_min)
		snd_pcm_tick_prepare(substream);
	ret = frames;
 __end:
	spin_unlock_irq(&runtime->lock);
	return ret;
}

static int snd_pcm_hwsync(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;

	spin_lock_irq(&runtime->lock);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_DRAINING:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			goto __badfd;
	case SNDRV_PCM_STATE_RUNNING:
		if ((err = snd_pcm_update_hw_ptr(substream)) < 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_SUSPENDED:
		err = 0;
		break;
	case SNDRV_PCM_STATE_XRUN:
		err = -EPIPE;
		break;
	default:
	      __badfd:
		err = -EBADFD;
		break;
	}
	spin_unlock_irq(&runtime->lock);
	return err;
}
		
static int snd_pcm_delay(snd_pcm_substream_t *substream, snd_pcm_sframes_t *res)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	int err;
	snd_pcm_sframes_t n = 0;

	spin_lock_irq(&runtime->lock);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_DRAINING:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			goto __badfd;
	case SNDRV_PCM_STATE_RUNNING:
		if ((err = snd_pcm_update_hw_ptr(substream)) < 0)
			break;
		/* Fall through */
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_SUSPENDED:
		err = 0;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			n = snd_pcm_playback_hw_avail(runtime);
		else
			n = snd_pcm_capture_avail(runtime);
		break;
	case SNDRV_PCM_STATE_XRUN:
		err = -EPIPE;
		break;
	default:
	      __badfd:
		err = -EBADFD;
		break;
	}
	spin_unlock_irq(&runtime->lock);
	if (!err)
		if (put_user(n, res))
			err = -EFAULT;
	return err;
}
		
static int snd_pcm_playback_ioctl1(snd_pcm_substream_t *substream,
				   unsigned int cmd, void *arg);
static int snd_pcm_capture_ioctl1(snd_pcm_substream_t *substream,
				  unsigned int cmd, void *arg);

static int snd_pcm_common_ioctl1(snd_pcm_substream_t *substream,
				 unsigned int cmd, void *arg)
{
	snd_assert(substream != NULL, return -ENXIO);

	switch (cmd) {
	case SNDRV_PCM_IOCTL_PVERSION:
		return put_user(SNDRV_PCM_VERSION, (int *)arg) ? -EFAULT : 0;
	case SNDRV_PCM_IOCTL_INFO:
		return snd_pcm_info_user(substream, (snd_pcm_info_t *) arg);
	case SNDRV_PCM_IOCTL_TSTAMP:
	{
		int xarg;
		if (get_user(xarg, (int *) arg))
			return -EFAULT;
		substream->runtime->tstamp_timespec = xarg ? 1 : 0;
		return 0;
	}
	case SNDRV_PCM_IOCTL_HW_REFINE:
		return snd_pcm_hw_refine_user(substream, (snd_pcm_hw_params_t *) arg);
	case SNDRV_PCM_IOCTL_HW_PARAMS:
		return snd_pcm_hw_params_user(substream, (snd_pcm_hw_params_t *) arg);
	case SNDRV_PCM_IOCTL_HW_FREE:
		return snd_pcm_hw_free(substream);
	case SNDRV_PCM_IOCTL_SW_PARAMS:
		return snd_pcm_sw_params_user(substream, (snd_pcm_sw_params_t *) arg);
	case SNDRV_PCM_IOCTL_STATUS:
		return snd_pcm_status_user(substream, (snd_pcm_status_t *) arg);
	case SNDRV_PCM_IOCTL_CHANNEL_INFO:
		return snd_pcm_channel_info(substream, (snd_pcm_channel_info_t *) arg);
	case SNDRV_PCM_IOCTL_PREPARE:
		return snd_pcm_prepare(substream);
	case SNDRV_PCM_IOCTL_RESET:
		return snd_pcm_reset(substream);
	case SNDRV_PCM_IOCTL_START:
	{
		int res;
		spin_lock_irq(&substream->runtime->lock);
		res = snd_pcm_start(substream);
		spin_unlock_irq(&substream->runtime->lock);
		return res;
	}
	case SNDRV_PCM_IOCTL_LINK:
		return snd_pcm_link(substream, (long) arg);
	case SNDRV_PCM_IOCTL_UNLINK:
		return snd_pcm_unlink(substream);
	case SNDRV_PCM_IOCTL_RESUME:
		return snd_pcm_resume(substream);
	case SNDRV_PCM_IOCTL_XRUN:
		return snd_pcm_xrun(substream);
	case SNDRV_PCM_IOCTL_HWSYNC:
		return snd_pcm_hwsync(substream);
	case SNDRV_PCM_IOCTL_DELAY:
		return snd_pcm_delay(substream, (snd_pcm_sframes_t *) arg);
	case SNDRV_PCM_IOCTL_HW_REFINE_OLD:
		return snd_pcm_hw_refine_old_user(substream, (struct sndrv_pcm_hw_params_old *) arg);
	case SNDRV_PCM_IOCTL_HW_PARAMS_OLD:
		return snd_pcm_hw_params_old_user(substream, (struct sndrv_pcm_hw_params_old *) arg);
	}
	snd_printd("unknown ioctl = 0x%x\n", cmd);
	return -ENOTTY;
}

static int snd_pcm_playback_ioctl1(snd_pcm_substream_t *substream,
				   unsigned int cmd, void *arg)
{
	snd_assert(substream != NULL, return -ENXIO);
	snd_assert(substream->stream == SNDRV_PCM_STREAM_PLAYBACK, return -EINVAL);
	switch (cmd) {
	case SNDRV_PCM_IOCTL_WRITEI_FRAMES:
	{
		snd_xferi_t xferi, *_xferi = arg;
		snd_pcm_runtime_t *runtime = substream->runtime;
		snd_pcm_sframes_t result;
		if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
			return -EBADFD;
		if (put_user(0, &_xferi->result))
			return -EFAULT;
		if (copy_from_user(&xferi, _xferi, sizeof(xferi)))
			return -EFAULT;
		result = snd_pcm_lib_write(substream, xferi.buf, xferi.frames);
		__put_user(result, &_xferi->result);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_WRITEN_FRAMES:
	{
		snd_xfern_t xfern, *_xfern = arg;
		snd_pcm_runtime_t *runtime = substream->runtime;
		void *bufs;
		snd_pcm_sframes_t result;
		if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
			return -EBADFD;
		if (runtime->channels > 128)
			return -EINVAL;
		if (put_user(0, &_xfern->result))
			return -EFAULT;
		if (copy_from_user(&xfern, _xfern, sizeof(xfern)))
			return -EFAULT;
		bufs = kmalloc(sizeof(void *) * runtime->channels, GFP_KERNEL);
		if (bufs == NULL)
			return -ENOMEM;
		if (copy_from_user(bufs, xfern.bufs, sizeof(void *) * runtime->channels)) {
			kfree(bufs);
			return -EFAULT;
		}
		result = snd_pcm_lib_writev(substream, bufs, xfern.frames);
		kfree(bufs);
		__put_user(result, &_xfern->result);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_REWIND:
	{
		snd_pcm_uframes_t frames, *_frames = arg;
		snd_pcm_sframes_t result;
		if (get_user(frames, _frames))
			return -EFAULT;
		if (put_user(0, _frames))
			return -EFAULT;
		result = snd_pcm_playback_rewind(substream, frames);
		__put_user(result, _frames);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_FORWARD:
	{
		snd_pcm_uframes_t frames, *_frames = arg;
		snd_pcm_sframes_t result;
		if (get_user(frames, _frames))
			return -EFAULT;
		if (put_user(0, _frames))
			return -EFAULT;
		result = snd_pcm_playback_forward(substream, frames);
		__put_user(result, _frames);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_PAUSE:
	{
		int res;
		spin_lock_irq(&substream->runtime->lock);
		res = snd_pcm_pause(substream, (long) arg);
		spin_unlock_irq(&substream->runtime->lock);
		return res;
	}
	case SNDRV_PCM_IOCTL_DRAIN:
		return snd_pcm_playback_drain(substream);
	case SNDRV_PCM_IOCTL_DROP:
		return snd_pcm_playback_drop(substream);
	}
	return snd_pcm_common_ioctl1(substream, cmd, arg);
}

static int snd_pcm_capture_ioctl1(snd_pcm_substream_t *substream,
				  unsigned int cmd, void *arg)
{
	snd_assert(substream != NULL, return -ENXIO);
	snd_assert(substream->stream == SNDRV_PCM_STREAM_CAPTURE, return -EINVAL);
	switch (cmd) {
	case SNDRV_PCM_IOCTL_READI_FRAMES:
	{
		snd_xferi_t xferi, *_xferi = arg;
		snd_pcm_runtime_t *runtime = substream->runtime;
		snd_pcm_sframes_t result;
		if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
			return -EBADFD;
		if (put_user(0, &_xferi->result))
			return -EFAULT;
		if (copy_from_user(&xferi, _xferi, sizeof(xferi)))
			return -EFAULT;
		result = snd_pcm_lib_read(substream, xferi.buf, xferi.frames);
		__put_user(result, &_xferi->result);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_READN_FRAMES:
	{
		snd_xfern_t xfern, *_xfern = arg;
		snd_pcm_runtime_t *runtime = substream->runtime;
		void *bufs;
		snd_pcm_sframes_t result;
		if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
			return -EBADFD;
		if (runtime->channels > 128)
			return -EINVAL;
		if (put_user(0, &_xfern->result))
			return -EFAULT;
		if (copy_from_user(&xfern, _xfern, sizeof(xfern)))
			return -EFAULT;
		bufs = kmalloc(sizeof(void *) * runtime->channels, GFP_KERNEL);
		if (bufs == NULL)
			return -ENOMEM;
		if (copy_from_user(bufs, xfern.bufs, sizeof(void *) * runtime->channels)) {
			kfree(bufs);
			return -EFAULT;
		}
		result = snd_pcm_lib_readv(substream, bufs, xfern.frames);
		kfree(bufs);
		__put_user(result, &_xfern->result);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_REWIND:
	{
		snd_pcm_uframes_t frames, *_frames = arg;
		snd_pcm_sframes_t result;
		if (get_user(frames, _frames))
			return -EFAULT;
		if (put_user(0, _frames))
			return -EFAULT;
		result = snd_pcm_capture_rewind(substream, frames);
		__put_user(result, _frames);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_FORWARD:
	{
		snd_pcm_uframes_t frames, *_frames = arg;
		snd_pcm_sframes_t result;
		if (get_user(frames, _frames))
			return -EFAULT;
		if (put_user(0, _frames))
			return -EFAULT;
		result = snd_pcm_capture_forward(substream, frames);
		__put_user(result, _frames);
		return result < 0 ? result : 0;
	}
	case SNDRV_PCM_IOCTL_DRAIN:
		return snd_pcm_capture_drain(substream);
	case SNDRV_PCM_IOCTL_DROP:
		return snd_pcm_capture_drop(substream);
	}
	return snd_pcm_common_ioctl1(substream, cmd, arg);
}

static int snd_pcm_playback_ioctl(struct inode *inode, struct file *file,
				  unsigned int cmd, unsigned long arg)
{
	snd_pcm_file_t *pcm_file;

	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, return -ENXIO);

	if (((cmd >> 8) & 0xff) != 'A')
		return -ENOTTY;

	return snd_pcm_playback_ioctl1(pcm_file->substream, cmd, (void *) arg);
}

static int snd_pcm_capture_ioctl(struct inode *inode, struct file *file,
				 unsigned int cmd, unsigned long arg)
{
	snd_pcm_file_t *pcm_file;

	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, return -ENXIO);

	if (((cmd >> 8) & 0xff) != 'A')
		return -ENOTTY;

	return snd_pcm_capture_ioctl1(pcm_file->substream, cmd, (void *) arg);
}

int snd_pcm_kernel_playback_ioctl(snd_pcm_substream_t *substream,
				  unsigned int cmd, void *arg)
{
	mm_segment_t fs;
	int result;
	
	fs = snd_enter_user();
	result = snd_pcm_playback_ioctl1(substream, cmd, arg);
	snd_leave_user(fs);
	return result;
}

int snd_pcm_kernel_capture_ioctl(snd_pcm_substream_t *substream,
				 unsigned int cmd, void *arg)
{
	mm_segment_t fs;
	int result;
	
	fs = snd_enter_user();
	result = snd_pcm_capture_ioctl1(substream, cmd, arg);
	snd_leave_user(fs);
	return result;
}

int snd_pcm_kernel_ioctl(snd_pcm_substream_t *substream,
			 unsigned int cmd, void *arg)
{
	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		return snd_pcm_kernel_playback_ioctl(substream, cmd, arg);
	case SNDRV_PCM_STREAM_CAPTURE:
		return snd_pcm_kernel_capture_ioctl(substream, cmd, arg);
	default:
		return -EINVAL;
	}
}

static ssize_t snd_pcm_read(struct file *file, char *buf, size_t count, loff_t * offset)
{
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;
	snd_pcm_sframes_t result;

	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, return -ENXIO);
	substream = pcm_file->substream;
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	if (!frame_aligned(runtime, count))
		return -EINVAL;
	count = bytes_to_frames(runtime, count);
	result = snd_pcm_lib_read(substream, buf, count);
	if (result > 0)
		result = frames_to_bytes(runtime, result);
	return result;
}

static ssize_t snd_pcm_write(struct file *file, const char *buf, size_t count, loff_t * offset)
{
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;
	snd_pcm_sframes_t result;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 0)
	up(&file->f_dentry->d_inode->i_sem);
#endif
	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, result = -ENXIO; goto end);
	substream = pcm_file->substream;
	snd_assert(substream != NULL, result = -ENXIO; goto end);
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		result = -EBADFD;
		goto end;
	}
	if (!frame_aligned(runtime, count)) {
		result = -EINVAL;
		goto end;
	}
	count = bytes_to_frames(runtime, count);
	result = snd_pcm_lib_write(substream, buf, count);
	if (result > 0)
		result = frames_to_bytes(runtime, result);
 end:
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 0)
	down(&file->f_dentry->d_inode->i_sem);
#endif
	return result;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 3, 44)
static ssize_t snd_pcm_readv(struct file *file, const struct iovec *_vector,
			     unsigned long count, loff_t * offset)

{
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;
	snd_pcm_sframes_t result;
	unsigned long i;
	void **bufs;
	snd_pcm_uframes_t frames;

	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, return -ENXIO);
	substream = pcm_file->substream;
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	if (count > 1024 || count != runtime->channels)
		return -EINVAL;
	if (!frame_aligned(runtime, _vector->iov_len))
		return -EINVAL;
	frames = bytes_to_samples(runtime, _vector->iov_len);
	bufs = kmalloc(sizeof(void *) * count, GFP_KERNEL);
	if (bufs == NULL)
		return -ENOMEM;
	for (i = 0; i < count; ++i)
		bufs[i] = _vector[i].iov_base;
	result = snd_pcm_lib_readv(substream, bufs, frames);
	if (result > 0)
		result = frames_to_bytes(runtime, result);
	kfree(bufs);
	return result;
}

static ssize_t snd_pcm_writev(struct file *file, const struct iovec *_vector,
			      unsigned long count, loff_t * offset)
{
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;
	snd_pcm_sframes_t result;
	unsigned long i;
	void **bufs;
	snd_pcm_uframes_t frames;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 0)
	up(&file->f_dentry->d_inode->i_sem);
#endif
	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, result = -ENXIO; goto end);
	substream = pcm_file->substream;
	snd_assert(substream != NULL, result = -ENXIO; goto end);
	runtime = substream->runtime;
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN) {
		result = -EBADFD;
		goto end;
	}
	if (count > 128 || count != runtime->channels ||
	    !frame_aligned(runtime, _vector->iov_len)) {
		result = -EINVAL;
		goto end;
	}
	frames = bytes_to_samples(runtime, _vector->iov_len);
	bufs = kmalloc(sizeof(void *) * count, GFP_KERNEL);
	if (bufs == NULL)
		return -ENOMEM;
	for (i = 0; i < count; ++i)
		bufs[i] = _vector[i].iov_base;
	result = snd_pcm_lib_writev(substream, bufs, frames);
	if (result > 0)
		result = frames_to_bytes(runtime, result);
	kfree(bufs);
 end:
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 3, 0)
	down(&file->f_dentry->d_inode->i_sem);
#endif
	return result;
}
#endif

unsigned int snd_pcm_playback_poll(struct file *file, poll_table * wait)
{
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;
        unsigned int mask;
	snd_pcm_uframes_t avail;

	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, return 0);

	substream = pcm_file->substream;
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;

	poll_wait(file, &runtime->sleep, wait);

	spin_lock_irq(&runtime->lock);
	avail = snd_pcm_playback_avail(runtime);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
		if (avail >= runtime->control->avail_min) {
			mask = POLLOUT | POLLWRNORM;
			break;
		}
		/* Fall through */
	case SNDRV_PCM_STATE_DRAINING:
		mask = 0;
		break;
	default:
		mask = POLLOUT | POLLWRNORM | POLLERR;
		break;
	}
	spin_unlock_irq(&runtime->lock);
	return mask;
}

unsigned int snd_pcm_capture_poll(struct file *file, poll_table * wait)
{
	snd_pcm_file_t *pcm_file;
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;
        unsigned int mask;
	snd_pcm_uframes_t avail;

	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, return 0);

	substream = pcm_file->substream;
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;

	poll_wait(file, &runtime->sleep, wait);

	spin_lock_irq(&runtime->lock);
	avail = snd_pcm_capture_avail(runtime);
	switch (runtime->status->state) {
	case SNDRV_PCM_STATE_RUNNING:
	case SNDRV_PCM_STATE_PREPARED:
	case SNDRV_PCM_STATE_PAUSED:
		if (avail >= runtime->control->avail_min) {
			mask = POLLIN | POLLRDNORM;
			break;
		}
		mask = 0;
		break;
	case SNDRV_PCM_STATE_DRAINING:
		if (avail > 0) {
			mask = POLLIN | POLLRDNORM;
			break;
		}
		/* Fall through */
	default:
		mask = POLLIN | POLLRDNORM | POLLERR;
		break;
	}
	spin_unlock_irq(&runtime->lock);
	return mask;
}

#ifndef VM_RESERVED
#ifndef LINUX_2_2
static int snd_pcm_mmap_swapout(struct page * page, struct file * file)
#else
static int snd_pcm_mmap_swapout(struct vm_area_struct * area, struct page * page)
#endif
{
	return 0;
}
#endif

#ifndef LINUX_2_2
static struct page * snd_pcm_mmap_status_nopage(struct vm_area_struct *area, unsigned long address, int no_share)
#else
static unsigned long snd_pcm_mmap_status_nopage(struct vm_area_struct *area, unsigned long address, int no_share)
#endif
{
	snd_pcm_substream_t *substream = (snd_pcm_substream_t *)area->vm_private_data;
	snd_pcm_runtime_t *runtime;
	struct page * page;
	
	if (substream == NULL)
		return NOPAGE_OOM;
	runtime = substream->runtime;
	page = virt_to_page(runtime->status);
	get_page(page);
#ifndef LINUX_2_2
	return page;
#else
	return page_address(page);
#endif
}

static struct vm_operations_struct snd_pcm_vm_ops_status =
{
	.nopage =	snd_pcm_mmap_status_nopage,
#ifndef VM_RESERVED
	.swapout =	snd_pcm_mmap_swapout,
#endif
};

int snd_pcm_mmap_status(snd_pcm_substream_t *substream, struct file *file,
			struct vm_area_struct *area)
{
	snd_pcm_runtime_t *runtime;
	long size;
	if (!(area->vm_flags & VM_READ))
		return -EINVAL;
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -EAGAIN);
	size = area->vm_end - area->vm_start;
	if (size != PAGE_ALIGN(sizeof(snd_pcm_mmap_status_t)))
		return -EINVAL;
	area->vm_ops = &snd_pcm_vm_ops_status;
#ifndef LINUX_2_2
	area->vm_private_data = substream;
#else
	area->vm_private_data = (long)substream;	
#endif
#ifdef VM_RESERVED
	area->vm_flags |= VM_RESERVED;
#endif
	return 0;
}

#ifndef LINUX_2_2
static struct page * snd_pcm_mmap_control_nopage(struct vm_area_struct *area, unsigned long address, int no_share)
#else
static unsigned long snd_pcm_mmap_control_nopage(struct vm_area_struct *area, unsigned long address, int no_share)
#endif
{
	snd_pcm_substream_t *substream = (snd_pcm_substream_t *)area->vm_private_data;
	snd_pcm_runtime_t *runtime;
	struct page * page;
	
	if (substream == NULL)
		return NOPAGE_OOM;
	runtime = substream->runtime;
	page = virt_to_page(runtime->control);
	get_page(page);
#ifndef LINUX_2_2
	return page;
#else
	return page_address(page);
#endif
}

static struct vm_operations_struct snd_pcm_vm_ops_control =
{
	.nopage =	snd_pcm_mmap_control_nopage,
#ifndef VM_RESERVED
	.swapout =	snd_pcm_mmap_swapout,
#endif
};

static int snd_pcm_mmap_control(snd_pcm_substream_t *substream, struct file *file,
				struct vm_area_struct *area)
{
	snd_pcm_runtime_t *runtime;
	long size;
	if (!(area->vm_flags & VM_READ))
		return -EINVAL;
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -EAGAIN);
	size = area->vm_end - area->vm_start;
	if (size != PAGE_ALIGN(sizeof(snd_pcm_mmap_control_t)))
		return -EINVAL;
	area->vm_ops = &snd_pcm_vm_ops_control;
#ifndef LINUX_2_2
	area->vm_private_data = substream;
#else
	area->vm_private_data = (long)substream;	
#endif
#ifdef VM_RESERVED
	area->vm_flags |= VM_RESERVED;
#endif
	return 0;
}

static void snd_pcm_mmap_data_open(struct vm_area_struct *area)
{
	snd_pcm_substream_t *substream = (snd_pcm_substream_t *)area->vm_private_data;
	atomic_inc(&substream->runtime->mmap_count);
}

static void snd_pcm_mmap_data_close(struct vm_area_struct *area)
{
	snd_pcm_substream_t *substream = (snd_pcm_substream_t *)area->vm_private_data;
	atomic_dec(&substream->runtime->mmap_count);
}

#ifndef LINUX_2_2
static struct page * snd_pcm_mmap_data_nopage(struct vm_area_struct *area, unsigned long address, int no_share)
#else
static unsigned long snd_pcm_mmap_data_nopage(struct vm_area_struct *area, unsigned long address, int no_share)
#endif
{
	snd_pcm_substream_t *substream = (snd_pcm_substream_t *)area->vm_private_data;
	snd_pcm_runtime_t *runtime;
	unsigned long offset;
	struct page * page;
	void *vaddr;
	size_t dma_bytes;
	
	if (substream == NULL)
		return NOPAGE_OOM;
	runtime = substream->runtime;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 3, 25)
	offset = area->vm_pgoff << PAGE_SHIFT;
#else
	offset = area->vm_offset;
#endif
	offset += address - area->vm_start;
	snd_assert((offset % PAGE_SIZE) == 0, return NOPAGE_OOM);
	dma_bytes = PAGE_ALIGN(runtime->dma_bytes);
	if (offset > dma_bytes - PAGE_SIZE)
		return NOPAGE_SIGBUS;
	if (substream->ops->page) {
		page = substream->ops->page(substream, offset);
		if (! page)
			return NOPAGE_OOM;
	} else {
		vaddr = runtime->dma_area + offset;
		page = virt_to_page(vaddr);
	}
	get_page(page);
#ifndef LINUX_2_2
	return page;
#else
	return page_address(page);
#endif
}

static struct vm_operations_struct snd_pcm_vm_ops_data =
{
	.open =		snd_pcm_mmap_data_open,
	.close =	snd_pcm_mmap_data_close,
	.nopage =	snd_pcm_mmap_data_nopage,
#ifndef VM_RESERVED
	.swapout =	snd_pcm_mmap_swapout,
#endif
};

int snd_pcm_mmap_data(snd_pcm_substream_t *substream, struct file *file,
		      struct vm_area_struct *area)
{
	snd_pcm_runtime_t *runtime;
	long size;
	unsigned long offset;
	size_t dma_bytes;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (!(area->vm_flags & (VM_WRITE|VM_READ)))
			return -EINVAL;
	} else {
		if (!(area->vm_flags & VM_READ))
			return -EINVAL;
	}
	runtime = substream->runtime;
	snd_assert(runtime != NULL, return -EAGAIN);
	if (runtime->status->state == SNDRV_PCM_STATE_OPEN)
		return -EBADFD;
	if (!(runtime->info & SNDRV_PCM_INFO_MMAP))
		return -ENXIO;
	if (runtime->access == SNDRV_PCM_ACCESS_RW_INTERLEAVED ||
	    runtime->access == SNDRV_PCM_ACCESS_RW_NONINTERLEAVED)
		return -EINVAL;
	size = area->vm_end - area->vm_start;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 3, 25)
	offset = area->vm_pgoff << PAGE_SHIFT;
#else
	offset = area->vm_offset;
#endif
	dma_bytes = PAGE_ALIGN(runtime->dma_bytes);
	if ((size_t)size > dma_bytes)
		return -EINVAL;
	if (offset > dma_bytes - size)
		return -EINVAL;

	area->vm_ops = &snd_pcm_vm_ops_data;
#ifndef LINUX_2_2
	area->vm_private_data = substream;
#else
	area->vm_private_data = (long)substream;
#endif
#ifdef VM_RESERVED
	area->vm_flags |= VM_RESERVED;
#endif
	atomic_inc(&runtime->mmap_count);
	return 0;
}

static int snd_pcm_mmap(struct file *file, struct vm_area_struct *area)
{
	snd_pcm_file_t * pcm_file;
	snd_pcm_substream_t *substream;	
	unsigned long offset;
	
	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, return -ENXIO);
	substream = pcm_file->substream;
	snd_assert(substream != NULL, return -ENXIO);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 3, 25)
	offset = area->vm_pgoff << PAGE_SHIFT;
#else
	offset = area->vm_offset;
#endif
	switch (offset) {
	case SNDRV_PCM_MMAP_OFFSET_STATUS:
		return snd_pcm_mmap_status(substream, file, area);
	case SNDRV_PCM_MMAP_OFFSET_CONTROL:
		return snd_pcm_mmap_control(substream, file, area);
	default:
		return snd_pcm_mmap_data(substream, file, area);
	}
	return 0;
}

static int snd_pcm_fasync(int fd, struct file * file, int on)
{
	snd_pcm_file_t * pcm_file;
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;
	int err;

	pcm_file = snd_magic_cast(snd_pcm_file_t, file->private_data, return -ENXIO);
	substream = pcm_file->substream;
	snd_assert(substream != NULL, return -ENXIO);
	runtime = substream->runtime;

	err = fasync_helper(fd, file, on, &runtime->fasync);
	if (err < 0)
		return err;
	return 0;
}

/*
 *  To be removed helpers to keep binary compatibility
 */

#define __OLD_TO_NEW_MASK(x) ((x&7)|((x&0x07fffff8)<<5))
#define __NEW_TO_OLD_MASK(x) ((x&7)|((x&0xffffff00)>>5))

static void snd_pcm_hw_convert_from_old_params(snd_pcm_hw_params_t *params, struct sndrv_pcm_hw_params_old *oparams)
{
	unsigned int i;

	memset(params, 0, sizeof(*params));
	params->flags = oparams->flags;
	for (i = 0; i < sizeof(oparams->masks) / sizeof(unsigned int); i++)
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

static void snd_pcm_hw_convert_to_old_params(struct sndrv_pcm_hw_params_old *oparams, snd_pcm_hw_params_t *params)
{
	unsigned int i;

	memset(oparams, 0, sizeof(*oparams));
	oparams->flags = params->flags;
	for (i = 0; i < sizeof(oparams->masks) / sizeof(unsigned int); i++)
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

static int snd_pcm_hw_refine_old_user(snd_pcm_substream_t * substream, struct sndrv_pcm_hw_params_old * _oparams)
{
	snd_pcm_hw_params_t params;
	struct sndrv_pcm_hw_params_old oparams;
	int err;
	if (copy_from_user(&oparams, _oparams, sizeof(oparams)))
		return -EFAULT;
	snd_pcm_hw_convert_from_old_params(&params, &oparams);
	err = snd_pcm_hw_refine(substream, &params);
	snd_pcm_hw_convert_to_old_params(&oparams, &params);
	if (copy_to_user(_oparams, &oparams, sizeof(oparams)))
		return -EFAULT;
	return err;
}

static int snd_pcm_hw_params_old_user(snd_pcm_substream_t * substream, struct sndrv_pcm_hw_params_old * _oparams)
{
	snd_pcm_hw_params_t params;
	struct sndrv_pcm_hw_params_old oparams;
	int err;
	if (copy_from_user(&oparams, _oparams, sizeof(oparams)))
		return -EFAULT;
	snd_pcm_hw_convert_from_old_params(&params, &oparams);
	err = snd_pcm_hw_params(substream, &params);
	snd_pcm_hw_convert_to_old_params(&oparams, &params);
	if (copy_to_user(_oparams, &oparams, sizeof(oparams)))
		return -EFAULT;
	return err;
}

/*
 *  Register section
 */

static struct file_operations snd_pcm_f_ops_playback = {
#ifndef LINUX_2_2
	.owner =	THIS_MODULE,
#endif
	.write =	snd_pcm_write,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 3, 44)
	.writev =	snd_pcm_writev,
#endif
	.open =		snd_pcm_open,
	.release =	snd_pcm_release,
	.poll =		snd_pcm_playback_poll,
	.ioctl =	snd_pcm_playback_ioctl,
	.mmap =		snd_pcm_mmap,
	.fasync =	snd_pcm_fasync,
};

static struct file_operations snd_pcm_f_ops_capture = {
#ifndef LINUX_2_2
	.owner =	THIS_MODULE,
#endif
	.read =		snd_pcm_read,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 3, 44)
	.readv =	snd_pcm_readv,
#endif
	.open =		snd_pcm_open,
	.release =	snd_pcm_release,
	.poll =		snd_pcm_capture_poll,
	.ioctl =	snd_pcm_capture_ioctl,
	.mmap =		snd_pcm_mmap,
	.fasync =	snd_pcm_fasync,
};

snd_minor_t snd_pcm_reg[2] =
{
	{
		.comment =	"digital audio playback",
		.f_ops =	&snd_pcm_f_ops_playback,
	},
	{
		.comment =	"digital audio capture",
		.f_ops =	&snd_pcm_f_ops_capture,
	}
};
