/*
 * RelayFS buffer management and resizing code.
 *
 * Copyright (C) 2002, 2003 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 1999, 2000, 2001, 2002 - Karim Yaghmour (karim@opersys.com)
 *
 * This file is released under the GPL.
 */

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <asm/relay.h>
#include "resize.h"

/**
 *	alloc_page_array - alloc array to hold pages, but not pages
 *	@size: the total size of the memory represented by the page array
 *	@page_count: the number of pages the array can hold
 *	@err: 0 on success, negative otherwise
 *
 *	Returns a pointer to the page array if successful, NULL otherwise.
 */
static struct page **
alloc_page_array(int size, int *page_count, int *err)
{
	int n_pages;
	struct page **page_array;
	int page_array_size;

	*err = 0;
	
	size = PAGE_ALIGN(size);
	n_pages = size >> PAGE_SHIFT;
	page_array_size = n_pages * sizeof(struct page *);
	page_array = kmalloc(page_array_size, GFP_KERNEL);
	if (page_array == NULL) {
		*err = -ENOMEM;
		return NULL;
	}
	*page_count = n_pages;
	memset(page_array, 0, page_array_size);

	return page_array;
}

/**
 *	free_page_array - free array to hold pages, but not pages
 *	@page_array: pointer to the page array
 */
static inline void
free_page_array(struct page **page_array)
{
	kfree(page_array);
}

/**
 *	depopulate_page_array - free and unreserve all pages in the array
 *	@page_array: pointer to the page array
 *	@page_count: number of pages to free
 */
static void
depopulate_page_array(struct page **page_array, int page_count)
{
	int i;
	
	for (i = 0; i < page_count; i++) {
		ClearPageReserved(page_array[i]);
		__free_page(page_array[i]);
	}
}

/**
 *	populate_page_array - allocate and reserve pages
 *	@page_array: pointer to the page array
 *	@page_count: number of pages to allocate
 *
 *	Returns 0 if successful, negative otherwise.
 */
static int
populate_page_array(struct page **page_array, int page_count)
{
	int i;
	
	for (i = 0; i < page_count; i++) {
		page_array[i] = alloc_page(GFP_KERNEL);
		if (unlikely(!page_array[i])) {
			depopulate_page_array(page_array, i);
			return -ENOMEM;
		}
		SetPageReserved(page_array[i]);
	}
	return 0;
}

/**
 *	alloc_rchan_buf - allocate the initial channel buffer
 *	@size: total size of the buffer
 *	@page_array: receives a pointer to the buffer's page array
 *	@page_count: receives the number of pages allocated
 *
 *	Returns a pointer to the resulting buffer, NULL if unsuccessful
 */
void *
alloc_rchan_buf(unsigned long size, struct page ***page_array, int *page_count)
{
	void *mem;
	int err;

	*page_array = alloc_page_array(size, page_count, &err);
	if (!*page_array)
		return NULL;

	err = populate_page_array(*page_array, *page_count);
	if (err) {
		free_page_array(*page_array);
		*page_array = NULL;
		return NULL;
	}

	mem = vmap(*page_array, *page_count, GFP_KERNEL, PAGE_KERNEL);
	if (!mem) {
		depopulate_page_array(*page_array, *page_count);
		free_page_array(*page_array);
		*page_array = NULL;
		return NULL;
	}
	memset(mem, 0, size);

	return mem;
}

/**
 *	expand_check - check whether the channel needs expanding
 *	@rchan: the channel
 *
 *	If the channel needs expanding, the needs_resize callback is
 *	called with RELAY_RESIZE_EXPAND.
 *
 *	Returns the suggested number of sub-buffers for the new
 *	buffer.
 */
void
expand_check(struct rchan *rchan)
{
	u32 active_bufs;
	u32 new_n_bufs = 0;
	u32 threshold = rchan->n_bufs * RESIZE_THRESHOLD;

	if (rchan->init_buf)
		return;

	if (rchan->resize_min == 0)
		return;

	if (rchan->resizing || rchan->replace_buffer)
		return;
	
	active_bufs = rchan->bufs_produced - rchan->bufs_consumed + 1;

	if (rchan->resize_max && active_bufs == threshold) {
		new_n_bufs = rchan->n_bufs * 2;
	}

	if (new_n_bufs && (new_n_bufs * rchan->buf_size <= rchan->resize_max))
		rchan->callbacks->needs_resize(rchan->id,
					       RELAY_RESIZE_EXPAND,
					       rchan->buf_size, 
					       new_n_bufs);
}

/**
 *	can_shrink - check whether the channel can shrink
 *	@rchan: the channel
 *	@cur_idx: the current channel index
 *
 *	Returns the suggested number of sub-buffers for the new
 *	buffer, 0 if the buffer is not shrinkable.
 */
static inline u32
can_shrink(struct rchan *rchan, u32 cur_idx)
{
	u32 active_bufs = rchan->bufs_produced - rchan->bufs_consumed + 1;
	u32 new_n_bufs = 0;
	u32 cur_bufno_bytes = cur_idx % rchan->buf_size;

	if (rchan->resize_min == 0 ||
	    rchan->resize_min >= rchan->n_bufs * rchan->buf_size)
		goto out;
	
	if (active_bufs > 1)
		goto out;

	if (cur_bufno_bytes != rchan->bytes_consumed)
		goto out;
	
	new_n_bufs = rchan->resize_min / rchan->buf_size;
out:
	return new_n_bufs;
}

/**
 *	shrink_check: - timer function checking whether the channel can shrink
 *	@data: unused
 *
 *	Every SHRINK_TIMER_SECS, check whether the channel is shrinkable.
 *	If so, we attempt to atomically reset the channel to the beginning.
 *	The needs_resize callback is then called with RELAY_RESIZE_SHRINK.
 *	If the reset fails, it means we really shouldn't be shrinking now
 *	and need to wait until the next time around.
 */
static void
shrink_check(unsigned long data)
{
	struct rchan *rchan = (struct rchan *)data;
	u32 shrink_to_nbufs, cur_idx;
	
	del_timer(&rchan->shrink_timer);
	rchan->shrink_timer.expires = jiffies + SHRINK_TIMER_SECS * HZ;
	add_timer(&rchan->shrink_timer);

	if (rchan->init_buf)
		return;

	if (rchan->resizing || rchan->replace_buffer)
		return;

	if (using_lockless(rchan))
		cur_idx = idx(rchan);
	else
		cur_idx = relay_get_offset(rchan, NULL);

	shrink_to_nbufs = can_shrink(rchan, cur_idx);
	if (shrink_to_nbufs != 0 && reset_index(rchan, cur_idx) == 0) {
		update_readers_consumed(rchan, rchan->bufs_consumed, 0);
		rchan->callbacks->needs_resize(rchan->id,
					       RELAY_RESIZE_SHRINK,
					       rchan->buf_size, 
					       shrink_to_nbufs);
	}
}

/**
 *	init_shrink_timer: - Start timer used to check shrinkability.
 *	@rchan: the channel
 */
void
init_shrink_timer(struct rchan *rchan)
{
	if (rchan->resize_min) {
		init_timer(&rchan->shrink_timer);
		rchan->shrink_timer.function = shrink_check;
		rchan->shrink_timer.data = (unsigned long)rchan;
		rchan->shrink_timer.expires = jiffies + SHRINK_TIMER_SECS * HZ;
		add_timer(&rchan->shrink_timer);
	}
}


/**
 *	alloc_new_pages - allocate new pages for expanding buffer
 *	@rchan: the channel
 *
 *	Returns 0 on success, negative otherwise.
 */
static int
alloc_new_pages(struct rchan *rchan)
{
	int new_pages_size, err;

	if (unlikely(rchan->expand_page_array))	BUG();

	new_pages_size = rchan->resize_alloc_size - rchan->alloc_size;
	rchan->expand_page_array = alloc_page_array(new_pages_size,
					    &rchan->expand_page_count, &err);
	if (rchan->expand_page_array == NULL) {
		rchan->resize_err = -ENOMEM;
		return -ENOMEM;
	}
	
	err = populate_page_array(rchan->expand_page_array,
				  rchan->expand_page_count);
	if (err) {
		rchan->resize_err = -ENOMEM;
		free_page_array(rchan->expand_page_array);
		rchan->expand_page_array = NULL;
	}

	return err;
}

/**
 *	clear_resize_offset - helper function for buffer resizing
 *	@rchan: the channel
 *
 *	Clear the saved offset change.
 */
static inline void
clear_resize_offset(struct rchan *rchan)
{
	rchan->resize_offset.ge = 0UL;
	rchan->resize_offset.le = 0UL;
	rchan->resize_offset.delta = 0;
}

/**
 *	save_resize_offset - helper function for buffer resizing
 *	@rchan: the channel
 *	@ge: affected region ge this
 *	@le: affected region le this
 *	@delta: apply this delta
 *
 *	Save a resize offset.
 */
static inline void
save_resize_offset(struct rchan *rchan, u32 ge, u32 le, int delta)
{
	rchan->resize_offset.ge = ge;
	rchan->resize_offset.le = le;
	rchan->resize_offset.delta = delta;
}

/**
 *	update_file_offset - apply offset change to reader
 *	@reader: the channel reader
 *	@change_idx: the offset index into the offsets array
 *
 *	Returns non-zero if the offset was applied.
 *
 *	Apply the offset delta saved in change_idx to the reader's
 *	current read position.
 */
static inline int
update_file_offset(struct rchan_reader *reader)
{
	int applied = 0;
	struct rchan *rchan = reader->rchan;
	u32 f_pos;
	int delta = reader->rchan->resize_offset.delta;

	if (reader->vfs_reader)
		f_pos = (u32)reader->pos.file->f_pos;
	else
		f_pos = reader->pos.f_pos;

	if (f_pos == relay_get_offset(rchan, NULL))
		return 0;

	if ((f_pos >= rchan->resize_offset.ge - 1) &&
	    (f_pos <= rchan->resize_offset.le)) {
		if (reader->vfs_reader)
			reader->pos.file->f_pos += delta;
		else
			reader->pos.f_pos += delta;
		applied = 1;
	}

	return applied;
}

/**
 *	update_file_offsets - apply offset change to readers
 *	@rchan: the channel
 *
 *	Apply the saved offset deltas to all files open on the channel.
 */
static inline void
update_file_offsets(struct rchan *rchan)
{
	struct list_head *p;
	struct rchan_reader *reader;
	
	read_lock(&rchan->open_readers_lock);
	list_for_each(p, &rchan->open_readers) {
		reader = list_entry(p, struct rchan_reader, list);
		if (update_file_offset(reader))
			reader->offset_changed = 1;
	}
	read_unlock(&rchan->open_readers_lock);
}

/**
 *	setup_expand_buf - setup expand buffer for replacement
 *	@rchan: the channel
 *	@newsize: the size of the new buffer
 *	@oldsize: the size of the old buffer
 *	@old_n_bufs: the number of sub-buffers in the old buffer
 *
 *	Inserts new pages into the old buffer to create a larger
 *	new channel buffer, splitting them at old_cur_idx, the bottom
 *	half of the old buffer going to the bottom of the new, likewise
 *	for the top half.
 */
static void
setup_expand_buf(struct rchan *rchan, int newsize, int oldsize, u32 old_n_bufs)
{
	u32 cur_idx;
	int cur_bufno, delta, i, j;
	u32 ge, le;
	int cur_pageno;
	u32 free_bufs, free_pages;
	u32 free_pages_in_cur_buf;
	u32 free_bufs_to_end;
	u32 cur_pages = rchan->alloc_size >> PAGE_SHIFT;
	u32 pages_per_buf = cur_pages / rchan->n_bufs;
	u32 bufs_ready = rchan->bufs_produced - rchan->bufs_consumed;

	if (!rchan->resize_page_array || !rchan->expand_page_array ||
	    !rchan->buf_page_array)
		return;

	if (bufs_ready >= rchan->n_bufs) {
		bufs_ready = rchan->n_bufs;
		free_bufs = 0;
	} else
		free_bufs = rchan->n_bufs - bufs_ready - 1;

	cur_idx = relay_get_offset(rchan, NULL);
	cur_pageno = cur_idx / PAGE_SIZE;
	cur_bufno = cur_idx / rchan->buf_size;

	free_pages_in_cur_buf = (pages_per_buf - 1) - (cur_pageno % pages_per_buf);
	free_pages = free_bufs * pages_per_buf + free_pages_in_cur_buf;
	free_bufs_to_end = (rchan->n_bufs - 1) - cur_bufno;
	if (free_bufs >= free_bufs_to_end) {
		free_pages = free_bufs_to_end * pages_per_buf + free_pages_in_cur_buf;
		free_bufs = free_bufs_to_end;
	}
		
	for (i = 0, j = 0; i <= cur_pageno + free_pages; i++, j++)
		rchan->resize_page_array[j] = rchan->buf_page_array[i];
	for (i = 0; i < rchan->expand_page_count; i++, j++)
		rchan->resize_page_array[j] = rchan->expand_page_array[i];
	for (i = cur_pageno + free_pages + 1; i < rchan->buf_page_count; i++, j++)
		rchan->resize_page_array[j] = rchan->buf_page_array[i];

	delta = newsize - oldsize;
	ge = (cur_pageno + 1 + free_pages) * PAGE_SIZE;
	le = oldsize;
	save_resize_offset(rchan, ge, le, delta);

	rchan->expand_buf_id = rchan->buf_id + 1 + free_bufs;
}

/**
 *	setup_shrink_buf - setup shrink buffer for replacement
 *	@rchan: the channel
 *
 *	Removes pages from the old buffer to create a smaller
 *	new channel buffer.
 */
static void
setup_shrink_buf(struct rchan *rchan)
{
	int i;
	int copy_end_page;

	if (!rchan->resize_page_array || !rchan->shrink_page_array || 
	    !rchan->buf_page_array)
		return;
	
	copy_end_page = rchan->resize_alloc_size / PAGE_SIZE;

	for (i = 0; i < copy_end_page; i++)
		rchan->resize_page_array[i] = rchan->buf_page_array[i];
}

/**
 *	cleanup_failed_alloc - relaybuf_alloc helper
 */
static void
cleanup_failed_alloc(struct rchan *rchan)
{
	if (rchan->expand_page_array) {
		depopulate_page_array(rchan->expand_page_array,
				      rchan->expand_page_count);
		free_page_array(rchan->expand_page_array);
		rchan->expand_page_array = NULL;
		rchan->expand_page_count = 0;
	} else if (rchan->shrink_page_array) {
		free_page_array(rchan->shrink_page_array);
		rchan->shrink_page_array = NULL;
		rchan->shrink_page_count = 0;
	}

	if (rchan->resize_page_array) {
		free_page_array(rchan->resize_page_array);
		rchan->resize_page_array = NULL;
		rchan->resize_page_count = 0;
	}
}

/**
 *	relaybuf_alloc - allocate a new resized channel buffer
 *	@private: pointer to the channel struct
 *
 *	Internal - manages the allocation and remapping of new channel
 *	buffers.
 */
static void 
relaybuf_alloc(void *private)
{
	struct rchan *rchan = (struct rchan *)private;
	int i, j, err;
	u32 old_cur_idx;
	int free_size;
	int free_start_page, free_end_page;
	u32 newsize, oldsize;

	if (rchan->resize_alloc_size > rchan->alloc_size) {
		err = alloc_new_pages(rchan);
		if (err) goto cleanup;
	} else {
		free_size = rchan->alloc_size - rchan->resize_alloc_size;
		BUG_ON(free_size <= 0);
		rchan->shrink_page_array = alloc_page_array(free_size,
					    &rchan->shrink_page_count, &err);
		if (rchan->shrink_page_array == NULL)
			goto cleanup;
		free_start_page = rchan->resize_alloc_size / PAGE_SIZE;
		free_end_page = rchan->alloc_size / PAGE_SIZE;
		for (i = 0, j = free_start_page; j < free_end_page; i++, j++)
			rchan->shrink_page_array[i] = rchan->buf_page_array[j];
	}

	rchan->resize_page_array = alloc_page_array(rchan->resize_alloc_size,
					    &rchan->resize_page_count, &err);
	if (rchan->resize_page_array == NULL)
		goto cleanup;

	old_cur_idx = relay_get_offset(rchan, NULL);
	clear_resize_offset(rchan);
	newsize = rchan->resize_alloc_size;
	oldsize = rchan->alloc_size;
	if (newsize > oldsize)
		setup_expand_buf(rchan, newsize, oldsize, rchan->n_bufs);
	else
		setup_shrink_buf(rchan);

	rchan->resize_buf = vmap(rchan->resize_page_array, rchan->resize_page_count, GFP_KERNEL, PAGE_KERNEL);

	if (rchan->resize_buf == NULL)
		goto cleanup;

	rchan->replace_buffer = 1;
	rchan->resizing = 0;

	rchan->callbacks->needs_resize(rchan->id, RELAY_RESIZE_REPLACE, 0, 0);
	return;

cleanup:
	cleanup_failed_alloc(rchan);
	rchan->resize_err = -ENOMEM;
	return;
}

/**
 *	relaybuf_free - free a resized channel buffer
 *	@private: pointer to the channel struct
 *
 *	Internal - manages the de-allocation and unmapping of old channel
 *	buffers.
 */
static void
relaybuf_free(void *private)
{
	struct free_rchan_buf *free_buf = (struct free_rchan_buf *)private;
	int i;

	if (free_buf->unmap_buf)
		vunmap(free_buf->unmap_buf);

	for (i = 0; i < 3; i++) {
		if (!free_buf->page_array[i].array)
			continue;
		if (free_buf->page_array[i].count)
			depopulate_page_array(free_buf->page_array[i].array,
					      free_buf->page_array[i].count);
		free_page_array(free_buf->page_array[i].array);
	}

	kfree(free_buf);
}

/**
 *	calc_order - determine the power-of-2 order of a resize
 *	@high: the larger size
 *	@low: the smaller size
 *
 *	Returns order
 */
static inline int
calc_order(u32 high, u32 low)
{
	int order = 0;
	
	if (!high || !low || high <= low)
		return 0;
	
	while (high > low) {
		order++;
		high /= 2;
	}
	
	return order;
}

/**
 *	check_size - check the sanity of the requested channel size
 *	@rchan: the channel
 *	@nbufs: the new number of sub-buffers
 *	@err: return code
 *
 *	Returns the non-zero total buffer size if ok, otherwise 0 and
 *	sets errcode if not.
 */
static inline u32
check_size(struct rchan *rchan, u32 nbufs, int *err)
{
	u32 new_channel_size = 0;

	*err = 0;
	
	if (nbufs > rchan->n_bufs) {
		rchan->resize_order = calc_order(nbufs, rchan->n_bufs);
		if (!rchan->resize_order) {
			*err = -EINVAL;
			goto out;
		}

		new_channel_size = rchan->buf_size * nbufs;
		if (new_channel_size > rchan->resize_max) {
			*err = -EINVAL;
			goto out;
		}
	} else if (nbufs < rchan->n_bufs) {
		if (rchan->n_bufs < 2) {
			*err = -EINVAL;
			goto out;
		}
		rchan->resize_order = -calc_order(rchan->n_bufs, nbufs);
		if (!rchan->resize_order) {
			*err = -EINVAL;
			goto out;
		}
		
		new_channel_size = rchan->buf_size * nbufs;
		if (new_channel_size < rchan->resize_min) {
			*err = -EINVAL;
			goto out;
		}
	} else
		*err = -EINVAL;
out:
	return new_channel_size;
}

/**
 *	__relay_realloc_buffer - allocate a new channel buffer
 *	@rchan: the channel
 *	@new_nbufs: the new number of sub-buffers
 *	@async: do the allocation using a work queue
 *
 *	Internal - see relay_realloc_buffer() for details.
 */
static int
__relay_realloc_buffer(struct rchan *rchan, u32 new_nbufs, int async)
{
	u32 new_channel_size;
	int err = 0;
	
	if (new_nbufs == rchan->n_bufs)
		return -EINVAL;
		
	if (down_trylock(&rchan->resize_sem))
		return -EBUSY;

	if (rchan->init_buf) {
		err = -EPERM;
		goto out;
	}

	if (rchan->replace_buffer) {
		err = -EBUSY;
		goto out;
	}

	if (rchan->resizing) {
		err = -EBUSY;
		goto out;
	} else
		rchan->resizing = 1;

	if (rchan->resize_failures > MAX_RESIZE_FAILURES) {
		err = -ENOMEM;
		goto out;
	}

	new_channel_size = check_size(rchan, new_nbufs, &err);
	if (err)
		goto out;
	
	rchan->resize_n_bufs = new_nbufs;
	rchan->resize_buf_size = rchan->buf_size;
	rchan->resize_alloc_size = FIX_SIZE(new_channel_size);
	
	if (async) {
		INIT_WORK(&rchan->work, relaybuf_alloc, rchan);
		schedule_delayed_work(&rchan->work, 1);
	} else
		relaybuf_alloc((void *)rchan);
out:
	up(&rchan->resize_sem);
	
	return err;
}

/**
 *	relay_realloc_buffer - allocate a new channel buffer
 *	@rchan_id: the channel id
 *	@bufsize: the new sub-buffer size
 *	@nbufs: the new number of sub-buffers
 *
 *	Allocates a new channel buffer using the specified sub-buffer size
 *	and count.  If async is non-zero, the allocation is done in the
 *	background using a work queue.  When the allocation has completed,
 *	the needs_resize() callback is called with a resize_type of
 *	RELAY_RESIZE_REPLACE.  This function doesn't replace the old buffer
 *	with the new - see relay_replace_buffer().  See
 *	Documentation/filesystems/relayfs.txt for more details.
 *
 *	Returns 0 on success, or errcode if the channel is busy or if
 *	the allocation couldn't happen for some reason.
 */
int
relay_realloc_buffer(int rchan_id, u32 new_nbufs, int async)
{
	int err;
	
	struct rchan *rchan;

	rchan = rchan_get(rchan_id);
	if (rchan == NULL)
		return -EBADF;

	err = __relay_realloc_buffer(rchan, new_nbufs, async);
	
	rchan_put(rchan);

	return err;
}

/**
 *	expand_cancel_check - check whether the current expand needs canceling
 *	@rchan: the channel
 *
 *	Returns 1 if the expand should be canceled, 0 otherwise.
 */
static int
expand_cancel_check(struct rchan *rchan)
{
	if (rchan->buf_id >= rchan->expand_buf_id)
		return 1;
	else
		return 0;
}

/**
 *	shrink_cancel_check - check whether the current shrink needs canceling
 *	@rchan: the channel
 *
 *	Returns 1 if the shrink should be canceled, 0 otherwise.
 */
static int
shrink_cancel_check(struct rchan *rchan, u32 newsize)
{
	u32 active_bufs = rchan->bufs_produced - rchan->bufs_consumed + 1;
	u32 cur_idx = relay_get_offset(rchan, NULL);

	if (cur_idx >= newsize)
		return 1;

	if (active_bufs > 1)
		return 1;

	return 0;
}

/**
 *	switch_rchan_buf - do_replace_buffer helper
 */
static void
switch_rchan_buf(struct rchan *rchan,
		 int newsize,
		 int oldsize,
		 u32 old_nbufs,
		 u32 cur_idx)
{
	u32 newbufs, cur_bufno;
	int i;

	cur_bufno = cur_idx / rchan->buf_size;

	rchan->buf = rchan->resize_buf;
	rchan->alloc_size = rchan->resize_alloc_size;
	rchan->n_bufs = rchan->resize_n_bufs;

	if (newsize > oldsize) {
		u32 ge = rchan->resize_offset.ge;
		u32 moved_buf = ge / rchan->buf_size;

		newbufs = (newsize - oldsize) / rchan->buf_size;
		for (i = moved_buf; i < old_nbufs; i++) {
			if (using_lockless(rchan))
				atomic_set(&fill_count(rchan, i + newbufs), 
					   atomic_read(&fill_count(rchan, i)));
			rchan->unused_bytes[i + newbufs] = rchan->unused_bytes[i];
 		}
		for (i = moved_buf; i < moved_buf + newbufs; i++) {
			if (using_lockless(rchan))
				atomic_set(&fill_count(rchan, i),
					   (int)RELAY_BUF_SIZE(offset_bits(rchan)));
			rchan->unused_bytes[i] = 0;
		}
	}

	rchan->buf_idx = cur_bufno;

	if (!using_lockless(rchan)) {
		cur_write_pos(rchan) = rchan->buf + cur_idx;
		write_buf(rchan) = rchan->buf + cur_bufno * rchan->buf_size;
		write_buf_end(rchan) = write_buf(rchan) + rchan->buf_size;
		write_limit(rchan) = write_buf_end(rchan) - rchan->end_reserve;
	} else {
		idx(rchan) &= idx_mask(rchan);
		bufno_bits(rchan) += rchan->resize_order;
		idx_mask(rchan) =
			(1UL << (bufno_bits(rchan) + offset_bits(rchan))) - 1;
	}
}

/**
 *	do_replace_buffer - does the work of channel buffer replacement
 *	@rchan: the channel
 *	@newsize: new channel buffer size
 *	@oldsize: old channel buffer size
 *	@old_n_bufs: old channel sub-buffer count
 *
 *	Returns 0 if replacement happened, 1 if canceled
 *
 *	Does the work of switching buffers and fixing everything up
 *	so the channel can continue with a new size.
 */
static int
do_replace_buffer(struct rchan *rchan,
		  int newsize,
		  int oldsize,
		  u32 old_nbufs)
{
	u32 cur_idx;
	int err = 0;
	int canceled;

	cur_idx = relay_get_offset(rchan, NULL);

	if (newsize > oldsize)
		canceled = expand_cancel_check(rchan);
	else
		canceled = shrink_cancel_check(rchan, newsize);

	if (canceled) {
		err = -EAGAIN;
		goto out;
	}

	switch_rchan_buf(rchan, newsize, oldsize, old_nbufs, cur_idx);

	if (rchan->resize_offset.delta)
		update_file_offsets(rchan);

	atomic_set(&rchan->suspended, 0);

	rchan->old_buf_page_array = rchan->buf_page_array;
	rchan->buf_page_array = rchan->resize_page_array;
	rchan->buf_page_count = rchan->resize_page_count;
	rchan->resize_page_array = NULL;
	rchan->resize_page_count = 0;
	rchan->resize_buf = NULL;
	rchan->resize_buf_size = 0;
	rchan->resize_alloc_size = 0;
	rchan->resize_n_bufs = 0;
	rchan->resize_err = 0;
	rchan->resize_order = 0;
out:
	rchan->callbacks->needs_resize(rchan->id,
				       RELAY_RESIZE_REPLACED,
				       rchan->buf_size,
				       rchan->n_bufs);
	return err;
}

/**
 *	add_free_page_array - add a page_array to be freed
 *	@free_rchan_buf: the free_rchan_buf struct
 *	@page_array: the page array to free
 *	@page_count: the number of pages to free, 0 to free the array only
 *
 *	Internal - Used add page_arrays to be freed asynchronously.
 */
static inline void
add_free_page_array(struct free_rchan_buf *free_rchan_buf,
		    struct page **page_array, int page_count)
{
	int cur = free_rchan_buf->cur++;
	
	free_rchan_buf->page_array[cur].array = page_array;
	free_rchan_buf->page_array[cur].count = page_count;
}

/**
 *	free_rchan_buf - free a channel buffer
 *	@buf: pointer to the buffer to free
 *	@page_array: pointer to the buffer's page array
 *	@page_count: number of pages in page array
 */
int
free_rchan_buf(void *buf, struct page **page_array, int page_count)
{
	struct free_rchan_buf *free_buf;

	free_buf = kmalloc(sizeof(struct free_rchan_buf), GFP_ATOMIC);
	if (!free_buf)
		return -ENOMEM;
	memset(free_buf, 0, sizeof(struct free_rchan_buf));

	free_buf->unmap_buf = buf;
	add_free_page_array(free_buf, page_array, page_count);

	INIT_WORK(&free_buf->work, relaybuf_free, free_buf);
	schedule_delayed_work(&free_buf->work, 1);

	return 0;
}

/**
 *	free_replaced_buffer - free a channel's old buffer
 *	@rchan: the channel
 *	@oldbuf: the old buffer
 *	@oldsize: old buffer size
 *
 *	Frees a channel buffer via work queue.
 */
static int
free_replaced_buffer(struct rchan *rchan, char *oldbuf, int oldsize)
{
	struct free_rchan_buf *free_buf;

	free_buf = kmalloc(sizeof(struct free_rchan_buf), GFP_ATOMIC);
	if (!free_buf)
		return -ENOMEM;
	memset(free_buf, 0, sizeof(struct free_rchan_buf));

	free_buf->unmap_buf = oldbuf;
	add_free_page_array(free_buf, rchan->old_buf_page_array, 0);
	rchan->old_buf_page_array = NULL;
	add_free_page_array(free_buf, rchan->expand_page_array, 0);
	add_free_page_array(free_buf, rchan->shrink_page_array, rchan->shrink_page_count);

	rchan->expand_page_array = NULL;
	rchan->expand_page_count = 0;
	rchan->shrink_page_array = NULL;
	rchan->shrink_page_count = 0;

	INIT_WORK(&free_buf->work, relaybuf_free, free_buf);
	schedule_delayed_work(&free_buf->work, 1);

	return 0;
}

/**
 *	free_canceled_resize - free buffers allocated for a canceled resize
 *	@rchan: the channel
 *
 *	Frees canceled buffers via work queue.
 */
static int
free_canceled_resize(struct rchan *rchan)
{
	struct free_rchan_buf *free_buf;

	free_buf = kmalloc(sizeof(struct free_rchan_buf), GFP_ATOMIC);
	if (!free_buf)
		return -ENOMEM;
	memset(free_buf, 0, sizeof(struct free_rchan_buf));

	if (rchan->resize_alloc_size > rchan->alloc_size)
		add_free_page_array(free_buf, rchan->expand_page_array, rchan->expand_page_count);
	else
		add_free_page_array(free_buf, rchan->shrink_page_array, 0);
	
	add_free_page_array(free_buf, rchan->resize_page_array, 0);
	free_buf->unmap_buf = rchan->resize_buf;

	rchan->expand_page_array = NULL;
	rchan->expand_page_count = 0;
	rchan->shrink_page_array = NULL;
	rchan->shrink_page_count = 0;
	rchan->resize_page_array = NULL;
	rchan->resize_page_count = 0;
	rchan->resize_buf = NULL;

	INIT_WORK(&free_buf->work, relaybuf_free, free_buf);
	schedule_delayed_work(&free_buf->work, 1);

	return 0;
}

/**
 *	__relay_replace_buffer - replace channel buffer with new buffer
 *	@rchan: the channel
 *
 *	Internal - see relay_replace_buffer() for details.
 *
 *	Returns 0 if successful, negative otherwise.
 */
static int
__relay_replace_buffer(struct rchan *rchan)
{
	int oldsize;
	int err = 0;
	char *oldbuf;
	
	if (down_trylock(&rchan->resize_sem))
		return -EBUSY;

	if (rchan->init_buf) {
		err = -EPERM;
		goto out;
	}

	if (!rchan->replace_buffer)
		goto out;

	if (rchan->resizing) {
		err = -EBUSY;
		goto out;
	}

	if (rchan->resize_buf == NULL) {
		err = -EINVAL;
		goto out;
	}

	oldbuf = rchan->buf;
	oldsize = rchan->alloc_size;

	err = do_replace_buffer(rchan, rchan->resize_alloc_size,
				oldsize, rchan->n_bufs);
	if (err == 0)
		err = free_replaced_buffer(rchan, oldbuf, oldsize);
	else
		err = free_canceled_resize(rchan);
out:
	rchan->replace_buffer = 0;
	up(&rchan->resize_sem);
	
	return err;
}

/**
 *	relay_replace_buffer - replace channel buffer with new buffer
 *	@rchan_id: the channel id
 *
 *	Replaces the current channel buffer with the new buffer allocated
 *	by relay_alloc_buffer and contained in the channel struct.  When the
 *	replacement is complete, the needs_resize() callback is called with
 *	RELAY_RESIZE_REPLACED.
 *
 *	Returns 0 on success, or errcode if the channel is busy or if
 *	the replacement or previous allocation didn't happen for some reason.
 */
int
relay_replace_buffer(int rchan_id)
{
	int err;
	
	struct rchan *rchan;

	rchan = rchan_get(rchan_id);
	if (rchan == NULL)
		return -EBADF;

	err = __relay_replace_buffer(rchan);
	
	rchan_put(rchan);

	return err;
}

EXPORT_SYMBOL(relay_realloc_buffer);
EXPORT_SYMBOL(relay_replace_buffer);

