/*
 * RelayFS lockless scheme implementation.
 *
 * Copyright (C) 1999, 2000, 2001, 2002 - Karim Yaghmour (karim@opersys.com)
 * Copyright (C) 2002, 2003 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 2002, 2003 - Bob Wisniewski (bob@watson.ibm.com), IBM Corp
 *
 * This file is released under the GPL.
 */

#include <asm/relay.h>
#include "relay_lockless.h"
#include "resize.h"

/**
 *	compare_and_store_volatile - self-explicit
 *	@ptr: ptr to the word that will receive the new value
 *	@oval: the value we think is currently in *ptr
 *	@nval: the value *ptr will get if we were right
 */
inline int 
compare_and_store_volatile(volatile u32 *ptr, 
			   u32 oval,
			   u32 nval)
{
	u32 prev;

	barrier();
	prev = cmpxchg(ptr, oval, nval);
	barrier();

	return (prev == oval);
}

/**
 *	atomic_set_volatile - atomically set the value in ptr to nval.
 *	@ptr: ptr to the word that will receive the new value
 *	@nval: the new value
 */
inline void 
atomic_set_volatile(atomic_t *ptr,
		    u32 nval)
{
	barrier();
	atomic_set(ptr, (int)nval);
	barrier();
}

/**
 *	atomic_add_volatile - atomically add val to the value at ptr.
 *	@ptr: ptr to the word that will receive the addition
 *	@val: the value to add to *ptr
 */
inline void 
atomic_add_volatile(atomic_t *ptr, u32 val)
{
	barrier();
	atomic_add((int)val, ptr);
	barrier();
}

/**
 *	atomic_sub_volatile - atomically substract val from the value at ptr.
 *	@ptr: ptr to the word that will receive the subtraction
 *	@val: the value to subtract from *ptr
 */
inline void 
atomic_sub_volatile(atomic_t *ptr, s32 val)
{
	barrier();
	atomic_sub((int)val, ptr);
	barrier();
}

/**
 *	lockless_commit - commit a reserved slot in the buffer
 *	@rchan: the channel
 *	@from: commit the length starting here
 *	@len: length committed
 *	@deliver: length committed
 *	@interrupting: not used
 *
 *      Commits len bytes and calls deliver callback if applicable.
 */
inline void 
lockless_commit(struct rchan *rchan,
		char *from,
		u32 len, 
		int deliver, 
		int interrupting)
{
	u32 bufno, idx;
	
	idx = from - rchan->buf;

	if (len > 0) {
		bufno = RELAY_BUFNO_GET(idx, offset_bits(rchan));
		atomic_add_volatile(&fill_count(rchan, bufno), len);
	}

	if (deliver) {
		u32 mask = offset_mask(rchan);
		if (bulk_delivery(rchan)) {
			from = rchan->buf + RELAY_BUF_OFFSET_CLEAR(idx, mask);
			len += RELAY_BUF_OFFSET_GET(idx, mask);
		}
		rchan->callbacks->deliver(rchan->id, from, len);
		expand_check(rchan);
	}
}

/**
 *	get_buffer_end - get the address of the end of buffer 
 *	@rchan: the channel
 *	@buf_idx: index into channel corresponding to address
 */
static inline char * 
get_buffer_end(struct rchan *rchan, u32 buf_idx)
{
	return rchan->buf
		+ RELAY_BUF_OFFSET_CLEAR(buf_idx, offset_mask(rchan))
		+ RELAY_BUF_SIZE(offset_bits(rchan));
}


/**
 *	finalize_buffer - utility function consolidating end-of-buffer tasks.
 *	@rchan: the channel
 *	@end_idx: index into buffer to write the end-buffer event at
 *	@size_lost: number of unused bytes at the end of the buffer
 *	@time_stamp: the time of the end-buffer event
 *	@tsc: the timestamp counter associated with time
 *	@resetting: are we resetting the channel?
 *
 *	This function must be called with local irqs disabled.
 */
static inline void 
finalize_buffer(struct rchan *rchan,
		u32 end_idx,
		u32 size_lost, 
		struct timeval *time_stamp,
		u32 *tsc, 
		int resetting)
{
	char* cur_write_pos;
	char* write_buf_end;
	u32 bufno;
	int bytes_written;
	
	cur_write_pos = rchan->buf + end_idx;
	write_buf_end = get_buffer_end(rchan, end_idx - 1);

	bytes_written = rchan->callbacks->buffer_end(rchan->id, cur_write_pos, 
		     write_buf_end, *time_stamp, *tsc, using_tsc(rchan));
	if (bytes_written == 0)
		rchan->unused_bytes[rchan->buf_idx % rchan->n_bufs] = size_lost;
	
        bufno = RELAY_BUFNO_GET(end_idx, offset_bits(rchan));
        atomic_add_volatile(&fill_count(rchan, bufno), size_lost);
	if (resetting) {
		rchan->bufs_produced = rchan->bufs_produced + rchan->n_bufs;
		rchan->bufs_produced -= rchan->bufs_produced % rchan->n_bufs;
		rchan->bufs_consumed = rchan->bufs_produced;
		rchan->bytes_consumed = 0;
		update_readers_consumed(rchan, rchan->bufs_consumed, rchan->bytes_consumed);
	} else
		rchan->bufs_produced++;
}

/**
 *	lockless_finalize: - finalize last buffer at end of channel use
 *	@rchan: the channel
 */
inline void
lockless_finalize(struct rchan *rchan)
{
	u32 event_end_idx;
	u32 size_lost;
	unsigned long int flags;
	struct timeval time;
	u32 tsc;

	event_end_idx = RELAY_BUF_OFFSET_GET(idx(rchan), offset_mask(rchan));
	size_lost = RELAY_BUF_SIZE(offset_bits(rchan)) - event_end_idx;

	local_irq_save(flags);
	get_timestamp(&time, &tsc, rchan);
	finalize_buffer(rchan, idx(rchan) & idx_mask(rchan), size_lost, 
			&time, &tsc, 0);
	local_irq_restore(flags);
}

/**
 *	discard_check: - determine whether a write should be discarded
 *	@rchan: the channel
 *	@old_idx: index into buffer where check for space should begin
 *	@write_len: the length of the write to check
 *	@time_stamp: the time of the end-buffer event
 *	@tsc: the timestamp counter associated with time
 *
 *	The return value contains the result flags and is an ORed combination 
 *	of the following:
 *
 *	RELAY_WRITE_DISCARD_NONE - write should not be discarded
 *	RELAY_BUFFER_SWITCH - buffer switch occurred
 *	RELAY_WRITE_DISCARD - write should be discarded (all buffers are full)
 *	RELAY_WRITE_TOO_LONG - write won't fit into even an empty buffer
 */
static inline int
discard_check(struct rchan *rchan,
	      u32 old_idx,
	      u32 write_len, 
	      struct timeval *time_stamp,
	      u32 *tsc)
{
	u32 buffers_ready;
	u32 offset_mask = offset_mask(rchan);
	u8 offset_bits = offset_bits(rchan);
	u32 idx_mask = idx_mask(rchan);
	u32 size_lost;
	unsigned long int flags;

	if (write_len > RELAY_BUF_SIZE(offset_bits))
		return RELAY_WRITE_DISCARD | RELAY_WRITE_TOO_LONG;

	if (mode_continuous(rchan))
		return RELAY_WRITE_DISCARD_NONE;
	
	local_irq_save(flags);
	if (atomic_read(&rchan->suspended) == 1) {
		local_irq_restore(flags);
		return RELAY_WRITE_DISCARD;
	}
	if (rchan->half_switch) {
		local_irq_restore(flags);
		return RELAY_WRITE_DISCARD_NONE;
	}
	buffers_ready = rchan->bufs_produced - rchan->bufs_consumed;
	if (buffers_ready == rchan->n_bufs - 1) {
		atomic_set(&rchan->suspended, 1);
		size_lost = RELAY_BUF_SIZE(offset_bits)
			- RELAY_BUF_OFFSET_GET(old_idx, offset_mask);
		finalize_buffer(rchan, old_idx & idx_mask, size_lost, 
				time_stamp, tsc, 0);
		rchan->half_switch = 1;
		idx(rchan) = RELAY_BUF_OFFSET_CLEAR((old_idx & idx_mask), offset_mask(rchan)) + RELAY_BUF_SIZE(offset_bits) - 1;
		local_irq_restore(flags);

		return RELAY_BUFFER_SWITCH | RELAY_WRITE_DISCARD;
	}
	local_irq_restore(flags);

	return RELAY_WRITE_DISCARD_NONE;
}

/**
 *	switch_buffers - switch over to a new sub-buffer
 *	@rchan: the channel
 *	@slot_len: the length of the slot needed for the current write
 *	@offset: the offset calculated for the new index
 *	@ts: timestamp
 *	@tsc: the timestamp counter associated with time
 *	@old_idx: the value of the buffer control index when we were called
 *	@old_idx: the new calculated value of the buffer control index
 *	@resetting: are we resetting the channel?
 */
static inline void
switch_buffers(struct rchan *rchan,
	       u32 slot_len,
	       u32 offset,
	       struct timeval *ts,
	       u32 *tsc,
	       u32 new_idx,
	       u32 old_idx,
	       int resetting)
{
	u32 size_lost = rchan->end_reserve;
	unsigned long int flags;
	u32 idx_mask = idx_mask(rchan);
	u8 offset_bits = offset_bits(rchan);
	char *cur_write_pos;
	u32 new_buf_no;
	u32 start_reserve = rchan->start_reserve;
	
	if (resetting)
		size_lost = RELAY_BUF_SIZE(offset_bits(rchan)) - old_idx % rchan->buf_size;

	if (offset > 0)
		size_lost += slot_len - offset;
	else
		old_idx += slot_len;

	local_irq_save(flags);
	if (!rchan->half_switch)
		finalize_buffer(rchan, old_idx & idx_mask, size_lost,
				ts, tsc, resetting);
	rchan->half_switch = 0;
	rchan->buf_start_time = *ts;
	rchan->buf_start_tsc = *tsc;
	local_irq_restore(flags);

	cur_write_pos = rchan->buf + RELAY_BUF_OFFSET_CLEAR((new_idx
					     & idx_mask), offset_mask(rchan));
	if (resetting)
		rchan->buf_idx = 0;
	else
		rchan->buf_idx++;
	rchan->buf_id++;
	
	rchan->unused_bytes[rchan->buf_idx % rchan->n_bufs] = 0;

	rchan->callbacks->buffer_start(rchan->id, cur_write_pos, 
			       rchan->buf_id, *ts, *tsc, using_tsc(rchan));
	new_buf_no = RELAY_BUFNO_GET(new_idx & idx_mask, offset_bits);
	atomic_sub_volatile(&fill_count(rchan, new_buf_no),
			    RELAY_BUF_SIZE(offset_bits) - start_reserve);
	if (atomic_read(&fill_count(rchan, new_buf_no)) < start_reserve)
		atomic_set_volatile(&fill_count(rchan, new_buf_no), 
				    start_reserve);
}

/**
 *	lockless_reserve_slow - the slow reserve path in the lockless scheme
 *	@rchan: the channel
 *	@slot_len: the length of the slot to reserve
 *	@ts: variable that will receive the time the slot was reserved
 *	@tsc: the timestamp counter associated with time
 *	@old_idx: the value of the buffer control index when we were called
 *	@err: receives the result flags
 *
 *	Returns pointer to the beginning of the reserved slot, NULL if error.

 *	err values same as for lockless_reserve.
 */
static inline char *
lockless_reserve_slow(struct rchan *rchan,
		      u32 slot_len,
		      struct timeval *ts,
		      u32 *tsc,
		      u32 old_idx,
		      int *err)
{
	u32 new_idx, offset;
	unsigned long int flags;
	u32 offset_mask = offset_mask(rchan);
	u32 idx_mask = idx_mask(rchan);
	u32 start_reserve = rchan->start_reserve;
	u32 end_reserve = rchan->end_reserve;
	int discard_event;
	u32 reserved_idx;
	char *cur_write_pos;
	int initializing = 0;

	*err = RELAY_BUFFER_SWITCH_NONE;

	discard_event = discard_check(rchan, old_idx, slot_len, ts, tsc);
	if (discard_event != RELAY_WRITE_DISCARD_NONE) {
		*err = discard_event;
		return NULL;
	}

	local_irq_save(flags);
	if (rchan->initialized == 0) {
		rchan->initialized = initializing = 1;
		idx(rchan) = rchan->start_reserve + rchan->rchan_start_reserve;
	}
	local_irq_restore(flags);

	do {
		old_idx = idx(rchan);
		new_idx = old_idx + slot_len;

		offset = RELAY_BUF_OFFSET_GET(new_idx + end_reserve,
					      offset_mask);
		if ((offset < slot_len) && (offset > 0)) {
			reserved_idx = RELAY_BUF_OFFSET_CLEAR(new_idx 
				+ end_reserve, offset_mask) + start_reserve;
			new_idx = reserved_idx + slot_len;
		} else if (offset < slot_len) {
			reserved_idx = old_idx;
			new_idx = RELAY_BUF_OFFSET_CLEAR(new_idx
			      + end_reserve, offset_mask) + start_reserve;
		} else
			reserved_idx = old_idx;
		get_timestamp(ts, tsc, rchan);
	} while (!compare_and_store_volatile(&idx(rchan), old_idx, new_idx));

	reserved_idx &= idx_mask;

	if (initializing == 1) {
		cur_write_pos = rchan->buf 
			+ RELAY_BUF_OFFSET_CLEAR((old_idx & idx_mask),
						 offset_mask(rchan));
		rchan->buf_start_time = *ts;
		rchan->buf_start_tsc = *tsc;
		rchan->unused_bytes[0] = 0;

		rchan->callbacks->buffer_start(rchan->id, cur_write_pos, 
			       rchan->buf_id, *ts, *tsc, using_tsc(rchan));
	}

	if (offset < slot_len) {
		switch_buffers(rchan, slot_len, offset, ts, tsc, new_idx,
			       old_idx, 0);
		*err = RELAY_BUFFER_SWITCH;
	}

	/* If not using TSC, need to calc time delta */
	recalc_time_delta(ts, tsc, rchan);

	return rchan->buf + reserved_idx;
}

/**
 *	lockless_reserve - reserve a slot in the buffer for an event.
 *	@rchan: the channel
 *	@slot_len: the length of the slot to reserve
 *	@ts: variable that will receive the time the slot was reserved
 *	@tsc: the timestamp counter associated with time
 *	@err: receives the result flags
 *	@interrupting: not used
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
lockless_reserve(struct rchan *rchan,
		 u32 slot_len,
		 struct timeval *ts,
		 u32 *tsc,
		 int *err,
		 int *interrupting)
{
	u32 old_idx, new_idx, offset;
	u32 offset_mask = offset_mask(rchan);

	do {
		old_idx = idx(rchan);
		new_idx = old_idx + slot_len;

		offset = RELAY_BUF_OFFSET_GET(new_idx + rchan->end_reserve, 
					      offset_mask);
		if (offset < slot_len)
			return lockless_reserve_slow(rchan, slot_len, 
				     ts, tsc, old_idx, err);
		get_time_or_tsc(ts, tsc, rchan);
	} while (!compare_and_store_volatile(&idx(rchan), old_idx, new_idx));

	/* If not using TSC, need to calc time delta */
	recalc_time_delta(ts, tsc, rchan);

	*err = RELAY_BUFFER_SWITCH_NONE;

	return rchan->buf + (old_idx & idx_mask(rchan));
}

/**
 *	lockless_get_offset - get current and max channel offsets
 *	@rchan: the channel
 *	@max_offset: maximum channel offset
 *
 *	Returns the current and maximum channel offsets.
 */
u32 
lockless_get_offset(struct rchan *rchan,
			u32 *max_offset)
{
	if (max_offset)
		*max_offset = rchan->buf_size * rchan->n_bufs - 1;

	return rchan->initialized ? idx(rchan) & idx_mask(rchan) : 0;
}

/**
 *	lockless_reset - reset the channel
 *	@rchan: the channel
 *	@init: 1 if this is a first-time channel initialization
 */
void lockless_reset(struct rchan *rchan, int init)
{
	int i;
	
	/* Start first buffer at 0 - (end_reserve + 1) so that it
	   gets initialized via buffer_start callback as well. */
	idx(rchan) =  0UL - (rchan->end_reserve + 1);
	idx_mask(rchan) =
		(1UL << (bufno_bits(rchan) + offset_bits(rchan))) - 1;
	atomic_set(&fill_count(rchan, 0), 
		   (int)rchan->start_reserve + 
		   (int)rchan->rchan_start_reserve);
	for (i = 1; i < rchan->n_bufs; i++)
		atomic_set(&fill_count(rchan, i),
			   (int)RELAY_BUF_SIZE(offset_bits(rchan)));
}

/**
 *	lockless_reset_index - atomically set channel index to the beginning
 *	@rchan: the channel
 *	@old_idx: the current index
 *
 *	If this fails, it means that something else just logged something
 *	and therefore we probably no longer want to do this.  It's up to the
 *	caller anyway...
 *
 *	Returns 0 if the index was successfully set, negative otherwise
 */
int
lockless_reset_index(struct rchan *rchan, u32 old_idx)
{
	struct timeval ts;
	u32 tsc;
	u32 new_idx;

	if (compare_and_store_volatile(&idx(rchan), old_idx, 0)) {
		new_idx = rchan->start_reserve;
		switch_buffers(rchan, 0, 0, &ts, &tsc, new_idx, old_idx, 1);
		return 0;
	} else
		return -1;
}













