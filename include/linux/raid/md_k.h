/*
   md_k.h : kernel internal structure of the Linux MD driver
          Copyright (C) 1996-98 Ingo Molnar, Gadi Oxman
	  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#ifndef _MD_K_H
#define _MD_K_H

#define MD_RESERVED       0UL
#define LINEAR            1UL
#define RAID0             2UL
#define RAID1             3UL
#define RAID5             4UL
#define TRANSLUCENT       5UL
#define HSM               6UL
#define MULTIPATH         7UL
#define MAX_PERSONALITY   8UL

static inline int pers_to_level (int pers)
{
	switch (pers) {
		case MULTIPATH:		return -4;
		case HSM:		return -3;
		case TRANSLUCENT:	return -2;
		case LINEAR:		return -1;
		case RAID0:		return 0;
		case RAID1:		return 1;
		case RAID5:		return 5;
	}
	BUG();
	return MD_RESERVED;
}

static inline int level_to_pers (int level)
{
	switch (level) {
		case -4: return MULTIPATH;
		case -3: return HSM;
		case -2: return TRANSLUCENT;
		case -1: return LINEAR;
		case 0: return RAID0;
		case 1: return RAID1;
		case 4:
		case 5: return RAID5;
	}
	return MD_RESERVED;
}

typedef struct mddev_s mddev_t;
typedef struct mdk_rdev_s mdk_rdev_t;

#if (MINORBITS != 8)
#error MD doesnt handle bigger kdev yet
#endif

#define MAX_MD_DEVS  (1<<MINORBITS)	/* Max number of md dev */

/*
 * options passed in raidrun:
 */

#define MAX_CHUNK_SIZE (4096*1024)

/*
 * default readahead
 */

static inline int disk_faulty(mdp_disk_t * d)
{
	return d->state & (1 << MD_DISK_FAULTY);
}

static inline int disk_active(mdp_disk_t * d)
{
	return d->state & (1 << MD_DISK_ACTIVE);
}

static inline int disk_sync(mdp_disk_t * d)
{
	return d->state & (1 << MD_DISK_SYNC);
}

static inline int disk_spare(mdp_disk_t * d)
{
	return !disk_sync(d) && !disk_active(d) && !disk_faulty(d);
}

static inline int disk_removed(mdp_disk_t * d)
{
	return d->state & (1 << MD_DISK_REMOVED);
}

static inline void mark_disk_faulty(mdp_disk_t * d)
{
	d->state |= (1 << MD_DISK_FAULTY);
}

static inline void mark_disk_active(mdp_disk_t * d)
{
	d->state |= (1 << MD_DISK_ACTIVE);
}

static inline void mark_disk_sync(mdp_disk_t * d)
{
	d->state |= (1 << MD_DISK_SYNC);
}

static inline void mark_disk_spare(mdp_disk_t * d)
{
	d->state = 0;
}

static inline void mark_disk_removed(mdp_disk_t * d)
{
	d->state = (1 << MD_DISK_FAULTY) | (1 << MD_DISK_REMOVED);
}

static inline void mark_disk_inactive(mdp_disk_t * d)
{
	d->state &= ~(1 << MD_DISK_ACTIVE);
}

static inline void mark_disk_nonsync(mdp_disk_t * d)
{
	d->state &= ~(1 << MD_DISK_SYNC);
}

/*
 * MD's 'extended' device
 */
struct mdk_rdev_s
{
	struct list_head same_set;	/* RAID devices within the same set */
	struct list_head all;		/* all RAID devices */
	struct list_head pending;	/* undetected RAID devices */

	kdev_t dev;			/* Device number */
	kdev_t old_dev;			/*  "" when it was last imported */
	unsigned long size;		/* Device size (in blocks) */
	mddev_t *mddev;			/* RAID array if running */
	unsigned long last_events;	/* IO event timestamp */

	struct block_device *bdev;	/* block device handle */

	struct page	*sb_page;
	mdp_super_t	*sb;
	unsigned long	sb_offset;

	int alias_device;		/* device alias to the same disk */
	int faulty;			/* if faulty do not issue IO requests */
	int desc_nr;			/* descriptor index in the superblock */
};

typedef struct mdk_personality_s mdk_personality_t;

struct mddev_s
{
	void				*private;
	mdk_personality_t		*pers;
	int				__minor;
	mdp_super_t			*sb;
	struct list_head 		disks;
	int				sb_dirty;
	int				ro;

	struct mdk_thread_s		*sync_thread;	/* doing resync or reconstruct */
	unsigned long			curr_resync;	/* blocks scheduled */
	unsigned long			resync_mark;	/* a recent timestamp */
	unsigned long			resync_mark_cnt;/* blocks written at resync_mark */
	/* recovery_running is 0 for no recovery/resync,
	 * 1 for active recovery
	 * 2 for active resync
	 * -error for an error (e.g. -EINTR)
	 * it can only be set > 0 under reconfig_sem
	 */
	int				recovery_running;
	int				in_sync;	/* know to not need resync */
	struct semaphore		reconfig_sem;
	atomic_t			active;
	mdp_disk_t			*spare;

	atomic_t			recovery_active; /* blocks scheduled, but not written */
	wait_queue_head_t		recovery_wait;

	request_queue_t			queue;	/* for plugging ... */

	struct list_head		all_mddevs;
};

struct mdk_personality_s
{
	char *name;
	int (*make_request)(request_queue_t *q, struct bio *bio);
	int (*run)(mddev_t *mddev);
	int (*stop)(mddev_t *mddev);
	int (*status)(char *page, mddev_t *mddev);
	int (*error_handler)(mddev_t *mddev, struct block_device *bdev);
	int (*hot_add_disk) (mddev_t *mddev, mdp_disk_t *descriptor, mdk_rdev_t *rdev);
	int (*hot_remove_disk) (mddev_t *mddev, int number);
	int (*spare_write) (mddev_t *mddev, int number);
	int (*spare_inactive) (mddev_t *mddev);
	int (*spare_active) (mddev_t *mddev, mdp_disk_t **descriptor);
	int (*sync_request)(mddev_t *mddev, sector_t sector_nr, int go_faster);
};


/*
 * Currently we index md_array directly, based on the minor
 * number. This will have to change to dynamic allocation
 * once we start supporting partitioning of md devices.
 */
static inline int mdidx (mddev_t * mddev)
{
	return mddev->__minor;
}

static inline kdev_t mddev_to_kdev(mddev_t * mddev)
{
	return mk_kdev(MD_MAJOR, mdidx(mddev));
}

extern mdk_rdev_t * find_rdev(mddev_t * mddev, kdev_t dev);
extern mdk_rdev_t * find_rdev_nr(mddev_t *mddev, int nr);
extern mdp_disk_t *get_spare(mddev_t *mddev);

/*
 * iterates through some rdev ringlist. It's safe to remove the
 * current 'rdev'. Dont touch 'tmp' though.
 */
#define ITERATE_RDEV_GENERIC(head,field,rdev,tmp)			\
									\
	for ((tmp) = (head).next;					\
		(rdev) = (list_entry((tmp), mdk_rdev_t, field)),	\
			(tmp) = (tmp)->next, (tmp)->prev != &(head)	\
		; )
/*
 * iterates through the 'same array disks' ringlist
 */
#define ITERATE_RDEV(mddev,rdev,tmp)					\
	ITERATE_RDEV_GENERIC((mddev)->disks,same_set,rdev,tmp)


/*
 * Iterates through all 'RAID managed disks'
 */
#define ITERATE_RDEV_ALL(rdev,tmp)					\
	ITERATE_RDEV_GENERIC(all_raid_disks,all,rdev,tmp)

/*
 * Iterates through 'pending RAID disks'
 */
#define ITERATE_RDEV_PENDING(rdev,tmp)					\
	ITERATE_RDEV_GENERIC(pending_raid_disks,pending,rdev,tmp)

#define xchg_values(x,y) do { __typeof__(x) __tmp = x; \
				x = y; y = __tmp; } while (0)

typedef struct mdk_thread_s {
	void			(*run) (void *data);
	void			*data;
	wait_queue_head_t	wqueue;
	unsigned long           flags;
	struct completion	*event;
	struct task_struct	*tsk;
	const char		*name;
} mdk_thread_t;

#define THREAD_WAKEUP  0

#define MAX_DISKNAME_LEN 64

typedef struct dev_name_s {
	struct list_head list;
	kdev_t dev;
	char namebuf [MAX_DISKNAME_LEN];
	char *name;
} dev_name_t;


#define __wait_event_lock_irq(wq, condition, lock) 			\
do {									\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_UNINTERRUPTIBLE);		\
		if (condition)						\
			break;						\
		spin_unlock_irq(&lock);					\
		blk_run_queues();					\
		schedule();						\
		spin_lock_irq(&lock);					\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)

#define wait_event_lock_irq(wq, condition, lock) 			\
do {									\
	if (condition)	 						\
		break;							\
	__wait_event_lock_irq(wq, condition, lock);			\
} while (0)


#define __wait_disk_event(wq, condition) 				\
do {									\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_UNINTERRUPTIBLE);		\
		if (condition)						\
			break;						\
		blk_run_queues();					\
		schedule();						\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)

#define wait_disk_event(wq, condition) 					\
do {									\
	if (condition)	 						\
		break;							\
	__wait_disk_event(wq, condition);				\
} while (0)

#endif 

