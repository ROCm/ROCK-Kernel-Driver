/*
 *  Synchronization of ALSA sequencer queues
 *
 *   Copyright (c) 2000 by Takashi Iwai <tiwai@suse.de>
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
#ifndef __SND_SEQ_SYNC_H
#define __SND_SEQ_SYNC_H

typedef struct snd_queue_sync queue_sync_t;
typedef struct snd_seq_sync_parser seq_sync_parser_t;
typedef void *seq_sync_arg_t;

struct snd_queue_sync {
	unsigned char format;
	unsigned char time_format;
	unsigned char opt_info[6];	/* optional info */
	snd_seq_addr_t addr;		/* master/slave address */

	unsigned int counter;		/* current position */
	unsigned long resolution;	/* resolution for time */
	snd_seq_real_time_t cur_time;	/* current time */
	seq_timer_tick_t sync_tick;		/* tick info */

	union {
		struct sndrv_seq_queue_tick_sync tick;
		struct sndrv_seq_queue_time_sync time;
	} param;

	seq_sync_parser_t *parser;
	seq_sync_arg_t parser_arg;

	struct list_head list;
};


struct seq_sync_parser_ops {
	int (*open)(queue_sync_t *sync_info, seq_sync_arg_t *retp);
	int (*sync)(seq_sync_arg_t arg, const snd_seq_event_t *src, snd_seq_event_t *ev);
	int (*close)(seq_sync_arg_t arg);
};

struct snd_seq_sync_parser {
	unsigned int format;	/* supported format */
	struct seq_sync_parser_ops in;	/* sync-in (slave) */
	struct seq_sync_parser_ops out;	/* sync-out (mastering) */
};

/*
 * prototypes
 */
int snd_seq_sync_create_port(queue_t *queue);
int snd_seq_sync_delete_port(queue_t *queue);
void snd_seq_sync_clear(queue_t *q);
void snd_seq_sync_update_tempo(queue_t *q);
void snd_seq_sync_update_tick(queue_t *q, int master_only, int atomic, int hop);
void snd_seq_sync_update_time(queue_t *q, int master_only, int atomic, int hop);
void snd_seq_sync_check(queue_t *q, unsigned long resolution, int atomic, int hop);

sndrv_seq_time_frame_t snd_seq_position_to_time_frame(int format, unsigned int nsubs, unsigned int pos);
unsigned int snd_seq_time_frame_to_position(int format, unsigned int nsubs, sndrv_seq_time_frame_t *rtime);
unsigned long snd_seq_get_smpte_resolution(int time_format);


#endif
