/*
 * Public API and common code for RelayFS.
 *
 * Please see Documentation/filesystems/relayfs.txt for API description.
 * 
 * Copyright (C) 2002, 2003 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 1999, 2000, 2001, 2002 - Karim Yaghmour (karim@opersys.com)
 *
 * This file is released under the GPL.
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/page-flags.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/relay.h>
#include <asm/hardirq.h>

#include "relay_lockless.h"
#include "relay_locking.h"
#include "resize.h"

/* Relay channel table, indexed by channel id */
static struct rchan *	rchan_table[RELAY_MAX_CHANNELS];
static rwlock_t		rchan_table_lock = RW_LOCK_UNLOCKED;

/* Relay operation structs, one per scheme */
static struct relay_ops lockless_ops = {
	.reserve = lockless_reserve,
	.commit = lockless_commit,
	.get_offset = lockless_get_offset,
	.finalize = lockless_finalize,
	.reset = lockless_reset,
	.reset_index = lockless_reset_index
};

static struct relay_ops locking_ops = {
	.reserve = locking_reserve,
	.commit = locking_commit,
	.get_offset = locking_get_offset,
	.finalize = locking_finalize,
	.reset = locking_reset,
	.reset_index = locking_reset_index
};

/*
 * Low-level relayfs kernel API.  These functions should not normally be 
 * used by clients.  See high-level kernel API below.
 */

/**
 *	rchan_get - get channel associated with id, incrementing refcount 
 *	@rchan_id: the channel id
 *
 *	Returns channel if successful, NULL otherwise.
 */
struct rchan *
rchan_get(int rchan_id)
{
	struct rchan *rchan;
	
	if ((rchan_id < 0) || (rchan_id >= RELAY_MAX_CHANNELS))
		return NULL;
	
	read_lock(&rchan_table_lock);
	rchan = rchan_table[rchan_id];
	if (rchan)
		atomic_inc(&rchan->refcount);
	read_unlock(&rchan_table_lock);

	return rchan;
}

/**
 *	clear_readers - clear non-VFS readers
 *	@rchan: the channel
 *
 *	Clear the channel pointers of all non-VFS readers open on the channel.
 */
static inline void
clear_readers(struct rchan *rchan)
{
	struct list_head *p;
	struct rchan_reader *reader;
	
	read_lock(&rchan->open_readers_lock);
	list_for_each(p, &rchan->open_readers) {
		reader = list_entry(p, struct rchan_reader, list);
		if (!reader->vfs_reader)
			reader->rchan = NULL;
	}
	read_unlock(&rchan->open_readers_lock);
}

/**
 *	rchan_alloc_id - reserve a channel id and store associated channel
 *	@rchan: the channel
 *
 *	Returns channel id if successful, -1 otherwise.
 */
static inline int
rchan_alloc_id(struct rchan *rchan)
{
	int i;
	int rchan_id = -1;
	
	if (rchan == NULL)
		return -1;

	write_lock(&rchan_table_lock);
	for (i = 0; i < RELAY_MAX_CHANNELS; i++) {
		if (rchan_table[i] == NULL) {
			rchan_table[i] = rchan;
			rchan_id = rchan->id = i;
			break;
		}
	}
	if (rchan_id != -1)
		atomic_inc(&rchan->refcount);
	write_unlock(&rchan_table_lock);
	
	return rchan_id;
}

/**
 *	rchan_free_id - revoke a channel id and remove associated channel
 *	@rchan_id: the channel id
 */
static inline void
rchan_free_id(int rchan_id)
{
	struct rchan *rchan;

	if ((rchan_id < 0) || (rchan_id >= RELAY_MAX_CHANNELS))
		return;

	write_lock(&rchan_table_lock);
	rchan = rchan_table[rchan_id];
	rchan_table[rchan_id] = NULL;
	write_unlock(&rchan_table_lock);
}

/**
 *	rchan_destroy_buf - destroy the current channel buffer
 *	@rchan: the channel
 */
static inline int
rchan_destroy_buf(struct rchan *rchan)
{
	int err = 0;
	
	if (rchan->buf && !rchan->init_buf)
		err = free_rchan_buf(rchan->buf,
				     rchan->buf_page_array,
				     rchan->buf_page_count);

	return err;
}

/**
 *	relay_release - perform end-of-buffer processing for last buffer
 *	@rchan: the channel
 *
 *	Returns 0 if successful, negative otherwise.
 *
 *	Releases the channel buffer, destroys the channel, and removes the
 *	relay file from the relayfs filesystem.  Should only be called from 
 *	rchan_put().  If we're here, it means by definition refcount is 0.
 */
static int 
relay_release(struct rchan *rchan)
{
	int err = 0;
	
	if (rchan == NULL) {
		err = -EBADF;
		goto exit;
	}

	err = rchan_destroy_buf(rchan);
	if (err)
		goto exit;

	rchan_free_id(rchan->id);

	err = relayfs_remove_file(rchan->dentry);
	if (err)
		goto exit;

	clear_readers(rchan);
	kfree(rchan);
exit:
	return err;
}

/**
 *	rchan_get - decrement channel refcount, releasing it if 0
 *	@rchan: the channel
 *
 *	If the refcount reaches 0, the channel will be destroyed.
 */
void 
rchan_put(struct rchan *rchan)
{
	if (atomic_dec_and_test(&rchan->refcount))
		relay_release(rchan);
}

/**
 *	relay_reserve -  reserve a slot in the channel buffer
 *	@rchan: the channel
 *	@len: the length of the slot to reserve
 *	@td: the time delta between buffer start and current write, or TSC
 *	@err: receives the result flags
 *	@interrupting: 1 if interrupting previous, used only in locking scheme
 *
 *	Returns pointer to the beginning of the reserved slot, NULL if error.
 *
 *	The errcode value contains the result flags and is an ORed combination 
 *	of the following:
 *
 *	RELAY_BUFFER_SWITCH_NONE - no buffer switch occurred
 *	RELAY_EVENT_DISCARD_NONE - event should not be discarded
 *	RELAY_BUFFER_SWITCH - buffer switch occurred
 *	RELAY_EVENT_DISCARD - event should be discarded (all buffers are full)
 *	RELAY_EVENT_TOO_LONG - event won't fit into even an empty buffer
 *
 *	buffer_start and buffer_end callbacks are triggered at this point
 *	if applicable.
 */
char *
relay_reserve(struct rchan *rchan,
	      u32 len,
	      struct timeval *ts,
	      u32 *td,
	      int *err,
	      int *interrupting)
{
	if (rchan == NULL)
		return NULL;
	
	*interrupting = 0;

	return rchan->relay_ops->reserve(rchan, len, ts, td, err, interrupting);
}


/**
 *	wakeup_readers - wake up VFS readers waiting on a channel
 *	@private: the channel
 *
 *	This is the work function used to defer reader waking.  The
 *	reason waking is deferred is that calling directly from commit
 *	causes problems if you're writing from say the scheduler.
 */
static void 
wakeup_readers(void *private)
{
	struct rchan *rchan = (struct rchan *)private;

	wake_up_interruptible(&rchan->read_wait);
}


/**
 *	relay_commit - commit a reserved slot in the buffer
 *	@rchan: the channel
 *	@from: commit the length starting here
 *	@len: length committed
 *	@interrupting: 1 if interrupting previous, used only in locking scheme
 *
 *      After the write into the reserved buffer has been complted, this
 *      function must be called in order for the relay to determine whether 
 *      buffers are complete and to wake up VFS readers.
 *
 *	delivery callback is triggered at this point if applicable.
 */
void
relay_commit(struct rchan *rchan,
	     char *from,
	     u32 len,
	     int reserve_code,
	     int interrupting)
{
	int deliver;

	if (rchan == NULL)
		return;
	
	deliver = packet_delivery(rchan) || 
		   (reserve_code & RELAY_BUFFER_SWITCH);

	rchan->relay_ops->commit(rchan, from, len, deliver, interrupting);

	/* The params are always the same, so no worry about re-queuing */
	if (deliver && 	waitqueue_active(&rchan->read_wait)) {
		PREPARE_WORK(&rchan->wake_readers, wakeup_readers, rchan);
		schedule_delayed_work(&rchan->wake_readers, 1);
	}
}

/**
 *	relay_get_offset - get current and max channel buffer offsets
 *	@rchan: the channel
 *	@max_offset: maximum channel offset
 *
 *	Returns the current and maximum channel buffer offsets.
 */
u32
relay_get_offset(struct rchan *rchan, u32 *max_offset)
{
	return rchan->relay_ops->get_offset(rchan, max_offset);
}

/**
 *	reset_index - try once to reset the current channel index
 *	@rchan: the channel
 *	@old_index: the index read before reset
 *
 *	Attempts to reset the channel index to 0.  It tries once, and
 *	if it fails, returns negative, 0 otherwise.
 */
int
reset_index(struct rchan *rchan, u32 old_index)
{
	return rchan->relay_ops->reset_index(rchan, old_index);
}

/*
 * close() vm_op implementation for relayfs file mapping.
 */
static void
relay_file_mmap_close(struct vm_area_struct *vma)
{
	struct file *filp = vma->vm_file;
	struct rchan_reader *reader;
	struct rchan *rchan;

	reader = (struct rchan_reader *)filp->private_data;
	rchan = reader->rchan;

	atomic_dec(&rchan->mapped);

	rchan->callbacks->fileop_notify(reader->rchan->id, filp,
					RELAY_FILE_UNMAP);
}

/*
 * vm_ops for relay file mappings.
 */
static struct vm_operations_struct relay_file_mmap_ops = {
	.close = relay_file_mmap_close
};

/* \begin{Code inspired from BTTV driver} */
static inline unsigned long 
kvirt_to_pa(unsigned long adr)
{
	unsigned long kva, ret;

	kva = (unsigned long) page_address(vmalloc_to_page((void *) adr));
	kva |= adr & (PAGE_SIZE - 1);
	ret = __pa(kva);
	return ret;
}

static int
relay_mmap_region(struct vm_area_struct *vma,
		  const char *adr,
		  const char *start_pos,
		  unsigned long size)
{
	unsigned long start = (unsigned long) adr;
	unsigned long page, pos;

	pos = (unsigned long) start_pos;

	while (size > 0) {
		page = kvirt_to_pa(pos);
		if (remap_page_range(vma, start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return 0;
}
/* \end{Code inspired from BTTV driver} */

/**
 *	relay_mmap_buffer: - mmap buffer to process address space
 *	@rchan_id: relay channel id
 *	@vma: vm_area_struct describing memory to be mapped
 *
 *	Returns:
 *	0 if ok
 *	-EAGAIN, when remap failed
 *	-EINVAL, invalid requested length
 *
 *	Caller should already have grabbed mmap_sem.
 */
int 
__relay_mmap_buffer(struct rchan *rchan,
		    struct vm_area_struct *vma)
{
	int err = 0;
	unsigned long length = vma->vm_end - vma->vm_start;
	struct file *filp = vma->vm_file;

	if (rchan == NULL) {
		err = -EBADF;
		goto exit;
	}

	if (rchan->init_buf) {
		err = -EPERM;
		goto exit;
	}
	
	if (length != (unsigned long)rchan->alloc_size) {
		err = -EINVAL;
		goto exit;
	}

	err = relay_mmap_region(vma,
				(char *)vma->vm_start,
				rchan->buf,
				rchan->alloc_size);

	if (err == 0) {
		vma->vm_ops = &relay_file_mmap_ops;
		err = rchan->callbacks->fileop_notify(rchan->id, filp,
						      RELAY_FILE_MAP);
		if (err == 0)
			atomic_inc(&rchan->mapped);
	}
exit:	
	return err;
}

/*
 * High-level relayfs kernel API.  See Documentation/filesystems/relafys.txt.
 */

/*
 * rchan_callback implementations defining default channel behavior.  Used
 * in place of corresponding NULL values in client callback struct.
 */

/*
 * buffer_end() default callback.  Does nothing.
 */
static int 
buffer_end_default_callback(int rchan_id,
			    char *current_write_pos,
			    char *end_of_buffer,
			    struct timeval end_time,
			    u32 end_tsc,
			    int using_tsc) 
{
	return 0;
}

/*
 * buffer_start() default callback.  Does nothing.
 */
static int 
buffer_start_default_callback(int rchan_id,
			      char *current_write_pos,
			      u32 buffer_id,
			      struct timeval start_time,
			      u32 start_tsc,
			      int using_tsc)
{
	return 0;
}

/*
 * deliver() default callback.  Does nothing.
 */
static void 
deliver_default_callback(int rchan_id, char *from, u32 len)
{
}

/*
 * user_deliver() default callback.  Does nothing.
 */
static void 
user_deliver_default_callback(int rchan_id, char *from, u32 len)
{
}

/*
 * needs_resize() default callback.  Does nothing.
 */
static void
needs_resize_default_callback(int rchan_id,
			      int resize_type,
			      u32 suggested_buf_size,
			      u32 suggested_n_bufs)
{
}

/*
 * fileop_notify() default callback.  Does nothing.
 */
static int
fileop_notify_default_callback(int rchan_id,
			       struct file *filp,
			       enum relay_fileop fileop)
{
	return 0;
}

/*
 * ioctl() default callback.  Does nothing.
 */
static int
ioctl_default_callback(int rchan_id,
		       unsigned int cmd,
		       unsigned long arg)
{
	return 0;
}

/* relay channel default callbacks */
static struct rchan_callbacks default_channel_callbacks = {
	.buffer_start = buffer_start_default_callback,
	.buffer_end = buffer_end_default_callback,
	.deliver = deliver_default_callback,
	.user_deliver = user_deliver_default_callback,
	.needs_resize = needs_resize_default_callback,
	.fileop_notify = fileop_notify_default_callback,
	.ioctl = ioctl_default_callback,
};

/**
 *	check_attribute_flags - check sanity of channel attributes
 *	@flags: channel attributes
 *	@resizeable: 1 if true
 *
 *	Returns 0 if successful, negative otherwise.
 */
static int
check_attribute_flags(u32 *attribute_flags, int resizeable)
{
	u32 flags = *attribute_flags;
	
	if (!(flags & RELAY_DELIVERY_BULK) && !(flags & RELAY_DELIVERY_PACKET))
		return -EINVAL; /* Delivery mode must be specified */
	
	if (!(flags & RELAY_USAGE_SMP) && !(flags & RELAY_USAGE_GLOBAL))
		return -EINVAL; /* Usage must be specified */
	
	if (resizeable) {  /* Resizeable can never be continuous */
		*attribute_flags &= ~RELAY_MODE_CONTINUOUS;
		*attribute_flags |= RELAY_MODE_NO_OVERWRITE;
	}
	
	if ((flags & RELAY_MODE_CONTINUOUS) &&
	    (flags & RELAY_MODE_NO_OVERWRITE))
		return -EINVAL; /* Can't have it both ways */
	
	if (!(flags & RELAY_MODE_CONTINUOUS) &&
	    !(flags & RELAY_MODE_NO_OVERWRITE))
		*attribute_flags |= RELAY_MODE_CONTINUOUS; /* Default to continuous */
	
	if (!(flags & RELAY_SCHEME_ANY))
		return -EINVAL; /* One or both must be specified */
	else if (flags & RELAY_SCHEME_LOCKLESS) {
		if (have_cmpxchg())
			*attribute_flags &= ~RELAY_SCHEME_LOCKING;
		else if (flags & RELAY_SCHEME_LOCKING)
			*attribute_flags &= ~RELAY_SCHEME_LOCKLESS;
		else
			return -EINVAL; /* Locking scheme not an alternative */
	}
	
	if (!(flags & RELAY_TIMESTAMP_ANY))
		return -EINVAL; /* One or both must be specified */
	else if (flags & RELAY_TIMESTAMP_TSC) {
		if (have_tsc())
			*attribute_flags &= ~RELAY_TIMESTAMP_GETTIMEOFDAY;
		else if (flags & RELAY_TIMESTAMP_GETTIMEOFDAY)
			*attribute_flags &= ~RELAY_TIMESTAMP_TSC;
		else
			return -EINVAL; /* gettimeofday not an alternative */
	}

	return 0;
}

/*
 * High-level API functions.
 */

/**
 *	__relay_reset - internal reset function
 *	@rchan: the channel
 *	@init: 1 if this is a first-time channel initialization
 *
 *	See relay_reset for description of effect.
 */
void
__relay_reset(struct rchan *rchan, int init)
{
	int i;
	
	if (init) {
		rchan->version = RELAYFS_CHANNEL_VERSION;
		init_MUTEX(&rchan->resize_sem);
		init_waitqueue_head(&rchan->read_wait);
		init_waitqueue_head(&rchan->write_wait);
		atomic_set(&rchan->refcount, 0);
		INIT_LIST_HEAD(&rchan->open_readers);
		rchan->open_readers_lock = RW_LOCK_UNLOCKED;
	}
	
	rchan->buf_id = rchan->buf_idx = 0;
	atomic_set(&rchan->suspended, 0);
	atomic_set(&rchan->mapped, 0);
	rchan->half_switch = 0;
	rchan->bufs_produced = 0;
	rchan->bufs_consumed = 0;
	rchan->bytes_consumed = 0;
	rchan->initialized = 0;
	rchan->finalized = 0;
	rchan->resize_min = rchan->resize_max = 0;
	rchan->resizing = 0;
	rchan->replace_buffer = 0;
	rchan->resize_buf = NULL;
	rchan->resize_buf_size = 0;
	rchan->resize_alloc_size = 0;
	rchan->resize_n_bufs = 0;
	rchan->resize_err = 0;
	rchan->resize_failures = 0;
	rchan->resize_order = 0;

	rchan->expand_page_array = NULL;
	rchan->expand_page_count = 0;
	rchan->shrink_page_array = NULL;
	rchan->shrink_page_count = 0;
	rchan->resize_page_array = NULL;
	rchan->resize_page_count = 0;
	rchan->old_buf_page_array = NULL;
	rchan->expand_buf_id = 0;

	INIT_WORK(&rchan->wake_readers, NULL, NULL);
	INIT_WORK(&rchan->wake_writers, NULL, NULL);

	for (i = 0; i < RELAY_MAX_BUFS; i++)
		rchan->unused_bytes[i] = 0;
	
	rchan->relay_ops->reset(rchan, init);
}

/**
 *	relay_reset - reset the channel
 *	@rchan: the channel
 *
 *	Returns 0 if successful, negative if not.
 *
 *	This has the effect of erasing all data from the buffer and
 *	restarting the channel in its initial state.  The buffer itself
 *	is not freed, so any mappings are still in effect.
 *
 *	NOTE: Care should be taken that the channnel isn't actually
 *	being used by anything when this call is made.
 */
int
relay_reset(int rchan_id)
{
	struct rchan *rchan;

	rchan = rchan_get(rchan_id);
	if (rchan == NULL)
		return -EBADF;

	__relay_reset(rchan, 0);
	update_readers_consumed(rchan, 0, 0);

	rchan_put(rchan);

	return 0;
}

/**
 *	check_init_buf - check the sanity of init_buf, if present
 *	@init_buf: the initbuf
 *	@init_buf_size: the total initbuf size
 *	@bufsize: the channel's sub-buffer size
 *	@nbufs: the number of sub-buffers in the channel
 *
 *	Returns 0 if ok, negative otherwise.
 */
static int
check_init_buf(char *init_buf, u32 init_buf_size, u32 bufsize, u32 nbufs)
{
	int err = 0;
	
	if (init_buf && nbufs == 1) /* 1 sub-buffer makes no sense */
		err = -EINVAL;

	if (init_buf && (bufsize * nbufs != init_buf_size))
		err = -EINVAL;

	return err;
}

/**
 *	rchan_create_buf - allocate the initial channel buffer
 *	@rchan: the channel
 *	@size_alloc: the total size of the channel buffer
 *
 *	Returns 0 if successful, negative otherwise.
 */
static inline int
rchan_create_buf(struct rchan *rchan, int size_alloc)
{
	struct page **page_array;
	int page_count;

	if ((rchan->buf = (char *)alloc_rchan_buf(size_alloc, &page_array, &page_count)) == NULL) {
		rchan->buf_page_array = NULL;
		rchan->buf_page_count = 0;
		return -ENOMEM;
	}

	rchan->buf_page_array = page_array;
	rchan->buf_page_count = page_count;

	return 0;
}

/**
 *	rchan_create - allocate and initialize a channel, including buffer
 *	@chanpath: path specifying the relayfs channel file to create
 *	@bufsize: the size of the sub-buffers within the channel buffer
 *	@nbufs: the number of sub-buffers within the channel buffer
 *	@rchan_flags: flags specifying buffer attributes
 *	@err: err code
 *
 *	Returns channel if successful, NULL otherwise, err receives errcode.
 *
 *	Allocates a struct rchan representing a relay channel, according
 *	to the attributes passed in via rchan_flags.  Does some basic sanity
 *	checking but doesn't try to do anything smart.  In particular, the
 *	number of buffers must be a power of 2, and if the lockless scheme
 *	is being used, the sub-buffer size must also be a power of 2.  The
 *	locking scheme can use buffers of any size.
 */
static struct rchan *
rchan_create(const char *chanpath, 
	     int bufsize, 
	     int nbufs, 
	     u32 rchan_flags,
	     char *init_buf,
	     u32 init_buf_size,
	     int *err)
{
	int size_alloc;
	struct rchan *rchan = NULL;

	*err = 0;

	rchan = (struct rchan *)kmalloc(sizeof(struct rchan), GFP_KERNEL);
	if (rchan == NULL) {
		*err = -ENOMEM;
		return NULL;
	}
	rchan->buf = rchan->init_buf = NULL;

	*err = check_init_buf(init_buf, init_buf_size, bufsize, nbufs);
	if (*err)
		goto exit;
	
	if (nbufs == 1 && bufsize) {
		rchan->n_bufs = nbufs;
		rchan->buf_size = bufsize;
		size_alloc = bufsize;
		goto alloc;
	}
	
	if (bufsize <= 0 ||
	    (rchan_flags & RELAY_SCHEME_LOCKLESS && hweight32(bufsize) != 1) ||
	    hweight32(nbufs) != 1 ||
	    nbufs < RELAY_MIN_BUFS ||
	    nbufs > RELAY_MAX_BUFS) {
		*err = -EINVAL;
		goto exit;
	}

	size_alloc = FIX_SIZE(bufsize * nbufs);
	if (size_alloc > RELAY_MAX_BUF_SIZE) {
		*err = -EINVAL;
		goto exit;
	}
	rchan->n_bufs = nbufs;
	rchan->buf_size = bufsize;

	if (rchan_flags & RELAY_SCHEME_LOCKLESS) {
		offset_bits(rchan) = ffs(bufsize) - 1;
		offset_mask(rchan) =  RELAY_BUF_OFFSET_MASK(offset_bits(rchan));
		bufno_bits(rchan) = ffs(nbufs) - 1;
	}
alloc:
	if (rchan_alloc_id(rchan) == -1) {
		*err = -ENOMEM;
		goto exit;
	}

	if (init_buf == NULL) {
		*err = rchan_create_buf(rchan, size_alloc);
		if (*err) {
			rchan_free_id(rchan->id);
			goto exit;
		}
	} else
		rchan->buf = rchan->init_buf = init_buf;
	
	rchan->alloc_size = size_alloc;

	if (rchan_flags & RELAY_SCHEME_LOCKLESS)
		rchan->relay_ops = &lockless_ops;
	else
		rchan->relay_ops = &locking_ops;

exit:
	if (*err) {
		kfree(rchan);
		rchan = NULL;
	}

	return rchan;
}


static char tmpname[NAME_MAX];

/**
 *	rchan_create_dir - create directory for file
 *	@chanpath: path to file, including filename
 *	@residual: filename remaining after parse
 *	@topdir: the directory filename should be created in
 *
 *	Returns 0 if successful, negative otherwise.
 *
 *	Inspired by xlate_proc_name() in procfs.  Given a file path which
 *	includes the filename, creates any and all directories necessary 
 *	to create the file.
 */
static int 
rchan_create_dir(const char * chanpath, 
		 const char **residual, 
		 struct dentry **topdir)
{
	const char *cp = chanpath, *next;
	struct dentry *parent = NULL;
	int len, err = 0;
	
	while (1) {
		next = strchr(cp, '/');
		if (!next)
			break;

		len = next - cp;

		strncpy(tmpname, cp, len);
		tmpname[len] = '\0';
		err = relayfs_create_dir(tmpname, parent, &parent);
		if (err && (err != -EEXIST))
			return err;
		cp += len + 1;
	}

	*residual = cp;
	*topdir = parent;

	return err;
}

/**
 *	rchan_create_file - create file, including parent directories
 *	@chanpath: path to file, including filename
 *	@dentry: result dentry
 *	@data: data to associate with the file
 *
 *	Returns 0 if successful, negative otherwise.
 */
static int 
rchan_create_file(const char * chanpath, 
		  struct dentry **dentry, 
		  struct rchan * data,
		  int mode)
{
	int err;
	const char * fname;
	struct dentry *topdir;

	err = rchan_create_dir(chanpath, &fname, &topdir);
	if (err && (err != -EEXIST))
		return err;

	err = relayfs_create_file(fname, topdir, dentry, (void *)data, mode);

	return err;
}

/**
 *	relay_open - create a new file/channel buffer in relayfs
 *	@chanpath: name of file to create, including path
 *	@bufsize: size of sub-buffers
 *	@nbufs: number of sub-buffers
 *	@flags: channel attributes
 *	@callbacks: client callback functions
 *	@start_reserve: number of bytes to reserve at start of each sub-buffer
 *	@end_reserve: number of bytes to reserve at end of each sub-buffer
 *	@rchan_start_reserve: additional reserve at start of first sub-buffer
 *	@resize_min: minimum total buffer size, if set
 *	@resize_max: maximum total buffer size, if set
 *	@mode: the perms to be given to the relayfs file, 0 to accept defaults
 *	@init_buf: initial memory buffer to start out with, NULL if N/A
 *	@init_buf_size: initial memory buffer size to start out with, 0 if N/A
 *
 *	Returns channel id if successful, negative otherwise.
 *
 *	Creates a relay channel using the sizes and attributes specified.
 *	The default permissions, used if mode == 0 are S_IRUSR | S_IWUSR.  See
 *	Documentation/filesystems/relayfs.txt for details.
 */
int
relay_open(const char *chanpath,
	   int bufsize,
	   int nbufs,
	   u32 flags,
	   struct rchan_callbacks *channel_callbacks,
	   u32 start_reserve,
	   u32 end_reserve,
	   u32 rchan_start_reserve,
	   u32 resize_min,
	   u32 resize_max,
	   int mode,
	   char *init_buf,
	   u32 init_buf_size)
{
	int err;
	struct rchan *rchan;
	struct dentry *dentry;
	struct rchan_callbacks *callbacks = NULL;

	if (chanpath == NULL)
		return -EINVAL;

	if (nbufs != 1) {
		err = check_attribute_flags(&flags, resize_min ? 1 : 0);
		if (err)
			return err;
	}

	rchan = rchan_create(chanpath, bufsize, nbufs, flags, init_buf, init_buf_size, &err);

	if (err < 0)
		return err;

	/* Create file in fs */
	if ((err = rchan_create_file(chanpath, &dentry, rchan, mode)) < 0) {
		rchan_destroy_buf(rchan);
		rchan_free_id(rchan->id);
		kfree(rchan);
		return err;
	}

	rchan->dentry = dentry;

	if (channel_callbacks == NULL)
		callbacks = &default_channel_callbacks;
	else
		callbacks = channel_callbacks;

	if (callbacks->buffer_end == NULL)
		callbacks->buffer_end = buffer_end_default_callback;
	if (callbacks->buffer_start == NULL)
		callbacks->buffer_start = buffer_start_default_callback;
	if (callbacks->deliver == NULL)
		callbacks->deliver = deliver_default_callback;
	if (callbacks->user_deliver == NULL)
		callbacks->user_deliver = user_deliver_default_callback;
	if (callbacks->needs_resize == NULL)
		callbacks->needs_resize = needs_resize_default_callback;
	if (callbacks->fileop_notify == NULL)
		callbacks->fileop_notify = fileop_notify_default_callback;
	if (callbacks->ioctl == NULL)
		callbacks->ioctl = ioctl_default_callback;
	rchan->callbacks = callbacks;

	/* Just to let the client know the sizes used */
	rchan->callbacks->needs_resize(rchan->id,
				       RELAY_RESIZE_REPLACED,
				       rchan->buf_size,
				       rchan->n_bufs);

	rchan->flags = flags;
	rchan->start_reserve = start_reserve;
	rchan->end_reserve = end_reserve;
	rchan->rchan_start_reserve = rchan_start_reserve;

	__relay_reset(rchan, 1);

	if (resize_min > 0 && resize_max > 0 && 
	   resize_max < RELAY_MAX_TOTAL_BUF_SIZE) {
		rchan->resize_min = resize_min;
		rchan->resize_max = resize_max;
		init_shrink_timer(rchan);
	}

	rchan_get(rchan->id);

	return rchan->id;
}

/**
 *	relay_discard_init_buf - alloc channel buffer and copy init_buf into it
 *	@rchan_id: the channel id
 *
 *	Returns 0 if successful, negative otherwise.
 *
 *	NOTE: May sleep.  Should also be called only when the channel isn't
 *	actively being written into.
 */
int
relay_discard_init_buf(int rchan_id)
{
	struct rchan *rchan;
	int err = 0;
	
	rchan = rchan_get(rchan_id);
	if (rchan == NULL)
		return -EBADF;

	if (rchan->init_buf == NULL) {
		err = -EINVAL;
		goto out;
	}
	
	err = rchan_create_buf(rchan, rchan->alloc_size);
	if (err)
		goto out;
	
	memcpy(rchan->buf, rchan->init_buf, rchan->n_bufs * rchan->buf_size);
	rchan->init_buf = NULL;
out:
	rchan_put(rchan);
	
	return err;
}

/**
 *	relay_finalize - perform end-of-buffer processing for last buffer
 *	@rchan_id: the channel id
 *	@releasing: true if called when releasing file
 *
 *	Returns 0 if successful, negative otherwise.
 */
static int 
relay_finalize(int rchan_id)
{
	struct rchan *rchan = rchan_get(rchan_id);
	if (rchan == NULL)
		return -EBADF;

	if (rchan->finalized == 0) {
		rchan->relay_ops->finalize(rchan);
		rchan->finalized = 1;
	}

	if (waitqueue_active(&rchan->read_wait)) {
		PREPARE_WORK(&rchan->wake_readers, wakeup_readers, rchan);
		schedule_delayed_work(&rchan->wake_readers, 1);
	}

	rchan_put(rchan);

	return 0;
}

/**
 *	restore_callbacks - restore default channel callbacks
 *	@rchan: the channel
 *
 *	Restore callbacks to the default versions.
 */
static inline void
restore_callbacks(struct rchan *rchan)
{
	if (rchan->callbacks != &default_channel_callbacks)
		rchan->callbacks = &default_channel_callbacks;
}

/**
 *	relay_close - close the channel
 *	@rchan_id: relay channel id
 *	
 *	Finalizes the last sub-buffer and marks the channel as finalized.
 *	The channel buffer and channel data structure are then freed
 *	automatically when the last reference to the channel is given up.
 */
int 
relay_close(int rchan_id)
{
	int err;
	struct rchan *rchan;

	if ((rchan_id < 0) || (rchan_id >= RELAY_MAX_CHANNELS))
		return -EBADF;

	err = relay_finalize(rchan_id);

	if (!err) {
		read_lock(&rchan_table_lock);
		rchan = rchan_table[rchan_id];
		read_unlock(&rchan_table_lock);

		if (rchan) {
			restore_callbacks(rchan);
			if (rchan->resize_min)
				del_timer(&rchan->shrink_timer);
			rchan_put(rchan);
		}
	}
	
	return err;
}

/**
 *	relay_write - reserve a slot in the channel and write data into it
 *	@rchan_id: relay channel id
 *	@data_ptr: data to be written into reserved slot
 *	@count: number of bytes to write
 *	@td_offset: optional offset where time delta should be written
 *	@wrote_pos: optional ptr returning buf pos written to, ignored if NULL 
 *
 *	Returns the number of bytes written, 0 or negative on failure.
 *
 *	Reserves space in the channel and writes count bytes of data_ptr
 *	to it.  Automatically performs any necessary locking, depending
 *	on the scheme and SMP usage in effect (no locking is done for the
 *	lockless scheme regardless of usage). 
 *
 *	If td_offset is >= 0, the internal time delta calculated when
 *	slot was reserved will be written at that offset.
 *
 *	If wrote_pos is non-NULL, it will receive the location the data
 *	was written to, which may be needed for some applications but is not
 *	normally interesting.
 */
int
relay_write(int rchan_id, 
	    const void *data_ptr, 
	    size_t count,
	    int td_offset,
	    void **wrote_pos)
{
	unsigned long flags;
	char *reserved, *write_pos;
	int bytes_written = 0;
	int reserve_code, interrupting;
	struct timeval ts;
	u32 td;
	struct rchan *rchan;
	
	rchan = rchan_get(rchan_id);
	if (rchan == NULL)
		return -EBADF;

	relay_lock_channel(rchan, flags); /* nop for lockless */

	write_pos = reserved = relay_reserve(rchan, count, &ts, &td, 
					     &reserve_code, &interrupting);

	if (reserved != NULL) {
		relay_write_direct(write_pos, data_ptr, count);
		if ((td_offset >= 0) && (td_offset < count - sizeof(td)))
			*((u32 *)(reserved + td_offset)) = td;
		bytes_written = count;
	} else if (reserve_code == RELAY_WRITE_TOO_LONG)
		bytes_written = -EINVAL;

	if (bytes_written > 0)
		relay_commit(rchan, reserved, bytes_written, reserve_code, interrupting);

	relay_unlock_channel(rchan, flags); /* nop for lockless */

	rchan_put(rchan);

	if (wrote_pos)
		*wrote_pos = reserved;
	
	return bytes_written;
}

/**
 *	wakeup_writers - wake up VFS writers waiting on a channel
 *	@private: the channel
 *
 *	This is the work function used to defer writer waking.  The
 *	reason waking is deferred is that calling directly from 
 *	buffers_consumed causes problems if you're writing from say 
 *	the scheduler.
 */
static void 
wakeup_writers(void *private)
{
	struct rchan *rchan = (struct rchan *)private;
	
	wake_up_interruptible(&rchan->write_wait);
}


/**
 *	__relay_buffers_consumed - internal version of relay_buffers_consumed
 *	@rchan: the relay channel
 *	@bufs_consumed: number of buffers to add to current count for channel
 *	
 *	Internal - updates the channel's consumed buffer count.
 */
static void
__relay_buffers_consumed(struct rchan *rchan, u32 bufs_consumed)
{
	rchan->bufs_consumed += bufs_consumed;
	
	if (rchan->bufs_consumed > rchan->bufs_produced)
		rchan->bufs_consumed = rchan->bufs_produced;
	
	atomic_set(&rchan->suspended, 0);

	PREPARE_WORK(&rchan->wake_writers, wakeup_writers, rchan);
	schedule_delayed_work(&rchan->wake_writers, 1);
}

/**
 *	__reader_buffers_consumed - update reader/channel consumed buffer count
 *	@reader: channel reader
 *	@bufs_consumed: number of buffers to add to current count for channel
 *	
 *	Internal - updates the reader's consumed buffer count.  If the reader's
 *	resulting total is greater than the channel's, update the channel's.
*/
static void
__reader_buffers_consumed(struct rchan_reader *reader, u32 bufs_consumed)
{
	reader->bufs_consumed += bufs_consumed;
	
	if (reader->bufs_consumed > reader->rchan->bufs_consumed)
		__relay_buffers_consumed(reader->rchan, bufs_consumed);
}

/**
 *	relay_buffers_consumed - add to the # buffers consumed for the channel
 *	@reader: channel reader
 *	@bufs_consumed: number of buffers to add to current count for channel
 *	
 *	Adds to the channel's consumed buffer count.  buffers_consumed should
 *	be the number of buffers newly consumed, not the total number consumed.
 *
 *	NOTE: kernel clients don't need to call this function if the reader
 *	is auto-consuming or the channel is MODE_CONTINUOUS.
 */
void 
relay_buffers_consumed(struct rchan_reader *reader, u32 bufs_consumed)
{
	if (reader && reader->rchan)
		__reader_buffers_consumed(reader, bufs_consumed);
}

/**
 *	__relay_bytes_consumed - internal version of relay_bytes_consumed 
 *	@rchan: the relay channel
 *	@bytes_consumed: number of bytes to add to current count for channel
 *	@read_offset: where the bytes were consumed from
 *	
 *	Internal - updates the channel's consumed count.
*/
static void
__relay_bytes_consumed(struct rchan *rchan, u32 bytes_consumed, u32 read_offset)
{
	u32 consuming_idx;
	u32 unused;

	consuming_idx = read_offset / rchan->buf_size;

	if (consuming_idx >= rchan->n_bufs)
		consuming_idx = rchan->n_bufs - 1;
	rchan->bytes_consumed += bytes_consumed;

	unused = rchan->unused_bytes[consuming_idx];
	
	if (rchan->bytes_consumed + unused >= rchan->buf_size) {
		__relay_buffers_consumed(rchan, 1);
		rchan->bytes_consumed = 0;
	}
}

/**
 *	__reader_bytes_consumed - update reader/channel consumed count
 *	@reader: channel reader
 *	@bytes_consumed: number of bytes to add to current count for channel
 *	@read_offset: where the bytes were consumed from
 *	
 *	Internal - updates the reader's consumed count.  If the reader's
 *	resulting total is greater than the channel's, update the channel's.
*/
static void
__reader_bytes_consumed(struct rchan_reader *reader, u32 bytes_consumed, u32 read_offset)
{
	u32 consuming_idx;
	u32 unused;

	consuming_idx = read_offset / reader->rchan->buf_size;

	if (consuming_idx >= reader->rchan->n_bufs)
		consuming_idx = reader->rchan->n_bufs - 1;

	reader->bytes_consumed += bytes_consumed;
	
	unused = reader->rchan->unused_bytes[consuming_idx];
	
	if (reader->bytes_consumed + unused >= reader->rchan->buf_size) {
		reader->bufs_consumed++;
		reader->bytes_consumed = 0;
	}

	if ((reader->bufs_consumed > reader->rchan->bufs_consumed) ||
	    ((reader->bufs_consumed == reader->rchan->bufs_consumed) &&
	     (reader->bytes_consumed > reader->rchan->bytes_consumed)))
		__relay_bytes_consumed(reader->rchan, bytes_consumed, read_offset);
}

/**
 *	relay_bytes_consumed - add to the # bytes consumed for the channel
 *	@reader: channel reader
 *	@bytes_consumed: number of bytes to add to current count for channel
 *	@read_offset: where the bytes were consumed from
 *	
 *	Adds to the channel's consumed count.  bytes_consumed should be the
 *	number of bytes actually read e.g. return value of relay_read() and
 *	the read_offset should be the actual offset the bytes were read from
 *	e.g. the actual_read_offset set by relay_read(). See
 *	Documentation/filesystems/relayfs.txt for more details.
 *
 *	NOTE: kernel clients don't need to call this function if the reader
 *	is auto-consuming or the channel is MODE_CONTINUOUS.
 */
void
relay_bytes_consumed(struct rchan_reader *reader, u32 bytes_consumed, u32 read_offset)
{
	if (reader && reader->rchan)
		__reader_bytes_consumed(reader, bytes_consumed, read_offset);
}

/**
 *	update_readers_consumed - apply offset change to reader
 *	@rchan: the channel
 *
 *	Apply the consumed counts to all readers open on the channel.
 */
void
update_readers_consumed(struct rchan *rchan, u32 bufs_consumed, u32 bytes_consumed)
{
	struct list_head *p;
	struct rchan_reader *reader;
	
	read_lock(&rchan->open_readers_lock);
	list_for_each(p, &rchan->open_readers) {
		reader = list_entry(p, struct rchan_reader, list);
		reader->bufs_consumed = bufs_consumed;
		reader->bytes_consumed = bytes_consumed;
		if (reader->vfs_reader) 
			reader->pos.file->f_pos = 0;
		else
			reader->pos.f_pos = 0;
		reader->offset_changed = 1;
	}
	read_unlock(&rchan->open_readers_lock);
}

/**
 *	do_read - utility function to do the actual read to user
 *	@rchan: the channel
 *	@buf: user buf to read into, NULL if just getting info
 *	@count: bytes requested
 *	@read_offset: offset into channel
 *	@new_offset: new offset into channel after read
 *	@actual_read_offset: read offset actually used
 *
 *	Returns the number of bytes read, 0 if none.
 */
static ssize_t
do_read(struct rchan *rchan, char *buf, size_t count, u32 read_offset, u32 *new_offset, u32 *actual_read_offset)
{
	u32 read_bufno, cur_bufno;
	u32 avail_offset, cur_idx, max_offset, buf_end_offset;
	u32 avail_count, buf_size;
	int unused_bytes = 0;
	size_t read_count = 0;
	u32 last_buf_byte_offset;

	*actual_read_offset = read_offset;
	
	buf_size = rchan->buf_size;
	if (unlikely(!buf_size)) BUG();

	read_bufno = read_offset / buf_size;
	if (unlikely(read_bufno >= RELAY_MAX_BUFS)) BUG();
	unused_bytes = rchan->unused_bytes[read_bufno];

	avail_offset = cur_idx = relay_get_offset(rchan, &max_offset);

	if (cur_idx == read_offset) {
		if (atomic_read(&rchan->suspended) == 1) {
			read_offset += 1;
			if (read_offset >= max_offset)
				read_offset = 0;
			*actual_read_offset = read_offset;
		} else {
			*new_offset = read_offset;
			return 0;
		}
	} else {
		last_buf_byte_offset = (read_bufno + 1) * buf_size - 1;
		if (read_offset == last_buf_byte_offset) {
			if (unused_bytes != 1) {
				read_offset += 1;
				if (read_offset >= max_offset)
					read_offset = 0;
				*actual_read_offset = read_offset;
			}
		}
	}

	read_bufno = read_offset / buf_size;
	if (unlikely(read_bufno >= RELAY_MAX_BUFS)) BUG();
	unused_bytes = rchan->unused_bytes[read_bufno];

	cur_bufno = cur_idx / buf_size;

	buf_end_offset = (read_bufno + 1) * buf_size - unused_bytes;
	if (avail_offset > buf_end_offset)
		avail_offset = buf_end_offset;
	else if (avail_offset < read_offset)
		avail_offset = buf_end_offset;
	avail_count = avail_offset - read_offset;
	read_count = avail_count >= count ? count : avail_count;

	if (read_count && buf != NULL)
		if (copy_to_user(buf, rchan->buf + read_offset, read_count))
			return -EFAULT;

	if (read_bufno == cur_bufno)
		if (read_count && (read_offset + read_count >= buf_end_offset) && (read_offset + read_count <= cur_idx)) {
			*new_offset = cur_idx;
			return read_count;
		}

	if (read_offset + read_count + unused_bytes > max_offset)
		*new_offset = 0;
	else if (read_offset + read_count >= buf_end_offset)
		*new_offset = read_offset + read_count + unused_bytes;
	else
		*new_offset = read_offset + read_count;

	return read_count;
}

/**
 *	__relay_read - read bytes from channel, relative to current reader pos
 *	@reader: channel reader
 *	@buf: user buf to read into, NULL if just getting info
 *	@count: bytes requested
 *	@read_offset: offset into channel
 *	@new_offset: new offset into channel after read
 *	@actual_read_offset: read offset actually used
 *	@wait: if non-zero, wait for something to read
 *
 *	Internal - see relay_read() for details.
 *
 *	Returns the number of bytes read, 0 if none, negative on failure.
 */
static ssize_t
__relay_read(struct rchan_reader *reader, char *buf, size_t count, u32 read_offset, u32 *new_offset, u32 *actual_read_offset, int wait)
{
	int err = 0;
	size_t read_count = 0;
	struct rchan *rchan = reader->rchan;

	if (!wait && !rchan->initialized)
		return -EAGAIN;

	if (using_lockless(rchan))
		read_offset &= idx_mask(rchan);

	if (read_offset >= rchan->n_bufs * rchan->buf_size) {
		*new_offset = 0;
		if (!wait)
			return -EAGAIN;
		else
			return -EINTR;
	}
	
	if (buf != NULL && wait) {
		err = wait_event_interruptible(rchan->read_wait,
		       ((rchan->finalized == 1) ||
			(atomic_read(&rchan->suspended) == 1) ||
			(relay_get_offset(rchan, NULL) != read_offset)));

		if (rchan->finalized)
			return 0;

		if (reader->offset_changed) {
			reader->offset_changed = 0;
			return -EINTR;
		}
		
		if (err)
			return err;
	}

	read_count = do_read(rchan, buf, count, read_offset, new_offset, actual_read_offset);

	if (read_count < 0)
		err = read_count;
	
	if (err)
		return err;
	else
		return read_count;
}

/**
 *	relay_read - read bytes from channel, relative to current reader pos
 *	@reader: channel reader
 *	@buf: user buf to read into, NULL if just getting info
 *	@count: bytes requested
 *	@wait: if non-zero, wait for something to read
 *	@actual_read_offset: set read offset actually used, must not be NULL
 *
 *	Reads count bytes from the channel, or as much as is available within
 *	the sub-buffer currently being read.  The read offset that will be
 *	read from is the position contained within the reader object.  If the
 *	wait flag is set, buf is non-NULL, and there is nothing available,
 *	it will wait until there is.  If the wait flag is 0 and there is
 *	nothing available, -EAGAIN is returned.  If buf is NULL, the value
 *	returned is the number of bytes that would have been read.
 *	actual_read_offset is the value that should be passed as the read
 *	offset to relay_bytes_consumed, needed only if the reader is not
 *	auto-consuming and the channel is MODE_NO_OVERWRITE, but in any case,
 *	it must not be NULL.  See Documentation/filesystems/relayfs.txt for
 *	more details.
 */
ssize_t
relay_read(struct rchan_reader *reader, char *buf, size_t count, int wait, u32 *actual_read_offset)
{
	u32 new_offset;
	u32 read_offset;
	ssize_t read_count;
	
	if (reader == NULL || reader->rchan == NULL)
		return -EBADF;

	if (actual_read_offset == NULL)
		return -EINVAL;

	if (reader->vfs_reader)
		read_offset = (u32)(reader->pos.file->f_pos);
	else
		read_offset = reader->pos.f_pos;
	*actual_read_offset = read_offset;
	
	read_count = __relay_read(reader, buf, count, read_offset,
				  &new_offset, actual_read_offset, wait);

	if (read_count < 0)
		return read_count;

	if (reader->vfs_reader)
		reader->pos.file->f_pos = new_offset;
	else
		reader->pos.f_pos = new_offset;

	if (reader->auto_consume && ((read_count) || (new_offset != read_offset)))
		__reader_bytes_consumed(reader, read_count, *actual_read_offset);

	if (read_count == 0 && !wait)
		return -EAGAIN;
	
	return read_count;
}

/**
 *	relay_bytes_avail - number of bytes available in current sub-buffer
 *	@reader: channel reader
 *	
 *	Returns the number of bytes available relative to the reader's
 *	current read position within the corresponding sub-buffer, 0 if
 *	there is nothing available.  See Documentation/filesystems/relayfs.txt
 *	for more details.
 */
ssize_t
relay_bytes_avail(struct rchan_reader *reader)
{
	u32 f_pos;
	u32 new_offset;
	u32 actual_read_offset;
	ssize_t bytes_read;
	
	if (reader == NULL || reader->rchan == NULL)
		return -EBADF;
	
	if (reader->vfs_reader)
		f_pos = (u32)reader->pos.file->f_pos;
	else
		f_pos = reader->pos.f_pos;
	new_offset = f_pos;

	bytes_read = __relay_read(reader, NULL, reader->rchan->buf_size,
				  f_pos, &new_offset, &actual_read_offset, 0);

	if ((new_offset != f_pos) &&
	    ((bytes_read == -EINTR) || (bytes_read == 0)))
		bytes_read = -EAGAIN;
	else if ((bytes_read < 0) && (bytes_read != -EAGAIN))
		bytes_read = 0;

	return bytes_read;
}

/**
 *	rchan_empty - boolean, is the channel empty wrt reader?
 *	@reader: channel reader
 *	
 *	Returns 1 if the channel is empty, 0 otherwise.
 */
int
rchan_empty(struct rchan_reader *reader)
{
	ssize_t avail_count;
	u32 buffers_ready;
	struct rchan *rchan = reader->rchan;
	u32 cur_idx, curbuf_bytes;
	int mapped;

	if (atomic_read(&rchan->suspended) == 1)
		return 0;

	mapped = atomic_read(&rchan->mapped);
	
	if (mapped && bulk_delivery(rchan)) {
		buffers_ready = rchan->bufs_produced - rchan->bufs_consumed;
		return buffers_ready ? 0 : 1;
	}

	if (mapped && packet_delivery(rchan)) {
		buffers_ready = rchan->bufs_produced - rchan->bufs_consumed;
		if (buffers_ready)
			return 0;
		else {
			cur_idx = relay_get_offset(rchan, NULL);
			curbuf_bytes = cur_idx % rchan->buf_size;
			return curbuf_bytes == rchan->bytes_consumed ? 1 : 0;
		}
	}

	avail_count = relay_bytes_avail(reader);

	return avail_count ? 0 : 1;
}

/**
 *	rchan_full - boolean, is the channel full wrt consuming reader?
 *	@reader: channel reader
 *	
 *	Returns 1 if the channel is full, 0 otherwise.
 */
int
rchan_full(struct rchan_reader *reader)
{
	u32 buffers_ready;
	struct rchan *rchan = reader->rchan;

	if (mode_continuous(rchan))
		return 0;

	buffers_ready = rchan->bufs_produced - rchan->bufs_consumed;

	return buffers_ready > reader->rchan->n_bufs - 1 ? 1 : 0;
}

/**
 *	relay_info - get status and other information about a relay channel
 *	@rchan_id: relay channel id
 *	@rchan_info: pointer to the rchan_info struct to be filled in
 *	
 *	Fills in an rchan_info struct with channel status and attribute 
 *	information.  See Documentation/filesystems/relayfs.txt for details.
 *
 *	Returns 0 if successful, negative otherwise.
 */
int 
relay_info(int rchan_id, struct rchan_info *rchan_info)
{
	int i;
	struct rchan *rchan;

	rchan = rchan_get(rchan_id);
	if (rchan == NULL)
		return -EBADF;

	rchan_info->flags = rchan->flags;
	rchan_info->buf_size = rchan->buf_size;
	rchan_info->buf_addr = rchan->buf;
	rchan_info->alloc_size = rchan->alloc_size;
	rchan_info->n_bufs = rchan->n_bufs;
	rchan_info->cur_idx = relay_get_offset(rchan, NULL);
	rchan_info->bufs_produced = rchan->bufs_produced;
	rchan_info->bufs_consumed = rchan->bufs_consumed;
	rchan_info->buf_id = rchan->buf_id;

	for (i = 0; i < rchan->n_bufs; i++) {
		rchan_info->unused_bytes[i] = rchan->unused_bytes[i];
		if (using_lockless(rchan))
			rchan_info->buffer_complete[i] = (atomic_read(&fill_count(rchan, i)) == rchan->buf_size);
		else
			rchan_info->buffer_complete[i] = 0;
	}

	rchan_put(rchan);

	return 0;
}

/**
 *	__add_rchan_reader - creates and adds a reader to a channel
 *	@rchan: relay channel
 *	@filp: the file associated with rchan, if applicable
 *	@auto_consume: boolean, whether reader's reads automatically consume
 *	@map_reader: boolean, whether reader's reading via a channel mapping
 *
 *	Returns a pointer to the reader object create, NULL if unsuccessful
 *
 *	Creates and initializes an rchan_reader object for reading the channel.
 *	If filp is non-NULL, the reader is a VFS reader, otherwise not.
 *
 *	If the reader is a map reader, it isn't considered a VFS reader for
 *	our purposes.  Also, map_readers can't be auto-consuming.
 */
struct rchan_reader *
__add_rchan_reader(struct rchan *rchan, struct file *filp, int auto_consume, int map_reader)
{
	struct rchan_reader *reader;
	u32 will_read;
	
	reader = kmalloc(sizeof(struct rchan_reader), GFP_KERNEL);

	if (reader) {
		write_lock(&rchan->open_readers_lock);
		reader->rchan = rchan;
		if (filp) {
			reader->vfs_reader = 1;
			reader->pos.file = filp;
		} else {
			reader->vfs_reader = 0;
			reader->pos.f_pos = 0;
		}
		reader->map_reader = map_reader;
		reader->auto_consume = auto_consume;

		if (!map_reader) {
			will_read = rchan->bufs_produced % rchan->n_bufs;
			if (!will_read && atomic_read(&rchan->suspended))
				will_read = rchan->n_bufs;
			reader->bufs_consumed = rchan->bufs_produced - will_read;
			rchan->bufs_consumed = reader->bufs_consumed;
			rchan->bytes_consumed = reader->bytes_consumed = 0;
			reader->offset_changed = 0;
		}
		
		list_add(&reader->list, &rchan->open_readers);
		write_unlock(&rchan->open_readers_lock);
	}

	return reader;
}

/**
 *	add_rchan_reader - create a reader for a channel
 *	@rchan_id: relay channel handle
 *	@auto_consume: boolean, whether reader's reads automatically consume
 *
 *	Returns a pointer to the reader object created, NULL if unsuccessful
 *
 *	Creates and initializes an rchan_reader object for reading the channel.
 *	This function is useful only for non-VFS readers.
 */
struct rchan_reader *
add_rchan_reader(int rchan_id, int auto_consume)
{
	struct rchan *rchan = rchan_get(rchan_id);
	if (rchan == NULL)
		return NULL;

	return __add_rchan_reader(rchan, NULL, auto_consume, 0);
}

/**
 *	add_map_reader - create a map reader for a channel
 *	@rchan_id: relay channel handle
 *
 *	Returns a pointer to the reader object created, NULL if unsuccessful
 *
 *	Creates and initializes an rchan_reader object for reading the channel.
 *	This function is useful only for map readers.
 */
struct rchan_reader *
add_map_reader(int rchan_id)
{
	struct rchan *rchan = rchan_get(rchan_id);
	if (rchan == NULL)
		return NULL;

	return __add_rchan_reader(rchan, NULL, 0, 1);
}

/**
 *	__remove_rchan_reader - destroy a channel reader
 *	@reader: channel reader
 *
 *	Internal - removes reader from the open readers list, and frees it.
 */
void
__remove_rchan_reader(struct rchan_reader *reader)
{
	struct list_head *p;
	struct rchan_reader *found_reader = NULL;
	
	write_lock(&reader->rchan->open_readers_lock);
	list_for_each(p, &reader->rchan->open_readers) {
		found_reader = list_entry(p, struct rchan_reader, list);
		if (found_reader == reader) {
			list_del(&found_reader->list);
			break;
		}
	}
	write_unlock(&reader->rchan->open_readers_lock);

	if (found_reader)
		kfree(found_reader);
}

/**
 *	remove_rchan_reader - destroy a channel reader
 *	@reader: channel reader
 *
 *	Finds and removes the given reader from the channel.  This function
 *	is useful only for non-VFS readers.
 *
 *	Returns 0 if successful, negative otherwise.
 */
int 
remove_rchan_reader(struct rchan_reader *reader)
{
	int err = 0;
	
	if (reader) {
		rchan_put(reader->rchan);
		__remove_rchan_reader(reader);
	} else
		err = -EINVAL;

	return err;
}

/**
 *	remove_map_reader - destroy a map reader
 *	@reader: channel reader
 *
 *	Finds and removes the given map reader from the channel.  This function
 *	is useful only for map readers.
 *
 *	Returns 0 if successful, negative otherwise.
 */
int 
remove_map_reader(struct rchan_reader *reader)
{
	return remove_rchan_reader(reader);
}

EXPORT_SYMBOL(relay_open);
EXPORT_SYMBOL(relay_close);
EXPORT_SYMBOL(relay_reset);
EXPORT_SYMBOL(relay_reserve);
EXPORT_SYMBOL(relay_commit);
EXPORT_SYMBOL(relay_read);
EXPORT_SYMBOL(relay_write);
EXPORT_SYMBOL(relay_bytes_avail);
EXPORT_SYMBOL(relay_buffers_consumed);
EXPORT_SYMBOL(relay_bytes_consumed);
EXPORT_SYMBOL(relay_info);
EXPORT_SYMBOL(relay_discard_init_buf);


