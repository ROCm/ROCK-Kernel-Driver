/*
 * OSS compatible sequencer driver
 * read fifo queue
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

#ifndef __SEQ_OSS_READQ_H
#define __SEQ_OSS_READQ_H

#include "seq_oss_device.h"


/*
 * definition of read queue
 */
struct seq_oss_readq_t {
	evrec_t *q;
	int qlen;
	int maxlen;
	int head, tail;
	unsigned long pre_event_timeout;
	unsigned long input_time;
	wait_queue_head_t midi_sleep;
	spinlock_t lock;
};

seq_oss_readq_t *snd_seq_oss_readq_new(seq_oss_devinfo_t *dp, int maxlen);
void snd_seq_oss_readq_delete(seq_oss_readq_t *q);
void snd_seq_oss_readq_clear(seq_oss_readq_t *readq);
unsigned int snd_seq_oss_readq_poll(seq_oss_readq_t *readq, struct file *file, poll_table *wait);
int snd_seq_oss_readq_puts(seq_oss_readq_t *readq, int dev, unsigned char *data, int len);
int snd_seq_oss_readq_put_event(seq_oss_readq_t *readq, evrec_t *ev);
int snd_seq_oss_readq_put_timestamp(seq_oss_readq_t *readq, unsigned long curt, int seq_mode);
evrec_t *snd_seq_oss_readq_pick(seq_oss_readq_t *q, int blocking, unsigned long *rflags);
void snd_seq_oss_readq_unlock(seq_oss_readq_t *q, unsigned long flags);
void snd_seq_oss_readq_free(seq_oss_readq_t *q, unsigned long flags);

#endif
