/*
 * raid1.c : Multiple Devices driver for Linux
 *
 * Copyright (C) 1999, 2000, 2001 Ingo Molnar, Red Hat
 *
 * Copyright (C) 1996, 1997, 1998 Ingo Molnar, Miguel de Icaza, Gadi Oxman
 *
 * RAID-1 management functions.
 *
 * Better read-balancing code written by Mika Kuoppala <miku@iki.fi>, 2000
 *
 * Fixes to reconstruction by Jakob Østergaard" <jakob@ostenfeld.dk>
 * Various fixes by Neil Brown <neilb@cse.unsw.edu.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/raid/raid1.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER
#define MD_PERSONALITY

#define MAX_WORK_PER_DISK 128
/*
 * Number of guaranteed r1bios in case of extreme VM load:
 */
#define	NR_RAID1_BIOS 256

static mdk_personality_t raid1_personality;
static spinlock_t retry_list_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(retry_list_head);

static inline void check_all_w_bios_empty(r1bio_t *r1_bio)
{
	int i;

	return;
	for (i = 0; i < MD_SB_DISKS; i++)
		if (r1_bio->write_bios[i])
			BUG();
}

static inline void check_all_bios_empty(r1bio_t *r1_bio)
{
	return;
	if (r1_bio->read_bio)
		BUG();
	check_all_w_bios_empty(r1_bio);
}

static void * r1bio_pool_alloc(int gfp_flags, void *data)
{
	r1bio_t *r1_bio;

	r1_bio = kmalloc(sizeof(r1bio_t), gfp_flags);
	if (r1_bio)
		memset(r1_bio, 0, sizeof(*r1_bio));

	return r1_bio;
}

static void r1bio_pool_free(void *r1_bio, void *data)
{
	check_all_bios_empty(r1_bio);
	kfree(r1_bio);
}

#define RESYNC_BLOCK_SIZE (64*1024)
#define RESYNC_PAGES ((RESYNC_BLOCK_SIZE + PAGE_SIZE-1) / PAGE_SIZE)
#define RESYNC_WINDOW (2048*1024)

static void * r1buf_pool_alloc(int gfp_flags, void *data)
{
	conf_t *conf = data;
	struct page *page;
	r1bio_t *r1_bio;
	struct bio *bio;
	int i, j;

	r1_bio = mempool_alloc(conf->r1bio_pool, gfp_flags);
	check_all_bios_empty(r1_bio);

	bio = bio_alloc(gfp_flags, RESYNC_PAGES);
	if (!bio)
		goto out_free_r1_bio;

	for (i = 0; i < RESYNC_PAGES; i++) {
		page = alloc_page(gfp_flags);
		if (unlikely(!page))
			goto out_free_pages;

		bio->bi_io_vec[i].bv_page = page;
		bio->bi_io_vec[i].bv_len = PAGE_SIZE;
		bio->bi_io_vec[i].bv_offset = 0;
	}

	/*
	 * Allocate a single data page for this iovec.
	 */
	bio->bi_vcnt = RESYNC_PAGES;
	bio->bi_idx = 0;
	bio->bi_size = RESYNC_BLOCK_SIZE;
	bio->bi_end_io = NULL;
	atomic_set(&bio->bi_cnt, 1);

	r1_bio->master_bio = bio;

	return r1_bio;

out_free_pages:
	for (j = 0; j < i; j++)
		__free_page(bio->bi_io_vec[j].bv_page);
	bio_put(bio);
out_free_r1_bio:
	mempool_free(r1_bio, conf->r1bio_pool);
	return NULL;
}

static void r1buf_pool_free(void *__r1_bio, void *data)
{
	int i;
	conf_t *conf = data;
	r1bio_t *r1bio = __r1_bio;
	struct bio *bio = r1bio->master_bio;

	check_all_bios_empty(r1bio);
	if (atomic_read(&bio->bi_cnt) != 1)
		BUG();
	for (i = 0; i < RESYNC_PAGES; i++) {
		__free_page(bio->bi_io_vec[i].bv_page);
		bio->bi_io_vec[i].bv_page = NULL;
	}
	if (atomic_read(&bio->bi_cnt) != 1)
		BUG();
	bio_put(bio);
	mempool_free(r1bio, conf->r1bio_pool);
}

static void put_all_bios(conf_t *conf, r1bio_t *r1_bio)
{
	int i;

	if (r1_bio->read_bio) {
		if (atomic_read(&r1_bio->read_bio->bi_cnt) != 1)
			BUG();
		bio_put(r1_bio->read_bio);
		r1_bio->read_bio = NULL;
	}
	for (i = 0; i < MD_SB_DISKS; i++) {
		struct bio **bio = r1_bio->write_bios + i;
		if (*bio) {
			if (atomic_read(&(*bio)->bi_cnt) != 1)
				BUG();
			bio_put(*bio);
		}
		*bio = NULL;
	}
	check_all_bios_empty(r1_bio);
}

static inline void free_r1bio(r1bio_t *r1_bio)
{
	conf_t *conf = mddev_to_conf(r1_bio->mddev);

	put_all_bios(conf, r1_bio);
	mempool_free(r1_bio, conf->r1bio_pool);
}

static inline void put_buf(r1bio_t *r1_bio)
{
	conf_t *conf = mddev_to_conf(r1_bio->mddev);
	struct bio *bio = r1_bio->master_bio;

	/*
	 * undo any possible partial request fixup magic:
	 */
	if (bio->bi_size != RESYNC_BLOCK_SIZE)
		bio->bi_io_vec[bio->bi_vcnt-1].bv_len = PAGE_SIZE;
	put_all_bios(conf, r1_bio);
	mempool_free(r1_bio, conf->r1buf_pool);
}

static int map(mddev_t *mddev, kdev_t *rdev)
{
	conf_t *conf = mddev_to_conf(mddev);
	int i, disks = MD_SB_DISKS;

	/*
	 * Later we do read balancing on the read side
	 * now we use the first available disk.
	 */

	for (i = 0; i < disks; i++) {
		if (conf->mirrors[i].operational) {
			*rdev = conf->mirrors[i].dev;
			return 0;
		}
	}

	printk (KERN_ERR "raid1_map(): huh, no more operational devices?\n");
	return -1;
}

static void reschedule_retry(r1bio_t *r1_bio)
{
	unsigned long flags;
	mddev_t *mddev = r1_bio->mddev;
	conf_t *conf = mddev_to_conf(mddev);

	spin_lock_irqsave(&retry_list_lock, flags);
	list_add(&r1_bio->retry_list, &retry_list_head);
	spin_unlock_irqrestore(&retry_list_lock, flags);

	md_wakeup_thread(conf->thread);
}


static void inline raid_request_done(unsigned long sector, conf_t *conf, int phase)
{
	unsigned long flags;
	spin_lock_irqsave(&conf->segment_lock, flags);
	if (sector < conf->start_active)
		conf->cnt_done--;
	else if (sector >= conf->start_future && conf->phase == phase)
		conf->cnt_future--;
	else if (!--conf->cnt_pending)
		wake_up(&conf->wait_ready);

	spin_unlock_irqrestore(&conf->segment_lock, flags);
}

static void inline sync_request_done(sector_t sector, conf_t *conf)
{
	unsigned long flags;

	spin_lock_irqsave(&conf->segment_lock, flags);
	if (sector >= conf->start_ready)
		--conf->cnt_ready;
	else if (sector >= conf->start_active) {
		if (!--conf->cnt_active) {
			conf->start_active = conf->start_ready;
			wake_up(&conf->wait_done);
		}
	}
	spin_unlock_irqrestore(&conf->segment_lock, flags);
}

/*
 * raid_end_bio_io() is called when we have finished servicing a mirrored
 * operation and are ready to return a success/failure code to the buffer
 * cache layer.
 */
static int raid_end_bio_io(r1bio_t *r1_bio, int uptodate, int nr_sectors)
{
	struct bio *bio = r1_bio->master_bio;

	raid_request_done(bio->bi_sector, mddev_to_conf(r1_bio->mddev),
			test_bit(R1BIO_SyncPhase, &r1_bio->state));

	bio_endio(bio, uptodate, nr_sectors);
	free_r1bio(r1_bio);

	return 0;
}

static int end_request(struct bio *bio, int nr_sectors)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	r1bio_t * r1_bio = (r1bio_t *)(bio->bi_private);

	/*
	 * this branch is our 'one mirror IO has finished' event handler:
	 */
	if (!uptodate)
		md_error(r1_bio->mddev, bio->bi_dev);
	else
		/*
		 * Set R1BIO_Uptodate in our master bio, so that
		 * we will return a good error code for to the higher
		 * levels even if IO on some other mirrored buffer fails.
		 *
		 * The 'master' represents the complex operation to
		 * user-side. So if something waits for IO, then it will
		 * wait for the 'master' bio.
		 */
		set_bit(R1BIO_Uptodate, &r1_bio->state);

	/*
	 * We split up the read and write side, imho they are
	 * conceptually different.
	 */

	if ((r1_bio->cmd == READ) || (r1_bio->cmd == READA)) {
		if (!r1_bio->read_bio)
			BUG();
		/*
		 * we have only one bio on the read side
		 */
		if (uptodate) {
			raid_end_bio_io(r1_bio, uptodate, nr_sectors);
			return 0;
		}
		/*
		 * oops, read error:
		 */
		printk(KERN_ERR "raid1: %s: rescheduling sector %lu\n",
			partition_name(bio->bi_dev), r1_bio->sector);
		reschedule_retry(r1_bio);
		return 0;
	}

	if (r1_bio->read_bio)
		BUG();
	/*
	 * WRITE:
	 *
	 * Let's see if all mirrored write operations have finished
	 * already.
	 */
	if (atomic_dec_and_test(&r1_bio->remaining))
		raid_end_bio_io(r1_bio, uptodate, nr_sectors);
	return 0;
}

/*
 * This routine returns the disk from which the requested read should
 * be done. It bookkeeps the last read position for every disk
 * in array and when new read requests come, the disk which last
 * position is nearest to the request, is chosen.
 *
 * TODO: now if there are 2 mirrors in the same 2 devices, performance
 * degrades dramatically because position is mirror, not device based.
 * This should be changed to be device based. Also atomic sequential
 * reads should be somehow balanced.
 */

static int read_balance(conf_t *conf, struct bio *bio, r1bio_t *r1_bio)
{
	const int sectors = bio->bi_size >> 9;
	const unsigned long this_sector = r1_bio->sector;
	unsigned long new_distance, current_distance;
	int new_disk = conf->last_used, disk = new_disk;

	/*
	 * Check if it is sane at all to balance
	 */

	if (conf->resync_mirrors)
		goto rb_out;


	/* make sure that disk is operational */
	while( !conf->mirrors[new_disk].operational) {
		if (new_disk <= 0) new_disk = conf->raid_disks;
		new_disk--;
		if (new_disk == disk) {
			/*
			 * This means no working disk was found
			 * Nothing much to do, lets not change anything
			 * and hope for the best...
			 */

			new_disk = conf->last_used;

			goto rb_out;
		}
	}
	disk = new_disk;
	/* now disk == new_disk == starting point for search */

	/*
	 * Don't touch anything for sequential reads.
	 */
	if (this_sector == conf->mirrors[new_disk].head_position)
		goto rb_out;

	/*
	 * If reads have been done only on a single disk
	 * for a time, lets give another disk a change.
	 * This is for kicking those idling disks so that
	 * they would find work near some hotspot.
	 */
	if (conf->sect_count >= conf->mirrors[new_disk].sect_limit) {
		conf->sect_count = 0;

		do {
			if (new_disk <= 0)
				new_disk = conf->raid_disks;
			new_disk--;
			if (new_disk == disk)
				break;
		} while ((conf->mirrors[new_disk].write_only) ||
			(!conf->mirrors[new_disk].operational));

		goto rb_out;
	}

	current_distance = abs(this_sector -
				conf->mirrors[disk].head_position);

	/* Find the disk which is closest */

	do {
		if (disk <= 0)
			disk = conf->raid_disks;
		disk--;

		if ((conf->mirrors[disk].write_only) ||
				(!conf->mirrors[disk].operational))
			continue;

		new_distance = abs(this_sector -
					conf->mirrors[disk].head_position);

		if (new_distance < current_distance) {
			conf->sect_count = 0;
			current_distance = new_distance;
			new_disk = disk;
		}
	} while (disk != conf->last_used);

rb_out:
	conf->mirrors[new_disk].head_position = this_sector + sectors;

	conf->last_used = new_disk;
	conf->sect_count += sectors;

	return new_disk;
}

/*
 * Wait if the reconstruction state machine puts up a bar for
 * new requests in this sector range:
 */
static inline void new_request(conf_t *conf, r1bio_t *r1_bio)
{
	spin_lock_irq(&conf->segment_lock);
	wait_event_lock_irq(conf->wait_done,
			r1_bio->sector < conf->start_active ||
			r1_bio->sector >= conf->start_future,
			conf->segment_lock);
	if (r1_bio->sector < conf->start_active)
		conf->cnt_done++;
	else {
		conf->cnt_future++;
		if (conf->phase)
			set_bit(R1BIO_SyncPhase, &r1_bio->state);
	}
	spin_unlock_irq(&conf->segment_lock);
}

static int make_request(mddev_t *mddev, int rw, struct bio * bio)
{
	conf_t *conf = mddev_to_conf(mddev);
	mirror_info_t *mirror;
	r1bio_t *r1_bio;
	struct bio *read_bio;
	int i, sum_bios = 0, disks = MD_SB_DISKS;

	/*
	 * make_request() can abort the operation when READA is being
	 * used and no empty request is available.
	 *
	 * Currently, just replace the command with READ.
	 */
	if (rw == READA)
		rw = READ;

	r1_bio = mempool_alloc(conf->r1bio_pool, GFP_NOIO);
	check_all_bios_empty(r1_bio);

	r1_bio->master_bio = bio;

	r1_bio->mddev = mddev;
	r1_bio->sector = bio->bi_sector;
	r1_bio->cmd = rw;

	new_request(conf, r1_bio);

	if (rw == READ) {
		/*
		 * read balancing logic:
		 */
		mirror = conf->mirrors + read_balance(conf, bio, r1_bio);

		read_bio = bio_clone(bio, GFP_NOIO);
		if (r1_bio->read_bio)
			BUG();
		r1_bio->read_bio = read_bio;

		read_bio->bi_sector = r1_bio->sector;
		read_bio->bi_dev = mirror->dev;
		read_bio->bi_end_io = end_request;
		read_bio->bi_rw = rw;
		read_bio->bi_private = r1_bio;

		generic_make_request(read_bio);
		return 0;
	}

	/*
	 * WRITE:
	 */

	check_all_w_bios_empty(r1_bio);

	for (i = 0; i < disks; i++) {
		struct bio *mbio;
		if (!conf->mirrors[i].operational)
			continue;

		mbio = bio_clone(bio, GFP_NOIO);
		if (r1_bio->write_bios[i])
			BUG();
		r1_bio->write_bios[i] = mbio;

		mbio->bi_sector	= r1_bio->sector;
		mbio->bi_dev = conf->mirrors[i].dev;
		mbio->bi_end_io	= end_request;
		mbio->bi_rw = rw;
		mbio->bi_private = r1_bio;

		sum_bios++;
	}
	if (!sum_bios) {
		/*
		 * If all mirrors are non-operational
		 * then return an IO error:
		 */
		raid_end_bio_io(r1_bio, 0, 0);
		return 0;
	}
	atomic_set(&r1_bio->remaining, sum_bios);

	/*
	 * We have to be a bit careful about the semaphore above, thats
	 * why we start the requests separately. Since kmalloc() could
	 * fail, sleep and make_request() can sleep too, this is the
	 * safer solution. Imagine, end_request decreasing the semaphore
	 * before we could have set it up ... We could play tricks with
	 * the semaphore (presetting it and correcting at the end if
	 * sum_bios is not 'n' but we have to do end_request by hand if
	 * all requests finish until we had a chance to set up the
	 * semaphore correctly ... lots of races).
	 */
	for (i = 0; i < disks; i++) {
		struct bio *mbio;
		mbio = r1_bio->write_bios[i];
		if (!mbio)
			continue;

		generic_make_request(mbio);
	}
	return 0;
}

static int status(char *page, mddev_t *mddev)
{
	conf_t *conf = mddev_to_conf(mddev);
	int sz = 0, i;

	sz += sprintf(page+sz, " [%d/%d] [", conf->raid_disks,
						conf->working_disks);
	for (i = 0; i < conf->raid_disks; i++)
		sz += sprintf(page+sz, "%s",
			conf->mirrors[i].operational ? "U" : "_");
	sz += sprintf (page+sz, "]");
	return sz;
}

#define LAST_DISK KERN_ALERT \
"raid1: only one disk left and IO error.\n"

#define NO_SPARE_DISK KERN_ALERT \
"raid1: no spare disk left, degrading mirror level by one.\n"

#define DISK_FAILED KERN_ALERT \
"raid1: Disk failure on %s, disabling device. \n" \
"	Operation continuing on %d devices\n"

#define START_SYNCING KERN_ALERT \
"raid1: start syncing spare disk.\n"

#define ALREADY_SYNCING KERN_INFO \
"raid1: syncing already in progress.\n"

static void mark_disk_bad(mddev_t *mddev, int failed)
{
	conf_t *conf = mddev_to_conf(mddev);
	mirror_info_t *mirror = conf->mirrors+failed;
	mdp_super_t *sb = mddev->sb;

	mirror->operational = 0;
	mark_disk_faulty(sb->disks+mirror->number);
	mark_disk_nonsync(sb->disks+mirror->number);
	mark_disk_inactive(sb->disks+mirror->number);
	if (!mirror->write_only)
		sb->active_disks--;
	sb->working_disks--;
	sb->failed_disks++;
	mddev->sb_dirty = 1;
	md_wakeup_thread(conf->thread);
	if (!mirror->write_only)
		conf->working_disks--;
	printk(DISK_FAILED, partition_name(mirror->dev),
				conf->working_disks);
}

static int error(mddev_t *mddev, kdev_t dev)
{
	conf_t *conf = mddev_to_conf(mddev);
	mirror_info_t * mirrors = conf->mirrors;
	int disks = MD_SB_DISKS;
	int i;

	/*
	 * Find the drive.
	 * If it is not operational, then we have already marked it as dead
	 * else if it is the last working disks, ignore the error, let the
	 * next level up know.
	 * else mark the drive as failed
	 */
	for (i = 0; i < disks; i++)
		if (mirrors[i].dev == dev && mirrors[i].operational)
			break;
	if (i == disks)
		return 0;

	if (i < conf->raid_disks && conf->working_disks == 1)
		/*
		 * Don't fail the drive, act as though we were just a
		 * normal single drive
		 */
		return 1;
	mark_disk_bad(mddev, i);
	return 0;
}

#undef LAST_DISK
#undef NO_SPARE_DISK
#undef DISK_FAILED
#undef START_SYNCING


static void print_conf(conf_t *conf)
{
	int i;
	mirror_info_t *tmp;

	printk("RAID1 conf printout:\n");
	if (!conf) {
		printk("(!conf)\n");
		return;
	}
	printk(" --- wd:%d rd:%d nd:%d\n", conf->working_disks,
			conf->raid_disks, conf->nr_disks);

	for (i = 0; i < MD_SB_DISKS; i++) {
		tmp = conf->mirrors + i;
		printk(" disk %d, s:%d, o:%d, n:%d rd:%d us:%d dev:%s\n",
			i, tmp->spare, tmp->operational,
			tmp->number, tmp->raid_disk, tmp->used_slot,
			partition_name(tmp->dev));
	}
}

static void close_sync(conf_t *conf)
{
	mddev_t *mddev = conf->mddev;
	/*
	 * If reconstruction was interrupted, we need to close the "active"
	 * and "pending" holes.
	 * we know that there are no active rebuild requests,
	 * os cnt_active == cnt_ready == 0
	 */
	spin_lock_irq(&conf->segment_lock);
	conf->start_active = conf->start_pending;
	conf->start_ready = conf->start_pending;
	wait_event_lock_irq(conf->wait_ready, !conf->cnt_pending, conf->segment_lock);
	conf->start_active = conf->start_ready = conf->start_pending = conf->start_future;
	conf->start_future = mddev->sb->size+1;
	conf->cnt_pending = conf->cnt_future;
	conf->cnt_future = 0;
	conf->phase = conf->phase ^1;
	wait_event_lock_irq(conf->wait_ready, !conf->cnt_pending, conf->segment_lock);
	conf->start_active = conf->start_ready = conf->start_pending = conf->start_future = 0;
	conf->phase = 0;
	conf->cnt_future = conf->cnt_done;;
	conf->cnt_done = 0;
	spin_unlock_irq(&conf->segment_lock);
	wake_up(&conf->wait_done);
}

static int diskop(mddev_t *mddev, mdp_disk_t **d, int state)
{
	int err = 0;
	int i, failed_disk = -1, spare_disk = -1, removed_disk = -1, added_disk = -1;
	conf_t *conf = mddev->private;
	mirror_info_t *tmp, *sdisk, *fdisk, *rdisk, *adisk;
	mdp_super_t *sb = mddev->sb;
	mdp_disk_t *failed_desc, *spare_desc, *added_desc;
	mdk_rdev_t *spare_rdev, *failed_rdev;

	print_conf(conf);
	spin_lock_irq(&conf->device_lock);
	/*
	 * find the disk ...
	 */
	switch (state) {

	case DISKOP_SPARE_ACTIVE:

		/*
		 * Find the failed disk within the RAID1 configuration ...
		 * (this can only be in the first conf->working_disks part)
		 */
		for (i = 0; i < conf->raid_disks; i++) {
			tmp = conf->mirrors + i;
			if ((!tmp->operational && !tmp->spare) ||
					!tmp->used_slot) {
				failed_disk = i;
				break;
			}
		}
		/*
		 * When we activate a spare disk we _must_ have a disk in
		 * the lower (active) part of the array to replace.
		 */
		if ((failed_disk == -1) || (failed_disk >= conf->raid_disks)) {
			MD_BUG();
			err = 1;
			goto abort;
		}
		/* fall through */

	case DISKOP_SPARE_WRITE:
	case DISKOP_SPARE_INACTIVE:

		/*
		 * Find the spare disk ... (can only be in the 'high'
		 * area of the array)
		 */
		for (i = conf->raid_disks; i < MD_SB_DISKS; i++) {
			tmp = conf->mirrors + i;
			if (tmp->spare && tmp->number == (*d)->number) {
				spare_disk = i;
				break;
			}
		}
		if (spare_disk == -1) {
			MD_BUG();
			err = 1;
			goto abort;
		}
		break;

	case DISKOP_HOT_REMOVE_DISK:

		for (i = 0; i < MD_SB_DISKS; i++) {
			tmp = conf->mirrors + i;
			if (tmp->used_slot && (tmp->number == (*d)->number)) {
				if (tmp->operational) {
					err = -EBUSY;
					goto abort;
				}
				removed_disk = i;
				break;
			}
		}
		if (removed_disk == -1) {
			MD_BUG();
			err = 1;
			goto abort;
		}
		break;

	case DISKOP_HOT_ADD_DISK:

		for (i = conf->raid_disks; i < MD_SB_DISKS; i++) {
			tmp = conf->mirrors + i;
			if (!tmp->used_slot) {
				added_disk = i;
				break;
			}
		}
		if (added_disk == -1) {
			MD_BUG();
			err = 1;
			goto abort;
		}
		break;
	}

	switch (state) {
	/*
	 * Switch the spare disk to write-only mode:
	 */
	case DISKOP_SPARE_WRITE:
		sdisk = conf->mirrors + spare_disk;
		sdisk->operational = 1;
		sdisk->write_only = 1;
		break;
	/*
	 * Deactivate a spare disk:
	 */
	case DISKOP_SPARE_INACTIVE:
		close_sync(conf);
		sdisk = conf->mirrors + spare_disk;
		sdisk->operational = 0;
		sdisk->write_only = 0;
		break;
	/*
	 * Activate (mark read-write) the (now sync) spare disk,
	 * which means we switch it's 'raid position' (->raid_disk)
	 * with the failed disk. (only the first 'conf->nr_disks'
	 * slots are used for 'real' disks and we must preserve this
	 * property)
	 */
	case DISKOP_SPARE_ACTIVE:
		close_sync(conf);
		sdisk = conf->mirrors + spare_disk;
		fdisk = conf->mirrors + failed_disk;

		spare_desc = &sb->disks[sdisk->number];
		failed_desc = &sb->disks[fdisk->number];

		if (spare_desc != *d) {
			MD_BUG();
			err = 1;
			goto abort;
		}

		if (spare_desc->raid_disk != sdisk->raid_disk) {
			MD_BUG();
			err = 1;
			goto abort;
		}

		if (sdisk->raid_disk != spare_disk) {
			MD_BUG();
			err = 1;
			goto abort;
		}

		if (failed_desc->raid_disk != fdisk->raid_disk) {
			MD_BUG();
			err = 1;
			goto abort;
		}

		if (fdisk->raid_disk != failed_disk) {
			MD_BUG();
			err = 1;
			goto abort;
		}

		/*
		 * do the switch finally
		 */
		spare_rdev = find_rdev_nr(mddev, spare_desc->number);
		failed_rdev = find_rdev_nr(mddev, failed_desc->number);

		/*
		 * There must be a spare_rdev, but there may not be a
		 * failed_rdev. That slot might be empty...
		 */
		spare_rdev->desc_nr = failed_desc->number;
		if (failed_rdev)
			failed_rdev->desc_nr = spare_desc->number;

		xchg_values(*spare_desc, *failed_desc);
		xchg_values(*fdisk, *sdisk);

		/*
		 * (careful, 'failed' and 'spare' are switched from now on)
		 *
		 * we want to preserve linear numbering and we want to
		 * give the proper raid_disk number to the now activated
		 * disk. (this means we switch back these values)
		 */
		xchg_values(spare_desc->raid_disk, failed_desc->raid_disk);
		xchg_values(sdisk->raid_disk, fdisk->raid_disk);
		xchg_values(spare_desc->number, failed_desc->number);
		xchg_values(sdisk->number, fdisk->number);

		*d = failed_desc;

		if (sdisk->dev == MKDEV(0,0))
			sdisk->used_slot = 0;
		/*
		 * this really activates the spare.
		 */
		fdisk->spare = 0;
		fdisk->write_only = 0;

		/*
		 * if we activate a spare, we definitely replace a
		 * non-operational disk slot in the 'low' area of
		 * the disk array.
		 */

		conf->working_disks++;

		break;

	case DISKOP_HOT_REMOVE_DISK:
		rdisk = conf->mirrors + removed_disk;

		if (rdisk->spare && (removed_disk < conf->raid_disks)) {
			MD_BUG();
			err = 1;
			goto abort;
		}
		rdisk->dev = MKDEV(0,0);
		rdisk->used_slot = 0;
		conf->nr_disks--;
		break;

	case DISKOP_HOT_ADD_DISK:
		adisk = conf->mirrors + added_disk;
		added_desc = *d;

		if (added_disk != added_desc->number) {
			MD_BUG();
			err = 1;
			goto abort;
		}

		adisk->number = added_desc->number;
		adisk->raid_disk = added_desc->raid_disk;
		adisk->dev = MKDEV(added_desc->major, added_desc->minor);

		adisk->operational = 0;
		adisk->write_only = 0;
		adisk->spare = 1;
		adisk->used_slot = 1;
		adisk->head_position = 0;
		conf->nr_disks++;

		break;

	default:
		MD_BUG();
		err = 1;
		goto abort;
	}
abort:
	spin_unlock_irq(&conf->device_lock);
	if (state == DISKOP_SPARE_ACTIVE || state == DISKOP_SPARE_INACTIVE) {
		mempool_destroy(conf->r1buf_pool);
		conf->r1buf_pool = NULL;
	}

	print_conf(conf);
	return err;
}


#define IO_ERROR KERN_ALERT \
"raid1: %s: unrecoverable I/O read error for block %lu\n"

#define REDIRECT_SECTOR KERN_ERR \
"raid1: %s: redirecting sector %lu to another mirror\n"

static int end_sync_read(struct bio *bio, int nr_sectors)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	r1bio_t * r1_bio = (r1bio_t *)(bio->bi_private);

	check_all_w_bios_empty(r1_bio);
	if (r1_bio->read_bio != bio)
		BUG();
	/*
	 * we have read a block, now it needs to be re-written,
	 * or re-read if the read failed.
	 * We don't do much here, just schedule handling by raid1d
	 */
	if (!uptodate)
		md_error (r1_bio->mddev, bio->bi_dev);
	else
		set_bit(R1BIO_Uptodate, &r1_bio->state);
	reschedule_retry(r1_bio);

	return 0;
}

static int end_sync_write(struct bio *bio, int nr_sectors)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	r1bio_t * r1_bio = (r1bio_t *)(bio->bi_private);
	mddev_t *mddev = r1_bio->mddev;

	if (!uptodate)
		md_error(mddev, bio->bi_dev);

	if (atomic_dec_and_test(&r1_bio->remaining)) {
		sync_request_done(r1_bio->sector, mddev_to_conf(mddev));
		md_done_sync(mddev, r1_bio->master_bio->bi_size >> 9, uptodate);
		put_buf(r1_bio);
	}
	return 0;
}

static void sync_request_write(mddev_t *mddev, r1bio_t *r1_bio)
{
	conf_t *conf = mddev_to_conf(mddev);
	int i, sum_bios = 0;
	int disks = MD_SB_DISKS;
	struct bio *bio, *mbio;

	bio = r1_bio->master_bio;

	/*
	 * have to allocate lots of bio structures and
	 * schedule writes
	 */
	if (!test_bit(R1BIO_Uptodate, &r1_bio->state)) {
		/*
		 * There is no point trying a read-for-reconstruct as
		 * reconstruct is about to be aborted
		 */
		printk(IO_ERROR, partition_name(bio->bi_dev), r1_bio->sector);
		md_done_sync(mddev, r1_bio->master_bio->bi_size >> 9, 0);
		return;
	}

	check_all_w_bios_empty(r1_bio);

	for (i = 0; i < disks ; i++) {
		if (!conf->mirrors[i].operational)
			continue;
		if (i == conf->last_used)
			/*
			 * we read from here, no need to write
			 */
			continue;
		if (i < conf->raid_disks && !conf->resync_mirrors)
			/*
			 * don't need to write this we are just rebuilding
			 */
			continue;

		mbio = bio_clone(bio, GFP_NOIO);
		if (r1_bio->write_bios[i])
			BUG();
		r1_bio->write_bios[i] = mbio;
		mbio->bi_dev = conf->mirrors[i].dev;
		mbio->bi_sector = r1_bio->sector;
		mbio->bi_end_io	= end_sync_write;
		mbio->bi_rw = WRITE;
		mbio->bi_private = r1_bio;

		sum_bios++;
	}
	if (i != disks)
		BUG();
	atomic_set(&r1_bio->remaining, sum_bios);


	if (!sum_bios) {
		/*
		 * Nowhere to write this to... I guess we
		 * must be done
		 */
		printk(IO_ERROR, partition_name(bio->bi_dev), r1_bio->sector);
		sync_request_done(r1_bio->sector, conf);
		md_done_sync(mddev, r1_bio->master_bio->bi_size >> 9, 0);
		put_buf(r1_bio);
		return;
	}
	for (i = 0; i < disks ; i++) {
		mbio = r1_bio->write_bios[i];
		if (!mbio)
			continue;

		md_sync_acct(mbio->bi_dev, mbio->bi_size >> 9);
		generic_make_request(mbio);
	}
}

/*
 * This is a kernel thread which:
 *
 *	1.	Retries failed read operations on working mirrors.
 *	2.	Updates the raid superblock when problems encounter.
 *	3.	Performs writes following reads for array syncronising.
 */

static void raid1d(void *data)
{
	struct list_head *head = &retry_list_head;
	r1bio_t *r1_bio;
	struct bio *bio;
	unsigned long flags;
	mddev_t *mddev;
	kdev_t dev;


	for (;;) {
		spin_lock_irqsave(&retry_list_lock, flags);
		if (list_empty(head))
			break;
		r1_bio = list_entry(head->prev, r1bio_t, retry_list);
		list_del(head->prev);
		spin_unlock_irqrestore(&retry_list_lock, flags);
		check_all_w_bios_empty(r1_bio);

		mddev = r1_bio->mddev;
		if (mddev->sb_dirty) {
			printk(KERN_INFO "raid1: dirty sb detected, updating.\n");
			mddev->sb_dirty = 0;
			md_update_sb(mddev);
		}
		bio = r1_bio->master_bio;
		switch(r1_bio->cmd) {
		case SPECIAL:
			sync_request_write(mddev, r1_bio);
			break;
		case READ:
		case READA:
			dev = bio->bi_dev;
			map(mddev, &bio->bi_dev);
			if (bio->bi_dev == dev) {
				printk(IO_ERROR, partition_name(bio->bi_dev), r1_bio->sector);
				raid_end_bio_io(r1_bio, 0, 0);
				break;
			}
			printk(REDIRECT_SECTOR,
				partition_name(bio->bi_dev), r1_bio->sector);
			bio->bi_sector = r1_bio->sector;
			bio->bi_rw = r1_bio->cmd;

			generic_make_request(bio);
			break;
		}
	}
	spin_unlock_irqrestore(&retry_list_lock, flags);
}
#undef IO_ERROR
#undef REDIRECT_SECTOR

/*
 * Private kernel thread to reconstruct mirrors after an unclean
 * shutdown.
 */
static void raid1syncd(void *data)
{
	conf_t *conf = data;
	mddev_t *mddev = conf->mddev;

	if (!conf->resync_mirrors)
		return;
	if (conf->resync_mirrors == 2)
		return;
	down(&mddev->recovery_sem);
	if (!md_do_sync(mddev, NULL)) {
		/*
		 * Only if everything went Ok.
		 */
		conf->resync_mirrors = 0;
	}

	close_sync(conf);

	up(&mddev->recovery_sem);
}

static int init_resync(conf_t *conf)
{
	int buffs;

	conf->start_active = 0;
	conf->start_ready = 0;
	conf->start_pending = 0;
	conf->start_future = 0;
	conf->phase = 0;

	buffs = RESYNC_WINDOW / RESYNC_BLOCK_SIZE;
	if (conf->r1buf_pool)
		BUG();
	conf->r1buf_pool = mempool_create(buffs, r1buf_pool_alloc, r1buf_pool_free, conf);
	if (!conf->r1buf_pool)
		return -ENOMEM;
	conf->window = 2048;
	conf->cnt_future += conf->cnt_done+conf->cnt_pending;
	conf->cnt_done = conf->cnt_pending = 0;
	if (conf->cnt_ready || conf->cnt_active)
		MD_BUG();
	return 0;
}

static void wait_sync_pending(conf_t *conf, sector_t sector_nr)
{
	spin_lock_irq(&conf->segment_lock);
	while (sector_nr >= conf->start_pending) {
//		printk("wait .. sect=%lu start_active=%d ready=%d pending=%d future=%d, cnt_done=%d active=%d ready=%d pending=%d future=%d\n", sector_nr, conf->start_active, conf->start_ready, conf->start_pending, conf->start_future, conf->cnt_done, conf->cnt_active, conf->cnt_ready, conf->cnt_pending, conf->cnt_future);
		wait_event_lock_irq(conf->wait_done, !conf->cnt_active,
					conf->segment_lock);
		wait_event_lock_irq(conf->wait_ready, !conf->cnt_pending,
					conf->segment_lock);
		conf->start_active = conf->start_ready;
		conf->start_ready = conf->start_pending;
		conf->start_pending = conf->start_future;
		conf->start_future = conf->start_future+conf->window;

		// Note: falling off the end is not a problem
		conf->phase = conf->phase ^1;
		conf->cnt_active = conf->cnt_ready;
		conf->cnt_ready = 0;
		conf->cnt_pending = conf->cnt_future;
		conf->cnt_future = 0;
		wake_up(&conf->wait_done);
	}
	conf->cnt_ready++;
	spin_unlock_irq(&conf->segment_lock);
}

/*
 * perform a "sync" on one "block"
 *
 * We need to make sure that no normal I/O request - particularly write
 * requests - conflict with active sync requests.
 * This is achieved by conceptually dividing the block space into a
 * number of sections:
 *  DONE: 0 .. a-1     These blocks are in-sync
 *  ACTIVE: a.. b-1    These blocks may have active sync requests, but
 *                     no normal IO requests
 *  READY: b .. c-1    These blocks have no normal IO requests - sync
 *                     request may be happening
 *  PENDING: c .. d-1  These blocks may have IO requests, but no new
 *                     ones will be added
 *  FUTURE:  d .. end  These blocks are not to be considered yet. IO may
 *                     be happening, but not sync
 *
 * We keep a
 *   phase    which flips (0 or 1) each time d moves and
 * a count of:
 *   z =  active io requests in FUTURE since d moved - marked with
 *        current phase
 *   y =  active io requests in FUTURE before d moved, or PENDING -
 *        marked with previous phase
 *   x =  active sync requests in READY
 *   w =  active sync requests in ACTIVE
 *   v =  active io requests in DONE
 *
 * Normally, a=b=c=d=0 and z= active io requests
 *   or a=b=c=d=END and v= active io requests
 * Allowed changes to a,b,c,d:
 * A:  c==d &&  y==0 -> d+=window, y=z, z=0, phase=!phase
 * B:  y==0 -> c=d
 * C:   b=c, w+=x, x=0
 * D:  w==0 -> a=b
 * E: a==b==c==d==end -> a=b=c=d=0, z=v, v=0
 *
 * At start of sync we apply A.
 * When y reaches 0, we apply B then A then being sync requests
 * When sync point reaches c-1, we wait for y==0, and W==0, and
 * then apply apply B then A then D then C.
 * Finally, we apply E
 *
 * The sync request simply issues a "read" against a working drive
 * This is marked so that on completion the raid1d thread is woken to
 * issue suitable write requests
 */

static int sync_request(mddev_t *mddev, sector_t sector_nr)
{
	conf_t *conf = mddev_to_conf(mddev);
	mirror_info_t *mirror;
	r1bio_t *r1_bio;
	struct bio *read_bio, *bio;
	sector_t max_sector, nr_sectors;
	int disk, partial;

	if (!sector_nr)
		if (init_resync(conf))
			return -ENOMEM;

	wait_sync_pending(conf, sector_nr);

	/*
	 * If reconstructing, and >1 working disc,
	 * could dedicate one to rebuild and others to
	 * service read requests ..
	 */
	disk = conf->last_used;
	/* make sure disk is operational */
	while (!conf->mirrors[disk].operational) {
		if (disk <= 0)
			disk = conf->raid_disks;
		disk--;
		if (disk == conf->last_used)
			break;
	}
	conf->last_used = disk;

	mirror = conf->mirrors+conf->last_used;

	r1_bio = mempool_alloc(conf->r1buf_pool, GFP_NOIO);
	check_all_bios_empty(r1_bio);

	r1_bio->mddev = mddev;
	r1_bio->sector = sector_nr;
	r1_bio->cmd = SPECIAL;

	max_sector = mddev->sb->size << 1;
	if (sector_nr >= max_sector)
		BUG();

	bio = r1_bio->master_bio;
	nr_sectors = RESYNC_BLOCK_SIZE >> 9;
	if (max_sector - sector_nr < nr_sectors)
		nr_sectors = max_sector - sector_nr;
	bio->bi_size = nr_sectors << 9;
	bio->bi_vcnt = (bio->bi_size + PAGE_SIZE-1) / PAGE_SIZE;
	/*
	 * Is there a partial page at the end of the request?
	 */
	partial = bio->bi_size % PAGE_SIZE;
	if (partial)
		bio->bi_io_vec[bio->bi_vcnt-1].bv_len = partial;


	read_bio = bio_clone(r1_bio->master_bio, GFP_NOIO);

	read_bio->bi_sector = sector_nr;
	read_bio->bi_dev = mirror->dev;
	read_bio->bi_end_io = end_sync_read;
	read_bio->bi_rw = READ;
	read_bio->bi_private = r1_bio;

	if (r1_bio->read_bio)
		BUG();
	r1_bio->read_bio = read_bio;

	md_sync_acct(read_bio->bi_dev, nr_sectors);

	generic_make_request(read_bio);

	return nr_sectors;
}

#define INVALID_LEVEL KERN_WARNING \
"raid1: md%d: raid level not set to mirroring (%d)\n"

#define NO_SB KERN_ERR \
"raid1: disabled mirror %s (couldn't access raid superblock)\n"

#define ERRORS KERN_ERR \
"raid1: disabled mirror %s (errors detected)\n"

#define NOT_IN_SYNC KERN_ERR \
"raid1: disabled mirror %s (not in sync)\n"

#define INCONSISTENT KERN_ERR \
"raid1: disabled mirror %s (inconsistent descriptor)\n"

#define ALREADY_RUNNING KERN_ERR \
"raid1: disabled mirror %s (mirror %d already operational)\n"

#define OPERATIONAL KERN_INFO \
"raid1: device %s operational as mirror %d\n"

#define MEM_ERROR KERN_ERR \
"raid1: couldn't allocate memory for md%d\n"

#define SPARE KERN_INFO \
"raid1: spare disk %s\n"

#define NONE_OPERATIONAL KERN_ERR \
"raid1: no operational mirrors for md%d\n"

#define ARRAY_IS_ACTIVE KERN_INFO \
"raid1: raid set md%d active with %d out of %d mirrors\n"

#define THREAD_ERROR KERN_ERR \
"raid1: couldn't allocate thread for md%d\n"

#define START_RESYNC KERN_WARNING \
"raid1: raid set md%d not clean; reconstructing mirrors\n"

static int run(mddev_t *mddev)
{
	conf_t *conf;
	int i, j, disk_idx;
	mirror_info_t *disk;
	mdp_super_t *sb = mddev->sb;
	mdp_disk_t *descriptor;
	mdk_rdev_t *rdev;
	struct list_head *tmp;
	int start_recovery = 0;

	MOD_INC_USE_COUNT;

	if (sb->level != 1) {
		printk(INVALID_LEVEL, mdidx(mddev), sb->level);
		goto out;
	}
	/*
	 * copy the already verified devices into our private RAID1
	 * bookkeeping area. [whatever we allocate in run(),
	 * should be freed in stop()]
	 */
	conf = kmalloc(sizeof(conf_t), GFP_KERNEL);
	mddev->private = conf;
	if (!conf) {
		printk(MEM_ERROR, mdidx(mddev));
		goto out;
	}
	memset(conf, 0, sizeof(*conf));

	conf->r1bio_pool = mempool_create(NR_RAID1_BIOS, r1bio_pool_alloc,
						r1bio_pool_free, NULL);
	if (!conf->r1bio_pool) {
		printk(MEM_ERROR, mdidx(mddev));
		goto out;
	}

//	for (tmp = (mddev)->disks.next; rdev = ((mdk_rdev_t *)((char *)(tmp)-(unsigned long)(&((mdk_rdev_t *)0)->same_set))), tmp = tmp->next, tmp->prev != &(mddev)->disks ; ) {

	ITERATE_RDEV(mddev, rdev, tmp) {
		if (rdev->faulty) {
			printk(ERRORS, partition_name(rdev->dev));
		} else {
			if (!rdev->sb) {
				MD_BUG();
				continue;
			}
		}
		if (rdev->desc_nr == -1) {
			MD_BUG();
			continue;
		}
		descriptor = &sb->disks[rdev->desc_nr];
		disk_idx = descriptor->raid_disk;
		disk = conf->mirrors + disk_idx;

		if (disk_faulty(descriptor)) {
			disk->number = descriptor->number;
			disk->raid_disk = disk_idx;
			disk->dev = rdev->dev;
			disk->sect_limit = MAX_WORK_PER_DISK;
			disk->operational = 0;
			disk->write_only = 0;
			disk->spare = 0;
			disk->used_slot = 1;
			disk->head_position = 0;
			continue;
		}
		if (disk_active(descriptor)) {
			if (!disk_sync(descriptor)) {
				printk(NOT_IN_SYNC,
					partition_name(rdev->dev));
				continue;
			}
			if ((descriptor->number > MD_SB_DISKS) ||
					(disk_idx > sb->raid_disks)) {

				printk(INCONSISTENT,
					partition_name(rdev->dev));
				continue;
			}
			if (disk->operational) {
				printk(ALREADY_RUNNING,
					partition_name(rdev->dev),
					disk_idx);
				continue;
			}
			printk(OPERATIONAL, partition_name(rdev->dev),
					disk_idx);
			disk->number = descriptor->number;
			disk->raid_disk = disk_idx;
			disk->dev = rdev->dev;
			disk->sect_limit = MAX_WORK_PER_DISK;
			disk->operational = 1;
			disk->write_only = 0;
			disk->spare = 0;
			disk->used_slot = 1;
			disk->head_position = 0;
			conf->working_disks++;
		} else {
		/*
		 * Must be a spare disk ..
		 */
			printk(SPARE, partition_name(rdev->dev));
			disk->number = descriptor->number;
			disk->raid_disk = disk_idx;
			disk->dev = rdev->dev;
			disk->sect_limit = MAX_WORK_PER_DISK;
			disk->operational = 0;
			disk->write_only = 0;
			disk->spare = 1;
			disk->used_slot = 1;
			disk->head_position = 0;
		}
	}
	conf->raid_disks = sb->raid_disks;
	conf->nr_disks = sb->nr_disks;
	conf->mddev = mddev;
	conf->device_lock = SPIN_LOCK_UNLOCKED;

	conf->segment_lock = SPIN_LOCK_UNLOCKED;
	init_waitqueue_head(&conf->wait_done);
	init_waitqueue_head(&conf->wait_ready);

	if (!conf->working_disks) {
		printk(NONE_OPERATIONAL, mdidx(mddev));
		goto out_free_conf;
	}

	for (i = 0; i < MD_SB_DISKS; i++) {

		descriptor = sb->disks+i;
		disk_idx = descriptor->raid_disk;
		disk = conf->mirrors + disk_idx;

		if (disk_faulty(descriptor) && (disk_idx < conf->raid_disks) &&
				!disk->used_slot) {

			disk->number = descriptor->number;
			disk->raid_disk = disk_idx;
			disk->dev = MKDEV(0,0);

			disk->operational = 0;
			disk->write_only = 0;
			disk->spare = 0;
			disk->used_slot = 1;
			disk->head_position = 0;
		}
	}

	/*
	 * find the first working one and use it as a starting point
	 * to read balancing.
	 */
	for (j = 0; !conf->mirrors[j].operational && j < MD_SB_DISKS; j++)
		/* nothing */;
	conf->last_used = j;


	if (conf->working_disks != sb->raid_disks) {
		printk(KERN_ALERT "raid1: md%d, not all disks are operational -- trying to recover array\n", mdidx(mddev));
		start_recovery = 1;
	}

	{
		const char * name = "raid1d";

		conf->thread = md_register_thread(raid1d, conf, name);
		if (!conf->thread) {
			printk(THREAD_ERROR, mdidx(mddev));
			goto out_free_conf;
		}
	}

	if (!start_recovery && !(sb->state & (1 << MD_SB_CLEAN)) &&
						(conf->working_disks > 1)) {
		const char * name = "raid1syncd";

		conf->resync_thread = md_register_thread(raid1syncd, conf, name);
		if (!conf->resync_thread) {
			printk(THREAD_ERROR, mdidx(mddev));
			goto out_free_conf;
		}

		printk(START_RESYNC, mdidx(mddev));
		conf->resync_mirrors = 1;
		md_wakeup_thread(conf->resync_thread);
	}

	/*
	 * Regenerate the "device is in sync with the raid set" bit for
	 * each device.
	 */
	for (i = 0; i < MD_SB_DISKS; i++) {
		mark_disk_nonsync(sb->disks+i);
		for (j = 0; j < sb->raid_disks; j++) {
			if (!conf->mirrors[j].operational)
				continue;
			if (sb->disks[i].number == conf->mirrors[j].number)
				mark_disk_sync(sb->disks+i);
		}
	}
	sb->active_disks = conf->working_disks;

	if (start_recovery)
		md_recover_arrays();


	printk(ARRAY_IS_ACTIVE, mdidx(mddev), sb->active_disks, sb->raid_disks);
	/*
	 * Ok, everything is just fine now
	 */
	return 0;

out_free_conf:
	if (conf->r1bio_pool)
		mempool_destroy(conf->r1bio_pool);
	kfree(conf);
	mddev->private = NULL;
out:
	MOD_DEC_USE_COUNT;
	return -EIO;
}

#undef INVALID_LEVEL
#undef NO_SB
#undef ERRORS
#undef NOT_IN_SYNC
#undef INCONSISTENT
#undef ALREADY_RUNNING
#undef OPERATIONAL
#undef SPARE
#undef NONE_OPERATIONAL
#undef ARRAY_IS_ACTIVE

static int stop_resync(mddev_t *mddev)
{
	conf_t *conf = mddev_to_conf(mddev);

	if (conf->resync_thread) {
		if (conf->resync_mirrors) {
			conf->resync_mirrors = 2;
			md_interrupt_thread(conf->resync_thread);

			printk(KERN_INFO "raid1: mirror resync was not fully finished, restarting next time.\n");
			return 1;
		}
		return 0;
	}
	return 0;
}

static int restart_resync(mddev_t *mddev)
{
	conf_t *conf = mddev_to_conf(mddev);

	if (conf->resync_mirrors) {
		if (!conf->resync_thread) {
			MD_BUG();
			return 0;
		}
		conf->resync_mirrors = 1;
		md_wakeup_thread(conf->resync_thread);
		return 1;
	}
	return 0;
}

static int stop(mddev_t *mddev)
{
	conf_t *conf = mddev_to_conf(mddev);

	md_unregister_thread(conf->thread);
	if (conf->resync_thread)
		md_unregister_thread(conf->resync_thread);
	if (conf->r1bio_pool)
		mempool_destroy(conf->r1bio_pool);
	kfree(conf);
	mddev->private = NULL;
	MOD_DEC_USE_COUNT;
	return 0;
}

static mdk_personality_t raid1_personality =
{
	name:		"raid1",
	make_request:	make_request,
	run:		run,
	stop:		stop,
	status:		status,
	error_handler:	error,
	diskop:		diskop,
	stop_resync:	stop_resync,
	restart_resync:	restart_resync,
	sync_request:	sync_request
};

static int __init raid_init(void)
{
	return register_md_personality(RAID1, &raid1_personality);
}

static void raid_exit(void)
{
	unregister_md_personality(RAID1);
}

module_init(raid_init);
module_exit(raid_exit);
MODULE_LICENSE("GPL");
