/*
 *  MIDI clock event converter
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

typedef struct midi_clock {
	unsigned int cur_pos;
} midi_clock_t;

static int midi_open(queue_sync_t *sync_info, seq_sync_arg_t *retp)
{
	midi_clock_t *arg;

	if ((arg = kmalloc(sizeof(*arg), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	sync_info->param.tick.ppq = 24;
	sync_info->param.tick.ticks = 1;
	arg->cur_pos = 0;
	*retp = arg;
	return 0;
}

static int midi_sync_out(seq_sync_arg_t _arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	switch (src->type) {
	case SNDRV_SEQ_EVENT_SYNC:
		ev->type = SNDRV_SEQ_EVENT_CLOCK;
		return 1;
	case SNDRV_SEQ_EVENT_SYNC_POS:
		ev->type = SNDRV_SEQ_EVENT_SONGPOS;
		ev->data.control.value = src->data.queue.param.position / 6;
		return 1;
	}
	return 0;
}

static int midi_sync_in(seq_sync_arg_t _arg, const snd_seq_event_t *src, snd_seq_event_t *ev)
{
	midi_clock_t *arg = _arg;
	switch (src->type) {
	case SNDRV_SEQ_EVENT_CLOCK:
		ev->type = SNDRV_SEQ_EVENT_SYNC;
		ev->data.queue.param.position = arg->cur_pos;
		arg->cur_pos++;
		return 1;
	case SNDRV_SEQ_EVENT_SONGPOS:
		ev->type = SNDRV_SEQ_EVENT_SYNC_POS;
		arg->cur_pos = src->data.control.value * 6;
		ev->data.queue.param.position = arg->cur_pos;
		return 1;
	}
	return 0;
}

/* exported */
seq_sync_parser_t snd_seq_midi_clock_parser = {
	format: SNDRV_SEQ_SYNC_FMT_MIDI_CLOCK,
	in: {
		open: midi_open,
		sync: midi_sync_in,
	},
	out: {
		open: midi_open,
		sync: midi_sync_out,
	},
};

#endif /* SNDRV_SEQ_SYNC_SUPPORT */
