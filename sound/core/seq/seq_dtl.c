/*
 * DTL(e) event converter
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

typedef struct dtl_out {
	int out_mtp_network;
	unsigned int time_format;
	unsigned int full_frame_count;
	unsigned char sysex[11];
} dtl_out_t;

typedef struct dtl_in {
	unsigned int time_format;
	unsigned int cur_pos;
} dtl_in_t;


static int dtl_open_out(queue_sync_t *sync_info, seq_sync_arg_t *retp)
{
	dtl_out_t *arg;

	if (sync_info->time_format >= 4)
		return -EINVAL;
	if ((arg = kmalloc(sizeof(*arg), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	arg->out_mtp_network = sync_info->opt_info[0];
	arg->full_frame_count = sync_info->opt_info[1];
	arg->time_format = sync_info->time_format;
	sync_info->param.time.subframes = 1; /* MTC uses quarter frame */
	sync_info->param.time.resolution = snd_seq_get_smpte_resolution(arg->time_format);
	*retp = arg;
	return 0;
}

static int dtl_open_in(queue_sync_t *sync_info, seq_sync_arg_t *retp)
{
	dtl_in_t *arg;

	if (sync_info->time_format >= 4)
		return -EINVAL;
	if ((arg = kmalloc(sizeof(*arg), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	arg->time_format = sync_info->time_format;
	arg->cur_pos = 0;
	sync_info->param.time.subframes = 1; /* MTC uses quarter frame */
	sync_info->param.time.resolution = snd_seq_get_smpte_resolution(arg->time_format);
	*retp = arg;
	return 0;
}


/* decode sync position */
static int sync_pos_out(dtl_out_t *arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	sndrv_seq_time_frame_t cur_out;
	unsigned char *buf = arg->sysex;

	if (arg->time_format != src->data.queue.sync_time_format)
		return -EINVAL;

	cur_out = snd_seq_position_to_time_frame(arg->time_format, 1, src->data.queue.param.position);
	buf[0] = 0xf0; /* SYSEX */
	buf[1] = 0x00; /* MOTU */
	buf[2] = 0x33; /* MOTU */
	buf[3] = 0x7f;
	buf[4] = 0x0c; /* DTL full frame */
	buf[5] = arg->out_mtp_network; /* 0x00 or 0x08 */
	buf[6] = cur_out.hour | (arg->time_format << 5);
	buf[7] = cur_out.min;
	buf[8] = cur_out.sec;
	buf[9] = cur_out.frame;
	buf[10] = 0xf7;

	ev->type = SNDRV_SEQ_EVENT_SYSEX;
	ev->flags &= ~SNDRV_SEQ_EVENT_LENGTH_MASK;
	ev->flags |= SNDRV_SEQ_EVENT_LENGTH_VARIABLE;
	ev->data.ext.len = 11;
	ev->data.ext.ptr = buf;

	return 1;
}

/* decode sync signal */
static int sync_out(dtl_out_t *arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	unsigned int pos;

	if (arg->time_format != src->data.queue.sync_time_format)
		return -EINVAL;
	pos = src->data.queue.param.position;
	if (arg->full_frame_count &&
	    (pos % arg->full_frame_count) == 0)
		/* send full frame */
		return sync_pos_out(arg, src, ev);
	ev->type = SNDRV_SEQ_EVENT_CLOCK;
	return 1;
}

static int dtl_sync_out(seq_sync_arg_t _arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	dtl_out_t *arg = _arg;
	switch (src->type) {
	case SNDRV_SEQ_EVENT_SYNC:
		return sync_out(arg, src, ev);
	case SNDRV_SEQ_EVENT_SYNC_POS:
		return sync_pos_out(arg, src, ev);
	}
	return 0;
}

/* decode sync position */
static int sync_pos_in(dtl_in_t *arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	unsigned time_format;
	static unsigned char id[] = {
		0xf0, 0x00, 0x33, 0x7f, 0x0c,
	};
	sndrv_seq_time_frame_t cur_in;
	char buf[11];

	if (snd_seq_expand_var_event(src, 11, buf, 1, 0) != 11)
		return 0;
	if (memcmp(buf, id, sizeof(id)) != 0)
		return 0;
	time_format = (buf[6] >> 5) & 3;
	if (time_format != arg->time_format)
		return -EINVAL;
	cur_in.hour = buf[6] & 0x1f;
	cur_in.min = buf[7];
	cur_in.sec = buf[8];
	cur_in.frame = buf[9];
	arg->cur_pos = snd_seq_time_frame_to_position(time_format, 1, &cur_in);

	ev->type = SNDRV_SEQ_EVENT_SYNC_POS;
	ev->data.queue.sync_time_format = time_format;
	ev->data.queue.param.position = arg->cur_pos;

	return 1;
}

static int dtl_sync_in(seq_sync_arg_t _arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	dtl_in_t *arg = _arg;
	switch (src->type) {
	case SNDRV_SEQ_EVENT_CLOCK:
		arg->cur_pos++;
		ev->type = SNDRV_SEQ_EVENT_SYNC;
		ev->data.queue.param.position = arg->cur_pos;
		return 1;
	case SNDRV_SEQ_EVENT_SYSEX:
		return sync_pos_in(arg, src, ev);
	}
	return 0;
}

/* exported */
seq_sync_parser_t snd_seq_dtl_parser = {
	format: SNDRV_SEQ_SYNC_FMT_DTL,
	in: {
		open: dtl_open_in,
		sync: dtl_sync_in,
	},
	out: {
		open: dtl_open_out,
		sync: dtl_sync_out,
	},
};

#endif /* SNDRV_SEQ_SYNC_SUPPORT */
