#ifndef __LINUX__AIO_H
#define __LINUX__AIO_H

#include <linux/list.h>
#include <asm/atomic.h>

#include <linux/aio_abi.h>

#define AIO_MAXSEGS		4
#define AIO_KIOGRP_NR_ATOMIC	8

struct kioctx;

/* Notes on cancelling a kiocb:
 *	If a kiocb is cancelled, aio_complete may return 0 to indicate 
 *	that cancel has not yet disposed of the kiocb.  All cancel 
 *	operations *must* call aio_put_req to dispose of the kiocb 
 *	to guard against races with the completion code.
 */
#define KIOCB_C_CANCELLED	0x01
#define KIOCB_C_COMPLETE	0x02

#define KIOCB_SYNC_KEY		(~0U)

#define KIOCB_PRIVATE_SIZE	(16 * sizeof(long))

struct kiocb {
	int			ki_users;
	unsigned		ki_key;		/* id of this request */

	struct file		*ki_filp;
	struct kioctx		*ki_ctx;	/* may be NULL for sync ops */
	int			(*ki_cancel)(struct kiocb *, struct io_event *);

	struct list_head	ki_list;

	void			*ki_data;	/* for use by the the file */
	void			*ki_user_obj;	/* pointer to userland's iocb */
	__u64			ki_user_data;	/* user's data for completion */

	long			private[KIOCB_PRIVATE_SIZE/sizeof(long)];
};

#define init_sync_kiocb(x, filp)			\
	do {						\
		struct task_struct *tsk = current;	\
		(x)->ki_users = 1;			\
		(x)->ki_key = KIOCB_SYNC_KEY;		\
		(x)->ki_filp = (filp);			\
		(x)->ki_ctx = &tsk->active_mm->default_kioctx;	\
		(x)->ki_cancel = NULL;			\
		(x)->ki_user_obj = tsk;			\
	} while (0)

#define AIO_RING_MAGIC			0xa10a10a1
#define AIO_RING_COMPAT_FEATURES	1
#define AIO_RING_INCOMPAT_FEATURES	0
struct aio_ring {
	unsigned	id;	/* kernel internal index number */
	unsigned	nr;	/* number of io_events */
	unsigned	head;
	unsigned	tail;

	unsigned	magic;
	unsigned	compat_features;
	unsigned	incompat_features;
	unsigned	header_length;	/* size of aio_ring */


	struct io_event		io_events[0];
}; /* 128 bytes + ring size */

#define aio_ring_avail(info, ring)	(((ring)->head + (info)->nr - 1 - (ring)->tail) % (info)->nr)

#define AIO_RING_PAGES	8
struct aio_ring_info {
	unsigned long		mmap_base;
	unsigned long		mmap_size;

	struct page		**ring_pages;
	spinlock_t		ring_lock;
	long			nr_pages;

	unsigned		nr, tail;

	struct page		*internal_pages[AIO_RING_PAGES];
};

struct kioctx {
	atomic_t		users;
	int			dead;
	struct mm_struct	*mm;

	/* This needs improving */
	unsigned long		user_id;
	struct kioctx		*next;

	wait_queue_head_t	wait;

	spinlock_t		ctx_lock;

	int			reqs_active;
	struct list_head	active_reqs;	/* used for cancellation */

	unsigned		max_reqs;

	struct aio_ring_info	ring_info;
};

/* prototypes */
extern unsigned aio_max_size;

extern ssize_t FASTCALL(wait_on_sync_kiocb(struct kiocb *iocb));
extern int FASTCALL(aio_put_req(struct kiocb *iocb));
extern int FASTCALL(aio_complete(struct kiocb *iocb, long res, long res2));
extern void FASTCALL(__put_ioctx(struct kioctx *ctx));
struct mm_struct;
extern void FASTCALL(exit_aio(struct mm_struct *mm));

#define get_ioctx(kioctx)	do { if (unlikely(atomic_read(&(kioctx)->users) <= 0)) BUG(); atomic_inc(&(kioctx)->users); } while (0)
#define put_ioctx(kioctx)	do { if (unlikely(atomic_dec_and_test(&(kioctx)->users))) __put_ioctx(kioctx); else if (unlikely(atomic_read(&(kioctx)->users) < 0)) BUG(); } while (0)

#include <linux/aio_abi.h>

static inline struct kiocb *list_kiocb(struct list_head *h)
{
	return list_entry(h, struct kiocb, ki_list);
}

/* for sysctl: */
extern unsigned aio_max_nr, aio_max_size, aio_max_pinned;

#endif /* __LINUX__AIO_H */
