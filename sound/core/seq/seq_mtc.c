/*
 * MTC event converter
 *
 *  Copyright (c) 2000 by Takashi Iwai <tiwai@suse.de>
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
#include <sound/core.h>
#include "seq_queue.h"

#ifdef SNDRV_SEQ_SYNC_SUPPORT

typedef struct mtc_out {
	int out_channel;
	unsigned int time_format;
	sndrv_seq_time_frame_t cur_time;
	unsigned int decode_offset;
	unsigned char sysex[10];
} mtc_out_t;

typedef struct mtc_in {
	unsigned int time_format;
	sndrv_seq_time_frame_t cur_time;
	unsigned int cur_pos;
	int prev_in_offset;
} mtc_in_t;


static int mtc_open_out(queue_sync_t *sync_info, seq_sync_arg_t *retp)
{
	mtc_out_t *arg;

	if (sync_info->time_format >= 4)
		return -EINVAL;
	if ((arg = kmalloc(sizeof(*arg), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	arg->out_channel = sync_info->opt_info[0];
	if (arg->out_channel == 0)
		arg->out_channel = 127;
	arg->time_format = sync_info->time_format;
	sync_info->param.time.subframes = 4; /* MTC uses quarter frame */
	sync_info->param.time.resolution = snd_seq_get_smpte_resolution(arg->time_format);
	memset(&arg->cur_time, 0, sizeof(arg->cur_time));
	*retp = arg;
	return 0;
}

static int mtc_open_in(queue_sync_t *sync_info, seq_sync_arg_t *retp)
{
	mtc_in_t *arg;

	if (sync_info->time_format >= 4)
		return -EINVAL;
	if ((arg = kmalloc(sizeof(*arg), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	arg->time_format = sync_info->time_format;
	memset(&arg->cur_time, 0, sizeof(arg->cur_time));
	arg->cur_pos = 0;
	arg->prev_in_offset = -1;
	sync_info->param.time.subframes = 4; /* MTC uses quarter frame */
	sync_info->param.time.resolution = snd_seq_get_smpte_resolution(arg->time_format);
	*retp = arg;
	return 0;
}


/* decode sync signal */
static int sync_out(mtc_out_t *arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	int val, offset;

	if (arg->time_format != src->data.queue.sync_time_format)
		return -EINVAL;
	offset = (src->data.queue.param.position + arg->decode_offset) % 8;
	if (offset == 0) {
		/* convert and remember the current time
		   for the following 7 MTC quarter frames */
		arg->cur_time = snd_seq_position_to_time_frame(arg->time_format, 4, src->data.queue.param.position);
	}
	switch (offset) {
	case 0: val = arg->cur_time.frame & 0x0f; break;
	case 1: val = (arg->cur_time.frame >> 4) & 0x0f; break;
	case 2: val = arg->cur_time.sec & 0x0f; break;
	case 3: val = (arg->cur_time.sec >> 4) & 0x0f; break;
	case 4: val = arg->cur_time.min & 0x0f; break;
	case 5: val = (arg->cur_time.min >> 4) & 0x0f; break;
	case 6: val = arg->cur_time.hour & 0x0f; break;
	case 7:
	default:
		val = ((arg->cur_time.hour >> 4) & 0x01) | (arg->time_format << 1);
		break;
	}
	val |= (offset << 4);
	ev->type = SNDRV_SEQ_EVENT_QFRAME;
	ev->data.control.value = val;
	return 1;
}

/* decode sync position */
static int sync_pos_out(mtc_out_t *arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	unsigned int pos;
	unsigned char *buf = arg->sysex;

	if (arg->time_format != src->data.queue.sync_time_format)
		return -EINVAL;

	pos = src->data.queue.param.position; /* quarter frames */
	arg->decode_offset = pos & 4;
	pos /= 4;
	arg->cur_time = snd_seq_position_to_time_frame(arg->time_format, 4, pos);

	buf[0] = 0xf0; /* SYSEX */
	buf[1] = 0x7f;
	buf[2] = arg->out_channel;
	buf[3] = 0x01;
	buf[4] = 0x01;
	buf[5] = arg->cur_time.hour | (arg->time_format << 5);
	buf[6] = arg->cur_time.min;
	buf[7] = arg->cur_time.sec;
	buf[8] = arg->cur_time.frame;
	buf[9] = 0xf7;

	ev->type = SNDRV_SEQ_EVENT_SYSEX;
	ev->flags &= ~SNDRV_SEQ_EVENT_LENGTH_MASK;
	ev->flags |= SNDRV_SEQ_EVENT_LENGTH_VARIABLE;
	ev->data.ext.len = 10;
	ev->data.ext.ptr = buf;

	return 1;
}

static int mtc_sync_out(seq_sync_arg_t _arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	mtc_out_t *arg = _arg;
	switch (src->type) {
	case SNDRV_SEQ_EVENT_SYNC:
		return sync_out(arg, src, ev);
	case SNDRV_SEQ_EVENT_SYNC_POS:
		return sync_pos_out(arg, src, ev);
	}
	return 0;
}

/* decode sync signal */
static int sync_in(mtc_in_t *arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	int val, offset;
	unsigned int time_format;

	offset = (src->data.control.value & 0x70) >> 4;
	val = src->data.control.value & 0x0f;
	if (offset > 0 && offset != arg->prev_in_offset + 1) {
		/* bad quarter frame message - something missing.. */
		arg->prev_in_offset = -1; /* wait for next 0 */
		return -EINVAL;
	}
	switch (offset) {
	case 0: arg->cur_time.frame =  val; break;
	case 1: arg->cur_time.frame |= (val << 4); break;
	case 2: arg->cur_time.sec = val; break;
	case 3: arg->cur_time.sec |= (val << 4); break;
	case 4: arg->cur_time.min = val; break;
	case 5: arg->cur_time.min |= (val << 4); break;
	case 6: arg->cur_time.hour = val; break;
	case 7:
	default:
		arg->cur_time.hour |= (val & 1) << 4;
		time_format = (val >> 1) & 3;
		if (time_format != arg->time_format)
			return -EINVAL;
		arg->cur_pos = snd_seq_time_frame_to_position(time_format, 4, &arg->cur_time);
		arg->cur_pos += 7; /* correct the receive time */
		break;
	}

	ev->type = SNDRV_SEQ_EVENT_SYNC;
	ev->data.queue.sync_time_format = arg->time_format;
	ev->data.queue.param.position = arg->cur_pos;
	arg->cur_pos++;

	return 1; /* composed */
}

/* decode sync position */
static int sync_pos_in(mtc_in_t *arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	unsigned time_format;
	char buf[10];

	if (snd_seq_expand_var_event(src, 10, buf, 1, 0) != 10)
		return 0;
	if (buf[1] != 0x7f || buf[3] != 0x01 || buf[4] != 0x01)
		return 0;
	time_format = (buf[5] >> 5) & 3;
	if (time_format != arg->time_format)
		return -EINVAL;
	arg->cur_time.hour = buf[5] & 0x1f;
	arg->cur_time.min = buf[6];
	arg->cur_time.sec = buf[7];
	arg->cur_time.frame = buf[8];
	arg->cur_pos = snd_seq_time_frame_to_position(time_format, 4, &arg->cur_time);

	ev->type = SNDRV_SEQ_EVENT_SYNC_POS;
	ev->data.queue.sync_time_format = time_format;
	ev->data.queue.param.position = arg->cur_pos;

	return 1; /* composed */
}

static int mtc_sync_in(seq_sync_arg_t _arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	mtc_in_t *arg = _arg;
	switch (src->type) {
	case SNDRV_SEQ_EVENT_QFRAME:
		return sync_in(arg, src, ev);
	case SNDRV_SEQ_EVENT_SYSEX:
		return sync_pos_in(arg, src, ev);
	}
	return 0;
}

/* exported */
seq_sync_parser_t snd_seq_mtc_parser = {
	format: SNDRV_SEQ_SYNC_FMT_MTC,
	in: {
		open: mtc_open_in,
		sync: mtc_sync_in,
	},
	out: {
		open: mtc_open_out,
		sync: mtc_sync_out,
	},
};

#endif /* SNDRV_SEQ_SYNC_SUPPORT */
