/*
 * linux/include/linux/relayfs_fs.h
 *
 * Copyright (C) 2002, 2003 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 1999, 2000, 2001, 2002 - Karim Yaghmour (karim@opersys.com)
 *
 * RelayFS definitions and declarations
 *
 * Please see Documentation/filesystems/relayfs.txt for more info.
 */

#ifndef _LINUX_RELAYFS_FS_H
#define _LINUX_RELAYFS_FS_H

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/fs.h>

/*
 * Tracks changes to rchan struct
 */
#define RELAYFS_CHANNEL_VERSION		1

/*
 * Maximum number of simultaneously open channels
 */
#define RELAY_MAX_CHANNELS		256

/*
 * Relay properties
 */
#define RELAY_MIN_BUFS			2
#define RELAY_MIN_BUFSIZE		4096
#define RELAY_MAX_BUFS			256
#define RELAY_MAX_BUF_SIZE		0x1000000
#define RELAY_MAX_TOTAL_BUF_SIZE	0x8000000

/*
 * Lockless scheme utility macros
 */
#define RELAY_MAX_BUFNO(bufno_bits) (1UL << (bufno_bits))
#define RELAY_BUF_SIZE(offset_bits) (1UL << (offset_bits))
#define RELAY_BUF_OFFSET_MASK(offset_bits) (RELAY_BUF_SIZE(offset_bits) - 1)
#define RELAY_BUFNO_GET(index, offset_bits) ((index) >> (offset_bits))
#define RELAY_BUF_OFFSET_GET(index, mask) ((index) & (mask))
#define RELAY_BUF_OFFSET_CLEAR(index, mask) ((index) & ~(mask))

/*
 * Flags returned by relay_reserve()
 */
#define RELAY_BUFFER_SWITCH_NONE	0x0
#define RELAY_WRITE_DISCARD_NONE	0x0
#define RELAY_BUFFER_SWITCH		0x1
#define RELAY_WRITE_DISCARD		0x2
#define RELAY_WRITE_TOO_LONG		0x4

/*
 * Relay attribute flags
 */
#define RELAY_DELIVERY_BULK		0x1
#define RELAY_DELIVERY_PACKET		0x2
#define RELAY_SCHEME_LOCKLESS		0x4
#define RELAY_SCHEME_LOCKING		0x8
#define RELAY_SCHEME_ANY		0xC
#define RELAY_TIMESTAMP_TSC		0x10
#define RELAY_TIMESTAMP_GETTIMEOFDAY	0x20
#define RELAY_TIMESTAMP_ANY		0x30
#define RELAY_USAGE_SMP			0x40
#define RELAY_USAGE_GLOBAL		0x80
#define RELAY_MODE_CONTINUOUS		0x100
#define RELAY_MODE_NO_OVERWRITE		0x200

/*
 * Flags for needs_resize() callback
 */
#define RELAY_RESIZE_NONE	0x0
#define RELAY_RESIZE_EXPAND	0x1
#define RELAY_RESIZE_SHRINK	0x2
#define RELAY_RESIZE_REPLACE	0x4
#define RELAY_RESIZE_REPLACED	0x8

/*
 * Values for fileop_notify() callback
 */
enum relay_fileop
{
	RELAY_FILE_OPEN,
	RELAY_FILE_CLOSE,
	RELAY_FILE_MAP,
	RELAY_FILE_UNMAP
};

/*
 * Data structure returned by relay_info()
 */
struct rchan_info
{
	u32 flags;		/* relay attribute flags for channel */
	u32 buf_size;		/* channel's sub-buffer size */
	char *buf_addr;		/* address of channel start */
	u32 alloc_size;		/* total buffer size actually allocated */
	u32 n_bufs;		/* number of sub-buffers in channel */
	u32 cur_idx;		/* current write index into channel */
	u32 bufs_produced;	/* current count of sub-buffers produced */
	u32 bufs_consumed;	/* current count of sub-buffers consumed */
	u32 buf_id;		/* buf_id of current sub-buffer */
	int buffer_complete[RELAY_MAX_BUFS];	/* boolean per sub-buffer */
	int unused_bytes[RELAY_MAX_BUFS];	/* count per sub-buffer */
};

/*
 * Relay channel client callbacks
 */
struct rchan_callbacks
{
	/*
	 * buffer_start - called at the beginning of a new sub-buffer
	 * @rchan_id: the channel id
	 * @current_write_pos: position in sub-buffer client should write to
	 * @buffer_id: the id of the new sub-buffer
	 * @start_time: the timestamp associated with the start of sub-buffer
	 * @start_tsc: the TSC associated with the timestamp, if using_tsc
	 * @using_tsc: boolean, indicates whether start_tsc is valid
	 *
	 * Return value should be the number of bytes written by the client.
	 *
	 * See Documentation/filesystems/relayfs.txt for details.
	 */
	int (*buffer_start) (int rchan_id,
			     char *current_write_pos,
			     u32 buffer_id,
			     struct timeval start_time,
			     u32 start_tsc,
			     int using_tsc);

	/*
	 * buffer_end - called at the end of a sub-buffer
	 * @rchan_id: the channel id
	 * @current_write_pos: position in sub-buffer of end of data
	 * @end_of_buffer: the position of the end of the sub-buffer
	 * @end_time: the timestamp associated with the end of the sub-buffer
	 * @end_tsc: the TSC associated with the end_time, if using_tsc
	 * @using_tsc: boolean, indicates whether end_tsc is valid
	 *
	 * Return value should be the number of bytes written by the client.
	 *
	 * See Documentation/filesystems/relayfs.txt for details.
	 */
	int (*buffer_end) (int rchan_id,
			   char *current_write_pos,
			   char *end_of_buffer,
			   struct timeval end_time,
			   u32 end_tsc,
			   int using_tsc);

	/*
	 * deliver - called when data is ready for the client
	 * @rchan_id: the channel id
	 * @from: the start of the delivered data
	 * @len: the length of the delivered data
	 *
	 * See Documentation/filesystems/relayfs.txt for details.
	 */
	void (*deliver) (int rchan_id, char *from, u32 len);

	/*
	 * user_deliver - called when data has been written from userspace
	 * @rchan_id: the channel id
	 * @from: the start of the delivered data
	 * @len: the length of the delivered data
	 *
	 * See Documentation/filesystems/relayfs.txt for details.
	 */
	void (*user_deliver) (int rchan_id, char *from, u32 len);

	/*
	 * needs_resize - called when a resizing event occurs
	 * @rchan_id: the channel id
	 * @resize_type: the type of resizing event
	 * @suggested_buf_size: the suggested new sub-buffer size
	 * @suggested_buf_size: the suggested new number of sub-buffers
	 *
	 * See Documentation/filesystems/relayfs.txt for details.
	 */
	void (*needs_resize)(int rchan_id,
			     int resize_type,
			     u32 suggested_buf_size,
			     u32 suggested_n_bufs);

	/*
	 * fileop_notify - called on open/close/mmap/munmap of a relayfs file
	 * @rchan_id: the channel id
	 * @filp: relayfs file pointer
	 * @fileop: which file operation is in progress
	 *
	 * The return value can direct the outcome of the operation.
	 *
	 * See Documentation/filesystems/relayfs.txt for details.
	 */
        int (*fileop_notify)(int rchan_id,
			     struct file *filp,
			     enum relay_fileop fileop);
};

/*
 * Lockless scheme-specific data
 */
struct lockless_rchan
{
	u8 bufno_bits;		/* # bits used for sub-buffer id */
	u8 offset_bits;		/* # bits used for offset within sub-buffer */
	u32 index;		/* current index = sub-buffer id and offset */
	u32 offset_mask;	/* used to obtain offset portion of index */
	u32 index_mask;		/* used to mask off unused bits index */
	atomic_t fill_count[RELAY_MAX_BUFS];	/* fill count per sub-buffer */
};

/*
 * Locking scheme-specific data
 */
struct locking_rchan
{
	char *write_buf;		/* start of write sub-buffer */
	char *write_buf_end;		/* end of write sub-buffer */
	char *current_write_pos;	/* current write pointer */
	char *write_limit;		/* takes reserves into account */
	char *in_progress_event_pos;	/* used for interrupted writes */
	u16 in_progress_event_size;	/* used for interrupted writes */
	char *interrupted_pos;		/* used for interrupted writes */
	u16 interrupting_size;		/* used for interrupted writes */
	spinlock_t lock;		/* channel lock for locking scheme */
};

struct relay_ops;

/*
 * Offset resizing data structure
 */
struct resize_offset
{
	u32 ge;
	u32 le;
	int delta;
};

/*
 * Relay channel data structure
 */
struct rchan
{
	u32 version;			/* the version of this struct */
	char *buf;			/* the channel buffer */
	union
	{
		struct lockless_rchan lockless;
		struct locking_rchan locking;
	} scheme;			/* scheme-specific channel data */

	int id;				/* the channel id */
	struct rchan_callbacks *callbacks;	/* client callbacks */
	u32 flags;			/* relay channel attributes */
	u32 buf_id;			/* current sub-buffer id */
	u32 buf_idx;			/* current sub-buffer index */

	atomic_t mapped;		/* map count */

	atomic_t suspended;		/* channel suspended i.e full? */
	int half_switch;		/* used internally for suspend */

	struct timeval  buf_start_time;	/* current sub-buffer start time */
	u32 buf_start_tsc;		/* current sub-buffer start TSC */
	
	u32 buf_size;			/* sub-buffer size */
	u32 alloc_size;			/* total buffer size allocated */
	u32 n_bufs;			/* number of sub-buffers */

	u32 bufs_produced;		/* count of sub-buffers produced */
	u32 bufs_consumed;		/* count of sub-buffers consumed */
	u32 bytes_consumed;		/* bytes consumed in cur sub-buffer */

	int initialized;		/* first buffer initialized? */
	int finalized;			/* channel finalized? */

	u32 start_reserve;		/* reserve at start of sub-buffers */
	u32 end_reserve;		/* reserve at end of sub-buffers */
	u32 rchan_start_reserve;	/* additional reserve sub-buffer 0 */
	
	struct dentry *dentry;		/* channel file dentry */

	wait_queue_head_t read_wait;	/* VFS read wait queue */
	wait_queue_head_t write_wait;	/* VFS write wait queue */
	struct work_struct wake_readers; /* reader wake-up work struct */
	struct work_struct wake_writers; /* reader wake-up work struct */
	atomic_t refcount;		/* channel refcount */

	struct relay_ops *relay_ops;	/* scheme-specific channel ops */

	int unused_bytes[RELAY_MAX_BUFS]; /* unused count per sub-buffer */

	struct semaphore resize_sem;	/* serializes alloc/repace */
	struct work_struct work;	/* resize allocation work struct */

	struct list_head open_readers;	/* open readers for this channel */
	rwlock_t open_readers_lock;	/* protection for open_readers list */

	char *init_buf;			/* init channel buffer, if non-NULL */
	
	u32 resize_min;			/* minimum resized total buffer size */
	u32 resize_max;			/* maximum resized total buffer size */
	char *resize_buf;		/* for autosize alloc/free */
	u32 resize_buf_size;		/* resized sub-buffer size */
	u32 resize_n_bufs;		/* resized number of sub-buffers */
	u32 resize_alloc_size;		/* resized actual total size */
	int resizing;			/* is resizing in progress? */
	int resize_err;			/* resizing err code */
	int resize_failures;		/* number of resize failures */
	int replace_buffer;		/* is the alloced buffer ready?  */
	struct resize_offset resize_offset; /* offset change */
	struct timer_list shrink_timer;	/* timer used for shrinking */
	int resize_order;		/* size of last resize */
	u32 expand_buf_id;		/* subbuf id expand will occur at */

	struct page **buf_page_array;	/* array of current buffer pages */
	int buf_page_count;		/* number of current buffer pages */
	struct page **expand_page_array;/* new pages to be inserted */
	int expand_page_count;		/* number of new pages */
	struct page **shrink_page_array;/* old pages to be freed */
	int shrink_page_count;		/* number of old pages */
	struct page **resize_page_array;/* will become current pages */
	int resize_page_count;		/* number of resize pages */
	struct page **old_buf_page_array; /* hold for freeing */
} ____cacheline_aligned;

/*
 * Relay channel reader struct
 */
struct rchan_reader
{
	struct list_head list;		/* for list inclusion */
	struct rchan *rchan;		/* the channel we're reading from */
	int auto_consume;		/* does this reader auto-consume? */
	u32 bufs_consumed;		/* buffers this reader has consumed */
	u32 bytes_consumed;		/* bytes consumed in cur sub-buffer */
	int offset_changed;		/* have channel offsets changed? */
	int vfs_reader;			/* are we a VFS reader? */
	int map_reader;			/* are we an mmap reader? */

	union
	{
		struct file *file;
		u32 f_pos;
	} pos;				/* current read offset */
};

/*
 * These help make union member access less tedious
 */
#define channel_buffer(rchan) ((rchan)->buf)
#define idx(rchan) ((rchan)->scheme.lockless.index)
#define bufno_bits(rchan) ((rchan)->scheme.lockless.bufno_bits)
#define offset_bits(rchan) ((rchan)->scheme.lockless.offset_bits)
#define offset_mask(rchan) ((rchan)->scheme.lockless.offset_mask)
#define idx_mask(rchan) ((rchan)->scheme.lockless.index_mask)
#define bulk_delivery(rchan) (((rchan)->flags & RELAY_DELIVERY_BULK) ? 1 : 0)
#define packet_delivery(rchan) (((rchan)->flags & RELAY_DELIVERY_PACKET) ? 1 : 0)
#define using_lockless(rchan) (((rchan)->flags & RELAY_SCHEME_LOCKLESS) ? 1 : 0)
#define using_locking(rchan) (((rchan)->flags & RELAY_SCHEME_LOCKING) ? 1 : 0)
#define using_tsc(rchan) (((rchan)->flags & RELAY_TIMESTAMP_TSC) ? 1 : 0)
#define using_gettimeofday(rchan) (((rchan)->flags & RELAY_TIMESTAMP_GETTIMEOFDAY) ? 1 : 0)
#define usage_smp(rchan) (((rchan)->flags & RELAY_USAGE_SMP) ? 1 : 0)
#define usage_global(rchan) (((rchan)->flags & RELAY_USAGE_GLOBAL) ? 1 : 0)
#define mode_continuous(rchan) (((rchan)->flags & RELAY_MODE_CONTINUOUS) ? 1 : 0)
#define fill_count(rchan, i) ((rchan)->scheme.lockless.fill_count[(i)])
#define write_buf(rchan) ((rchan)->scheme.locking.write_buf)
#define read_buf(rchan) ((rchan)->scheme.locking.read_buf)
#define write_buf_end(rchan) ((rchan)->scheme.locking.write_buf_end)
#define read_buf_end(rchan) ((rchan)->scheme.locking.read_buf_end)
#define cur_write_pos(rchan) ((rchan)->scheme.locking.current_write_pos)
#define read_limit(rchan) ((rchan)->scheme.locking.read_limit)
#define write_limit(rchan) ((rchan)->scheme.locking.write_limit)
#define in_progress_event_pos(rchan) ((rchan)->scheme.locking.in_progress_event_pos)
#define in_progress_event_size(rchan) ((rchan)->scheme.locking.in_progress_event_size)
#define interrupted_pos(rchan) ((rchan)->scheme.locking.interrupted_pos)
#define interrupting_size(rchan) ((rchan)->scheme.locking.interrupting_size)
#define channel_lock(rchan) ((rchan)->scheme.locking.lock)


/**
 *	calc_time_delta - utility function for time delta calculation
 *	@now: current time
 *	@start: start time
 *
 *	Returns the time delta produced by subtracting start time from now.
 */
static inline u32
calc_time_delta(struct timeval *now, 
		struct timeval *start)
{
	return (now->tv_sec - start->tv_sec) * 1000000
		+ (now->tv_usec - start->tv_usec);
}

/**
 *	recalc_time_delta - utility function for time delta recalculation
 *	@now: current time
 *	@new_delta: the new time delta calculated
 *	@cpu: the associated CPU id
 */
static inline void 
recalc_time_delta(struct timeval *now,
		  u32 *new_delta,
		  struct rchan *rchan)
{
	if (using_tsc(rchan) == 0)
		*new_delta = calc_time_delta(now, &rchan->buf_start_time);
}

/**
 *	have_cmpxchg - does this architecture have a cmpxchg?
 *
 *	Returns 1 if this architecture has a cmpxchg useable by 
 *	the lockless scheme, 0 otherwise.
 */
static inline int 
have_cmpxchg(void)
{
#if defined(__HAVE_ARCH_CMPXCHG)
	return 1;
#else
	return 0;
#endif
}

/**
 *	relay_write_direct - write data directly into destination buffer
 */
#define relay_write_direct(DEST, SRC, SIZE) \
do\
{\
   memcpy(DEST, SRC, SIZE);\
   DEST += SIZE;\
} while (0);

/**
 *	relay_lock_channel - lock the relay channel if applicable
 *
 *	This macro only affects the locking scheme.  If the locking scheme
 *	is in use and the channel usage is SMP, does a local_irq_save.  If the 
 *	locking sheme is in use and the channel usage is GLOBAL, uses 
 *	spin_lock_irqsave.  FLAGS is initialized to 0 since we know that
 *	it is being initialized prior to use and we avoid the compiler warning.
 */
#define relay_lock_channel(RCHAN, FLAGS) \
do\
{\
   FLAGS = 0;\
   if (using_locking(RCHAN)) {\
      if (usage_smp(RCHAN)) {\
         local_irq_save(FLAGS); \
      } else {\
         spin_lock_irqsave(&(RCHAN)->scheme.locking.lock, FLAGS); \
      }\
   }\
} while (0);

/**
 *	relay_unlock_channel - unlock the relay channel if applicable
 *
 *	This macro only affects the locking scheme.  See relay_lock_channel.
 */
#define relay_unlock_channel(RCHAN, FLAGS) \
do\
{\
   if (using_locking(RCHAN)) {\
      if (usage_smp(RCHAN)) {\
         local_irq_restore(FLAGS); \
      } else {\
         spin_unlock_irqrestore(&(RCHAN)->scheme.locking.lock, FLAGS); \
      }\
   }\
} while (0);

/*
 * Define cmpxchg if we don't have it
 */
#ifndef __HAVE_ARCH_CMPXCHG
#define cmpxchg(p,o,n) 0
#endif

/*
 * High-level relayfs kernel API, fs/relayfs/relay.c
 */
extern int
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
	   u32 init_buf_size);

extern int
relay_close(int rchan_id);

extern int
relay_write(int rchan_id,
	    const void *data_ptr, 
	    size_t count,
	    int td_offset,
	    void **wrote_pos);

extern ssize_t
relay_read(struct rchan_reader *reader,
	   char *buf,
	   size_t count,
	   int wait,
	   u32 *actual_read_offset);

extern int
relay_discard_init_buf(int rchan_id);

extern struct rchan_reader *
add_rchan_reader(int rchan_id, int autoconsume);

extern int
remove_rchan_reader(struct rchan_reader *reader);

extern struct rchan_reader *
add_map_reader(int rchan_id);

extern int
remove_map_reader(struct rchan_reader *reader);

extern int 
relay_info(int rchan_id, struct rchan_info *rchan_info);

extern void 
relay_buffers_consumed(struct rchan_reader *reader, u32 buffers_consumed);

extern void
relay_bytes_consumed(struct rchan_reader *reader, u32 bytes_consumed, u32 read_offset);

extern ssize_t
relay_bytes_avail(struct rchan_reader *reader);

extern int
relay_realloc_buffer(int rchan_id, u32 new_nbufs, int in_background);

extern int
relay_replace_buffer(int rchan_id);

extern int
rchan_empty(struct rchan_reader *reader);

extern int
rchan_full(struct rchan_reader *reader);

extern void
update_readers_consumed(struct rchan *rchan, u32 bufs_consumed, u32 bytes_consumed);

extern int 
__relay_mmap_buffer(struct rchan *rchan, struct vm_area_struct *vma);

extern struct rchan_reader *
__add_rchan_reader(struct rchan *rchan, struct file *filp, int auto_consume, int map_reader);

extern void
__remove_rchan_reader(struct rchan_reader *reader);

/*
 * Low-level relayfs kernel API, fs/relayfs/relay.c
 */
extern struct rchan *
rchan_get(int rchan_id);

extern void
rchan_put(struct rchan *rchan);

extern char *
relay_reserve(struct rchan *rchan,
	      u32 data_len,
	      struct timeval *time_stamp,
	      u32 *time_delta,
	      int *errcode,
	      int *interrupting);

extern void 
relay_commit(struct rchan *rchan,
	     char *from, 
	     u32 len, 
	     int reserve_code,
	     int interrupting);

extern u32 
relay_get_offset(struct rchan *rchan, u32 *max_offset);

extern int
relay_reset(int rchan_id);

/*
 * VFS functions, fs/relayfs/inode.c
 */
extern int 
relayfs_create_dir(const char *name, 
		   struct dentry *parent, 
		   struct dentry **dentry);

extern int
relayfs_create_file(const char * name,
		    struct dentry *parent, 
		    struct dentry **dentry,
		    void * data,
		    int mode);

extern int 
relayfs_remove_file(struct dentry *dentry);

extern int
reset_index(struct rchan *rchan, u32 old_index);


/*
 * klog functions, fs/relayfs/klog.c
 */
extern int
create_klog_channel(void);

extern int
remove_klog_channel(void);

/*
 * Scheme-specific channel ops
 */
struct relay_ops
{
	char * (*reserve) (struct rchan *rchan,
			   u32 slot_len,
			   struct timeval *time_stamp,
			   u32 *tsc,
			   int * errcode,
			   int * interrupting);
	
	void (*commit) (struct rchan *rchan,
			char *from,
			u32 len, 
			int deliver, 
			int interrupting);

	u32 (*get_offset) (struct rchan *rchan,
			   u32 *max_offset);
	
	void (*resume) (struct rchan *rchan);
	void (*finalize) (struct rchan *rchan);
	void (*reset) (struct rchan *rchan,
		       int init);
	int (*reset_index) (struct rchan *rchan,
			    u32 old_index);
};

#endif /* _LINUX_RELAYFS_FS_H */





