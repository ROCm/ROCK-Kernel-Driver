#ifndef _RAID1_H
#define _RAID1_H

#include <linux/raid/md.h>

typedef struct mirror_info mirror_info_t;

struct mirror_info {
	mdk_rdev_t	*rdev;
	sector_t	head_position;
};

typedef struct r1bio_s r1bio_t;

struct r1_private_data_s {
	mddev_t			*mddev;
	mirror_info_t		*mirrors;
	int			raid_disks;
	int			working_disks;
	int			last_used;
	sector_t		next_seq_sect;
	spinlock_t		device_lock;

	/* for use when syncing mirrors: */

	spinlock_t		resync_lock;
	int nr_pending;
	int barrier;
	sector_t		next_resync;

	wait_queue_head_t	wait_idle;
	wait_queue_head_t	wait_resume;

	mempool_t *r1bio_pool;
	mempool_t *r1buf_pool;
};

typedef struct r1_private_data_s conf_t;

/*
 * this is the only point in the RAID code where we violate
 * C type safety. mddev->private is an 'opaque' pointer.
 */
#define mddev_to_conf(mddev) ((conf_t *) mddev->private)

/*
 * this is our 'private' RAID1 bio.
 *
 * it contains information about what kind of IO operations were started
 * for this RAID1 operation, and about their status:
 */

struct r1bio_s {
	atomic_t		remaining; /* 'have we finished' count,
					    * used from IRQ handlers
					    */
	int			cmd;
	sector_t		sector;
	unsigned long		state;
	mddev_t			*mddev;
	/*
	 * original bio going to /dev/mdx
	 */
	struct bio		*master_bio;
	/*
	 * if the IO is in READ direction, then this bio is used:
	 */
	struct bio		*read_bio;
	int			read_disk;

	r1bio_t			*next_r1; /* next for retry or in free list */
	struct list_head	retry_list;
	/*
	 * if the IO is in WRITE direction, then multiple bios are used.
	 * We choose the number when they are allocated.
	 */
	struct bio		*write_bios[0];
};

/* bits for r1bio.state */
#define	R1BIO_Uptodate	1

#endif
