/*
 * OSS compatible sequencer driver
 *
 * read/write/select interface to device file
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include "seq_oss_device.h"
#include "seq_oss_readq.h"
#include "seq_oss_writeq.h"
#include "seq_oss_synth.h"
#include <sound/seq_oss_legacy.h>
#include "seq_oss_event.h"
#include "seq_oss_timer.h"
#include "../seq_clientmgr.h"


/*
 * protoypes
 */
static int insert_queue(seq_oss_devinfo_t *dp, evrec_t *rec, struct file *opt);


/*
 * read interface
 */

int
snd_seq_oss_read(seq_oss_devinfo_t *dp, char __user *buf, int count)
{
	seq_oss_readq_t *readq = dp->readq;
	int cnt, pos;
	evrec_t *q;
	unsigned long flags;

	if (readq == NULL || ! is_read_mode(dp->file_mode))
		return -EIO;

	/* copy queued events to read buffer */
	cnt = count;
	pos = 0;
	q = snd_seq_oss_readq_pick(readq, !is_nonblock_mode(dp->file_mode), &flags);
	if (q == NULL)
		return 0;
	do {
		int ev_len;
		/* tansfer the data */
		ev_len = ev_length(q);
		if (copy_to_user(buf + pos, q, ev_len)) {
			snd_seq_oss_readq_unlock(readq, flags);
			break;
		}
		snd_seq_oss_readq_free(readq, flags);
		pos += ev_len;
		cnt -= ev_len;
		if (cnt < ev_len)
			break;
	} while ((q = snd_seq_oss_readq_pick(readq, 0, &flags)) != NULL);

	return count - cnt;
}


/*
 * write interface
 */

int
snd_seq_oss_write(seq_oss_devinfo_t *dp, const char __user *buf, int count, struct file *opt)
{
	int rc, c, p, ev_size;
	evrec_t rec;

	if (! is_write_mode(dp->file_mode) || dp->writeq == NULL)
		return -EIO;

	c = count;
	p = 0;
	while (c >= SHORT_EVENT_SIZE) {
		if (copy_from_user(rec.c, buf + p, SHORT_EVENT_SIZE))
			break;
		p += SHORT_EVENT_SIZE;

		if (rec.s.code == SEQ_FULLSIZE) {
			/* load patch */
			int fmt = (*(unsigned short *)rec.c) & 0xffff;
			return snd_seq_oss_synth_load_patch(dp, rec.s.dev, fmt, buf, p, c);

		}
		if (ev_is_long(&rec)) {
			/* extended code */
			if (rec.s.code == SEQ_EXTENDED &&
			    dp->seq_mode == SNDRV_SEQ_OSS_MODE_MUSIC)
				return -EINVAL;
			ev_size = LONG_EVENT_SIZE;
			if (c < ev_size)
				break;
			/* copy the reset 4 bytes */
			if (copy_from_user(rec.c + SHORT_EVENT_SIZE, buf + p,
					   LONG_EVENT_SIZE - SHORT_EVENT_SIZE))
				break;
			p += LONG_EVENT_SIZE - SHORT_EVENT_SIZE;

		} else {
			/* old-type code */
			if (dp->seq_mode == SNDRV_SEQ_OSS_MODE_MUSIC)
				return -EINVAL;
			ev_size = SHORT_EVENT_SIZE;
		}

		/* insert queue */
		if ((rc = insert_queue(dp, &rec, opt)) < 0)
			break;

		c -= ev_size;
	}

	if (count == c && is_nonblock_mode(dp->file_mode))
		return -EAGAIN;
	return count - c;
}


/*
 * insert event record to write queue
 * return: 0 = OK, non-zero = NG
 */
static int
insert_queue(seq_oss_devinfo_t *dp, evrec_t *rec, struct file *opt)
{
	int rc = 0;
	snd_seq_event_t event;

	/* if this is a timing event, process the current time */
	if (snd_seq_oss_process_timer_event(dp->timer, rec))
		return 0; /* no need to insert queue */

	/* parse this event */
	memset(&event, 0, sizeof(event));
	/* set dummy -- to be sure */
	event.type = SNDRV_SEQ_EVENT_NOTEOFF;
	snd_seq_oss_fill_addr(dp, &event, dp->addr.port, dp->addr.client);

	if (snd_seq_oss_process_event(dp, rec, &event))
		return 0; /* invalid event - no need to insert queue */

	event.time.tick = snd_seq_oss_timer_cur_tick(dp->timer);
	if (dp->timer->realtime || !dp->timer->running) {
		snd_seq_oss_dispatch(dp, &event, 0, 0);
	} else {
		if (is_nonblock_mode(dp->file_mode))
			rc = snd_seq_kernel_client_enqueue(dp->cseq, &event, 0, 0);
		else
			rc = snd_seq_kernel_client_enqueue_blocking(dp->cseq, &event, opt, 0, 0);
	}
	return rc;
}
		

/*
 * select / poll
 */
  
unsigned int
snd_seq_oss_poll(seq_oss_devinfo_t *dp, struct file *file, poll_table * wait)
{
	unsigned int mask = 0;

	/* input */
	if (dp->readq && is_read_mode(dp->file_mode)) {
		if (snd_seq_oss_readq_poll(dp->readq, file, wait))
			mask |= POLLIN | POLLRDNORM;
	}

	/* output */
	if (dp->writeq && is_write_mode(dp->file_mode)) {
		if (snd_seq_kernel_client_write_poll(dp->cseq, file, wait))
			mask |= POLLOUT | POLLWRNORM;
	}
	return mask;
}
