/*
 *   ALSA sequencer queue synchronization routine
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

#define __NO_VERSION__
#include <sound/driver.h>
#include <sound/core.h>

#include "seq_memory.h"
#include "seq_queue.h"
#include "seq_clientmgr.h"
#include "seq_fifo.h"
#include "seq_timer.h"
#include "seq_info.h"

#ifdef SNDRV_SEQ_SYNC_SUPPORT

#define FOR_EACH_LIST(var,list) \
for (var = (list)->next; var != list; var = var->next)

/*
 * callbacks
 */
static int event_input_sync(snd_seq_event_t * ev, int direct, void *private_data, int atomic, int hop);
static int queue_master_add(void *private_data, snd_seq_port_subscribe_t *subs);
static int queue_master_remove(void *private_data, snd_seq_port_subscribe_t *subs);
static void queue_delete_all_masters(queue_t *q);
static int queue_slave_set(void *private_data, snd_seq_port_subscribe_t *subs);
static int queue_slave_reset(void *private_data, snd_seq_port_subscribe_t *subs);
static void queue_sync_close_parser(queue_sync_t *sync, int slave);

/*
 * pre-defined event parsers
 */

extern seq_sync_parser_t snd_seq_midi_clock_parser; /* seq_midi_clock.c */
extern seq_sync_parser_t snd_seq_mtc_parser; /* seq_mtc.c */
extern seq_sync_parser_t snd_seq_dtl_parser; /* seq_dtl.c */

static seq_sync_parser_t *event_parsers[] = {
	&snd_seq_midi_clock_parser,
	&snd_seq_mtc_parser,
	&snd_seq_dtl_parser,
	NULL
};

/*
 * create a sync port corresponding to the specified queue
 */
int snd_seq_sync_create_port(queue_t *queue)
{
	snd_seq_port_info_t port;
	snd_seq_port_callback_t pcallbacks;

	memset(&pcallbacks, 0, sizeof(pcallbacks));
	memset(&port, 0, sizeof(port));
	pcallbacks.owner = THIS_MODULE;
	pcallbacks.event_input = event_input_sync;
	pcallbacks.subscribe = queue_master_add;
	pcallbacks.unsubscribe = queue_master_remove;
	pcallbacks.use = queue_slave_set;
	pcallbacks.unuse = queue_slave_reset;
	pcallbacks.private_data = queue;
	pcallbacks.callback_all = 1; /* call callbacks at each subscription */
	port.capability = SNDRV_SEQ_PORT_CAP_READ|SNDRV_SEQ_PORT_CAP_SUBS_READ|
		SNDRV_SEQ_PORT_CAP_WRITE|SNDRV_SEQ_PORT_CAP_SUBS_WRITE|
		SNDRV_SEQ_PORT_CAP_DUPLEX|
		SNDRV_SEQ_PORT_CAP_SYNC_READ|SNDRV_SEQ_PORT_CAP_SYNC_WRITE;
	port.type = 0;
	sprintf(port.name, "Sync Queue %d", queue->queue);
	port.kernel = &pcallbacks;
	port.flags = SNDRV_SEQ_PORT_FLG_GIVEN_PORT;
	port.port = snd_seq_queue_sync_port(queue->queue);
	if (snd_seq_kernel_client_ctl(SNDRV_SEQ_CLIENT_SYSTEM, SNDRV_SEQ_IOCTL_CREATE_PORT, &port) < 0)
		return -ENOMEM;
	queue->sync_port.client = SNDRV_SEQ_CLIENT_SYSTEM;
	queue->sync_port.port = port.port;
	return 0;
}

/*
 * delete attached sync port to the queue
 */
int snd_seq_sync_delete_port(queue_t *queue)
{
	snd_seq_port_info_t port;

	memset(&port, 0, sizeof(port));
	port.client = queue->sync_port.client;
	port.port = queue->sync_port.port;
	if (snd_seq_kernel_client_ctl(SNDRV_SEQ_CLIENT_SYSTEM, SNDRV_SEQ_IOCTL_DELETE_PORT, &port) < 0)
		return -ENOMEM;
	queue_delete_all_masters(queue);
	queue_sync_close_parser(&queue->slave, 1);
	return 0;
}


/*
 * send a sync signal to the sync slave client
 */
static void queue_send_sync_event(queue_t *q, queue_sync_t *master, int type, int atomic, int hop)
{
	snd_seq_event_t event;

	memset(&event, 0, sizeof(event));
	
	event.flags = SNDRV_SEQ_TIME_MODE_ABS;
	/* since we use direct delivery, we have to convert time stamp here.. */
	switch (master->format & SNDRV_SEQ_SYNC_MODE) {
	case SNDRV_SEQ_SYNC_TICK:
		event.flags |= SNDRV_SEQ_TIME_STAMP_TICK;
		event.time.tick = q->timer->tick.cur_tick;
		break;
	case SNDRV_SEQ_SYNC_TIME:
		event.flags |= SNDRV_SEQ_TIME_STAMP_REAL;
		event.time.time = q->timer->cur_time;
		break;
	}
	event.type = type;
	event.data.queue.queue = q->queue;
	event.data.queue.sync_format = master->format;
	event.data.queue.sync_time_format = master->time_format;
	event.data.queue.param.position = master->counter;
	event.source = q->sync_port;
	event.dest = master->addr;
	if (master->parser) {
		snd_seq_event_t newev;
		newev = event;
		if (master->parser->out.sync(master->parser_arg, &event, &newev) > 0) {
			snd_seq_kernel_client_dispatch(SNDRV_SEQ_CLIENT_SYSTEM, &newev, atomic, hop);
			return;
		}
	}
	snd_seq_kernel_client_dispatch(SNDRV_SEQ_CLIENT_SYSTEM, &event, atomic, hop);
}

/*
 * initialize the sync position
 */
static void queue_sync_clear(queue_sync_t *sync)
{
	memset(&sync->cur_time, 0, sizeof(sync->cur_time));
	sync->counter = 0;
	sync->sync_tick.cur_tick = 0;
	sync->sync_tick.fraction = 0;
}

/*
 * initialize all sync positions
 */
void snd_seq_sync_clear(queue_t *q)
{
	struct list_head *head;

	/* clear master positions */
	read_lock(&q->master_lock);
	FOR_EACH_LIST(head, &q->master_head) {
		queue_sync_t *master = list_entry(head, queue_sync_t, list);
		queue_sync_clear(master);
	}
	read_unlock(&q->master_lock);
	read_lock(&q->slave_lock);
	queue_sync_clear(&q->slave);
	read_unlock(&q->slave_lock);
}


/*
 * change tick resolution of sync master/slave
 */
static void queue_sync_set_tick_resolution(queue_t *q, queue_sync_t *sync)
{
	unsigned int tempo, ppq;
	tempo = q->timer->tempo;
	if (sync->param.tick.ppq == 0)
		ppq = q->timer->ppq;
	else
		ppq = sync->param.tick.ppq;
	snd_seq_timer_set_tick_resolution(&sync->sync_tick, tempo, ppq, sync->param.tick.ticks);
}


/*
 * update sync-master resolutions
 */
void snd_seq_sync_update_tempo(queue_t *q)
{
	struct list_head *head;

	read_lock(&q->master_lock);
	FOR_EACH_LIST(head, &q->master_head) {
		queue_sync_t *master = list_entry(head, queue_sync_t, list);
		if (master->format & SNDRV_SEQ_SYNC_TICK)
			queue_sync_set_tick_resolution(q, master);
	}
	read_unlock(&q->master_lock);
	read_lock(&q->slave_lock);
	if (q->slave.format & SNDRV_SEQ_SYNC_TICK)
		queue_sync_set_tick_resolution(q, &q->slave);
	read_unlock(&q->slave_lock);
}


/*
 * change the tick position from the current tick of the queue
 */
static void queue_sync_change_tick(queue_t *q, queue_sync_t *sync)
{
	if (sync->param.tick.ppq == 0)
		sync->counter = q->timer->tick.cur_tick;
	else
		sync->counter = (q->timer->tick.cur_tick * sync->param.tick.ppq) / q->timer->ppq;
	sync->counter /= sync->param.tick.ticks;
	sync->sync_tick.cur_tick = sync->counter;
	sync->sync_tick.fraction = 0;
}

/*
 * change the time position from the current time of the queue
 */
static void queue_sync_change_time(queue_t *q, queue_sync_t *sync)
{
	/* we need 64bit calculation here.. */	
	u64 nsec;

	nsec = q->timer->cur_time.tv_sec;
	nsec *= 1000000000UL;
	nsec += q->timer->cur_time.tv_nsec;
	u64_div(nsec, sync->param.time.resolution, sync->counter);
	sync->counter *= sync->param.time.subframes;
	sync->cur_time = q->timer->cur_time;
}


/*
 * update the tick position of all sync
 */
void snd_seq_sync_update_tick(queue_t *q, int master_only, int atomic, int hop)
{
	struct list_head *head;

	read_lock(&q->master_lock);
	FOR_EACH_LIST(head, &q->master_head) {
		queue_sync_t *master = list_entry(head, queue_sync_t, list);
		if (master->format & SNDRV_SEQ_SYNC_TICK) {
			queue_sync_change_tick(q, master);
			queue_send_sync_event(q, master, SNDRV_SEQ_EVENT_SYNC_POS, atomic, hop); /* broadcast to client */
		}
	}
	read_unlock(&q->master_lock);
	if (master_only)
		return;
	read_lock(&q->slave_lock);
	if (q->slave.format & SNDRV_SEQ_SYNC_TICK)
		queue_sync_change_tick(q, &q->slave);
	read_unlock(&q->slave_lock);
}

/*
 * update the time position of all sync
 */
void snd_seq_sync_update_time(queue_t *q, int master_only, int atomic, int hop)
{
	struct list_head *head;

	read_lock(&q->master_lock);
	FOR_EACH_LIST(head, &q->master_head) {
		queue_sync_t *master = list_entry(head, queue_sync_t, list);
		if (master->format & SNDRV_SEQ_SYNC_TIME) {
			queue_sync_change_time(q, master);
			queue_send_sync_event(q, master, SNDRV_SEQ_EVENT_SYNC_POS, atomic, hop);
		}
	}
	read_unlock(&q->master_lock);
	if (master_only)
		return;
	read_lock(&q->slave_lock);
	if (q->slave.format & SNDRV_SEQ_SYNC_TIME)
		queue_sync_change_time(q, &q->slave);
	read_unlock(&q->slave_lock);
}


/*
 * check the current timer value and send sync messages if the sync
 * time is elapsed
 */
static void queue_master_check(queue_t *q, unsigned long ticks, int atomic, int hop)
{
	struct list_head *head;

	read_lock(&q->master_lock);
	FOR_EACH_LIST(head, &q->master_head) {
		queue_sync_t *master = list_entry(head, queue_sync_t, list);
		switch (master->format & SNDRV_SEQ_SYNC_MODE) {
		case SNDRV_SEQ_SYNC_TICK:
			snd_seq_timer_update_tick(&master->sync_tick, ticks);
			while (master->sync_tick.cur_tick >= master->counter) {
				queue_send_sync_event(q, master, SNDRV_SEQ_EVENT_SYNC, atomic, hop);
				master->counter++;
			}
			break;
		case SNDRV_SEQ_SYNC_TIME:
			while (snd_seq_compare_real_time(&q->timer->cur_time, &master->cur_time)) {
				queue_send_sync_event(q, master, SNDRV_SEQ_EVENT_SYNC, atomic, hop);
				snd_seq_inc_time_nsec(&master->cur_time, master->resolution);
				master->counter++;
			}
			break;
		}
	}
	read_unlock(&q->master_lock);
}


/*
 * slave stuff
 */

/*
 * update tick
 */
static void queue_slave_check(queue_t *q, unsigned long ticks)
{
	switch (q->slave.format & SNDRV_SEQ_SYNC_MODE) {
	case SNDRV_SEQ_SYNC_TICK:
		snd_seq_timer_update_tick(&q->slave.sync_tick, ticks);
		break;
	case SNDRV_SEQ_SYNC_TIME:
		/* nothing */
		break;
	}
}

/*
 * slave synchronization in real-time unit
 */
static int queue_slave_sync_time(queue_t *q, unsigned int position)
{
	struct timeval tm;
	long diff_time, new_period;
	queue_sync_t *sync = &q->slave;
	sndrv_seq_queue_time_sync_t *p = &sync->param.time;
	seq_timer_t *tmr = q->timer;
	u64 external_counter, tmp;

	do_gettimeofday(&tm);
	if (tmr->sync_start) {
		/* XXX: we should use 64bit for diff_time, too. */
		diff_time = (tm.tv_sec - tmr->sync_last_tm.tv_sec) * 1000000 +
			((long)tm.tv_usec - (long)tmr->sync_last_tm.tv_usec);
		diff_time = (p->x0 * tmr->sync_time_diff + p->x1 * (diff_time * 1000)) / (p->x0 + p->x1);
#define MIN_DIFF_TIME	1000	/* 1ms minimum */
		if (diff_time < MIN_DIFF_TIME)
			diff_time = MIN_DIFF_TIME;
		tmr->sync_time_diff = diff_time;
		tmp = (u64)tmr->base_period * (u64)sync->resolution;
		u64_div(tmp, diff_time, new_period);

		/* phase adjustment */
		external_counter = position;
		external_counter *= sync->resolution;

		/* calculate current time */
		tmp = snd_seq_timer_get_cur_nsec(tmr, &tm);

		if (external_counter > tmp) {
			tmp = external_counter - tmp;
			if (tmp < p->max_time_diff) {
				/* locked */
				int hz = p->phase_correct_time / tmr->base_period;
				diff_time = (u32)tmp;
				diff_time /= hz;
				new_period += diff_time;
				q->flags &= ~SNDRV_SEQ_QUEUE_FLG_SYNC_LOST;
			}
		} else {
			tmp = tmp - external_counter;
			if (tmp == 0)
				q->flags &= ~SNDRV_SEQ_QUEUE_FLG_SYNC_LOST;
			else if (tmp < p->max_time_diff) {
				/* locked */
				int hz = p->phase_correct_time / tmr->base_period;
				diff_time = (u32)tmp;
				diff_time /= hz;
				if (new_period - diff_time > MIN_DIFF_TIME) {
					new_period -= diff_time;
					q->flags &= ~SNDRV_SEQ_QUEUE_FLG_SYNC_LOST;
				} else
					q->flags |= SNDRV_SEQ_QUEUE_FLG_SYNC_LOST;
			}
		}
		tmr->period = new_period;
	} else {
		tmr->sync_start = 1;
		tmr->sync_time_diff = sync->resolution;
	}
	tmr->sync_last_tm = tm;
	sync->counter = position;

	return 0;
}

/*
 * slave synchronization in tick unit
 */
static int queue_slave_sync_tick(queue_t *q, unsigned int position)
{
	struct timeval tm;
	long diff_time, tick_diff;
	unsigned int tick_time;
	queue_sync_t *sync = &q->slave;
	seq_timer_t *tmr = q->timer;
	sndrv_seq_queue_tick_sync_t *p = &sync->param.tick;

	do_gettimeofday(&tm);
	if (tmr->sync_start) {
		/* XXX: diff_time should be 64bit for enough long sync period.. */
		diff_time = (tm.tv_sec - tmr->sync_last_tm.tv_sec) * 1000000 +
			((long)tm.tv_usec - (long)tmr->sync_last_tm.tv_usec);
		diff_time *= 1000; /* in nsec */
		tick_time = (p->x0 * sync->sync_tick.resolution +
			     p->x1 * diff_time) / (p->x0 + p->x1);
		/* phase adjustment */
		tick_diff = (long)position - (long)sync->sync_tick.cur_tick;
		if (tick_diff != 0) {
			if (tick_diff >= -p->max_tick_diff &&
			    tick_diff <= p->max_tick_diff) {
				/* locked */
				q->flags &= ~SNDRV_SEQ_QUEUE_FLG_SYNC_LOST;
				tick_time = (tick_time * p->max_tick_diff2) /
					(p->max_tick_diff2 + tick_diff);
			} else {
				/* sync lost.. freewheeling */
				q->flags |= SNDRV_SEQ_QUEUE_FLG_SYNC_LOST;
			}
		} else
			q->flags &= ~SNDRV_SEQ_QUEUE_FLG_SYNC_LOST;

#define MIN_TICK_TIME	1000	/* 1ms */
		if (tick_time < MIN_TICK_TIME)
			tick_time = MIN_TICK_TIME;

		sync->sync_tick.resolution = tick_time;
		snd_seq_timer_update_tick(&sync->sync_tick, 0);
		if (p->ppq)
			tmr->tick.resolution = (tick_time * p->ppq) / tmr->ppq;
		else
			tmr->tick.resolution = tick_time;
		snd_seq_timer_update_tick(&tmr->tick, 0);
		tmr->tempo = (tmr->tick.resolution * tmr->ppq) / 1000;

	} else 
		tmr->sync_start = 1;
	tmr->sync_last_tm = tm;

	sync->counter = position;

	return 0;
}


/*
 */
static void queue_slave_jump_to_time(queue_t *q, unsigned int position, int atomic, int hop)
{
	u64 nsec;
	queue_sync_t *sync = &q->slave;

	q->slave.counter = position;
	nsec = sync->counter;
	nsec *= sync->resolution;
	u64_divmod(nsec, 1000000000, sync->cur_time.tv_sec, sync->cur_time.tv_nsec);
	q->timer->cur_time = sync->cur_time;

	/* update master */
	snd_seq_sync_update_time(q, 1, atomic, hop);
}

static void queue_slave_jump_to_tick(queue_t *q, unsigned int position, int atomic, int hop)
{
	unsigned int tick;
	queue_sync_t *sync = &q->slave;

	sync->counter = position;
	sync->sync_tick.cur_tick = sync->counter;
	sync->sync_tick.fraction = 0;

	/* update queue timer */
	if (sync->param.tick.ppq == 0)
		tick = sync->counter;
	else
		tick = sync->counter * q->timer->ppq / sync->param.tick.ppq;
	q->timer->tick.cur_tick = tick * sync->param.tick.ticks;
	q->timer->tick.fraction = 0;

	/* update master */
	snd_seq_sync_update_tick(q, 1, atomic, hop);
}


/*
 * event input callback
 */
static int event_input_sync(snd_seq_event_t * ev, int direct, void *private_data, int atomic, int hop)
{
	queue_t *q = private_data;
	unsigned long flags;
	snd_seq_event_t newev;

	snd_assert(q != NULL, return -ENXIO);

	/* lock the queue owner access.. */
	spin_lock_irqsave(&q->owner_lock, flags);
	q->klocked = 1;
	spin_unlock_irqrestore(&q->owner_lock, flags);

	read_lock(&q->slave_lock);
	if (q->slave.format) {
		if (q->slave.parser) {
			memset(&newev, 0, sizeof(newev));
			if (q->slave.parser->in.sync(q->slave.parser_arg, ev, &newev) > 0)
				ev = &newev;
		}
	}
	if (ev->type == SNDRV_SEQ_EVENT_SYNC) {
		/* slave signal received */
		switch (q->slave.format & SNDRV_SEQ_SYNC_MODE) {
		case SNDRV_SEQ_SYNC_TICK:
			queue_slave_sync_tick(q, ev->data.queue.param.position);
			break;
		case SNDRV_SEQ_SYNC_TIME:
			queue_slave_sync_time(q, ev->data.queue.param.position);
			break;
		}
	} else if (ev->type == SNDRV_SEQ_EVENT_SYNC_POS) {
		/* jump to position */
		switch (q->slave.format & SNDRV_SEQ_SYNC_MODE) {
		case SNDRV_SEQ_SYNC_TICK:
			if (q->timer->running)
				queue_slave_sync_tick(q, ev->data.queue.param.position);
			else
				queue_slave_jump_to_tick(q, ev->data.queue.param.position, atomic, hop);
			break;
		case SNDRV_SEQ_SYNC_TIME:
			if (q->timer->running)
				queue_slave_sync_time(q, ev->data.queue.param.position);
			else
				queue_slave_jump_to_time(q, ev->data.queue.param.position, atomic, hop);
			break;
		}
	} else {
		/* control queue */
		snd_seq_queue_process_event(q, ev, 0, atomic, hop);
	}
	read_unlock(&q->slave_lock);

	/* unlock */
	spin_lock_irqsave(&q->owner_lock, flags);
	q->klocked = 0;
	spin_unlock_irqrestore(&q->owner_lock, flags);

	return 0;
}


/*
 * initialize sync parameters
 */
static int queue_param_init(queue_t *q, queue_sync_t *sync,
			    snd_seq_addr_t *addr, sndrv_seq_queue_sync_t *info,
			    int slave)
{
	seq_sync_parser_t *parser, **list;

	sync->format = info->format;
	sync->time_format = info->time_format;
	*sync->opt_info = *info->info;
	sync->addr = *addr;
	/* copy params */
	if (info->format&SNDRV_SEQ_SYNC_TICK)
	    sync->param.tick=info->param.tick;
	else
	    sync->param.time=info->param.time;

	sync->parser = NULL;
	sync->parser_arg = NULL;
	for (list = event_parsers; (parser = *list) != NULL; list++) {
		if (parser->format == sync->format) {
			int err;
			if (slave)
				err = parser->in.open(sync, &sync->parser_arg);
			else
				err = parser->out.open(sync, &sync->parser_arg);
			if (err < 0)
				return err;
			sync->parser = parser;
			break;
		}
	}

	switch (sync->format & SNDRV_SEQ_SYNC_MODE) {
	case SNDRV_SEQ_SYNC_TICK:
		if (sync->param.tick.ppq > 200)
			goto __error;
		if (sync->param.tick.ticks == 0)
			sync->param.tick.ticks = 1;
		queue_sync_set_tick_resolution(q, sync);
		/* sync slave parameters -- will be configurable */
		sync->param.tick.x0 = 4;
		sync->param.tick.x1 = 1;
		sync->param.tick.max_tick_diff = 50;
		sync->param.tick.max_tick_diff2 = sync->param.tick.max_tick_diff * 2;
		break;
	case SNDRV_SEQ_SYNC_TIME:
		sync->resolution = sync->param.time.resolution;
		if (sync->param.time.subframes == 0)
			goto __error;
		sync->resolution /= sync->param.time.subframes;
		if (sync->resolution < 1000000) /* minimum = 1ms */
			goto __error;
		/* sync slave parameters -- will be configurable */
		sync->param.time.x0 = 2;
		sync->param.time.x1 = 1;
		sync->param.time.max_time_diff = 1000000000UL; /* 1s */
		sync->param.time.phase_correct_time = 100000000UL; /* 0.1s */
		break;
	default:
		snd_printd("seq_sync: invalid format %x\n", sync->format);
		goto __error;
	}
	return 0;

__error:
	queue_sync_close_parser(sync, slave);
	return -EINVAL;
}


/*
 * close event parser if exists
 */
static void queue_sync_close_parser(queue_sync_t *sync, int slave)
{
	if (sync->parser == NULL)
		return;
	if (slave) {
		if (sync->parser->in.close)
			sync->parser->in.close(sync->parser_arg);
		else if (sync->parser_arg)
			kfree(sync->parser_arg);
	} else {
		if (sync->parser->out.close)
			sync->parser->out.close(sync->parser_arg);
		else if (sync->parser_arg)
			kfree(sync->parser_arg);
	}
	sync->parser = NULL;
	sync->parser_arg = NULL;
}


/*
 * add to master list
 */
static int queue_master_add(void *private_data, snd_seq_port_subscribe_t *subs)
{
	queue_t *q = private_data;
	queue_sync_t *master;
	unsigned long flags;
	int err;

	snd_assert(q != NULL, return -EINVAL);
	if (! subs->sync)
		return -EINVAL;
	master = snd_kcalloc(sizeof(*master), GFP_KERNEL);
	if (master == NULL)
		return -ENOMEM;
	err = queue_param_init(q, master, &subs->dest, &subs->opt.sync_info, 0);
	if (err < 0) {
		kfree(master);
		return err;
	}
	write_lock_irqsave(&q->master_lock, flags);
	list_add(&master->list, &q->master_head);
	write_unlock_irqrestore(&q->master_lock, flags);

	return 0;
}

/*
 * remove master
 */
static int queue_master_remove(void *private_data, snd_seq_port_subscribe_t *subs)
{
	queue_t *q = private_data;
	sndrv_seq_queue_sync_t *info;
	snd_seq_addr_t *addr;
	struct list_head *head;
	unsigned long flags;

	snd_assert(q != NULL, return -EINVAL);
	if (! subs->sync)
		return -EINVAL;
	info = &subs->opt.sync_info;
	addr = &subs->dest;

	write_lock_irqsave(&q->master_lock, flags);
	FOR_EACH_LIST(head, &q->master_head) {
		queue_sync_t *master = list_entry(head, queue_sync_t, list);
		if (master->format == info->format &&
		    master->addr.client == addr->client &&
		    master->addr.port == addr->port) {
			list_del(&master->list);
			write_unlock_irqrestore(&q->master_lock, flags);
			queue_sync_close_parser(master, 0);
			kfree(master);
			return 0;
		}
	}
	write_unlock_irqrestore(&q->master_lock, flags);
	snd_printd("seq_queue: can't find master from %d.%d format %0x\n", addr->client, addr->port, info->format);
	return -ENXIO;
}

/* remove all master connections if any exist */
static void queue_delete_all_masters(queue_t *q)
{
	struct list_head *head;
	unsigned long flags;

	write_lock_irqsave(&q->master_lock, flags);
	FOR_EACH_LIST(head, &q->master_head) {
		queue_sync_t *master = list_entry(head, queue_sync_t, list);
		list_del(&master->list);
		queue_sync_close_parser(master, 0);
		kfree(master);
	}
	write_unlock_irqrestore(&q->master_lock, flags);
}

/*
 * set slave mode
 */
static int queue_slave_set(void *private_data, snd_seq_port_subscribe_t *subs)
{
	queue_t *q = private_data;
	unsigned long flags;
	int err;

	snd_assert(q != NULL, return -EINVAL);
	if (! subs->sync)
		return -EINVAL;
	write_lock_irqsave(&q->slave_lock, flags);
	if (q->slave.format) {
		write_unlock_irqrestore(&q->slave_lock, flags);
		return -EBUSY;
	}
	err = queue_param_init(q, &q->slave, &subs->sender,
			       &subs->opt.sync_info, 1);
	if (err < 0) {
		q->slave.format = 0;
		write_unlock_irqrestore(&q->slave_lock, flags);
		return err;
	}
	write_unlock_irqrestore(&q->slave_lock, flags);
	return 0;
}

/*
 * remove slave mode
 */
static int queue_slave_reset(void *private_data, snd_seq_port_subscribe_t *subs)
{
	queue_t *q = private_data;
	unsigned long flags;

	snd_assert(q != NULL, return -EINVAL);
	if (! subs->sync)
		return -EINVAL;
	write_lock_irqsave(&q->slave_lock, flags);
	if (q->slave.addr.client == subs->sender.client &&
	    q->slave.addr.port == subs->sender.port) {
		q->slave.format = 0;
		queue_sync_close_parser(&q->slave, 1);
		write_unlock_irqrestore(&q->slave_lock, flags);
		return 0;
	}
	write_unlock_irqrestore(&q->slave_lock, flags);
	snd_printd("seq_queue: can't match slave condition\n");
	return -ENXIO;
}


/*
 * sync check
 * this function is called at each timer interrupt.
 */

void snd_seq_sync_check(queue_t *q, unsigned long resolution, int atomic, int hop)
{
	queue_master_check(q, resolution, atomic, hop);
	queue_slave_check(q, resolution);
}


/*
 * support functions for SMPTE time frame
 */
static unsigned int linear_time_to_position(sndrv_seq_time_frame_t time,
					    int nframes, int nsubs)
{
	unsigned int count;
	count = time.hour * 60 + time.min;
	count = count * 60 + time.sec;
	count = count * nframes + time.frame;
	count = count * nsubs + time.subframe;
	return count;
}

static sndrv_seq_time_frame_t linear_position_to_time(unsigned int count,
						      int nframes, int nsubs)
{
	sndrv_seq_time_frame_t time;
	time.subframe = count % nsubs;
	count /= nsubs;
	time.hour = count / (3600 * nframes);
	count %= 3600 * nframes;
	time.min = count / (60 * nframes);
	count %= 60 * nframes;
	time.sec = count / nframes;
	time.frame = count % nframes;
	return time;
}

/* drop frame - only 30fps */
#define NFRAMES		30
#define FRAMES_PER_MIN		(NFRAMES * 60 - 2)
#define FRAMES_PER_10MIN	(FRAMES_PER_MIN * 10 + 2)
#define FRAMES_PER_HOUR		(FRAMES_PER_10MIN * 6)

static unsigned int drop_time_to_position(sndrv_seq_time_frame_t time, int nsubs)
{
	unsigned int count, min;

	min = time.min % 10;
	count = time.frame;
	if (min > 0) {
		if (time.sec == 0 && time.frame < 2)
			count = 2;
	}
	count += time.sec * NFRAMES;
	count += min * FRAMES_PER_MIN;
	count += (time.min / 10) * FRAMES_PER_10MIN;
	count += time.hour * (FRAMES_PER_HOUR);
	count *= nsubs;
	count += time.subframe;

	return count;
}

static sndrv_seq_time_frame_t drop_position_to_time(int count, int nsubs)
{
	unsigned int min10;
	sndrv_seq_time_frame_t time;

	time.subframe = count % nsubs;
	count /= nsubs;
	min10 = count / FRAMES_PER_10MIN;
	time.hour = min10 / 6;
	min10 %= 6;
	count %= FRAMES_PER_10MIN;
	if (count < 2) {
		time.min = min10 * 10;
		time.sec = 0;
	} else {
		count -= 2;
		time.min = count / FRAMES_PER_MIN;
		time.min += min10 * 10;
		count %= FRAMES_PER_MIN;
		count += 2;
		time.sec = count / NFRAMES;
		count %= NFRAMES;
	}
	time.frame = count;

	return time;
}

/* convert from position counter to time frame */
sndrv_seq_time_frame_t snd_seq_position_to_time_frame(int format, unsigned int nsubs, unsigned int pos)
{
	switch (format) {
	case SNDRV_SEQ_SYNC_FPS_24:
		return linear_position_to_time(pos, 24, nsubs);
	case SNDRV_SEQ_SYNC_FPS_25:
		return linear_position_to_time(pos, 25, nsubs);
	case SNDRV_SEQ_SYNC_FPS_30_NDP:
		return linear_position_to_time(pos, 30, nsubs);
	case SNDRV_SEQ_SYNC_FPS_30_DP:
	default:
		return drop_position_to_time(pos, nsubs);
	}
}

/* convert from position counter to time frame */
unsigned int snd_seq_time_frame_to_position(int format, unsigned int nsubs, sndrv_seq_time_frame_t *rtime)
{
	switch (format) {
	case SNDRV_SEQ_SYNC_FPS_24:
		return linear_time_to_position(*rtime, 24, nsubs);
	case SNDRV_SEQ_SYNC_FPS_25:
		return linear_time_to_position(*rtime, 25, nsubs);
	case SNDRV_SEQ_SYNC_FPS_30_NDP:
		return linear_time_to_position(*rtime, 30, nsubs);
	case SNDRV_SEQ_SYNC_FPS_30_DP:
	default:
		return drop_time_to_position(*rtime, nsubs);
	}
}

/* resolution in nsec */
unsigned long snd_seq_get_smpte_resolution(int time_format)
{
	switch (time_format) {
	case SNDRV_SEQ_SYNC_FPS_24:
		return 1000000000UL / 24;
	case SNDRV_SEQ_SYNC_FPS_25:
		return 1000000000UL / 25;
	case SNDRV_SEQ_SYNC_FPS_30_DP:
	case SNDRV_SEQ_SYNC_FPS_30_NDP:
		return (unsigned long)(1000000000.0/29.97);
	}
	return 0;
}


/*
 * proc interface
 */

static void print_sync_info(snd_info_buffer_t *buffer, queue_sync_t *sync)
{
	snd_iprintf(buffer, " [%s] ==> %d:%d\n",
		    (sync->format & SNDRV_SEQ_SYNC_TICK ? "tick" : "time"),
		    sync->addr.client, sync->addr.port);
	snd_iprintf(buffer, "    format 0x%0x / time_format %d\n",
		    sync->format, sync->time_format);
	switch (sync->format & SNDRV_SEQ_SYNC_MODE) {
	case SNDRV_SEQ_SYNC_TICK:
		snd_iprintf(buffer, "   ppq: %d, ticks: %d\n",
			    sync->param.tick.ppq, sync->param.tick.ticks);
		snd_iprintf(buffer, "   resolution: %ld ns, position: %d\n",
			    sync->sync_tick.resolution,
			    sync->counter);
		break;
	case SNDRV_SEQ_SYNC_TIME:
		snd_iprintf(buffer, "   subframes %d, resolution: %ld ns, position: %d\n",
			    sync->param.time.subframes,
			    sync->resolution,
			    sync->counter);
		break;
	}
}

void snd_seq_sync_info_read(queue_t *q, snd_info_buffer_t *buffer)
{
	struct list_head *head;
	int count = 0;

	read_lock(&q->master_lock);
	FOR_EACH_LIST(head, &q->master_head) {
		queue_sync_t *master = list_entry(head, queue_sync_t, list);
		snd_iprintf(buffer, "master %d", count);
		print_sync_info(buffer, master);
		count++;
	}
	read_unlock(&q->master_lock);
	if (q->slave.format) {
		snd_iprintf(buffer, "slave");
		print_sync_info(buffer, &q->slave);
		count++;
	}
	if (count)
		snd_iprintf(buffer, "\n");
}

#endif /* SNDRV_SEQ_SYNC_SUPPORT */
