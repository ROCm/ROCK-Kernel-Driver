/*
 * RelayFS locking scheme implementation.
 *
 * Copyright (C) 1999, 2000, 2001, 2002 - Karim Yaghmour (karim@opersys.com)
 * Copyright (C) 2002, 2003 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 *
 * This file is released under the GPL.
 */

#include <asm/relay.h>
#include "relay_locking.h"
#include "resize.h"

/**
 *	switch_buffers - switches between read and write buffers.
 *	@cur_time: current time.
 *	@cur_tsc: the TSC associated with current_time, if applicable
 *	@rchan: the channel
 *	@finalizing: if true, don't start a new buffer 
 *	@resetting: if true, 
 *
 *	This should be called from with interrupts disabled.
 */
static void 
switch_buffers(struct timeval cur_time,
	       u32 cur_tsc,
	       struct rchan *rchan,
	       int finalizing,
	       int resetting,
	       int finalize_buffer_only)
{
	char *chan_buf_end;
	int bytes_written;

	if (!rchan->half_switch) {
		bytes_written = rchan->callbacks->buffer_end(rchan->id,
			     cur_write_pos(rchan), write_buf_end(rchan),
			     cur_time, cur_tsc, using_tsc(rchan));
		if (bytes_written == 0)
			rchan->unused_bytes[rchan->buf_idx % rchan->n_bufs] = 
				write_buf_end(rchan) - cur_write_pos(rchan);
	}

	if (finalize_buffer_only) {
		rchan->bufs_produced++;
		return;
	}
	
	chan_buf_end = rchan->buf + rchan->n_bufs * rchan->buf_size;
	if((write_buf(rchan) + rchan->buf_size >= chan_buf_end) || resetting)
		write_buf(rchan) = rchan->buf;
	else
		write_buf(rchan) += rchan->buf_size;
	write_buf_end(rchan) = write_buf(rchan) + rchan->buf_size;
	write_limit(rchan) = write_buf_end(rchan) - rchan->end_reserve;
	cur_write_pos(rchan) = write_buf(rchan);

	rchan->buf_start_time = cur_time;
	rchan->buf_start_tsc = cur_tsc;

	if (resetting)
		rchan->buf_idx = 0;
	else
		rchan->buf_idx++;
	rchan->buf_id++;

	if (!packet_delivery(rchan))
		rchan->unused_bytes[rchan->buf_idx % rchan->n_bufs] = 0;

	if (resetting) {
		rchan->bufs_produced = rchan->bufs_produced + rchan->n_bufs;
		rchan->bufs_produced -= rchan->bufs_produced % rchan->n_bufs;
		rchan->bufs_consumed = rchan->bufs_produced;
		rchan->bytes_consumed = 0;
		update_readers_consumed(rchan, rchan->bufs_consumed, rchan->bytes_consumed);
	} else if (!rchan->half_switch)
		rchan->bufs_produced++;

	rchan->half_switch = 0;
	
	if (!finalizing) {
		bytes_written = rchan->callbacks->buffer_start(rchan->id, cur_write_pos(rchan), rchan->buf_id, cur_time, cur_tsc, using_tsc(rchan));
		cur_write_pos(rchan) += bytes_written;
	}
}

/**
 *	locking_reserve - reserve a slot in the buffer for an event.
 *	@rchan: the channel
 *	@slot_len: the length of the slot to reserve
 *	@ts: variable that will receive the time the slot was reserved
 *	@tsc: the timestamp counter associated with time
 *	@err: receives the result flags
 *	@interrupting: if this write is interrupting another, set to non-zero 
 *
 *	Returns pointer to the beginning of the reserved slot, NULL if error.
 *
 *	The err value contains the result flags and is an ORed combination 
 *	of the following:
 *
 *	RELAY_BUFFER_SWITCH_NONE - no buffer switch occurred
 *	RELAY_EVENT_DISCARD_NONE - event should not be discarded
 *	RELAY_BUFFER_SWITCH - buffer switch occurred
 *	RELAY_EVENT_DISCARD - event should be discarded (all buffers are full)
 *	RELAY_EVENT_TOO_LONG - event won't fit into even an empty buffer
 */
inline char *
locking_reserve(struct rchan *rchan,
		u32 slot_len,
		struct timeval *ts,
		u32 *tsc,
		int *err,
		int *interrupting)
{
	u32 buffers_ready;
	int bytes_written;

	*err = RELAY_BUFFER_SWITCH_NONE;

	if (slot_len >= rchan->buf_size) {
		*err = RELAY_WRITE_DISCARD | RELAY_WRITE_TOO_LONG;
		return NULL;
	}

	if (rchan->initialized == 0) {
		rchan->initialized = 1;
		get_timestamp(&rchan->buf_start_time, 
			      &rchan->buf_start_tsc, rchan);
		rchan->unused_bytes[0] = 0;
		bytes_written = rchan->callbacks->buffer_start(
			rchan->id, cur_write_pos(rchan), 
			rchan->buf_id, rchan->buf_start_time, 
			rchan->buf_start_tsc, using_tsc(rchan));
		cur_write_pos(rchan) += bytes_written;
		*tsc = get_time_delta(ts, rchan);
		return cur_write_pos(rchan);
	}

	*tsc = get_time_delta(ts, rchan);

	if (in_progress_event_size(rchan)) {
		interrupted_pos(rchan) = cur_write_pos(rchan);
		cur_write_pos(rchan) = in_progress_event_pos(rchan) 
			+ in_progress_event_size(rchan) 
			+ interrupting_size(rchan);
		*interrupting = 1;
	} else {
		in_progress_event_pos(rchan) = cur_write_pos(rchan);
		in_progress_event_size(rchan) = slot_len;
		interrupting_size(rchan) = 0;
	}

	if (cur_write_pos(rchan) + slot_len > write_limit(rchan)) {
		if (atomic_read(&rchan->suspended) == 1) {
			in_progress_event_pos(rchan) = NULL;
			in_progress_event_size(rchan) = 0;
			interrupting_size(rchan) = 0;
			*err = RELAY_WRITE_DISCARD;
			return NULL;
		}

		buffers_ready = rchan->bufs_produced - rchan->bufs_consumed;
		if (buffers_ready == rchan->n_bufs - 1) {
			if (!mode_continuous(rchan)) {
				atomic_set(&rchan->suspended, 1);
				in_progress_event_pos(rchan) = NULL;
				in_progress_event_size(rchan) = 0;
				interrupting_size(rchan) = 0;
				get_timestamp(ts, tsc, rchan);
				switch_buffers(*ts, *tsc, rchan, 0, 0, 1);
				recalc_time_delta(ts, tsc, rchan);
				rchan->half_switch = 1;

				cur_write_pos(rchan) = write_buf_end(rchan) - 1;
				*err = RELAY_BUFFER_SWITCH | RELAY_WRITE_DISCARD;
				return NULL;
			}
		}

		get_timestamp(ts, tsc, rchan);
		switch_buffers(*ts, *tsc, rchan, 0, 0, 0);
		recalc_time_delta(ts, tsc, rchan);
		*err = RELAY_BUFFER_SWITCH;
	}

	return cur_write_pos(rchan);
}

/**
 *	locking_commit - commit a reserved slot in the buffer
 *	@rchan: the channel
 *	@from: commit the length starting here
 *	@len: length committed
 *	@deliver: length committed
 *	@interrupting: not used
 *
 *      Commits len bytes and calls deliver callback if applicable.
 */
inline void
locking_commit(struct rchan *rchan,
	       char *from,
	       u32 len, 
	       int deliver, 
	       int interrupting)
{
	cur_write_pos(rchan) += len;
	
	if (interrupting) {
		cur_write_pos(rchan) = interrupted_pos(rchan);
		interrupting_size(rchan) += len;
	} else {
		in_progress_event_size(rchan) = 0;
		if (interrupting_size(rchan)) {
			cur_write_pos(rchan) += interrupting_size(rchan);
			interrupting_size(rchan) = 0;
		}
	}

	if (deliver) {
		if (bulk_delivery(rchan)) {
			u32 cur_idx = cur_write_pos(rchan) - rchan->buf;
			u32 cur_bufno = cur_idx / rchan->buf_size;
			from = rchan->buf + cur_bufno * rchan->buf_size;
			len = cur_idx - cur_bufno * rchan->buf_size;
		}
		rchan->callbacks->deliver(rchan->id, from, len);
		expand_check(rchan);
	}
}

/**
 *	locking_finalize: - finalize last buffer at end of channel use
 *	@rchan: the channel
 */
inline void 
locking_finalize(struct rchan *rchan)
{
	unsigned long int flags;
	struct timeval time;
	u32 tsc;

	local_irq_save(flags);
	get_timestamp(&time, &tsc, rchan);
	switch_buffers(time, tsc, rchan, 1, 0, 0);
	local_irq_restore(flags);
}

/**
 *	locking_get_offset - get current and max 'file' offsets for VFS
 *	@rchan: the channel
 *	@max_offset: maximum channel offset
 *
 *	Returns the current and maximum buffer offsets in VFS terms.
 */
u32
locking_get_offset(struct rchan *rchan,
		   u32 *max_offset)
{
	if (max_offset)
		*max_offset = rchan->buf_size * rchan->n_bufs - 1;

	return cur_write_pos(rchan) - rchan->buf;
}

/**
 *	locking_reset - reset the channel
 *	@rchan: the channel
 *	@init: 1 if this is a first-time channel initialization
 */
void locking_reset(struct rchan *rchan, int init)
{
	if (init)
		channel_lock(rchan) = SPIN_LOCK_UNLOCKED;
	write_buf(rchan) = rchan->buf;
	write_buf_end(rchan) = write_buf(rchan) + rchan->buf_size;
	cur_write_pos(rchan) = write_buf(rchan);
	write_limit(rchan) = write_buf_end(rchan) - rchan->end_reserve;
	in_progress_event_pos(rchan) = NULL;
	in_progress_event_size(rchan) = 0;
	interrupted_pos(rchan) = NULL;
	interrupting_size(rchan) = 0;
}

/**
 *	locking_reset_index - atomically set channel index to the beginning
 *	@rchan: the channel
 *
 *	If this fails, it means that something else just logged something
 *	and therefore we probably no longer want to do this.  It's up to the
 *	caller anyway...
 *
 *	Returns 0 if the index was successfully set, negative otherwise
 */
int
locking_reset_index(struct rchan *rchan, u32 old_idx)
{
	unsigned long flags;
	struct timeval time;
	u32 tsc;
	u32 cur_idx;
	
	relay_lock_channel(rchan, flags);
	cur_idx = locking_get_offset(rchan, NULL);
	if (cur_idx != old_idx) {
		relay_unlock_channel(rchan, flags);
		return -1;
	}

	get_timestamp(&time, &tsc, rchan);
	switch_buffers(time, tsc, rchan, 0, 1, 0);

	relay_unlock_channel(rchan, flags);

	return 0;
}







